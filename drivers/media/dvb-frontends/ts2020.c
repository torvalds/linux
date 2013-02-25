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
#define FREQ_OFFSET_LOW_SYM_RATE 3000

struct ts2020_priv {
	/* i2c details */
	int i2c_address;
	struct i2c_adapter *i2c;
	u8 clk_out_div;
	u32 frequency;
};

static int ts2020_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
	return 0;
}

static int ts2020_writereg(struct dvb_frontend *fe, int reg, int data)
{
	struct ts2020_priv *priv = fe->tuner_priv;
	u8 buf[] = { reg, data };
	struct i2c_msg msg[] = {
		{
			.addr = priv->i2c_address,
			.flags = 0,
			.buf = buf,
			.len = 2
		}
	};
	int err;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	err = i2c_transfer(priv->i2c, msg, 1);
	if (err != 1) {
		printk(KERN_ERR
		       "%s: writereg error(err == %i, reg == 0x%02x, value == 0x%02x)\n",
		       __func__, err, reg, data);
		return -EREMOTEIO;
	}

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return 0;
}

static int ts2020_readreg(struct dvb_frontend *fe, u8 reg)
{
	struct ts2020_priv *priv = fe->tuner_priv;
	int ret;
	u8 b0[] = { reg };
	u8 b1[] = { 0 };
	struct i2c_msg msg[] = {
		{
			.addr = priv->i2c_address,
			.flags = 0,
			.buf = b0,
			.len = 1
		}, {
			.addr = priv->i2c_address,
			.flags = I2C_M_RD,
			.buf = b1,
			.len = 1
		}
	};

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	ret = i2c_transfer(priv->i2c, msg, 2);

	if (ret != 2) {
		printk(KERN_ERR "%s: reg=0x%x(error=%d)\n",
		       __func__, reg, ret);
		return ret;
	}

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return b1[0];
}

static int ts2020_sleep(struct dvb_frontend *fe)
{
	struct ts2020_priv *priv = fe->tuner_priv;
	int ret;
	u8 buf[] = { 10, 0 };
	struct i2c_msg msg = {
		.addr = priv->i2c_address,
		.flags = 0,
		.buf = buf,
		.len = 2
	};

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	ret = i2c_transfer(priv->i2c, &msg, 1);
	if (ret != 1)
		printk(KERN_ERR "%s: i2c error\n", __func__);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return (ret == 1) ? 0 : ret;
}

static int ts2020_init(struct dvb_frontend *fe)
{
	struct ts2020_priv *priv = fe->tuner_priv;

	ts2020_writereg(fe, 0x42, 0x73);
	ts2020_writereg(fe, 0x05, priv->clk_out_div);
	ts2020_writereg(fe, 0x20, 0x27);
	ts2020_writereg(fe, 0x07, 0x02);
	ts2020_writereg(fe, 0x11, 0xff);
	ts2020_writereg(fe, 0x60, 0xf9);
	ts2020_writereg(fe, 0x08, 0x01);
	ts2020_writereg(fe, 0x00, 0x41);

	return 0;
}

static int ts2020_tuner_gate_ctrl(struct dvb_frontend *fe, u8 offset)
{
	int ret;
	ret = ts2020_writereg(fe, 0x51, 0x1f - offset);
	ret |= ts2020_writereg(fe, 0x51, 0x1f);
	ret |= ts2020_writereg(fe, 0x50, offset);
	ret |= ts2020_writereg(fe, 0x50, 0x00);
	msleep(20);
	return ret;
}

static int ts2020_set_tuner_rf(struct dvb_frontend *fe)
{
	int reg;

	reg = ts2020_readreg(fe, 0x3d);
	reg &= 0x7f;
	if (reg < 0x16)
		reg = 0xa1;
	else if (reg == 0x16)
		reg = 0x99;
	else
		reg = 0xf9;

	ts2020_writereg(fe, 0x60, reg);
	reg = ts2020_tuner_gate_ctrl(fe, 0x08);

	return reg;
}

