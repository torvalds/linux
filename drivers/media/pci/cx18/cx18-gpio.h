/*
 *  cx18 gpio functions
 *
 *  Derived from ivtv-gpio.h
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *  Copyright (C) 2008  Andy Walls <awalls@md.metrocast.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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
