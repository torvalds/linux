/*
 * LED Core
 *
 * Copyright 2005 Openedhand Ltd.
 *
 * Author: Richard Purdie <rpurdie@openedhand.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifndef __LEDS_H_INCLUDED
#define __LEDS_H_INCLUDED

#include <linux/leds.h>

static inline void led_set_brightness(struct led_classdev *led_cdev,
					enum led_brightness value)
{
	if (value > LED_FULL)
		value = LED_FULL;
	led_cdev->brightness = value;
	if (!(led_cdev->flags & LED_SUSPENDED))
		led_cdev->brightness_set(led_cdev, value);
}

extern rwlock_t leds_list_lock;
extern struct list_head leds_list;

#endif	/* __LEDS_H_INCLUDED */
