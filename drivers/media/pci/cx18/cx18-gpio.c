// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  cx18 gpio functions
 *
 *  Derived from ivtv-gpio.c
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *  Copyright (C) 2008  Andy Walls <awalls@md.metrocast.net>
 */

#include "cx18-driver.h"
#include "cx18-io.h"
#include "cx18-cards.h"
#include "cx18-gpio.h"
#include "xc2028.h"

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

/*
 * File scope utility functions
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

static void gpio_update(struct cx18 *cx, u32 mask, u32 data)
{
	if (mask == 0)
		return;

	mutex_lock(&cx->gpio_lock);
	cx->gpio_val = (cx->gpio_val & ~mask) | (data & mask);
	gpio_write(cx);
	mutex_unlock(&cx->gpio_lock);
}

static void gpio_reset_seq(struct cx18 *cx, u32 active_lo, u32 active_hi,
			   unsigned int assert_msecs,
			   unsigned int recovery_msecs)
{
	u32 mask;

	mask = active_lo | active_hi;
	if (mask == 0)
		return;

	/*
	 * Assuming that active_hi and active_lo are a subsets of the bits in
	 * gpio_dir.  Also assumes that active_lo and active_hi don't overlap
	 * in any bit position
	 */

	/* Assert */
	gpio_update(cx, mask, ~active_lo);
	schedule_timeout_uninterruptible(msecs_to_jiffies(assert_msecs));

	/* Deassert */
	gpio_update(cx, mask, ~active_hi);
	schedule_timeout_uninterruptible(msecs_to_jiffies(recovery_msecs));
}

/*
 * GPIO Multiplexer - logical device
 */
static int gpiomux_log_status(struct v4l2_subdev *sd)
{
	struct cx18 *cx = v4l2_get_subdevdata(sd);

	mutex_lock(&cx->gpio_lock);
	CX18_INFO_DEV(sd, "GPIO:  direction 0x%08x, value 0x%08x\n",
		      cx->gpio_dir, cx->gpio_val);
	mutex_unlock(&cx->gpio_lock);
	return 0;
}

static int gpiomux_s_radio(struct v4l2_subdev *sd)
{
	struct cx18 *cx = v4l2_get_subdevdata(sd);

	/*
	 * FIXME - work out the cx->active/audio_input mess - this is
	 * intended to handle the switch to radio mode and set the
	 * audio routing, but we need to update the state in cx
	 */
	gpio_update(cx, cx->card->gpio_audio_input.mask,
			cx->card->gpio_audio_input.radio);
	return 0;
}

static int gpiomux_s_std(struct v4l2_subdev *sd, v4l2_std_id norm)
{
	struct cx18 *cx = v4l2_get_subdevdata(sd);
	u32 data;

	switch (cx->card->audio_inputs[cx->audio_input].muxer_input) {
	case 1:
		data = cx->card->gpio_audio_input.linein;
		break;
	case 0:
		data = cx->card->gpio_audio_input.tuner;
		break;
	default:
		/*
		 * FIXME - work out the cx->active/audio_input mess - this is
		 * intended to handle the switch from radio mode and set the
		 * audio routing, but we need to update the state in cx
		 */
		data = cx->card->gpio_audio_input.tuner;
		break;
	}
	gpio_update(cx, cx->card->gpio_audio_input.mask, data);
	return 0;
}

static int gpiomux_s_audio_routing(struct v4l2_subdev *sd,
				   u32 input, u32 output, u32 config)
{
	struct cx18 *cx = v4l2_get_subdevdata(sd);
	u32 data;

	switch (input) {
	case 0:
		data = cx->card->gpio_audio_input.tuner;
		break;
	case 1:
		data = cx->card->gpio_audio_input.linein;
		break;
	case 2:
		data = cx->card->gpio_audio_input.radio;
		break;
	default:
		return -EINVAL;
	}
	gpio_update(cx, cx->card->gpio_audio_input.mask, data);
	return 0;
}

static const struct v4l2_subdev_core_ops gpiomux_core_ops = {
	.log_status = gpiomux_log_status,
};

static const struct v4l2_subdev_tuner_ops gpiomux_tuner_ops = {
	.s_radio = gpiomux_s_radio,
};

static const struct v4l2_subdev_audio_ops gpiomux_audio_ops = {
	.s_routing = gpiomux_s_audio_routing,
};

static const struct v4l2_subdev_video_ops gpiomux_video_ops = {
	.s_std = gpiomux_s_std,
};

static const struct v4l2_subdev_ops gpiomux_ops = {
	.core = &gpiomux_core_ops,
	.tuner = &gpiomux_tuner_ops,
	.audio = &gpiomux_audio_ops,
	.video = &gpiomux_video_ops,
};

