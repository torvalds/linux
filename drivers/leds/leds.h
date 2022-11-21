/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * LED Core
 *
 * Copyright 2005 Openedhand Ltd.
 *
 * Author: Richard Purdie <rpurdie@openedhand.com>
 */
#ifndef __LEDS_H_INCLUDED
#define __LEDS_H_INCLUDED

#include <linux/rwsem.h>
#include <linux/leds.h>

static inline int led_get_brightness(struct led_classdev *led_cdev)
{
	return led_cdev->brightness;
}

void led_init_core(struct led_classdev *led_cdev);
void led_stop_software_blink(struct led_classdev *led_cdev);
void led_set_brightness_nopm(struct led_classdev *led_cdev, unsigned int value);
void led_set_brightness_nosleep(struct led_classdev *led_cdev, unsigned int value);
ssize_t led_trigger_read(struct file *filp, struct kobject *kobj,
			struct bin_attribute *attr, char *buf,
			loff_t pos, size_t count);
ssize_t led_trigger_write(struct file *filp, struct kobject *kobj,
			struct bin_attribute *bin_attr, char *buf,
			loff_t pos, size_t count);
enum led_default_state led_init_default_state_get(struct fwnode_handle *fwnode);

extern struct rw_semaphore leds_list_lock;
extern struct list_head leds_list;
extern struct list_head trigger_list;
extern const char * const led_colors[LED_COLOR_ID_MAX];

#endif	/* __LEDS_H_INCLUDED */
