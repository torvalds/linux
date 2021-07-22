/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  cx18 gpio functions
 *
 *  Derived from ivtv-gpio.h
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *  Copyright (C) 2008  Andy Walls <awalls@md.metrocast.net>
 */

void cx18_gpio_init(struct cx18 *cx);
int cx18_gpio_register(struct cx18 *cx, u32 hw);

enum cx18_gpio_reset_type {
	CX18_GPIO_RESET_I2C     = 0,
	CX18_GPIO_RESET_Z8F0811 = 1,
	CX18_GPIO_RESET_XC2028  = 2,
};

void cx18_reset_ir_gpio(void *data);
int cx18_reset_tuner_gpio(void *dev, int component, int cmd, int value);
