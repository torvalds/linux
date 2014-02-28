/*
 * Elonics E4000 silicon tuner driver
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

#include "e4000_priv.h"
#include <linux/math64.h>

/* Max transfer size done by I2C transfer functions */
#define MAX_XFER_SIZE  64

/* write multiple registers */
static int e4000_wr_regs(struct e4000_priv *priv, u8 reg, u8 *val, int len)
{
	int ret;
	u8 buf[MAX_XFER_SIZE];
	struct i2c_msg msg[1] = {
		{
			.addr = priv->cfg->i2c_addr,
			.flags = 0,
			.len = 1 + len,
			.buf = buf,
		}
	};

	if (1 + len > sizeof(buf)) {
		dev_warn(&priv->i2c->dev,
			 "%s: i2c wr reg=%04x: len=%d is too big!\n",
			 KBUILD_MODNAME, reg, len);
		return -EINVAL;
	}

	buf[0] = reg;
	memcpy(&buf[1], val, len);

	ret = i2c_transfer(priv->i2c, msg, 1);
	if (ret == 1) {
		ret = 0;
	} else {
		dev_warn(&priv->i2c->dev,
				"%s: i2c wr failed=%d reg=%02x len=%d\n",
				KBUILD_MODNAME, ret, reg, len);
		ret = -EREMOTEIO;
	}
	return ret;
}

/* read multiple registers */
static int e4000_rd_regs(struct e4000_priv *priv, u8 reg, u8 *val, int len)
{
	int ret;
	u8 buf[MAX_XFER_SIZE];
	struct i2c_msg msg[2] = {
		{
			.addr = priv->cfg->i2c_addr,
			.flags = 0,
			.len = 1,
			.buf = &reg,
		}, {
			.addr = priv->cfg->i2c_addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = buf,
		}
	};

	if (len > sizeof(buf)) {
		dev_warn(&priv->i2c->dev,
			 "%s: i2c rd reg=%04x: len=%d is too big!\n",
			 KBUILD_MODNAME, reg, len);
		return -EINVAL;
	}

	ret = i2c_transfer(priv->i2c, msg, 2);
	if (ret == 2) {
		memcpy(val, buf, len);
		ret = 0;
	} else {
		dev_warn(&priv->i2c->dev,
				"%s: i2c rd failed=%d reg=%02x len=%d\n",
				KBUILD_MODNAME, ret, reg, len);
		ret = -EREMOTEIO;
	}

	return ret;
}

/* write single register */
static int e4000_wr_reg(struct e4000_priv *priv, u8 reg, u8 val)
{
	return e4000_wr_regs(priv, reg, &val, 1);
}

/* read single register */
static int e4000_rd_reg(struct e4000_priv *priv, u8 reg, u8 *val)
{
	return e4000_rd_regs(priv, reg, val, 1);
}

