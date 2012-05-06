/*
 * Fitipower FC0012 tuner driver
 *
 * Copyright (C) 2012 Hans-Frieder Vogt <hfvogt@gmx.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "fc0012.h"
#include "fc0012-priv.h"

static int fc0012_writereg(struct fc0012_priv *priv, u8 reg, u8 val)
{
	u8 buf[2] = {reg, val};
	struct i2c_msg msg = {
		.addr = priv->addr, .flags = 0, .buf = buf, .len = 2
	};

	if (i2c_transfer(priv->i2c, &msg, 1) != 1) {
		err("I2C write reg failed, reg: %02x, val: %02x", reg, val);
		return -EREMOTEIO;
	}
	return 0;
}

static int fc0012_readreg(struct fc0012_priv *priv, u8 reg, u8 *val)
{
	struct i2c_msg msg[2] = {
		{ .addr = priv->addr, .flags = 0, .buf = &reg, .len = 1 },
		{ .addr = priv->addr, .flags = I2C_M_RD, .buf = val, .len = 1 },
	};

	if (i2c_transfer(priv->i2c, msg, 2) != 2) {
		err("I2C read reg failed, reg: %02x", reg);
		return -EREMOTEIO;
	}
	return 0;
}

static int fc0012_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
	return 0;
}

static int fc0012_init(struct dvb_frontend *fe)
{
	struct fc0012_priv *priv = fe->tuner_priv;
	int i, ret = 0;
	unsigned char reg[] = {
		0x00,	/* dummy reg. 0 */
		0x05,	/* reg. 0x01 */
		0x10,	/* reg. 0x02 */
		0x00,	/* reg. 0x03 */
		0x00,	/* reg. 0x04 */
		0x0f,	/* reg. 0x05: may also be 0x0a */
		0x00,	/* reg. 0x06: divider 2, VCO slow */
		0x00,	/* reg. 0x07: may also be 0x0f */
		0xff,	/* reg. 0x08: AGC Clock divide by 256, AGC gain 1/256,
			   Loop Bw 1/8 */
		0x6e,	/* reg. 0x09: Disable LoopThrough, Enable LoopThrough: 0x6f */
		0xb8,	/* reg. 0x0a: Disable LO Test Buffer */
		0x82,	/* reg. 0x0b: Output Clock is same as clock frequency,
			   may also be 0x83 */
		0xfc,	/* reg. 0x0c: depending on AGC Up-Down mode, may need 0xf8 */
		0x02,	/* reg. 0x0d: AGC Not Forcing & LNA Forcing, 0x02 for DVB-T */
		0x00,	/* reg. 0x0e */
		0x00,	/* reg. 0x0f */
		0x00,	/* reg. 0x10: may also be 0x0d */
		0x00,	/* reg. 0x11 */
		0x1f,	/* reg. 0x12: Set to maximum gain */
		0x08,	/* reg. 0x13: Set to Middle Gain: 0x08,
			   Low Gain: 0x00, High Gain: 0x10, enable IX2: 0x80 */
		0x00,	/* reg. 0x14 */
		0x04,	/* reg. 0x15: Enable LNA COMPS */
	};

	switch (priv->xtal_freq) {
	case FC_XTAL_27_MHZ:
	case FC_XTAL_28_8_MHZ:
		reg[0x07] |= 0x20;
		break;
	case FC_XTAL_36_MHZ:
	default:
		break;
	}

	if (priv->dual_master)
		reg[0x0c] |= 0x02;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1); /* open I2C-gate */

	for (i = 1; i < sizeof(reg); i++) {
		ret = fc0012_writereg(priv, i, reg[i]);
		if (ret)
			break;
	}

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0); /* close I2C-gate */

	if (ret)
		err("fc0012_writereg failed: %d", ret);

	return ret;
}

static int fc0012_sleep(struct dvb_frontend *fe)
{
	/* nothing to do here */
	return 0;
}

