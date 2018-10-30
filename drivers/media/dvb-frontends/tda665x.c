/*
	TDA665x tuner driver
	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <media/dvb_frontend.h>
#include "tda665x.h"

struct tda665x_state {
	struct dvb_frontend		*fe;
	struct i2c_adapter		*i2c;
	const struct tda665x_config	*config;

	u32 frequency;
	u32 bandwidth;
};

static int tda665x_read(struct tda665x_state *state, u8 *buf)
{
	const struct tda665x_config *config = state->config;
	int err = 0;
	struct i2c_msg msg = { .addr = config->addr, .flags = I2C_M_RD, .buf = buf, .len = 2 };

	err = i2c_transfer(state->i2c, &msg, 1);
	if (err != 1)
		goto exit;

	return err;
exit:
	printk(KERN_ERR "%s: I/O Error err=<%d>\n", __func__, err);
	return err;
}

static int tda665x_write(struct tda665x_state *state, u8 *buf, u8 length)
{
	const struct tda665x_config *config = state->config;
	int err = 0;
	struct i2c_msg msg = { .addr = config->addr, .flags = 0, .buf = buf, .len = length };

	err = i2c_transfer(state->i2c, &msg, 1);
	if (err != 1)
		goto exit;

	return err;
exit:
	printk(KERN_ERR "%s: I/O Error err=<%d>\n", __func__, err);
	return err;
}

static int tda665x_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct tda665x_state *state = fe->tuner_priv;

	*frequency = state->frequency;

	return 0;
}

static int tda665x_get_status(struct dvb_frontend *fe, u32 *status)
{
	struct tda665x_state *state = fe->tuner_priv;
	u8 result = 0;
	int err = 0;

	*status = 0;

	err = tda665x_read(state, &result);
	if (err < 0)
		goto exit;

	if ((result >> 6) & 0x01) {
		printk(KERN_DEBUG "%s: Tuner Phase Locked\n", __func__);
		*status = 1;
	}

	return err;
exit:
	printk(KERN_ERR "%s: I/O Error\n", __func__);
	return err;
}

static int tda665x_set_frequency(struct dvb_frontend *fe,
				 u32 new_frequency)
{
	struct tda665x_state *state = fe->tuner_priv;
	const struct tda665x_config *config = state->config;
	u32 frequency, status = 0;
	u8 buf[4];
	int err = 0;

	if ((new_frequency < config->frequency_max)
	    || (new_frequency > config->frequency_min)) {
		printk(KERN_ERR "%s: Frequency beyond limits, frequency=%d\n",
		       __func__, new_frequency);
		return -EINVAL;
	}

	frequency = new_frequency;

	frequency += config->frequency_offst;
	frequency *= config->ref_multiplier;
	frequency += config->ref_divider >> 1;
	frequency /= config->ref_divider;

	buf[0] = (u8) ((frequency & 0x7f00) >> 8);
	buf[1] = (u8) (frequency & 0x00ff) >> 0;
	buf[2] = 0x80 | 0x40 | 0x02;
	buf[3] = 0x00;

	/* restore frequency */
	frequency = new_frequency;

	if (frequency < 153000000) {
		/* VHF-L */
		buf[3] |= 0x01; /* fc, Low Band, 47 - 153 MHz */
		if (frequency < 68000000)
			buf[3] |= 0x40; /* 83uA */
		if (frequency < 1040000000)
			buf[3] |= 0x60; /* 122uA */
		if (frequency < 1250000000)
			buf[3] |= 0x80; /* 163uA */
		else
			buf[3] |= 0xa0; /* 254uA */
	} else if (frequency < 438000000) {
		/* VHF-H */
		buf[3] |= 0x02; /* fc, Mid Band, 153 - 438 MHz */
		if (frequency < 230000000)
			buf[3] |= 0x40;
		if (frequency < 300000000)
			buf[3] |= 0x60;
		else
			buf[3] |= 0x80;
	} else {
		/* UHF */
		buf[3] |= 0x04; /* fc, High Band, 438 - 862 MHz */
		if (frequency < 470000000)
			buf[3] |= 0x60;
		if (frequency < 526000000)
			buf[3] |= 0x80;
		else
			buf[3] |= 0xa0;
	}

	/* Set params */
	err = tda665x_write(state, buf, 5);
	if (err < 0)
		goto exit;

	/* sleep for some time */
	printk(KERN_DEBUG "%s: Waiting to Phase LOCK\n", __func__);
	msleep(20);
	/* check status */
	err = tda665x_get_status(fe, &status);
	if (err < 0)
		goto exit;

	if (status == 1) {
		printk(KERN_DEBUG "%s: Tuner Phase locked: status=%d\n",
		       __func__, status);
		state->frequency = frequency; /* cache successful state */
	} else {
		printk(KERN_ERR "%s: No Phase lock: status=%d\n",
		       __func__, status);
	}

	return 0;
exit:
	printk(KERN_ERR "%s: I/O Error\n", __func__);
	return err;
}

static int tda665x_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	tda665x_set_frequency(fe, c->frequency);

	return 0;
}

static void tda665x_release(struct dvb_frontend *fe)
{
	struct tda665x_state *state = fe->tuner_priv;

	fe->tuner_priv = NULL;
	kfree(state);
}

static const struct dvb_tuner_ops tda665x_ops = {
	.get_status	= tda665x_get_status,
	.set_params	= tda665x_set_params,
	.get_frequency	= tda665x_get_frequency,
	.release	= tda665x_release
};

struct dvb_frontend *tda665x_attach(struct dvb_frontend *fe,
				    const struct tda665x_config *config,
				    struct i2c_adapter *i2c)
{
	struct tda665x_state *state = NULL;
	struct dvb_tuner_info *info;

	state = kzalloc(sizeof(struct tda665x_state), GFP_KERNEL);
	if (!state)
		return NULL;

	state->config		= config;
	state->i2c		= i2c;
	state->fe		= fe;
	fe->tuner_priv		= state;
	fe->ops.tuner_ops	= tda665x_ops;
	info			 = &fe->ops.tuner_ops.info;

	memcpy(info->name, config->name, sizeof(config->name));
	info->frequency_min_hz	= config->frequency_min;
	info->frequency_max_hz	= config->frequency_max;
	info->frequency_step_hz	= config->frequency_offst;

	printk(KERN_DEBUG "%s: Attaching TDA665x (%s) tuner\n", __func__, info->name);

	return fe;
}
EXPORT_SYMBOL(tda665x_attach);

MODULE_DESCRIPTION("TDA665x driver");
MODULE_AUTHOR("Manu Abraham");
MODULE_LICENSE("GPL");
