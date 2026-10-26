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
void led_trigger_hw_control_changed_worker(struct work_struct *work);

extern struct rw_semaphore leds_list_lock;
extern struct list_head leds_list;
extern const struct attribute_group led_trigger_group;

#endif	/* __LEDS_H_INCLUDED */
