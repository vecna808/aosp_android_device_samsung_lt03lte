/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2013 The CyanogenMod Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "lights"
#include <cutils/log.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <hardware/lights.h>

static pthread_once_t g_init = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

char const*const BACKLIGHT_FILE = "/sys/class/leds/lcd-backlight/brightness";
char const*const COLORTONE_FILE = "/sys/class/mdnie/mdnie/mode";

char const*const BUTTONS_FILE = "/sys/class/sec/sec_touchkey/brightness";

char const*const LED_MENU_BRIGHTNESS = "/sys/class/leds/led:rgb_green/brightness";
char const*const LED_BACK_BRIGHTNESS = "/sys/class/leds/led:rgb_blue/brightness";

char const*const LED_MENU_BLINK = "/sys/class/leds/led:rgb_green/blink";
char const*const LED_BACK_BLINK = "/sys/class/leds/led:rgb_blue/blink";

void init_g_lock(void)
{
    pthread_mutex_init(&g_lock, NULL);
}

static int write_int(char const *path, int value)
{
    int fd;
    static int already_warned;

    already_warned = 0;

    //ALOGV("write_int: path %s, value %d", path, value);

    fd = open(path, O_RDWR);

    if (fd >= 0) {
        char buffer[20];
        int bytes = sprintf(buffer, "%d\n", value);
        int amt = write(fd, buffer, bytes);
        close(fd);
        return amt == -1 ? -errno : 0;
    } else {
        if (already_warned == 0) {
            ALOGE("write_int failed to open %s\n", path);
            already_warned = 1;
        }
        return -errno;
    }
}

static int read_int(char const *path, int* value)
{
    int fd;
    static int already_warned;

    already_warned = 0;

    ALOGV("read_int: path %s", path);

    fd = open(path, O_RDWR);

    if (fd >= 0) {
        char buffer[20];
        int bytes = read(fd, buffer, 20);
        if (bytes <= 0)
            return -errno;

        int amt = sscanf(buffer, "%d", value);
        close(fd);

        ALOGV("read_int: path %s, value %d", path, *value);
        return amt != 1 ? -errno : 0;
    } else {
        if (already_warned == 0) {
            ALOGE("write_int failed to open %s\n", path);
            already_warned = 1;
        }
        return -errno;
    }

}

static int rgb_to_brightness(struct light_state_t const *state)
{
    int color = state->color & 0x00ffffff;

    return ((77*((color>>16) & 0x00ff))
        + (150*((color>>8) & 0x00ff)) + (29*(color & 0x00ff))) >> 8;
}

static int is_lit(struct light_state_t const* state)
{
    return (state->color & 0x00ffffff) > 0;
}

static int set_light_backlight(struct light_device_t *dev,
            struct light_state_t const *state)
{
    int err = 0;
    int brightness = rgb_to_brightness(state);

    pthread_mutex_lock(&g_lock);
    err = write_int(BACKLIGHT_FILE, brightness);
    pthread_mutex_unlock(&g_lock);

    return err;
}

static int set_light_colortone(struct light_device_t *dev,
            struct light_state_t const *state)
{
    int err = 0;
    int colortone = rgb_to_brightness(state);

    pthread_mutex_lock(&g_lock);
    err = write_int(COLORTONE_FILE, colortone);
    pthread_mutex_unlock(&g_lock);

    return err;
}

static int set_light_buttons(struct light_device_t* dev,
        struct light_state_t const* state)
{
    int err = 0;
    int on = is_lit(state);

    pthread_mutex_lock(&g_lock);
    err = write_int(BUTTONS_FILE, on?1:0);
    pthread_mutex_unlock(&g_lock);

    return err;

}

static int set_light_leds_battery(struct light_device_t *dev,
            struct light_state_t const *state)
{
    int err = 0;
    int brightness = 0;
    int on = is_lit(state);

    pthread_mutex_lock(&g_lock);
    err = read_int(LED_BACK_BRIGHTNESS, &brightness);
    pthread_mutex_unlock(&g_lock);

    /*do not set if already on*/
    if (err != 0 || (brightness != 0 && brightness != 20))
        return err;

    pthread_mutex_lock(&g_lock);
    err = write_int(LED_BACK_BRIGHTNESS, on?20:0);
    pthread_mutex_unlock(&g_lock);
    
    return err;
}

static int set_light_leds_notifications(struct light_device_t *dev,
            struct light_state_t const *state)
{
    int err = 0;
    int brightness = 0;
    int on = is_lit(state);

    pthread_mutex_lock(&g_lock);
    err = read_int(LED_MENU_BRIGHTNESS, &brightness);
    pthread_mutex_unlock(&g_lock);

    /*do not set if already on*/
    if (err != 0 || (brightness != 0 && brightness != 20))
        return err;

    pthread_mutex_lock(&g_lock);
    err = write_int(LED_MENU_BLINK, on);
    pthread_mutex_unlock(&g_lock);
    
    return err;
}

static int close_lights(struct light_device_t *dev)
{
    //ALOGV("close_light is called");
    if (dev)
        free(dev);

    return 0;
}

static int set_light_leds_noop(struct light_device_t *dev,
            struct light_state_t const *state)
{
    return 0;
}

static int open_lights(const struct hw_module_t *module, char const *name,
                        struct hw_device_t **device)
{
    int (*set_light)(struct light_device_t *dev,
        struct light_state_t const *state);

    if (0 == strcmp(LIGHT_ID_BACKLIGHT, name))
        set_light = set_light_backlight;
    else if (0 == strcmp(LIGHT_ID_COLORTONE, name))
        set_light = set_light_colortone;
    else if (0 == strcmp(LIGHT_ID_BUTTONS, name))
        set_light = set_light_buttons;
    else if (0 == strcmp(LIGHT_ID_BATTERY, name))
        set_light = set_light_leds_battery;
    else if (0 == strcmp(LIGHT_ID_NOTIFICATIONS, name))
        set_light = set_light_leds_notifications;
    else if (0 == strcmp(LIGHT_ID_ATTENTION, name))
        set_light = set_light_leds_noop;
    else
        return -EINVAL;

    pthread_once(&g_init, init_g_lock);

    struct light_device_t *dev = malloc(sizeof(struct light_device_t));
    memset(dev, 0, sizeof(*dev));

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t *)module;
    dev->common.close = (int (*)(struct hw_device_t *))close_lights;
    dev->set_light = set_light;

    *device = (struct hw_device_t *)dev;

    return 0;
}

static struct hw_module_methods_t lights_module_methods = {
    .open =  open_lights,
};

struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = LIGHTS_HARDWARE_MODULE_ID,
    .name = "lt03lte Lights Module",
    .author = "The CyanogenMod Project",
    .methods = &lights_module_methods,
};
