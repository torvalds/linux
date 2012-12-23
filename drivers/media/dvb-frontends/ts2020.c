/*
    Montage Technology TS2020 - Silicon Tuner driver
    Copyright (C) 2009-2012 Konstantin Dimitrov <kosio.dimitrov@gmail.com>

    Copyright (C) 2009-2012 TurboSight.com

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

#include "dvb_frontend.h"
#include "ts2020.h"

#define TS2020_XTAL_FREQ   27000 /* in kHz */

struct ts2020_state {
	u8 tuner_address;
	struct i2c_adapter *i2c;
};

static int ts2020_readreg(struct dvb_frontend *fe, u8 reg)
{
	struct ts2020_state *state = fe->tuner_priv;

	int ret;
	u8 b0[] = { reg };
	u8 b1[] = { 0 };
	struct i2c_msg msg[] = {
		{
			.addr = state->tuner_address,
			.flags = 0,
			.buf = b0,
			.len = 1
		}, {
			.addr = state->tuner_address,
			.flags = I2C_M_RD,
			.buf = b1,
			.len = 1
		}
	};

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	ret = i2c_transfer(state->i2c, msg, 2);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	if (ret != 2) {
		printk(KERN_ERR "%s: reg=0x%x(error=%d)\n", __func__, reg, ret);
		return ret;
	}

	return b1[0];
}

static int ts2020_writereg(struct dvb_frontend *fe, int reg, int data)
{
	struct ts2020_state *state = fe->tuner_priv;

	u8 buf[] = { reg, data };
	struct i2c_msg msg = { .addr = state->tuner_address,
		.flags = 0, .buf = buf, .len = 2 };
	int err;


	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	err = i2c_transfer(state->i2c, &msg, 1);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	if (err != 1) {
		printk(KERN_ERR "%s: writereg error(err == %i, reg == 0x%02x,"
			 " value == 0x%02x)\n", __func__, err, reg, data);
		return -EREMOTEIO;
	}

	return 0;
}

static int ts2020_init(struct dvb_frontend *fe)
{
	ts2020_writereg(fe, 0x42, 0x73);
	ts2020_writereg(fe, 0x05, 0x01);
	ts2020_writereg(fe, 0x62, 0xf5);
	return 0;
}

static int ts2020_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	u16 ndiv, div4;

	div4 = (ts2020_readreg(fe, 0x10) & 0x10) >> 4;

	ndiv = ts2020_readreg(fe, 0x01);
	ndiv &= 0x0f;
	ndiv <<= 8;
	ndiv |= ts2020_readreg(fe, 0x02);

	/* actual tuned frequency, i.e. including the offset */
	*frequency = (ndiv - ndiv % 2 + 1024) * TS2020_XTAL_FREQ
		/ (6 + 8) / (div4 + 1) / 2;

	return 0;
}