static int ts2020_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct ts2020_priv *priv = fe->tuner_priv;
	int ret;
	u32 frequency = c->frequency;
	s32 offset_khz;
	u32 symbol_rate = (c->symbol_rate / 1000);
	u32 f3db, gdiv28;
	u16 value, ndiv, lpf_coeff;
	u8 lpf_mxdiv, mlpf_max, mlpf_min, nlpf;
	u8 lo = 0x01, div4 = 0x0;

	/* Calculate frequency divider */
	if (frequency < 1060000) {
		lo |= 0x10;
		div4 = 0x1;
		ndiv = (frequency * 14 * 4) / TS2020_XTAL_FREQ;
	} else
		ndiv = (frequency * 14 * 2) / TS2020_XTAL_FREQ;
	ndiv = ndiv + ndiv % 2;
	ndiv = ndiv - 1024;

	ret = ts2020_writereg(fe, 0x10, 0x80 | lo);

	/* Set frequency divider */
	ret |= ts2020_writereg(fe, 0x01, (ndiv >> 8) & 0xf);
	ret |= ts2020_writereg(fe, 0x02, ndiv & 0xff);

	ret |= ts2020_writereg(fe, 0x03, 0x06);
	ret |= ts2020_tuner_gate_ctrl(fe, 0x10);
	if (ret < 0)
		return -ENODEV;

	/* Tuner Frequency Range */
	ret = ts2020_writereg(fe, 0x10, lo);

	ret |= ts2020_tuner_gate_ctrl(fe, 0x08);

	/* Tuner RF */
	ret |= ts2020_set_tuner_rf(fe);

	gdiv28 = (TS2020_XTAL_FREQ / 1000 * 1694 + 500) / 1000;
	ret |= ts2020_writereg(fe, 0x04, gdiv28 & 0xff);
	ret |= ts2020_tuner_gate_ctrl(fe, 0x04);
	if (ret < 0)
		return -ENODEV;

	value = ts2020_readreg(fe, 0x26);

	f3db = (symbol_rate * 135) / 200 + 2000;
	f3db += FREQ_OFFSET_LOW_SYM_RATE;
	if (f3db < 7000)
		f3db = 7000;
	if (f3db > 40000)
		f3db = 40000;

	gdiv28 = gdiv28 * 207 / (value * 2 + 151);
	mlpf_max = gdiv28 * 135 / 100;
	mlpf_min = gdiv28 * 78 / 100;
	if (mlpf_max > 63)
		mlpf_max = 63;

	lpf_coeff = 2766;

	nlpf = (f3db * gdiv28 * 2 / lpf_coeff /
		(TS2020_XTAL_FREQ / 1000)  + 1) / 2;
	if (nlpf > 23)
		nlpf = 23;
	if (nlpf < 1)
		nlpf = 1;

	lpf_mxdiv = (nlpf * (TS2020_XTAL_FREQ / 1000)
		* lpf_coeff * 2  / f3db + 1) / 2;

	if (lpf_mxdiv < mlpf_min) {
		nlpf++;
		lpf_mxdiv = (nlpf * (TS2020_XTAL_FREQ / 1000)
			* lpf_coeff * 2  / f3db + 1) / 2;
	}

	if (lpf_mxdiv > mlpf_max)
		lpf_mxdiv = mlpf_max;

	ret = ts2020_writereg(fe, 0x04, lpf_mxdiv);
	ret |= ts2020_writereg(fe, 0x06, nlpf);

	ret |= ts2020_tuner_gate_ctrl(fe, 0x04);

	ret |= ts2020_tuner_gate_ctrl(fe, 0x01);

	msleep(80);
	/* calculate offset assuming 96000kHz*/
	offset_khz = (ndiv - ndiv % 2 + 1024) * TS2020_XTAL_FREQ
		/ (6 + 8) / (div4 + 1) / 2;

	priv->frequency = offset_khz;

	return (ret < 0) ? -EINVAL : 0;
}

static int ts2020_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct ts2020_priv *priv = fe->tuner_priv;
	*frequency = priv->frequency;
	return 0;
}

/* read TS2020 signal strength */
static int ts2020_read_signal_strength(struct dvb_frontend *fe,
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

static struct dvb_tuner_ops ts2020_tuner_ops = {
	.info = {
		.name = "TS2020",
		.frequency_min = 950000,
		.frequency_max = 2150000
	},
	.init = ts2020_init,
	.release = ts2020_release,
	.sleep = ts2020_sleep,
	.set_params = ts2020_set_params,
	.get_frequency = ts2020_get_frequency,
	.get_rf_strength = ts2020_read_signal_strength,
};

struct dvb_frontend *ts2020_attach(struct dvb_frontend *fe,
					const struct ts2020_config *config,
					struct i2c_adapter *i2c)
{
	struct ts2020_priv *priv = NULL;
	u8 buf;

	priv = kzalloc(sizeof(struct ts2020_priv), GFP_KERNEL);
	if (priv == NULL)
		return NULL;

	priv->i2c_address = config->tuner_address;
	priv->i2c = i2c;
	priv->clk_out_div = config->clk_out_div;
	fe->tuner_priv = priv;

	/* Wake Up the tuner */
	if ((0x03 & ts2020_readreg(fe, 0x00)) == 0x00) {
		ts2020_writereg(fe, 0x00, 0x01);
		msleep(2);
	}

	ts2020_writereg(fe, 0x00, 0x03);
	msleep(2);

	/* Check the tuner version */
	buf = ts2020_readreg(fe, 0x00);
	if ((buf == 0x01) || (buf == 0x41) || (buf == 0x81))
		printk(KERN_INFO "%s: Find tuner TS2020!\n", __func__);
	else {
		printk(KERN_ERR "%s: Read tuner reg[0] = %d\n", __func__, buf);
		kfree(priv);
		return NULL;
	}

	memcpy(&fe->ops.tuner_ops, &ts2020_tuner_ops,
				sizeof(struct dvb_tuner_ops));

	return fe;
}
EXPORT_SYMBOL(ts2020_attach);

MODULE_AUTHOR("Konstantin Dimitrov <kosio.dimitrov@gmail.com>");
MODULE_DESCRIPTION("Montage Technology TS2020 - Silicon tuner driver module");
MODULE_LICENSE("GPL");
