/*
	TDA8261 8PSK/QPSK tuner driver
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

#include "dvb_frontend.h"
#include "tda8261.h"

struct tda8261_state {
	struct dvb_frontend		*fe;
	struct i2c_adapter		*i2c;
	const struct tda8261_config	*config;

	/* state cache */
	u32 frequency;
	u32 bandwidth;
};

static int tda8261_read(struct tda8261_state *state, u8 *buf)
{
	const struct tda8261_config *config = state->config;
	int err = 0;
	struct i2c_msg msg = { .addr	= config->addr, .flags = I2C_M_RD,.buf = buf,  .len = 1 };

	if ((err = i2c_transfer(state->i2c, &msg, 1)) != 1)
		printk("%s: read error, err=%d\n", __func__, err);

	return err;
}

static int tda8261_write(struct tda8261_state *state, u8 *buf)
{
	const struct tda8261_config *config = state->config;
	int err = 0;
	struct i2c_msg msg = { .addr = config->addr, .flags = 0, .buf = buf, .len = 4 };

	if ((err = i2c_transfer(state->i2c, &msg, 1)) != 1)
		printk("%s: write error, err=%d\n", __func__, err);

	return err;
}

static int tda8261_get_status(struct dvb_frontend *fe, u32 *status)
{
	struct tda8261_state *state = fe->tuner_priv;
	u8 result = 0;
	int err = 0;

	*status = 0;

	if ((err = tda8261_read(state, &result)) < 0) {
		printk("%s: I/O Error\n", __func__);
		return err;
	}
	if ((result >> 6) & 0x01) {
		printk("%s: Tuner Phase Locked\n", __func__);
		*status = 1;
	}

	return err;
}

static const u32 div_tab[] = { 2000, 1000,  500,  250,  125 }; /* kHz */
static const u8  ref_div[] = { 0x00, 0x01, 0x02, 0x05, 0x07 };

static int tda8261_get_state(struct dvb_frontend *fe,
			     enum tuner_param param,
			     struct tuner_state *tstate)
{
	struct tda8261_state *state = fe->tuner_priv;
	int err = 0;

	switch (param) {
	case DVBFE_TUNER_FREQUENCY:
		tstate->frequency = state->frequency;
		break;
	case DVBFE_TUNER_BANDWIDTH:
		tstate->bandwidth = 40000000; /* FIXME! need to calculate Bandwidth */
		break;
	default:
		printk("%s: Unknown parameter (param=%d)\n", __func__, param);
		err = -EINVAL;
		break;
	}

	return err;
}

static int tda8261_set_state(struct dvb_frontend *fe,
			     enum tuner_param param,
			     struct tuner_state *tstate)
{
	struct tda8261_state *state = fe->tuner_priv;
	const struct tda8261_config *config = state->config;
	u32 frequency, N, status = 0;
	u8 buf[4];
	int err = 0;

	if (param & DVBFE_TUNER_FREQUENCY) {
		/**
		 * N = Max VCO Frequency / Channel Spacing
		 * Max VCO Frequency = VCO frequency + (channel spacing - 1)
		 * (to account for half channel spacing on either side)
		 */
		frequency = tstate->frequency;
		if ((frequency < 950000) || (frequency > 2150000)) {
			printk("%s: Frequency beyond limits, frequency=%d\n", __func__, frequency);
			return -EINVAL;
		}
		N = (frequency + (div_tab[config->step_size] - 1)) / div_tab[config->step_size];
		printk("%s: Step size=%d, Divider=%d, PG=0x%02x (%d)\n",
			__func__, config->step_size, div_tab[config->step_size], N, N);

		buf[0] = (N >> 8) & 0xff;
		buf[1] = N & 0xff;
		buf[2] = (0x01 << 7) | ((ref_div[config->step_size] & 0x07) << 1);

		if (frequency < 1450000)
			buf[3] = 0x00;
		else if (frequency < 2000000)
			buf[3] = 0x40;
		else if (frequency < 2150000)
			buf[3] = 0x80;

		/* Set params */
		if ((err = tda8261_write(state, buf)) < 0) {
			printk("%s: I/O Error\n", __func__);
			return err;
		}
		/* sleep for some time */
		printk("%s: Waiting to Phase LOCK\n", __func__);
		msleep(20);
		/* check status */
		if ((err = tda8261_get_status(fe, &status)) < 0) {
			printk("%s: I/O Error\n", __func__);
			return err;
		}
		if (status == 1) {
			printk("%s: Tuner Phase locked: status=%d\n", __func__, status);
			state->frequency = frequency; /* cache successful state */
		} else {
			printk("%s: No Phase lock: status=%d\n", __func__, status);
		}
	} else {
		printk("%s: Unknown parameter (param=%d)\n", __func__, param);
		return -EINVAL;
	}

	return 0;
}

static int tda8261_release(struct dvb_frontend *fe)
{
	struct tda8261_state *state = fe->tuner_priv;

	fe->tuner_priv = NULL;
	kfree(state);
	return 0;
}

static struct dvb_tuner_ops tda8261_ops = {

	.info = {
		.name		= "TDA8261",
//		.tuner_name	= NULL,
		.frequency_min	=  950000,
		.frequency_max	= 2150000,
		.frequency_step = 0
	},

	.set_state	= tda8261_set_state,
	.get_state	= tda8261_get_state,
	.get_status	= tda8261_get_status,
	.release	= tda8261_release
};

struct dvb_frontend *tda8261_attach(struct dvb_frontend *fe,
				    const struct tda8261_config *config,
				    struct i2c_adapter *i2c)
{
	struct tda8261_state *state = NULL;

	if ((state = kzalloc(sizeof (struct tda8261_state), GFP_KERNEL)) == NULL)
		goto exit;

	state->config		= config;
	state->i2c		= i2c;
	state->fe		= fe;
	fe->tuner_priv		= state;
	fe->ops.tuner_ops	= tda8261_ops;

	fe->ops.tuner_ops.info.frequency_step = div_tab[config->step_size];
//	fe->ops.tuner_ops.tuner_name	 = &config->buf;

//	printk("%s: Attaching %s TDA8261 8PSK/QPSK tuner\n",
//		__func__, fe->ops.tuner_ops.tuner_name);
	printk("%s: Attaching TDA8261 8PSK/QPSK tuner\n", __func__);

	return fe;

exit:
	kfree(state);
	return NULL;
}

EXPORT_SYMBOL(tda8261_attach);
MODULE_PARM_DESC(verbose, "Set verbosity level");

MODULE_AUTHOR("Manu Abraham");
MODULE_DESCRIPTION("TDA8261 8PSK/QPSK Tuner");
MODULE_LICENSE("GPL");
