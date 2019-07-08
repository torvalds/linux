// SPDX-License-Identifier: GPL-2.0-or-later
/*
	TDA8261 8PSK/QPSK tuner driver
	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

*/


#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <media/dvb_frontend.h>
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
		pr_err("%s: read error, err=%d\n", __func__, err);

	return err;
}

static int tda8261_write(struct tda8261_state *state, u8 *buf)
{
	const struct tda8261_config *config = state->config;
	int err = 0;
	struct i2c_msg msg = { .addr = config->addr, .flags = 0, .buf = buf, .len = 4 };

	if ((err = i2c_transfer(state->i2c, &msg, 1)) != 1)
		pr_err("%s: write error, err=%d\n", __func__, err);

	return err;
}

static int tda8261_get_status(struct dvb_frontend *fe, u32 *status)
{
	struct tda8261_state *state = fe->tuner_priv;
	u8 result = 0;
	int err = 0;

	*status = 0;

	if ((err = tda8261_read(state, &result)) < 0) {
		pr_err("%s: I/O Error\n", __func__);
		return err;
	}
	if ((result >> 6) & 0x01) {
		pr_debug("%s: Tuner Phase Locked\n", __func__);
		*status = 1;
	}

	return err;
}

static const u32 div_tab[] = { 2000, 1000,  500,  250,  125 }; /* kHz */
static const u8  ref_div[] = { 0x00, 0x01, 0x02, 0x05, 0x07 };

static int tda8261_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct tda8261_state *state = fe->tuner_priv;

	*frequency = state->frequency;

	return 0;
}

static int tda8261_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct tda8261_state *state = fe->tuner_priv;
	const struct tda8261_config *config = state->config;
	u32 frequency, N, status = 0;
	u8 buf[4];
	int err = 0;

	/*
	 * N = Max VCO Frequency / Channel Spacing
	 * Max VCO Frequency = VCO frequency + (channel spacing - 1)
	 * (to account for half channel spacing on either side)
	 */
	frequency = c->frequency;
	if ((frequency < 950000) || (frequency > 2150000)) {
		pr_warn("%s: Frequency beyond limits, frequency=%d\n",
			__func__, frequency);
		return -EINVAL;
	}
	N = (frequency + (div_tab[config->step_size] - 1)) / div_tab[config->step_size];
	pr_debug("%s: Step size=%d, Divider=%d, PG=0x%02x (%d)\n",
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
	err = tda8261_write(state, buf);
	if (err < 0) {
		pr_err("%s: I/O Error\n", __func__);
		return err;
	}
	/* sleep for some time */
	pr_debug("%s: Waiting to Phase LOCK\n", __func__);
	msleep(20);
	/* check status */
	if ((err = tda8261_get_status(fe, &status)) < 0) {
		pr_err("%s: I/O Error\n", __func__);
		return err;
	}
	if (status == 1) {
		pr_debug("%s: Tuner Phase locked: status=%d\n", __func__,
			 status);
		state->frequency = frequency; /* cache successful state */
	} else {
		pr_debug("%s: No Phase lock: status=%d\n", __func__, status);
	}

	return 0;
}

static void tda8261_release(struct dvb_frontend *fe)
{
	struct tda8261_state *state = fe->tuner_priv;

	fe->tuner_priv = NULL;
	kfree(state);
}

static const struct dvb_tuner_ops tda8261_ops = {

	.info = {
		.name		   = "TDA8261",
		.frequency_min_hz  =  950 * MHz,
		.frequency_max_hz  = 2150 * MHz,
	},

	.set_params	= tda8261_set_params,
	.get_frequency	= tda8261_get_frequency,
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

	fe->ops.tuner_ops.info.frequency_step_hz = div_tab[config->step_size] * kHz;

	pr_info("%s: Attaching TDA8261 8PSK/QPSK tuner\n", __func__);

	return fe;

exit:
	kfree(state);
	return NULL;
}

EXPORT_SYMBOL(tda8261_attach);

MODULE_AUTHOR("Manu Abraham");
MODULE_DESCRIPTION("TDA8261 8PSK/QPSK Tuner");
MODULE_LICENSE("GPL");
