// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Fitipower FC0012 tuner driver
 *
 * Copyright (C) 2012 Hans-Frieder Vogt <hfvogt@gmx.net>
 */

#include "fc0012.h"
#include "fc0012-priv.h"

static int fc0012_writereg(struct fc0012_priv *priv, u8 reg, u8 val)
{
	u8 buf[2] = {reg, val};
	struct i2c_msg msg = {
		.addr = priv->cfg->i2c_address, .flags = 0, .buf = buf, .len = 2
	};

	if (i2c_transfer(priv->i2c, &msg, 1) != 1) {
		dev_err(&priv->i2c->dev,
			"%s: I2C write reg failed, reg: %02x, val: %02x\n",
			KBUILD_MODNAME, reg, val);
		return -EREMOTEIO;
	}
	return 0;
}

static int fc0012_readreg(struct fc0012_priv *priv, u8 reg, u8 *val)
{
	struct i2c_msg msg[2] = {
		{ .addr = priv->cfg->i2c_address, .flags = 0,
			.buf = &reg, .len = 1 },
		{ .addr = priv->cfg->i2c_address, .flags = I2C_M_RD,
			.buf = val, .len = 1 },
	};

	if (i2c_transfer(priv->i2c, msg, 2) != 2) {
		dev_err(&priv->i2c->dev,
			"%s: I2C read reg failed, reg: %02x\n",
			KBUILD_MODNAME, reg);
		return -EREMOTEIO;
	}
	return 0;
}

static void fc0012_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
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

	switch (priv->cfg->xtal_freq) {
	case FC_XTAL_27_MHZ:
	case FC_XTAL_28_8_MHZ:
		reg[0x07] |= 0x20;
		break;
	case FC_XTAL_36_MHZ:
	default:
		break;
	}

	if (priv->cfg->dual_master)
		reg[0x0c] |= 0x02;

	if (priv->cfg->loop_through)
		reg[0x09] |= 0x01;

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
		dev_err(&priv->i2c->dev, "%s: fc0012_writereg failed: %d\n",
				KBUILD_MODNAME, ret);

	return ret;
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
	bool vco_select = false;

	if (fe->callback) {
		ret = fe->callback(priv->i2c, DVB_FRONTEND_COMPONENT_TUNER,
			FC_FE_CALLBACK_VHF_ENABLE, (freq > 300000 ? 0 : 1));
		if (ret)
			goto exit;
	}

	switch (priv->cfg->xtal_freq) {
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
		dev_err(&priv->i2c->dev, "%s: modulation type not supported!\n",
				KBUILD_MODNAME);
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
		dev_warn(&priv->i2c->dev, "%s: %s failed: %d\n",
				KBUILD_MODNAME, __func__, ret);
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
	*frequency = 0; /* Zero-IF */
	return 0;
}

static int fc0012_get_bandwidth(struct dvb_frontend *fe, u32 *bandwidth)
{
	struct fc0012_priv *priv = fe->tuner_priv;
	*bandwidth = priv->bandwidth;
	return 0;
}

#define INPUT_ADC_LEVEL	-8

static int fc0012_get_rf_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct fc0012_priv *priv = fe->tuner_priv;
	int ret;
	unsigned char tmp;
	int int_temp, lna_gain, int_lna, tot_agc_gain, power;
	static const int fc0012_lna_gain_table[] = {
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

	ret = fc0012_writereg(priv, 0x12, 0x00);
	if (ret)
		goto err;

	ret = fc0012_readreg(priv, 0x12, &tmp);
	if (ret)
		goto err;
	int_temp = tmp;

	ret = fc0012_readreg(priv, 0x13, &tmp);
	if (ret)
		goto err;
	lna_gain = tmp & 0x1f;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0); /* close I2C-gate */

	if (lna_gain < ARRAY_SIZE(fc0012_lna_gain_table)) {
		int_lna = fc0012_lna_gain_table[lna_gain];
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
		dev_warn(&priv->i2c->dev, "%s: %s failed: %d\n",
				KBUILD_MODNAME, __func__, ret);
	return ret;
}

static const struct dvb_tuner_ops fc0012_tuner_ops = {
	.info = {
		.name              = "Fitipower FC0012",

		.frequency_min_hz  =  37 * MHz,	/* estimate */
		.frequency_max_hz  = 862 * MHz,	/* estimate */
	},

	.release	= fc0012_release,

	.init		= fc0012_init,

	.set_params	= fc0012_set_params,

	.get_frequency	= fc0012_get_frequency,
	.get_if_frequency = fc0012_get_if_frequency,
	.get_bandwidth	= fc0012_get_bandwidth,

	.get_rf_strength = fc0012_get_rf_strength,
};

struct dvb_frontend *fc0012_attach(struct dvb_frontend *fe,
	struct i2c_adapter *i2c, const struct fc0012_config *cfg)
{
	struct fc0012_priv *priv;
	int ret;
	u8 chip_id;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	priv = kzalloc(sizeof(struct fc0012_priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		dev_err(&i2c->dev, "%s: kzalloc() failed\n", KBUILD_MODNAME);
		goto err;
	}

	priv->cfg = cfg;
	priv->i2c = i2c;

	/* check if the tuner is there */
	ret = fc0012_readreg(priv, 0x00, &chip_id);
	if (ret < 0)
		goto err;

	dev_dbg(&i2c->dev, "%s: chip_id=%02x\n", __func__, chip_id);

	switch (chip_id) {
	case 0xa1:
		break;
	default:
		ret = -ENODEV;
		goto err;
	}

	dev_info(&i2c->dev, "%s: Fitipower FC0012 successfully identified\n",
			KBUILD_MODNAME);

	if (priv->cfg->loop_through) {
		ret = fc0012_writereg(priv, 0x09, 0x6f);
		if (ret < 0)
			goto err;
	}

	/*
	 * TODO: Clock out en or div?
	 * For dual tuner configuration clearing bit [0] is required.
	 */
	if (priv->cfg->clock_out) {
		ret =  fc0012_writereg(priv, 0x0b, 0x82);
		if (ret < 0)
			goto err;
	}

	fe->tuner_priv = priv;
	memcpy(&fe->ops.tuner_ops, &fc0012_tuner_ops,
		sizeof(struct dvb_tuner_ops));

err:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	if (ret) {
		dev_dbg(&i2c->dev, "%s: failed: %d\n", __func__, ret);
		kfree(priv);
		return NULL;
	}

	return fe;
}
EXPORT_SYMBOL(fc0012_attach);

MODULE_DESCRIPTION("Fitipower FC0012 silicon tuner driver");
MODULE_AUTHOR("Hans-Frieder Vogt <hfvogt@gmx.net>");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.6");