static int fc0012_set_params(struct dvb_frontend *fe)
{
	struct fc0012_priv *priv = fe->tuner_priv;
	int i, ret = 0;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	u32 freq = p->frequency / 1000;
	u32 delsys = p->delivery_system;
	unsigned char reg[7], am, pm, multi, tmp;
	unsigned long f_vco;
	unsigned short xtal_freq_khz_2, xin, xdiv;
	int vco_select = false;

	if (fe->callback) {
		ret = fe->callback(priv->i2c, DVB_FRONTEND_COMPONENT_TUNER,
			FC_FE_CALLBACK_VHF_ENABLE, (freq > 300000 ? 0 : 1));
		if (ret)
			goto exit;
	}

	switch (priv->xtal_freq) {
	case FC_XTAL_27_MHZ:
		xtal_freq_khz_2 = 27000 / 2;
		break;
	case FC_XTAL_36_MHZ:
		xtal_freq_khz_2 = 36000 / 2;
		break;
	case FC_XTAL_28_8_MHZ:
	default:
		xtal_freq_khz_2 = 28800 / 2;
		break;
	}

	/* select frequency divider and the frequency of VCO */
	if (freq < 37084) {		/* freq * 96 < 3560000 */
		multi = 96;
		reg[5] = 0x82;
		reg[6] = 0x00;
	} else if (freq < 55625) {	/* freq * 64 < 3560000 */
		multi = 64;
		reg[5] = 0x82;
		reg[6] = 0x02;
	} else if (freq < 74167) {	/* freq * 48 < 3560000 */
		multi = 48;
		reg[5] = 0x42;
		reg[6] = 0x00;
	} else if (freq < 111250) {	/* freq * 32 < 3560000 */
		multi = 32;
		reg[5] = 0x42;
		reg[6] = 0x02;
	} else if (freq < 148334) {	/* freq * 24 < 3560000 */
		multi = 24;
		reg[5] = 0x22;
		reg[6] = 0x00;
	} else if (freq < 222500) {	/* freq * 16 < 3560000 */
		multi = 16;
		reg[5] = 0x22;
		reg[6] = 0x02;
	} else if (freq < 296667) {	/* freq * 12 < 3560000 */
		multi = 12;
		reg[5] = 0x12;
		reg[6] = 0x00;
	} else if (freq < 445000) {	/* freq * 8 < 3560000 */
		multi = 8;
		reg[5] = 0x12;
		reg[6] = 0x02;
	} else if (freq < 593334) {	/* freq * 6 < 3560000 */
		multi = 6;
		reg[5] = 0x0a;
		reg[6] = 0x00;
	} else {
		multi = 4;
		reg[5] = 0x0a;
		reg[6] = 0x02;
	}

	f_vco = freq * multi;

	if (f_vco >= 3060000) {
		reg[6] |= 0x08;
		vco_select = true;
	}

	if (freq >= 45000) {
		/* From divided value (XDIV) determined the FA and FP value */
		xdiv = (unsigned short)(f_vco / xtal_freq_khz_2);
		if ((f_vco - xdiv * xtal_freq_khz_2) >= (xtal_freq_khz_2 / 2))
			xdiv++;

		pm = (unsigned char)(xdiv / 8);
		am = (unsigned char)(xdiv - (8 * pm));

		if (am < 2) {
			reg[1] = am + 8;
			reg[2] = pm - 1;
		} else {
			reg[1] = am;
			reg[2] = pm;
		}
	} else {
		/* fix for frequency less than 45 MHz */
		reg[1] = 0x06;
		reg[2] = 0x11;
	}

	/* fix clock out */
	reg[6] |= 0x20;

	/* From VCO frequency determines the XIN ( fractional part of Delta
	   Sigma PLL) and divided value (XDIV) */
	xin = (unsigned short)(f_vco - (f_vco / xtal_freq_khz_2) * xtal_freq_khz_2);
	xin = (xin << 15) / xtal_freq_khz_2;
	if (xin >= 16384)
		xin += 32768;

	reg[3] = xin >> 8;	/* xin with 9 bit resolution */
	reg[4] = xin & 0xff;

	if (delsys == SYS_DVBT) {
		reg[6] &= 0x3f;	/* bits 6 and 7 describe the bandwidth */
		switch (p->bandwidth_hz) {
		case 6000000:
			reg[6] |= 0x80;
			break;
		case 7000000:
			reg[6] |= 0x40;
			break;
		case 8000000:
		default:
			break;
		}
	} else {
		err("%s: modulation type not supported!", __func__);
		return -EINVAL;
	}

	/* modified for Realtek demod */
	reg[5] |= 0x07;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1); /* open I2C-gate */

	for (i = 1; i <= 6; i++) {
		ret = fc0012_writereg(priv, i, reg[i]);
		if (ret)
			goto exit;
	}

	/* VCO Calibration */
	ret = fc0012_writereg(priv, 0x0e, 0x80);
	if (!ret)
		ret = fc0012_writereg(priv, 0x0e, 0x00);

	/* VCO Re-Calibration if needed */
	if (!ret)
		ret = fc0012_writereg(priv, 0x0e, 0x00);

	if (!ret) {
		msleep(10);
		ret = fc0012_readreg(priv, 0x0e, &tmp);
	}
	if (ret)
		goto exit;

	/* vco selection */
	tmp &= 0x3f;

	if (vco_select) {
		if (tmp > 0x3c) {
			reg[6] &= ~0x08;
			ret = fc0012_writereg(priv, 0x06, reg[6]);
			if (!ret)
				ret = fc0012_writereg(priv, 0x0e, 0x80);
			if (!ret)
				ret = fc0012_writereg(priv, 0x0e, 0x00);
		}
	} else {
		if (tmp < 0x02) {
			reg[6] |= 0x08;
			ret = fc0012_writereg(priv, 0x06, reg[6]);
			if (!ret)
				ret = fc0012_writereg(priv, 0x0e, 0x80);
			if (!ret)
				ret = fc0012_writereg(priv, 0x0e, 0x00);
		}
	}

	priv->frequency = p->frequency;
	priv->bandwidth = p->bandwidth_hz;

