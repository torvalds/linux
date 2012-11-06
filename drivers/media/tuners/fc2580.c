/*
 * FCI FC2580 silicon tuner driver
 *
 * Copyright (C) 2012 Antti Palosaari <crope@iki.fi>
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
 *    You should have received a copy of the GNU General Public License along
 *    with this program; if not, write to the Free Software Foundation, Inc.,
 *    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "fc2580_priv.h"

/*
 * TODO:
 * I2C write and read works only for one single register. Multiple registers
 * could not be accessed using normal register address auto-increment.
 * There could be (very likely) register to change that behavior....
 *
 * Due to that limitation functions:
 *   fc2580_wr_regs()
 *   fc2580_rd_regs()
 * could not be used for accessing more than one register at once.
 *
 * TODO:
 * Currently it blind writes bunch of static registers from the
 * fc2580_freq_regs_lut[] when fc2580_set_params() is called. Add some
 * logic to reduce unneeded register writes.
 */

/* write multiple registers */
static int fc2580_wr_regs(struct fc2580_priv *priv, u8 reg, u8 *val, int len)
{
	int ret;
	u8 buf[1 + len];
	struct i2c_msg msg[1] = {
		{
			.addr = priv->cfg->i2c_addr,
			.flags = 0,
			.len = sizeof(buf),
			.buf = buf,
		}
	};

	buf[0] = reg;
	memcpy(&buf[1], val, len);

	ret = i2c_transfer(priv->i2c, msg, 1);
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
static int fc2580_rd_regs(struct fc2580_priv *priv, u8 reg, u8 *val, int len)
{
	int ret;
	u8 buf[len];
	struct i2c_msg msg[2] = {
		{
			.addr = priv->cfg->i2c_addr,
			.flags = 0,
			.len = 1,
			.buf = &reg,
		}, {
			.addr = priv->cfg->i2c_addr,
			.flags = I2C_M_RD,
			.len = sizeof(buf),
			.buf = buf,
		}
	};

	ret = i2c_transfer(priv->i2c, msg, 2);
	if (ret == 2) {
		memcpy(val, buf, len);
		ret = 0;
	} else {
		dev_warn(&priv->i2c->dev, "%s: i2c rd failed=%d reg=%02x " \
				"len=%d\n", KBUILD_MODNAME, ret, reg, len);
		ret = -EREMOTEIO;
	}

	return ret;
}

/* write single register */
static int fc2580_wr_reg(struct fc2580_priv *priv, u8 reg, u8 val)
{
	return fc2580_wr_regs(priv, reg, &val, 1);
}

/* read single register */
static int fc2580_rd_reg(struct fc2580_priv *priv, u8 reg, u8 *val)
{
	return fc2580_rd_regs(priv, reg, val, 1);
}

/* write single register conditionally only when value differs from 0xff
 * XXX: This is special routine meant only for writing fc2580_freq_regs_lut[]
 * values. Do not use for the other purposes. */
static int fc2580_wr_reg_ff(struct fc2580_priv *priv, u8 reg, u8 val)
{
	if (val == 0xff)
		return 0;
	else
		return fc2580_wr_regs(priv, reg, &val, 1);
}

static int fc2580_set_params(struct dvb_frontend *fe)
{
	struct fc2580_priv *priv = fe->tuner_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret = 0, i;
	unsigned int r_val, n_val, k_val, k_val_reg, f_ref;
	u8 tmp_val, r18_val;
	u64 f_vco;

	/*
	 * Fractional-N synthesizer/PLL.
	 * Most likely all those PLL calculations are not correct. I am not
	 * sure, but it looks like it is divider based Fractional-N synthesizer.
	 * There is divider for reference clock too?
	 * Anyhow, synthesizer calculation results seems to be quite correct.
	 */

	dev_dbg(&priv->i2c->dev, "%s: delivery_system=%d frequency=%d " \
			"bandwidth_hz=%d\n", __func__,
			c->delivery_system, c->frequency, c->bandwidth_hz);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	/* PLL */
	for (i = 0; i < ARRAY_SIZE(fc2580_pll_lut); i++) {
		if (c->frequency <= fc2580_pll_lut[i].freq)
			break;
	}

	if (i == ARRAY_SIZE(fc2580_pll_lut))
		goto err;

	f_vco = c->frequency;
	f_vco *= fc2580_pll_lut[i].div;

	if (f_vco >= 2600000000UL)
		tmp_val = 0x0e | fc2580_pll_lut[i].band;
	else
		tmp_val = 0x06 | fc2580_pll_lut[i].band;

	ret = fc2580_wr_reg(priv, 0x02, tmp_val);
	if (ret < 0)
		goto err;

	if (f_vco >= 2UL * 76 * priv->cfg->clock) {
		r_val = 1;
		r18_val = 0x00;
	} else if (f_vco >= 1UL * 76 * priv->cfg->clock) {
		r_val = 2;
		r18_val = 0x10;
	} else {
		r_val = 4;
		r18_val = 0x20;
	}

	f_ref = 2UL * priv->cfg->clock / r_val;
	n_val = div_u64_rem(f_vco, f_ref, &k_val);
	k_val_reg = 1UL * k_val * (1 << 20) / f_ref;

	ret = fc2580_wr_reg(priv, 0x18, r18_val | ((k_val_reg >> 16) & 0xff));
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg(priv, 0x1a, (k_val_reg >> 8) & 0xff);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg(priv, 0x1b, (k_val_reg >> 0) & 0xff);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg(priv, 0x1c, n_val);
	if (ret < 0)
		goto err;

	if (priv->cfg->clock >= 28000000) {
		ret = fc2580_wr_reg(priv, 0x4b, 0x22);
		if (ret < 0)
			goto err;
	}

	if (fc2580_pll_lut[i].band == 0x00) {
		if (c->frequency <= 794000000)
			tmp_val = 0x9f;
		else
			tmp_val = 0x8f;

		ret = fc2580_wr_reg(priv, 0x2d, tmp_val);
		if (ret < 0)
			goto err;
	}

	/* registers */
	for (i = 0; i < ARRAY_SIZE(fc2580_freq_regs_lut); i++) {
		if (c->frequency <= fc2580_freq_regs_lut[i].freq)
			break;
	}

	if (i == ARRAY_SIZE(fc2580_freq_regs_lut))
		goto err;

	ret = fc2580_wr_reg_ff(priv, 0x25, fc2580_freq_regs_lut[i].r25_val);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg_ff(priv, 0x27, fc2580_freq_regs_lut[i].r27_val);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg_ff(priv, 0x28, fc2580_freq_regs_lut[i].r28_val);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg_ff(priv, 0x29, fc2580_freq_regs_lut[i].r29_val);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg_ff(priv, 0x2b, fc2580_freq_regs_lut[i].r2b_val);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg_ff(priv, 0x2c, fc2580_freq_regs_lut[i].r2c_val);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg_ff(priv, 0x2d, fc2580_freq_regs_lut[i].r2d_val);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg_ff(priv, 0x30, fc2580_freq_regs_lut[i].r30_val);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg_ff(priv, 0x44, fc2580_freq_regs_lut[i].r44_val);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg_ff(priv, 0x50, fc2580_freq_regs_lut[i].r50_val);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg_ff(priv, 0x53, fc2580_freq_regs_lut[i].r53_val);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg_ff(priv, 0x5f, fc2580_freq_regs_lut[i].r5f_val);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg_ff(priv, 0x61, fc2580_freq_regs_lut[i].r61_val);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg_ff(priv, 0x62, fc2580_freq_regs_lut[i].r62_val);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg_ff(priv, 0x63, fc2580_freq_regs_lut[i].r63_val);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg_ff(priv, 0x67, fc2580_freq_regs_lut[i].r67_val);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg_ff(priv, 0x68, fc2580_freq_regs_lut[i].r68_val);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg_ff(priv, 0x69, fc2580_freq_regs_lut[i].r69_val);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg_ff(priv, 0x6a, fc2580_freq_regs_lut[i].r6a_val);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg_ff(priv, 0x6b, fc2580_freq_regs_lut[i].r6b_val);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg_ff(priv, 0x6c, fc2580_freq_regs_lut[i].r6c_val);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg_ff(priv, 0x6d, fc2580_freq_regs_lut[i].r6d_val);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg_ff(priv, 0x6e, fc2580_freq_regs_lut[i].r6e_val);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg_ff(priv, 0x6f, fc2580_freq_regs_lut[i].r6f_val);
	if (ret < 0)
		goto err;

	/* IF filters */
	for (i = 0; i < ARRAY_SIZE(fc2580_if_filter_lut); i++) {
		if (c->bandwidth_hz <= fc2580_if_filter_lut[i].freq)
			break;
	}

	if (i == ARRAY_SIZE(fc2580_if_filter_lut))
		goto err;

	ret = fc2580_wr_reg(priv, 0x36, fc2580_if_filter_lut[i].r36_val);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg(priv, 0x37, 1UL * priv->cfg->clock * \
			fc2580_if_filter_lut[i].mul / 1000000000);
	if (ret < 0)
		goto err;

	ret = fc2580_wr_reg(priv, 0x39, fc2580_if_filter_lut[i].r39_val);
	if (ret < 0)
		goto err;

	/* calibration? */
	ret = fc2580_wr_reg(priv, 0x2e, 0x09);
	if (ret < 0)
		goto err;

	for (i = 0; i < 5; i++) {
		ret = fc2580_rd_reg(priv, 0x2f, &tmp_val);
		if (ret < 0)
			goto err;

		/* done when [7:6] are set */
		if ((tmp_val & 0xc0) == 0xc0)
			break;

		ret = fc2580_wr_reg(priv, 0x2e, 0x01);
		if (ret < 0)
			goto err;

		ret = fc2580_wr_reg(priv, 0x2e, 0x09);
		if (ret < 0)
			goto err;

		usleep_range(5000, 25000);
	}

	dev_dbg(&priv->i2c->dev, "%s: loop=%i\n", __func__, i);

	ret = fc2580_wr_reg(priv, 0x2e, 0x01);
	if (ret < 0)
		goto err;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return 0;
err:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int fc2580_init(struct dvb_frontend *fe)
{
	struct fc2580_priv *priv = fe->tuner_priv;
	int ret, i;

	dev_dbg(&priv->i2c->dev, "%s:\n", __func__);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	for (i = 0; i < ARRAY_SIZE(fc2580_init_reg_vals); i++) {
		ret = fc2580_wr_reg(priv, fc2580_init_reg_vals[i].reg,
				fc2580_init_reg_vals[i].val);
		if (ret < 0)
			goto err;
	}

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return 0;
err:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int fc2580_sleep(struct dvb_frontend *fe)
{
	struct fc2580_priv *priv = fe->tuner_priv;
	int ret;

	dev_dbg(&priv->i2c->dev, "%s:\n", __func__);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	ret = fc2580_wr_reg(priv, 0x02, 0x0a);
	if (ret < 0)
		goto err;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return 0;
err:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int fc2580_get_if_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct fc2580_priv *priv = fe->tuner_priv;

	dev_dbg(&priv->i2c->dev, "%s:\n", __func__);

	*frequency = 0; /* Zero-IF */

	return 0;
}

static int fc2580_release(struct dvb_frontend *fe)
{
	struct fc2580_priv *priv = fe->tuner_priv;

	dev_dbg(&priv->i2c->dev, "%s:\n", __func__);

	kfree(fe->tuner_priv);

	return 0;
}

static const struct dvb_tuner_ops fc2580_tuner_ops = {
	.info = {
		.name           = "FCI FC2580",
		.frequency_min  = 174000000,
		.frequency_max  = 862000000,
	},

	.release = fc2580_release,

	.init = fc2580_init,
	.sleep = fc2580_sleep,
	.set_params = fc2580_set_params,

	.get_if_frequency = fc2580_get_if_frequency,
};

struct dvb_frontend *fc2580_attach(struct dvb_frontend *fe,
		struct i2c_adapter *i2c, const struct fc2580_config *cfg)
{
	struct fc2580_priv *priv;
	int ret;
	u8 chip_id;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	priv = kzalloc(sizeof(struct fc2580_priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		dev_err(&i2c->dev, "%s: kzalloc() failed\n", KBUILD_MODNAME);
		goto err;
	}

	priv->cfg = cfg;
	priv->i2c = i2c;

	/* check if the tuner is there */
	ret = fc2580_rd_reg(priv, 0x01, &chip_id);
	if (ret < 0)
		goto err;

	dev_dbg(&priv->i2c->dev, "%s: chip_id=%02x\n", __func__, chip_id);

	switch (chip_id) {
	case 0x56:
	case 0x5a:
		break;
	default:
		goto err;
	}

	dev_info(&priv->i2c->dev,
			"%s: FCI FC2580 successfully identified\n",
			KBUILD_MODNAME);

	fe->tuner_priv = priv;
	memcpy(&fe->ops.tuner_ops, &fc2580_tuner_ops,
			sizeof(struct dvb_tuner_ops));

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return fe;
err:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	dev_dbg(&i2c->dev, "%s: failed=%d\n", __func__, ret);
	kfree(priv);
	return NULL;
}
EXPORT_SYMBOL(fc2580_attach);

MODULE_DESCRIPTION("FCI FC2580 silicon tuner driver");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_LICENSE("GPL");
