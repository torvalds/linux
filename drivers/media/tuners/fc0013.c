/*
 * Fitipower FC0013 tuner driver
 *
 * Copyright (C) 2012 Hans-Frieder Vogt <hfvogt@gmx.net>
 * partially based on driver code from Fitipower
 * Copyright (C) 2010 Fitipower Integrated Technology Inc
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 */

#include "fc0013.h"
#include "fc0013-priv.h"

static int fc0013_writereg(struct fc0013_priv *priv, u8 reg, u8 val)
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

static int fc0013_readreg(struct fc0013_priv *priv, u8 reg, u8 *val)
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

static void fc0013_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
}

static int fc0013_init(struct dvb_frontend *fe)
{
	struct fc0013_priv *priv = fe->tuner_priv;
	int i, ret = 0;
	unsigned char reg[] = {
		0x00,	/* reg. 0x00: dummy */
		0x09,	/* reg. 0x01 */
		0x16,	/* reg. 0x02 */
		0x00,	/* reg. 0x03 */
		0x00,	/* reg. 0x04 */
		0x17,	/* reg. 0x05 */
		0x02,	/* reg. 0x06 */
		0x0a,	/* reg. 0x07: CHECK */
		0xff,	/* reg. 0x08: AGC Clock divide by 256, AGC gain 1/256,
			   Loop Bw 1/8 */
		0x6f,	/* reg. 0x09: enable LoopThrough */
		0xb8,	/* reg. 0x0a: Disable LO Test Buffer */
		0x82,	/* reg. 0x0b: CHECK */
		0xfc,	/* reg. 0x0c: depending on AGC Up-Down mode, may need 0xf8 */
		0x01,	/* reg. 0x0d: AGC Not Forcing & LNA Forcing, may need 0x02 */
		0x00,	/* reg. 0x0e */
		0x00,	/* reg. 0x0f */
		0x00,	/* reg. 0x10 */
		0x00,	/* reg. 0x11 */
		0x00,	/* reg. 0x12 */
		0x00,	/* reg. 0x13 */
		0x50,	/* reg. 0x14: DVB-t High Gain, UHF.
			   Middle Gain: 0x48, Low Gain: 0x40 */
		0x01,	/* reg. 0x15 */
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
		ret = fc0013_writereg(priv, i, reg[i]);
		if (ret)
			break;
	}

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0); /* close I2C-gate */

	if (ret)
		err("fc0013_writereg failed: %d", ret);

	return ret;
}

static int fc0013_sleep(struct dvb_frontend *fe)
{
	/* nothing to do here */
	return 0;
}

int fc0013_rc_cal_add(struct dvb_frontend *fe, int rc_val)
{
	struct fc0013_priv *priv = fe->tuner_priv;
	int ret;
	u8 rc_cal;
	int val;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1); /* open I2C-gate */

	/* push rc_cal value, get rc_cal value */
	ret = fc0013_writereg(priv, 0x10, 0x00);
	if (ret)
		goto error_out;

	/* get rc_cal value */
	ret = fc0013_readreg(priv, 0x10, &rc_cal);
	if (ret)
		goto error_out;

	rc_cal &= 0x0f;

	val = (int)rc_cal + rc_val;

	/* forcing rc_cal */
	ret = fc0013_writereg(priv, 0x0d, 0x11);
	if (ret)
		goto error_out;

	/* modify rc_cal value */
	if (val > 15)
		ret = fc0013_writereg(priv, 0x10, 0x0f);
	else if (val < 0)
		ret = fc0013_writereg(priv, 0x10, 0x00);
	else
		ret = fc0013_writereg(priv, 0x10, (u8)val);

error_out:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0); /* close I2C-gate */

	return ret;
}
EXPORT_SYMBOL(fc0013_rc_cal_add);

