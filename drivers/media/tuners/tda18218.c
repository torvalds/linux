/*
 * NXP TDA18218HN silicon tuner driver
 *
 * Copyright (C) 2010 Antti Palosaari <crope@iki.fi>
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
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "tda18218_priv.h"

/* Max transfer size done by I2C transfer functions */
#define MAX_XFER_SIZE  64

/* write multiple registers */
static int tda18218_wr_regs(struct tda18218_priv *priv, u8 reg, u8 *val, u8 len)
{
	int ret = 0, len2, remaining;
	u8 buf[MAX_XFER_SIZE];
	struct i2c_msg msg[1] = {
		{
			.addr = priv->cfg->i2c_address,
			.flags = 0,
			.buf = buf,
		}
	};

	if (1 + len > sizeof(buf)) {
		dev_warn(&priv->i2c->dev,
			 "%s: i2c wr reg=%04x: len=%d is too big!\n",
			 KBUILD_MODNAME, reg, len);
		return -EINVAL;
	}

	for (remaining = len; remaining > 0;
			remaining -= (priv->cfg->i2c_wr_max - 1)) {
		len2 = remaining;
		if (len2 > (priv->cfg->i2c_wr_max - 1))
			len2 = (priv->cfg->i2c_wr_max - 1);

		msg[0].len = 1 + len2;
		buf[0] = reg + len - remaining;
		memcpy(&buf[1], &val[len - remaining], len2);

		ret = i2c_transfer(priv->i2c, msg, 1);
		if (ret != 1)
			break;
	}

	if (ret == 1) {
		ret = 0;
	} else {
		dev_warn(&priv->i2c->dev, "%s: i2c wr failed=%d reg=%02x " \
				"len=%d\n", KBUILD_MODNAME, ret, reg, len);
		ret = -EREMOTEIO;
	}

	return ret;
}

/* read multiple registers */
static int tda18218_rd_regs(struct tda18218_priv *priv, u8 reg, u8 *val, u8 len)
{
	int ret;
	u8 buf[MAX_XFER_SIZE]; /* we must start read always from reg 0x00 */
	struct i2c_msg msg[2] = {
		{
			.addr = priv->cfg->i2c_address,
			.flags = 0,
			.len = 1,
			.buf = "\x00",
		}, {
			.addr = priv->cfg->i2c_address,
			.flags = I2C_M_RD,
			.len = reg + len,
			.buf = buf,
		}
	};

	if (reg + len > sizeof(buf)) {
		dev_warn(&priv->i2c->dev,
			 "%s: i2c wr reg=%04x: len=%d is too big!\n",
			 KBUILD_MODNAME, reg, len);
		return -EINVAL;
	}

	ret = i2c_transfer(priv->i2c, msg, 2);
	if (ret == 2) {
		memcpy(val, &buf[reg], len);
		ret = 0;
	} else {
		dev_warn(&priv->i2c->dev, "%s: i2c rd failed=%d reg=%02x " \
				"len=%d\n", KBUILD_MODNAME, ret, reg, len);
		ret = -EREMOTEIO;
	}

	return ret;
}

/* write single register */
static int tda18218_wr_reg(struct tda18218_priv *priv, u8 reg, u8 val)
{
	return tda18218_wr_regs(priv, reg, &val, 1);
}

/* read single register */

static int tda18218_rd_reg(struct tda18218_priv *priv, u8 reg, u8 *val)
{
	return tda18218_rd_regs(priv, reg, val, 1);
}

static int tda18218_set_params(struct dvb_frontend *fe)
{
	struct tda18218_priv *priv = fe->tuner_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u32 bw = c->bandwidth_hz;
	int ret;
	u8 buf[3], i, BP_Filter, LP_Fc;
	u32 LO_Frac;
	/* TODO: find out correct AGC algorithm */
	u8 agc[][2] = {
		{ R20_AGC11, 0x60 },
		{ R23_AGC21, 0x02 },
		{ R20_AGC11, 0xa0 },
		{ R23_AGC21, 0x09 },
		{ R20_AGC11, 0xe0 },
		{ R23_AGC21, 0x0c },
		{ R20_AGC11, 0x40 },
		{ R23_AGC21, 0x01 },
		{ R20_AGC11, 0x80 },
		{ R23_AGC21, 0x08 },
		{ R20_AGC11, 0xc0 },
		{ R23_AGC21, 0x0b },
		{ R24_AGC22, 0x1c },
		{ R24_AGC22, 0x0c },
	};

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1); /* open I2C-gate */

	/* low-pass filter cut-off frequency */
	if (bw <= 6000000) {
		LP_Fc = 0;
		priv->if_frequency = 3000000;
	} else if (bw <= 7000000) {
		LP_Fc = 1;
		priv->if_frequency = 3500000;
	} else {
		LP_Fc = 2;
		priv->if_frequency = 4000000;
	}

	LO_Frac = c->frequency + priv->if_frequency;

	/* band-pass filter */
	if (LO_Frac < 188000000)
		BP_Filter = 3;
	else if (LO_Frac < 253000000)
		BP_Filter = 4;
	else if (LO_Frac < 343000000)
		BP_Filter = 5;
	else
		BP_Filter = 6;

	buf[0] = (priv->regs[R1A_IF1] & ~7) | BP_Filter; /* BP_Filter */
	buf[1] = (priv->regs[R1B_IF2] & ~3) | LP_Fc; /* LP_Fc */
	buf[2] = priv->regs[R1C_AGC2B];
	ret = tda18218_wr_regs(priv, R1A_IF1, buf, 3);
	if (ret)
		goto error;

	buf[0] = (LO_Frac / 1000) >> 12; /* LO_Frac_0 */
	buf[1] = (LO_Frac / 1000) >> 4; /* LO_Frac_1 */
	buf[2] = (LO_Frac / 1000) << 4 |
		(priv->regs[R0C_MD5] & 0x0f); /* LO_Frac_2 */
	ret = tda18218_wr_regs(priv, R0A_MD3, buf, 3);
	if (ret)
		goto error;

	buf[0] = priv->regs[R0F_MD8] | (1 << 6); /* Freq_prog_Start */
	ret = tda18218_wr_regs(priv, R0F_MD8, buf, 1);
	if (ret)
		goto error;

	buf[0] = priv->regs[R0F_MD8] & ~(1 << 6); /* Freq_prog_Start */
	ret = tda18218_wr_regs(priv, R0F_MD8, buf, 1);
	if (ret)
		goto error;

	/* trigger AGC */
	for (i = 0; i < ARRAY_SIZE(agc); i++) {
		ret = tda18218_wr_reg(priv, agc[i][0], agc[i][1]);
		if (ret)
			goto error;
	}

error:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0); /* close I2C-gate */

	if (ret)
		dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);

	return ret;
}

static int tda18218_get_if_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct tda18218_priv *priv = fe->tuner_priv;
	*frequency = priv->if_frequency;
	dev_dbg(&priv->i2c->dev, "%s: if_frequency=%d\n", __func__, *frequency);
	return 0;
}

static int tda18218_sleep(struct dvb_frontend *fe)
{
	struct tda18218_priv *priv = fe->tuner_priv;
	int ret;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1); /* open I2C-gate */

	/* standby */
	ret = tda18218_wr_reg(priv, R17_PD1, priv->regs[R17_PD1] | (1 << 0));

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0); /* close I2C-gate */

	if (ret)
		dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);

	return ret;
}

static int tda18218_init(struct dvb_frontend *fe)
{
	struct tda18218_priv *priv = fe->tuner_priv;
	int ret;

	/* TODO: calibrations */

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1); /* open I2C-gate */

	ret = tda18218_wr_regs(priv, R00_ID, priv->regs, TDA18218_NUM_REGS);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0); /* close I2C-gate */

	if (ret)
		dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);

	return ret;
}

static void tda18218_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
}

static const struct dvb_tuner_ops tda18218_tuner_ops = {
	.info = {
		.name           = "NXP TDA18218",

		.frequency_min  = 174000000,
		.frequency_max  = 864000000,
		.frequency_step =      1000,
	},

	.release       = tda18218_release,
	.init          = tda18218_init,
	.sleep         = tda18218_sleep,

	.set_params    = tda18218_set_params,

	.get_if_frequency = tda18218_get_if_frequency,
};

struct dvb_frontend *tda18218_attach(struct dvb_frontend *fe,
	struct i2c_adapter *i2c, struct tda18218_config *cfg)
{
	struct tda18218_priv *priv = NULL;
	u8 val;
	int ret;
	/* chip default registers values */
	static u8 def_regs[] = {
		0xc0, 0x88, 0x00, 0x8e, 0x03, 0x00, 0x00, 0xd0, 0x00, 0x40,
		0x00, 0x00, 0x07, 0xff, 0x84, 0x09, 0x00, 0x13, 0x00, 0x00,
		0x01, 0x84, 0x09, 0xf0, 0x19, 0x0a, 0x8e, 0x69, 0x98, 0x01,
		0x00, 0x58, 0x10, 0x40, 0x8c, 0x00, 0x0c, 0x48, 0x85, 0xc9,
		0xa7, 0x00, 0x00, 0x00, 0x30, 0x81, 0x80, 0x00, 0x39, 0x00,
		0x8a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf6, 0xf6
	};

	priv = kzalloc(sizeof(struct tda18218_priv), GFP_KERNEL);
	if (priv == NULL)
		return NULL;

	priv->cfg = cfg;
	priv->i2c = i2c;
	fe->tuner_priv = priv;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1); /* open I2C-gate */

	/* check if the tuner is there */
	ret = tda18218_rd_reg(priv, R00_ID, &val);
	if (!ret)
		dev_dbg(&priv->i2c->dev, "%s: chip id=%02x\n", __func__, val);
	if (ret || val != def_regs[R00_ID]) {
		kfree(priv);
		return NULL;
	}

	dev_info(&priv->i2c->dev,
			"%s: NXP TDA18218HN successfully identified\n",
			KBUILD_MODNAME);

	memcpy(&fe->ops.tuner_ops, &tda18218_tuner_ops,
		sizeof(struct dvb_tuner_ops));
	memcpy(priv->regs, def_regs, sizeof(def_regs));

	/* loop-through enabled chip default register values */
	if (priv->cfg->loop_through) {
		priv->regs[R17_PD1] = 0xb0;
		priv->regs[R18_PD2] = 0x59;
	}

	/* standby */
	ret = tda18218_wr_reg(priv, R17_PD1, priv->regs[R17_PD1] | (1 << 0));
	if (ret)
		dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0); /* close I2C-gate */

	return fe;
}
EXPORT_SYMBOL(tda18218_attach);

MODULE_DESCRIPTION("NXP TDA18218HN silicon tuner driver");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_LICENSE("GPL");