/*
 * GPIO Reset Controller - logical device
 */
static int resetctrl_log_status(struct v4l2_subdev *sd)
{
	struct cx18 *cx = v4l2_get_subdevdata(sd);

	mutex_lock(&cx->gpio_lock);
	CX18_INFO_DEV(sd, "GPIO:  direction 0x%08x, value 0x%08x\n",
		      cx->gpio_dir, cx->gpio_val);
	mutex_unlock(&cx->gpio_lock);
	return 0;
}

static int resetctrl_reset(struct v4l2_subdev *sd, u32 val)
{
	struct cx18 *cx = v4l2_get_subdevdata(sd);
	const struct cx18_gpio_i2c_slave_reset *p;

	p = &cx->card->gpio_i2c_slave_reset;
	switch (val) {
	case CX18_GPIO_RESET_I2C:
		gpio_reset_seq(cx, p->active_lo_mask, p->active_hi_mask,
			       p->msecs_asserted, p->msecs_recovery);
		break;
	case CX18_GPIO_RESET_Z8F0811:
		/*
		 * Assert timing for the Z8F0811 on HVR-1600 boards:
		 * 1. Assert RESET for min of 4 clock cycles at 18.432 MHz to
		 *    initiate
		 * 2. Reset then takes 66 WDT cycles at 10 kHz + 16 xtal clock
		 *    cycles (6,601,085 nanoseconds ~= 7 milliseconds)
		 * 3. DBG pin must be high before chip exits reset for normal
		 *    operation.  DBG is open drain and hopefully pulled high
		 *    since we don't normally drive it (GPIO 1?) for the
		 *    HVR-1600
		 * 4. Z8F0811 won't exit reset until RESET is deasserted
		 * 5. Zilog comes out of reset, loads reset vector address and
		 *    executes from there. Required recovery delay unknown.
		 */
		gpio_reset_seq(cx, p->ir_reset_mask, 0,
			       p->msecs_asserted, p->msecs_recovery);
		break;
	case CX18_GPIO_RESET_XC2028:
		if (cx->card->tuners[0].tuner == TUNER_XC2028)
			gpio_reset_seq(cx, (1 << cx->card->xceive_pin), 0,
				       1, 1);
		break;
	}
	return 0;
}

static const struct v4l2_subdev_core_ops resetctrl_core_ops = {
	.log_status = resetctrl_log_status,
	.reset = resetctrl_reset,
};

static const struct v4l2_subdev_ops resetctrl_ops = {
	.core = &resetctrl_core_ops,
};

/*
 * External entry points
 */
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

int cx18_gpio_register(struct cx18 *cx, u32 hw)
{
	struct v4l2_subdev *sd;
	const struct v4l2_subdev_ops *ops;
	char *str;

	switch (hw) {
	case CX18_HW_GPIO_MUX:
		sd = &cx->sd_gpiomux;
		ops = &gpiomux_ops;
		str = "gpio-mux";
		break;
	case CX18_HW_GPIO_RESET_CTRL:
		sd = &cx->sd_resetctrl;
		ops = &resetctrl_ops;
		str = "gpio-reset-ctrl";
		break;
	default:
		return -EINVAL;
	}

	v4l2_subdev_init(sd, ops);
	v4l2_set_subdevdata(sd, cx);
	snprintf(sd->name, sizeof(sd->name), "%s %s", cx->v4l2_dev.name, str);
	sd->grp_id = hw;
	return v4l2_device_register_subdev(&cx->v4l2_dev, sd);
}

void cx18_reset_ir_gpio(void *data)
{
	struct cx18 *cx = to_cx18((struct v4l2_device *)data);

	if (cx->card->gpio_i2c_slave_reset.ir_reset_mask == 0)
		return;

	CX18_DEBUG_INFO("Resetting IR microcontroller\n");

	v4l2_subdev_call(&cx->sd_resetctrl,
			 core, reset, CX18_GPIO_RESET_Z8F0811);
}
EXPORT_SYMBOL(cx18_reset_ir_gpio);
/* This symbol is exported for use by lirc_pvr150 for the IR-blaster */

/* Xceive tuner reset function */
int cx18_reset_tuner_gpio(void *dev, int component, int cmd, int value)
{
	struct i2c_algo_bit_data *algo = dev;
	struct cx18_i2c_algo_callback_data *cb_data = algo->data;
	struct cx18 *cx = cb_data->cx;

	if (cmd != XC2028_TUNER_RESET ||
	    cx->card->tuners[0].tuner != TUNER_XC2028)
		return 0;

	CX18_DEBUG_INFO("Resetting XCeive tuner\n");
	return v4l2_subdev_call(&cx->sd_resetctrl,
				core, reset, CX18_GPIO_RESET_XC2028);
}