static int ts2020_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	u8 mlpf, mlpf_new, mlpf_max, mlpf_min, nlpf, div4;
	u16 value, ndiv;
	u32 srate = 0, f3db;

	ts2020_init(fe);

	/* unknown */
	ts2020_writereg(fe, 0x07, 0x02);
	ts2020_writereg(fe, 0x10, 0x00);
	ts2020_writereg(fe, 0x60, 0x79);
	ts2020_writereg(fe, 0x08, 0x01);
	ts2020_writereg(fe, 0x00, 0x01);
	div4 = 0;

	/* calculate and set freq divider */
	if (c->frequency < 1146000) {
		ts2020_writereg(fe, 0x10, 0x11);
		div4 = 1;
		ndiv = ((c->frequency * (6 + 8) * 4) +
				(TS2020_XTAL_FREQ / 2)) /
				TS2020_XTAL_FREQ - 1024;
	} else {
		ts2020_writereg(fe, 0x10, 0x01);
		ndiv = ((c->frequency * (6 + 8) * 2) +
				(TS2020_XTAL_FREQ / 2)) /
				TS2020_XTAL_FREQ - 1024;
	}

	ts2020_writereg(fe, 0x01, (ndiv & 0x0f00) >> 8);
	ts2020_writereg(fe, 0x02, ndiv & 0x00ff);

	/* set pll */
	ts2020_writereg(fe, 0x03, 0x06);
	ts2020_writereg(fe, 0x51, 0x0f);
	ts2020_writereg(fe, 0x51, 0x1f);
	ts2020_writereg(fe, 0x50, 0x10);
	ts2020_writereg(fe, 0x50, 0x00);
	msleep(5);

	/* unknown */
	ts2020_writereg(fe, 0x51, 0x17);
	ts2020_writereg(fe, 0x51, 0x1f);
	ts2020_writereg(fe, 0x50, 0x08);
	ts2020_writereg(fe, 0x50, 0x00);
	msleep(5);

	value = ts2020_readreg(fe, 0x3d);
	value &= 0x0f;
	if ((value > 4) && (value < 15)) {
		value -= 3;
		if (value < 4)
			value = 4;
		value = ((value << 3) | 0x01) & 0x79;
	}

	ts2020_writereg(fe, 0x60, value);
	ts2020_writereg(fe, 0x51, 0x17);
	ts2020_writereg(fe, 0x51, 0x1f);
	ts2020_writereg(fe, 0x50, 0x08);
	ts2020_writereg(fe, 0x50, 0x00);

	/* set low-pass filter period */
	ts2020_writereg(fe, 0x04, 0x2e);
	ts2020_writereg(fe, 0x51, 0x1b);
	ts2020_writereg(fe, 0x51, 0x1f);
	ts2020_writereg(fe, 0x50, 0x04);
	ts2020_writereg(fe, 0x50, 0x00);
	msleep(5);

	srate = c->symbol_rate / 1000;

	f3db = (srate << 2) / 5 + 2000;
	if (srate < 5000)
		f3db += 3000;
	if (f3db < 7000)
		f3db = 7000;
	if (f3db > 40000)
		f3db = 40000;

	/* set low-pass filter baseband */
	value = ts2020_readreg(fe, 0x26);
	mlpf = 0x2e * 207 / ((value << 1) + 151);
	mlpf_max = mlpf * 135 / 100;
	mlpf_min = mlpf * 78 / 100;
	if (mlpf_max > 63)
		mlpf_max = 63;

	/* rounded to the closest integer */
	nlpf = ((mlpf * f3db * 1000) + (2766 * TS2020_XTAL_FREQ / 2))
			/ (2766 * TS2020_XTAL_FREQ);
	if (nlpf > 23)
		nlpf = 23;
	if (nlpf < 1)
		nlpf = 1;

	/* rounded to the closest integer */
	mlpf_new = ((TS2020_XTAL_FREQ * nlpf * 2766) +
			(1000 * f3db / 2)) / (1000 * f3db);

	if (mlpf_new < mlpf_min) {
		nlpf++;
		mlpf_new = ((TS2020_XTAL_FREQ * nlpf * 2766) +
				(1000 * f3db / 2)) / (1000 * f3db);
	}

	if (mlpf_new > mlpf_max)
		mlpf_new = mlpf_max;

	ts2020_writereg(fe, 0x04, mlpf_new);
	ts2020_writereg(fe, 0x06, nlpf);
	ts2020_writereg(fe, 0x51, 0x1b);
	ts2020_writereg(fe, 0x51, 0x1f);
	ts2020_writereg(fe, 0x50, 0x04);
	ts2020_writereg(fe, 0x50, 0x00);
	msleep(5);

	/* unknown */
	ts2020_writereg(fe, 0x51, 0x1e);
	ts2020_writereg(fe, 0x51, 0x1f);
	ts2020_writereg(fe, 0x50, 0x01);
	ts2020_writereg(fe, 0x50, 0x00);
	msleep(60);

	return 0;
}

static int ts2020_release(struct dvb_frontend *fe)
{
	struct ts2020_state *state = fe->tuner_priv;

	fe->tuner_priv = NULL;
	kfree(state);

	return 0;
}

int ts2020_get_signal_strength(struct dvb_frontend *fe,
	u16 *signal_strength)
{
	u16 sig_reading, sig_strength;
	u8 rfgain, bbgain;

	rfgain = ts2020_readreg(fe, 0x3d) & 0x1f;
	bbgain = ts2020_readreg(fe, 0x21) & 0x1f;

	if (rfgain > 15)
		rfgain = 15;
	if (bbgain > 13)
		bbgain = 13;

	sig_reading = rfgain * 2 + bbgain * 3;

	sig_strength = 40 + (64 - sig_reading) * 50 / 64 ;

	/* cook the value to be suitable for szap-s2 human readable output */
	*signal_strength = sig_strength * 1000;

	return 0;
}

static struct dvb_tuner_ops ts2020_ops = {
	.info = {
		.name = "Montage Technology TS2020 Silicon Tuner",
		.frequency_min = 950000,
		.frequency_max = 2150000,
	},

	.init = ts2020_init,
	.release = ts2020_release,
	.set_params = ts2020_set_params,
	.get_frequency = ts2020_get_frequency,
	.get_rf_strength = ts2020_get_signal_strength
};

struct dvb_frontend *ts2020_attach(struct dvb_frontend *fe,
	const struct ts2020_config *config, struct i2c_adapter *i2c)
{
	struct ts2020_state *state = NULL;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct ts2020_state), GFP_KERNEL);
	if (!state)
		return NULL;

	/* setup the state */
	state->tuner_address = config->tuner_address;
	state->i2c = i2c;
	fe->tuner_priv = state;
	fe->ops.tuner_ops = ts2020_ops;
	fe->ops.read_signal_strength = fe->ops.tuner_ops.get_rf_strength;

	return fe;
}
EXPORT_SYMBOL(ts2020_attach);

MODULE_AUTHOR("Konstantin Dimitrov <kosio.dimitrov@gmail.com>");
MODULE_DESCRIPTION("Montage Technology TS2020 - Silicon tuner driver module");
MODULE_LICENSE("GPL");
