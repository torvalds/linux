/*
 *  cx18 gpio functions
 *
 *  Derived from ivtv-gpio.c
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA
 */

#include "cx18-driver.h"
#include "cx18-cards.h"
#include "cx18-gpio.h"
#include "tuner-xc2028.h"

/********************* GPIO stuffs *********************/

/* GPIO registers */
#define CX18_REG_GPIO_IN     0xc72010
#define CX18_REG_GPIO_OUT1   0xc78100
#define CX18_REG_GPIO_DIR1   0xc78108
#define CX18_REG_GPIO_OUT2   0xc78104
#define CX18_REG_GPIO_DIR2   0xc7810c

static u32 gpio_dir;
static u32 gpio_val;

/*
 * HVR-1600 GPIO pins, courtesy of Hauppauge:
 *
 * gpio0: zilog ir process reset pin
 * gpio1: zilog programming pin (you should never use this)
 * gpio12: cx24227 reset pin
 * gpio13: cs5345 reset pin
*/

static void gpio_write(struct cx18 *cx)
{
	write_reg((gpio_dir & 0xffff) << 16, CX18_REG_GPIO_DIR1);
	write_reg(((gpio_dir & 0xffff) << 16) | (gpio_val & 0xffff),
			CX18_REG_GPIO_OUT1);
	write_reg(gpio_dir & 0xffff0000, CX18_REG_GPIO_DIR2);
	write_reg((gpio_dir & 0xffff0000) | ((gpio_val & 0xffff0000) >> 16),
			CX18_REG_GPIO_OUT2);
}

void cx18_gpio_init(struct cx18 *cx)
{
	gpio_dir = cx->card->gpio_init.direction;
	gpio_val = cx->card->gpio_init.initial_value;

	if (gpio_dir == 0)
		return;

	gpio_dir |= 1 << cx->card->xceive_pin;
	gpio_val |= 1 << cx->card->xceive_pin;

	CX18_DEBUG_INFO("GPIO initial dir: %08x/%08x out: %08x/%08x\n",
		   read_reg(CX18_REG_GPIO_DIR1), read_reg(CX18_REG_GPIO_DIR2),
		   read_reg(CX18_REG_GPIO_OUT1), read_reg(CX18_REG_GPIO_OUT2));

	gpio_write(cx);
}

/* Xceive tuner reset function */
int cx18_reset_tuner_gpio(void *dev, int cmd, int value)
{
	struct i2c_algo_bit_data *algo = dev;
	struct cx18_i2c_algo_callback_data *cb_data = algo->data;
	struct cx18 *cx = cb_data->cx;

	if (cmd != XC2028_TUNER_RESET)
		return 0;
	CX18_DEBUG_INFO("Resetting tuner\n");

	gpio_dir |= 1 << cx->card->xceive_pin;
	gpio_val &= ~(1 << cx->card->xceive_pin);

	gpio_write(cx);
	schedule_timeout_interruptible(msecs_to_jiffies(1));

	gpio_val |= 1 << cx->card->xceive_pin;
	gpio_write(cx);
	schedule_timeout_interruptible(msecs_to_jiffies(1));
	return 0;
}