int fc0013_rc_cal_reset(struct dvb_frontend *fe)
{
	struct fc0013_priv *priv = fe->tuner_priv;
	int ret;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1); /* open I2C-gate */

	ret = fc0013_writereg(priv, 0x0d, 0x01);
	if (!ret)
		ret = fc0013_writereg(priv, 0x10, 0x00);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0); /* close I2C-gate */

	return ret;
}
EXPORT_SYMBOL(fc0013_rc_cal_reset);

static int fc0013_set_vhf_track(struct fc0013_priv *priv, u32 freq)
{
	int ret;
	u8 tmp;

	ret = fc0013_readreg(priv, 0x1d, &tmp);
	if (ret)
		goto error_out;
	tmp &= 0xe3;
	if (freq <= 177500) {		/* VHF Track: 7 */
		ret = fc0013_writereg(priv, 0x1d, tmp | 0x1c);
	} else if (freq <= 184500) {	/* VHF Track: 6 */
		ret = fc0013_writereg(priv, 0x1d, tmp | 0x18);
	} else if (freq <= 191500) {	/* VHF Track: 5 */
		ret = fc0013_writereg(priv, 0x1d, tmp | 0x14);
	} else if (freq <= 198500) {	/* VHF Track: 4 */
		ret = fc0013_writereg(priv, 0x1d, tmp | 0x10);
	} else if (freq <= 205500) {	/* VHF Track: 3 */
		ret = fc0013_writereg(priv, 0x1d, tmp | 0x0c);
	} else if (freq <= 219500) {	/* VHF Track: 2 */
		ret = fc0013_writereg(priv, 0x1d, tmp | 0x08);
	} else if (freq < 300000) {	/* VHF Track: 1 */
		ret = fc0013_writereg(priv, 0x1d, tmp | 0x04);
	} else {			/* UHF and GPS */
		ret = fc0013_writereg(priv, 0x1d, tmp | 0x1c);
	}
error_out:
	return ret;
}