exit:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0); /* close I2C-gate */
	if (ret)
		warn("%s: failed: %d", __func__, ret);
	return ret;
}

static int fc0012_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct fc0012_priv *priv = fe->tuner_priv;
	*frequency = priv->frequency;
	return 0;
}

static int fc0012_get_if_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	/* CHECK: always ? */
	*frequency = 0;
	return 0;
}

static int fc0012_get_bandwidth(struct dvb_frontend *fe, u32 *bandwidth)
{
	struct fc0012_priv *priv = fe->tuner_priv;
	*bandwidth = priv->bandwidth;
	return 0;
}


static const struct dvb_tuner_ops fc0012_tuner_ops = {
	.info = {
		.name           = "Fitipower FC0012",

		.frequency_min  = 37000000,	/* estimate */
		.frequency_max  = 862000000,	/* estimate */
		.frequency_step = 0,
	},

	.release	= fc0012_release,

	.init		= fc0012_init,
	.sleep		= fc0012_sleep,

	.set_params	= fc0012_set_params,

	.get_frequency	= fc0012_get_frequency,
	.get_if_frequency = fc0012_get_if_frequency,
	.get_bandwidth	= fc0012_get_bandwidth,
};

struct dvb_frontend *fc0012_attach(struct dvb_frontend *fe,
	struct i2c_adapter *i2c, u8 i2c_address, int dual_master,
	enum fc001x_xtal_freq xtal_freq)
{
	struct fc0012_priv *priv = NULL;

	priv = kzalloc(sizeof(struct fc0012_priv), GFP_KERNEL);
	if (priv == NULL)
		return NULL;

	priv->i2c = i2c;
	priv->dual_master = dual_master;
	priv->addr = i2c_address;
	priv->xtal_freq = xtal_freq;

	info("Fitipower FC0012 successfully attached.");

	fe->tuner_priv = priv;

	memcpy(&fe->ops.tuner_ops, &fc0012_tuner_ops,
		sizeof(struct dvb_tuner_ops));

	return fe;
}
EXPORT_SYMBOL(fc0012_attach);

MODULE_DESCRIPTION("Fitipower FC0012 silicon tuner driver");
MODULE_AUTHOR("Hans-Frieder Vogt <hfvogt@gmx.net>");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.5");
