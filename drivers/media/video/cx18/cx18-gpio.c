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
	u32 dir = cx->gpio_dir;
	u32 val = cx->gpio_val;

	write_reg((dir & 0xffff) << 16, CX18_REG_GPIO_DIR1);
	write_reg(((dir & 0xffff) << 16) | (val & 0xffff),
			CX18_REG_GPIO_OUT1);
	write_reg(dir & 0xffff0000, CX18_REG_GPIO_DIR2);
	write_reg_sync((dir & 0xffff0000) | ((val & 0xffff0000) >> 16),
			CX18_REG_GPIO_OUT2);
}

void cx18_reset_i2c_slaves_gpio(struct cx18 *cx)
{
	const struct cx18_gpio_i2c_slave_reset *p;

	p = &cx->card->gpio_i2c_slave_reset;

	if ((p->active_lo_mask | p->active_hi_mask) == 0)
		return;

	/* Assuming that the masks are a subset of the bits in gpio_dir */

	/* Assert */
	cx->gpio_val =
		(cx->gpio_val | p->active_hi_mask) & ~(p->active_lo_mask);
	gpio_write(cx);
	schedule_timeout_uninterruptible(msecs_to_jiffies(p->msecs_asserted));

	/* Deassert */
	cx->gpio_val =
		(cx->gpio_val | p->active_lo_mask) & ~(p->active_hi_mask);
	gpio_write(cx);
	schedule_timeout_uninterruptible(msecs_to_jiffies(p->msecs_recovery));
}

void cx18_gpio_init(struct cx18 *cx)
{
	cx->gpio_dir = cx->card->gpio_init.direction;
	cx->gpio_val = cx->card->gpio_init.initial_value;

	if (cx->card->tuners[0].tuner == TUNER_XC2028) {
		cx->gpio_dir |= 1 << cx->card->xceive_pin;
		cx->gpio_val |= 1 << cx->card->xceive_pin;
	}

	if (cx->gpio_dir == 0)
		return;

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

	cx->gpio_val &= ~(1 << cx->card->xceive_pin);

	gpio_write(cx);
	schedule_timeout_interruptible(msecs_to_jiffies(1));

	cx->gpio_val |= 1 << cx->card->xceive_pin;
	gpio_write(cx);
	schedule_timeout_interruptible(msecs_to_jiffies(1));
	return 0;
}