static int fc0013_set_params(struct dvb_frontend *fe)
{
	struct fc0013_priv *priv = fe->tuner_priv;
	int i, ret = 0;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	u32 freq = p->frequency / 1000;
	u32 delsys = p->delivery_system;
	unsigned char reg[7], am, pm, multi, tmp;
	unsigned long f_vco;
	unsigned short xtal_freq_khz_2, xin, xdiv;
	bool vco_select = false;

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

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1); /* open I2C-gate */

	/* set VHF track */
	ret = fc0013_set_vhf_track(priv, freq);
	if (ret)
		goto exit;

	if (freq < 300000) {
		/* enable VHF filter */
		ret = fc0013_readreg(priv, 0x07, &tmp);
		if (ret)
			goto exit;
		ret = fc0013_writereg(priv, 0x07, tmp | 0x10);
		if (ret)
			goto exit;

		/* disable UHF & disable GPS */
		ret = fc0013_readreg(priv, 0x14, &tmp);
		if (ret)
			goto exit;
		ret = fc0013_writereg(priv, 0x14, tmp & 0x1f);
		if (ret)
			goto exit;
	} else if (freq <= 862000) {
		/* disable VHF filter */
		ret = fc0013_readreg(priv, 0x07, &tmp);
		if (ret)
			goto exit;
		ret = fc0013_writereg(priv, 0x07, tmp & 0xef);
		if (ret)
			goto exit;

		/* enable UHF & disable GPS */
		ret = fc0013_readreg(priv, 0x14, &tmp);
		if (ret)
			goto exit;
		ret = fc0013_writereg(priv, 0x14, (tmp & 0x1f) | 0x40);
		if (ret)
			goto exit;
	} else {
		/* disable VHF filter */
		ret = fc0013_readreg(priv, 0x07, &tmp);
		if (ret)
			goto exit;
		ret = fc0013_writereg(priv, 0x07, tmp & 0xef);
		if (ret)
			goto exit;

		/* disable UHF & enable GPS */
		ret = fc0013_readreg(priv, 0x14, &tmp);
		if (ret)
			goto exit;
		ret = fc0013_writereg(priv, 0x14, (tmp & 0x1f) | 0x20);
		if (ret)
			goto exit;
	}

	/* select frequency divider and the frequency of VCO */
	if (freq < 37084) {		/* freq * 96 < 3560000 */
		multi = 96;
		reg[5] = 0x82;
		reg[6] = 0x00;
	} else if (freq < 55625) {	/* freq * 64 < 3560000 */
		multi = 64;
		reg[5] = 0x02;
		reg[6] = 0x02;
	} else if (freq < 74167) {	/* freq * 48 < 3560000 */
		multi = 48;
		reg[5] = 0x42;
		reg[6] = 0x00;
	} else if (freq < 111250) {	/* freq * 32 < 3560000 */
		multi = 32;
		reg[5] = 0x82;
		reg[6] = 0x02;
	} else if (freq < 148334) {	/* freq * 24 < 3560000 */
		multi = 24;
		reg[5] = 0x22;
		reg[6] = 0x00;
	} else if (freq < 222500) {	/* freq * 16 < 3560000 */
		multi = 16;
		reg[5] = 0x42;
		reg[6] = 0x02;
	} else if (freq < 296667) {	/* freq * 12 < 3560000 */
		multi = 12;
		reg[5] = 0x12;
		reg[6] = 0x00;
	} else if (freq < 445000) {	/* freq * 8 < 3560000 */
		multi = 8;
		reg[5] = 0x22;
		reg[6] = 0x02;
	} else if (freq < 593334) {	/* freq * 6 < 3560000 */
		multi = 6;
		reg[5] = 0x0a;
		reg[6] = 0x00;
	} else if (freq < 950000) {	/* freq * 4 < 3800000 */
		multi = 4;
		reg[5] = 0x12;
		reg[6] = 0x02;
	} else {
		multi = 2;
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

	reg[3] = xin >> 8;
	reg[4] = xin & 0xff;

	if (delsys == SYS_DVBT) {
		reg[6] &= 0x3f; /* bits 6 and 7 describe the bandwidth */
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

	for (i = 1; i <= 6; i++) {
		ret = fc0013_writereg(priv, i, reg[i]);
		if (ret)
			goto exit;
	}

	ret = fc0013_readreg(priv, 0x11, &tmp);
	if (ret)
		goto exit;
	if (multi == 64)
		ret = fc0013_writereg(priv, 0x11, tmp | 0x04);
	else
		ret = fc0013_writereg(priv, 0x11, tmp & 0xfb);
	if (ret)
		goto exit;

	/* VCO Calibration */
	ret = fc0013_writereg(priv, 0x0e, 0x80);
	if (!ret)
		ret = fc0013_writereg(priv, 0x0e, 0x00);

	/* VCO Re-Calibration if needed */
	if (!ret)
		ret = fc0013_writereg(priv, 0x0e, 0x00);

	if (!ret) {
		msleep(10);
		ret = fc0013_readreg(priv, 0x0e, &tmp);
	}
	if (ret)
		goto exit;

	/* vco selection */
	tmp &= 0x3f;

	if (vco_select) {
		if (tmp > 0x3c) {
			reg[6] &= ~0x08;
			ret = fc0013_writereg(priv, 0x06, reg[6]);
			if (!ret)
				ret = fc0013_writereg(priv, 0x0e, 0x80);
			if (!ret)
				ret = fc0013_writereg(priv, 0x0e, 0x00);
		}
	} else {
		if (tmp < 0x02) {
			reg[6] |= 0x08;
			ret = fc0013_writereg(priv, 0x06, reg[6]);
			if (!ret)
				ret = fc0013_writereg(priv, 0x0e, 0x80);
			if (!ret)
				ret = fc0013_writereg(priv, 0x0e, 0x00);
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

static int fc0013_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct fc0013_priv *priv = fe->tuner_priv;
	*frequency = priv->frequency;
	return 0;
}

static int fc0013_get_if_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	/* always ? */
	*frequency = 0;
	return 0;
}

static int fc0013_get_bandwidth(struct dvb_frontend *fe, u32 *bandwidth)
{
	struct fc0013_priv *priv = fe->tuner_priv;
	*bandwidth = priv->bandwidth;
	return 0;
}

#define INPUT_ADC_LEVEL	-8

static int fc0013_get_rf_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct fc0013_priv *priv = fe->tuner_priv;
	int ret;
	unsigned char tmp;
	int int_temp, lna_gain, int_lna, tot_agc_gain, power;
	static const int fc0013_lna_gain_table[] = {
		/* low gain */
		-63, -58, -99, -73,
		-63, -65, -54, -60,
		/* middle gain */
		 71,  70,  68,  67,
		 65,  63,  61,  58,
		/* high gain */
		197, 191, 188, 186,
		184, 182, 181, 179,
	};

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1); /* open I2C-gate */

	ret = fc0013_writereg(priv, 0x13, 0x00);
	if (ret)
		goto err;

	ret = fc0013_readreg(priv, 0x13, &tmp);
	if (ret)
		goto err;
	int_temp = tmp;

	ret = fc0013_readreg(priv, 0x14, &tmp);
	if (ret)
		goto err;
	lna_gain = tmp & 0x1f;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0); /* close I2C-gate */

	if (lna_gain < ARRAY_SIZE(fc0013_lna_gain_table)) {
		int_lna = fc0013_lna_gain_table[lna_gain];
		tot_agc_gain = (abs((int_temp >> 5) - 7) - 2 +
				(int_temp & 0x1f)) * 2;
		power = INPUT_ADC_LEVEL - tot_agc_gain - int_lna / 10;

		if (power >= 45)
			*strength = 255;	/* 100% */
		else if (power < -95)
			*strength = 0;
		else
			*strength = (power + 95) * 255 / 140;

		*strength |= *strength << 8;
	} else {
		ret = -1;
	}

	goto exit;

err:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0); /* close I2C-gate */
exit:
	if (ret)
		warn("%s: failed: %d", __func__, ret);
	return ret;
}

static const struct dvb_tuner_ops fc0013_tuner_ops = {
	.info = {
		.name		= "Fitipower FC0013",

		.frequency_min	= 37000000,	/* estimate */
		.frequency_max	= 1680000000,	/* CHECK */
		.frequency_step	= 0,
	},

	.release	= fc0013_release,

	.init		= fc0013_init,
	.sleep		= fc0013_sleep,

	.set_params	= fc0013_set_params,

	.get_frequency	= fc0013_get_frequency,
	.get_if_frequency = fc0013_get_if_frequency,
	.get_bandwidth	= fc0013_get_bandwidth,

	.get_rf_strength = fc0013_get_rf_strength,
};

struct dvb_frontend *fc0013_attach(struct dvb_frontend *fe,
	struct i2c_adapter *i2c, u8 i2c_address, int dual_master,
	enum fc001x_xtal_freq xtal_freq)
{
	struct fc0013_priv *priv = NULL;

	priv = kzalloc(sizeof(struct fc0013_priv), GFP_KERNEL);
	if (priv == NULL)
		return NULL;

	priv->i2c = i2c;
	priv->dual_master = dual_master;
	priv->addr = i2c_address;
	priv->xtal_freq = xtal_freq;

	info("Fitipower FC0013 successfully attached.");

	fe->tuner_priv = priv;

	memcpy(&fe->ops.tuner_ops, &fc0013_tuner_ops,
		sizeof(struct dvb_tuner_ops));

	return fe;
}
EXPORT_SYMBOL(fc0013_attach);

MODULE_DESCRIPTION("Fitipower FC0013 silicon tuner driver");
MODULE_AUTHOR("Hans-Frieder Vogt <hfvogt@gmx.net>");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.2");
