/*
 * LED MULTI-CONTROL
 *
 * Copyright 2017 Allen Zhang <zwp@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifndef __LEDS_MULTI_H_INCLUDED
#define __LEDS_MULTI_H_INCLUDED

enum {
	TRIG_NONE = 0,
	TRIG_DEF_ON,
	TRIG_TIMER,
	TRIG_ONESHOT,
	TRIG_MAX,
};

struct led_ctrl_data {
	u32 trigger;
	/* the delay time(ms) of triggering a trigger */
	u32 delayed_trigger_ms;
	u32 brightness;
	u32 delay_on;
	u32 delay_off;
} __packed;

struct led_ctrl_scroll_data {
	u64 init_bitmap;
	/* the shift bits on every scrolling time*/
	u32 shifts;
	u32 shift_delay_ms;
} __packed;

struct led_ctrl_breath_data {
	u64 background_bitmap;
	u64 breath_bitmap;
	u32 change_delay_ms;
	u32 breath_steps;
} __packed;

#define MAX_LEDS_NUMBER	64

#define LEDS_MULTI_CTRL_IOCTL_MAGIC	'z'

#define LEDS_MULTI_CTRL_IOCTL_MULTI_SET	\
	_IOW(LEDS_MULTI_CTRL_IOCTL_MAGIC, 0x01, struct led_ctrl_data*)
#define LEDS_MULTI_CTRL_IOCTL_GET_LED_NUMBER	\
	_IOR(LEDS_MULTI_CTRL_IOCTL_MAGIC, 0x02, int)
#define LEDS_MULTI_CTRL_IOCTL_MULTI_SET_SCROLL	\
	_IOW(LEDS_MULTI_CTRL_IOCTL_MAGIC, 0x03, struct led_ctrl_scroll_data*)
#define LEDS_MULTI_CTRL_IOCTL_MULTI_SET_BREATH	\
	_IOW(LEDS_MULTI_CTRL_IOCTL_MAGIC, 0x04, struct led_ctrl_breath_data*)

int led_multi_control_register(struct led_classdev *led_cdev);
int led_multi_control_unregister(struct led_classdev *led_cdev);
int led_multi_control_init(struct device *dev);
int led_multi_control_exit(struct device *dev);

#endif	/* __LEDS_MULTI_H_INCLUDED */
