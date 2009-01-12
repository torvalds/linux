/*
 *  cx18 gpio functions
 *
 *  Derived from ivtv-gpio.c
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *  Copyright (C) 2008  Andy Walls <awalls@radix.net>
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
#include "cx18-io.h"
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
	u32 dir_lo = cx->gpio_dir & 0xffff;
	u32 val_lo = cx->gpio_val & 0xffff;
	u32 dir_hi = cx->gpio_dir >> 16;
	u32 val_hi = cx->gpio_val >> 16;

	cx18_write_reg_expect(cx, dir_lo << 16,
					CX18_REG_GPIO_DIR1, ~dir_lo, dir_lo);
	cx18_write_reg_expect(cx, (dir_lo << 16) | val_lo,
					CX18_REG_GPIO_OUT1, val_lo, dir_lo);
	cx18_write_reg_expect(cx, dir_hi << 16,
					CX18_REG_GPIO_DIR2, ~dir_hi, dir_hi);
	cx18_write_reg_expect(cx, (dir_hi << 16) | val_hi,
					CX18_REG_GPIO_OUT2, val_hi, dir_hi);
}

void cx18_reset_i2c_slaves_gpio(struct cx18 *cx)
{
	const struct cx18_gpio_i2c_slave_reset *p;

	p = &cx->card->gpio_i2c_slave_reset;

	if ((p->active_lo_mask | p->active_hi_mask) == 0)
		return;

	/* Assuming that the masks are a subset of the bits in gpio_dir */

	/* Assert */
	mutex_lock(&cx->gpio_lock);
	cx->gpio_val =
		(cx->gpio_val | p->active_hi_mask) & ~(p->active_lo_mask);
	gpio_write(cx);
	schedule_timeout_uninterruptible(msecs_to_jiffies(p->msecs_asserted));

	/* Deassert */
	cx->gpio_val =
		(cx->gpio_val | p->active_lo_mask) & ~(p->active_hi_mask);
	gpio_write(cx);
	schedule_timeout_uninterruptible(msecs_to_jiffies(p->msecs_recovery));
	mutex_unlock(&cx->gpio_lock);
}

void cx18_reset_ir_gpio(void *data)
{
	struct cx18 *cx = ((struct cx18_i2c_algo_callback_data *)data)->cx;
	const struct cx18_gpio_i2c_slave_reset *p;

	p = &cx->card->gpio_i2c_slave_reset;

	if (p->ir_reset_mask == 0)
		return;

	CX18_DEBUG_INFO("Resetting IR microcontroller\n");

	/*
	   Assert timing for the Z8F0811 on HVR-1600 boards:
	   1. Assert RESET for min of 4 clock cycles at 18.432 MHz to initiate
	   2. Reset then takes 66 WDT cycles at 10 kHz + 16 xtal clock cycles
		(6,601,085 nanoseconds ~= 7 milliseconds)
	   3. DBG pin must be high before chip exits reset for normal operation.
		DBG is open drain and hopefully pulled high since we don't
		normally drive it (GPIO 1?) for the HVR-1600
	   4. Z8F0811 won't exit reset until RESET is deasserted
	*/
	mutex_lock(&cx->gpio_lock);
	cx->gpio_val = cx->gpio_val & ~p->ir_reset_mask;
	gpio_write(cx);
	mutex_unlock(&cx->gpio_lock);
	schedule_timeout_uninterruptible(msecs_to_jiffies(p->msecs_asserted));

	/*
	   Zilog comes out of reset, loads reset vector address and executes
	   from there. Required recovery delay unknown.
	*/
	mutex_lock(&cx->gpio_lock);
	cx->gpio_val = cx->gpio_val | p->ir_reset_mask;
	gpio_write(cx);
	mutex_unlock(&cx->gpio_lock);
	schedule_timeout_uninterruptible(msecs_to_jiffies(p->msecs_recovery));
}
EXPORT_SYMBOL(cx18_reset_ir_gpio);
/* This symbol is exported for use by an infrared module for the IR-blaster */

void cx18_gpio_init(struct cx18 *cx)
{
	mutex_lock(&cx->gpio_lock);
	cx->gpio_dir = cx->card->gpio_init.direction;
	cx->gpio_val = cx->card->gpio_init.initial_value;

	if (cx->card->tuners[0].tuner == TUNER_XC2028) {
		cx->gpio_dir |= 1 << cx->card->xceive_pin;
		cx->gpio_val |= 1 << cx->card->xceive_pin;
	}

	if (cx->gpio_dir == 0) {
		mutex_unlock(&cx->gpio_lock);
		return;
	}

	CX18_DEBUG_INFO("GPIO initial dir: %08x/%08x out: %08x/%08x\n",
			cx18_read_reg(cx, CX18_REG_GPIO_DIR1),
			cx18_read_reg(cx, CX18_REG_GPIO_DIR2),
			cx18_read_reg(cx, CX18_REG_GPIO_OUT1),
			cx18_read_reg(cx, CX18_REG_GPIO_OUT2));

	gpio_write(cx);
	mutex_unlock(&cx->gpio_lock);
}

/* Xceive tuner reset function */
int cx18_reset_tuner_gpio(void *dev, int component, int cmd, int value)
{
	struct i2c_algo_bit_data *algo = dev;
	struct cx18_i2c_algo_callback_data *cb_data = algo->data;
	struct cx18 *cx = cb_data->cx;

	if (cmd != XC2028_TUNER_RESET)
		return 0;
	CX18_DEBUG_INFO("Resetting tuner\n");

	mutex_lock(&cx->gpio_lock);
	cx->gpio_val &= ~(1 << cx->card->xceive_pin);
	gpio_write(cx);
	mutex_unlock(&cx->gpio_lock);
	schedule_timeout_interruptible(msecs_to_jiffies(1));

	mutex_lock(&cx->gpio_lock);
	cx->gpio_val |= 1 << cx->card->xceive_pin;
	gpio_write(cx);
	mutex_unlock(&cx->gpio_lock);
	schedule_timeout_interruptible(msecs_to_jiffies(1));
	return 0;
}

int cx18_gpio(struct cx18 *cx, unsigned int command, void *arg)
{
	struct v4l2_routing *route = arg;
	u32 mask, data;

	switch (command) {
	case VIDIOC_INT_S_AUDIO_ROUTING:
		if (route->input > 2)
			return -EINVAL;
		mask = cx->card->gpio_audio_input.mask;
		switch (route->input) {
		case 0:
			data = cx->card->gpio_audio_input.tuner;
			break;
		case 1:
			data = cx->card->gpio_audio_input.linein;
			break;
		case 2:
		default:
			data = cx->card->gpio_audio_input.radio;
			break;
		}
		break;

	default:
		return -EINVAL;
	}
	if (mask) {
		mutex_lock(&cx->gpio_lock);
		cx->gpio_val = (cx->gpio_val & ~mask) | (data & mask);
		gpio_write(cx);
		mutex_unlock(&cx->gpio_lock);
	}
	return 0;
}