static int e4000_init(struct dvb_frontend *fe)
{
	struct e4000_priv *priv = fe->tuner_priv;
	int ret;

	dev_dbg(&priv->i2c->dev, "%s:\n", __func__);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	/* dummy I2C to ensure I2C wakes up */
	ret = e4000_wr_reg(priv, 0x02, 0x40);

	/* reset */
	ret = e4000_wr_reg(priv, 0x00, 0x01);
	if (ret < 0)
		goto err;

	/* disable output clock */
	ret = e4000_wr_reg(priv, 0x06, 0x00);
	if (ret < 0)
		goto err;

	ret = e4000_wr_reg(priv, 0x7a, 0x96);
	if (ret < 0)
		goto err;

	/* configure gains */
	ret = e4000_wr_regs(priv, 0x7e, "\x01\xfe", 2);
	if (ret < 0)
		goto err;

	ret = e4000_wr_reg(priv, 0x82, 0x00);
	if (ret < 0)
		goto err;

	ret = e4000_wr_reg(priv, 0x24, 0x05);
	if (ret < 0)
		goto err;

	ret = e4000_wr_regs(priv, 0x87, "\x20\x01", 2);
	if (ret < 0)
		goto err;

	ret = e4000_wr_regs(priv, 0x9f, "\x7f\x07", 2);
	if (ret < 0)
		goto err;

	/* DC offset control */
	ret = e4000_wr_reg(priv, 0x2d, 0x1f);
	if (ret < 0)
		goto err;

	ret = e4000_wr_regs(priv, 0x70, "\x01\x01", 2);
	if (ret < 0)
		goto err;

	/* gain control */
	ret = e4000_wr_reg(priv, 0x1a, 0x17);
	if (ret < 0)
		goto err;

	ret = e4000_wr_reg(priv, 0x1f, 0x1a);
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

static int e4000_sleep(struct dvb_frontend *fe)
{
	struct e4000_priv *priv = fe->tuner_priv;
	int ret;

	dev_dbg(&priv->i2c->dev, "%s:\n", __func__);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	ret = e4000_wr_reg(priv, 0x00, 0x00);
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

static int e4000_set_params(struct dvb_frontend *fe)
{
	struct e4000_priv *priv = fe->tuner_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i, sigma_delta;
	unsigned int f_vco;
	u8 buf[5], i_data[4], q_data[4];

	dev_dbg(&priv->i2c->dev,
			"%s: delivery_system=%d frequency=%d bandwidth_hz=%d\n",
			__func__, c->delivery_system, c->frequency,
			c->bandwidth_hz);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	/* gain control manual */
	ret = e4000_wr_reg(priv, 0x1a, 0x00);
	if (ret < 0)
		goto err;

	/* PLL */
	for (i = 0; i < ARRAY_SIZE(e4000_pll_lut); i++) {
		if (c->frequency <= e4000_pll_lut[i].freq)
			break;
	}

	if (i == ARRAY_SIZE(e4000_pll_lut))
		goto err;

	/*
	 * Note: Currently f_vco overflows when c->frequency is 1 073 741 824 Hz
	 * or more.
	 */
	f_vco = c->frequency * e4000_pll_lut[i].mul;
	sigma_delta = div_u64(0x10000ULL * (f_vco % priv->cfg->clock), priv->cfg->clock);
	buf[0] = f_vco / priv->cfg->clock;
	buf[1] = (sigma_delta >> 0) & 0xff;
	buf[2] = (sigma_delta >> 8) & 0xff;
	buf[3] = 0x00;
	buf[4] = e4000_pll_lut[i].div;

	dev_dbg(&priv->i2c->dev, "%s: f_vco=%u pll div=%d sigma_delta=%04x\n",
			__func__, f_vco, buf[0], sigma_delta);

	ret = e4000_wr_regs(priv, 0x09, buf, 5);
	if (ret < 0)
		goto err;

	/* LNA filter (RF filter) */
	for (i = 0; i < ARRAY_SIZE(e400_lna_filter_lut); i++) {
		if (c->frequency <= e400_lna_filter_lut[i].freq)
			break;
	}

	if (i == ARRAY_SIZE(e400_lna_filter_lut))
		goto err;

	ret = e4000_wr_reg(priv, 0x10, e400_lna_filter_lut[i].val);
	if (ret < 0)
		goto err;

	/* IF filters */
	for (i = 0; i < ARRAY_SIZE(e4000_if_filter_lut); i++) {
		if (c->bandwidth_hz <= e4000_if_filter_lut[i].freq)
			break;
	}

	if (i == ARRAY_SIZE(e4000_if_filter_lut))
		goto err;

	buf[0] = e4000_if_filter_lut[i].reg11_val;
	buf[1] = e4000_if_filter_lut[i].reg12_val;

	ret = e4000_wr_regs(priv, 0x11, buf, 2);
	if (ret < 0)
		goto err;

	/* frequency band */
	for (i = 0; i < ARRAY_SIZE(e4000_band_lut); i++) {
		if (c->frequency <= e4000_band_lut[i].freq)
			break;
	}

	if (i == ARRAY_SIZE(e4000_band_lut))
		goto err;

	ret = e4000_wr_reg(priv, 0x07, e4000_band_lut[i].reg07_val);
	if (ret < 0)
		goto err;

	ret = e4000_wr_reg(priv, 0x78, e4000_band_lut[i].reg78_val);
	if (ret < 0)
		goto err;

	/* DC offset */
	for (i = 0; i < 4; i++) {
		if (i == 0)
			ret = e4000_wr_regs(priv, 0x15, "\x00\x7e\x24", 3);
		else if (i == 1)
			ret = e4000_wr_regs(priv, 0x15, "\x00\x7f", 2);
		else if (i == 2)
			ret = e4000_wr_regs(priv, 0x15, "\x01", 1);
		else
			ret = e4000_wr_regs(priv, 0x16, "\x7e", 1);

		if (ret < 0)
			goto err;

		ret = e4000_wr_reg(priv, 0x29, 0x01);
		if (ret < 0)
			goto err;

		ret = e4000_rd_regs(priv, 0x2a, buf, 3);
		if (ret < 0)
			goto err;

		i_data[i] = (((buf[2] >> 0) & 0x3) << 6) | (buf[0] & 0x3f);
		q_data[i] = (((buf[2] >> 4) & 0x3) << 6) | (buf[1] & 0x3f);
	}

	swap(q_data[2], q_data[3]);
	swap(i_data[2], i_data[3]);

	ret = e4000_wr_regs(priv, 0x50, q_data, 4);
	if (ret < 0)
		goto err;

	ret = e4000_wr_regs(priv, 0x60, i_data, 4);
	if (ret < 0)
		goto err;

	/* gain control auto */
	ret = e4000_wr_reg(priv, 0x1a, 0x17);
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

static int e4000_get_if_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct e4000_priv *priv = fe->tuner_priv;

	dev_dbg(&priv->i2c->dev, "%s:\n", __func__);

	*frequency = 0; /* Zero-IF */

	return 0;
}

static int e4000_release(struct dvb_frontend *fe)
{
	struct e4000_priv *priv = fe->tuner_priv;

	dev_dbg(&priv->i2c->dev, "%s:\n", __func__);

	kfree(fe->tuner_priv);

	return 0;
}

static const struct dvb_tuner_ops e4000_tuner_ops = {
	.info = {
		.name           = "Elonics E4000",
		.frequency_min  = 174000000,
		.frequency_max  = 862000000,
	},

	.release = e4000_release,

	.init = e4000_init,
	.sleep = e4000_sleep,
	.set_params = e4000_set_params,

	.get_if_frequency = e4000_get_if_frequency,
};

struct dvb_frontend *e4000_attach(struct dvb_frontend *fe,
		struct i2c_adapter *i2c, const struct e4000_config *cfg)
{
	struct e4000_priv *priv;
	int ret;
	u8 chip_id;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	priv = kzalloc(sizeof(struct e4000_priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		dev_err(&i2c->dev, "%s: kzalloc() failed\n", KBUILD_MODNAME);
		goto err;
	}

	priv->cfg = cfg;
	priv->i2c = i2c;

	/* check if the tuner is there */
	ret = e4000_rd_reg(priv, 0x02, &chip_id);
	if (ret < 0)
		goto err;

	dev_dbg(&priv->i2c->dev, "%s: chip_id=%02x\n", __func__, chip_id);

	if (chip_id != 0x40)
		goto err;

	/* put sleep as chip seems to be in normal mode by default */
	ret = e4000_wr_reg(priv, 0x00, 0x00);
	if (ret < 0)
		goto err;

	dev_info(&priv->i2c->dev,
			"%s: Elonics E4000 successfully identified\n",
			KBUILD_MODNAME);

	fe->tuner_priv = priv;
	memcpy(&fe->ops.tuner_ops, &e4000_tuner_ops,
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
EXPORT_SYMBOL(e4000_attach);

MODULE_DESCRIPTION("Elonics E4000 silicon tuner driver");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_LICENSE("GPL");
