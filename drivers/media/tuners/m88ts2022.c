/*
 * Montage M88TS2022 silicon tuner driver
 *
 * Copyright (C) 2013 Antti Palosaari <crope@iki.fi>
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
 *
 * Some calculations are taken from existing TS2020 driver.
 */

#include "m88ts2022_priv.h"

/* write multiple registers */
static int m88ts2022_wr_regs(struct m88ts2022_priv *priv,
		u8 reg, const u8 *val, int len)
{
#define MAX_WR_LEN 3
#define MAX_WR_XFER_LEN (MAX_WR_LEN + 1)
	int ret;
	u8 buf[MAX_WR_XFER_LEN];
	struct i2c_msg msg[1] = {
		{
			.addr = priv->cfg->i2c_addr,
			.flags = 0,
			.len = 1 + len,
			.buf = buf,
		}
	};

	if (WARN_ON(len > MAX_WR_LEN))
		return -EINVAL;

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
static int m88ts2022_rd_regs(struct m88ts2022_priv *priv, u8 reg,
		u8 *val, int len)
{
#define MAX_RD_LEN 1
#define MAX_RD_XFER_LEN (MAX_RD_LEN)
	int ret;
	u8 buf[MAX_RD_XFER_LEN];
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

	if (WARN_ON(len > MAX_RD_LEN))
		return -EINVAL;

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
static int m88ts2022_wr_reg(struct m88ts2022_priv *priv, u8 reg, u8 val)
{
	return m88ts2022_wr_regs(priv, reg, &val, 1);
}

/* read single register */
static int m88ts2022_rd_reg(struct m88ts2022_priv *priv, u8 reg, u8 *val)
{
	return m88ts2022_rd_regs(priv, reg, val, 1);
}

/* write single register with mask */
static int m88ts2022_wr_reg_mask(struct m88ts2022_priv *priv,
		u8 reg, u8 val, u8 mask)
{
	int ret;
	u8 u8tmp;

	/* no need for read if whole reg is written */
	if (mask != 0xff) {
		ret = m88ts2022_rd_regs(priv, reg, &u8tmp, 1);
		if (ret)
			return ret;

		val &= mask;
		u8tmp &= ~mask;
		val |= u8tmp;
	}

	return m88ts2022_wr_regs(priv, reg, &val, 1);
}

static int m88ts2022_cmd(struct dvb_frontend *fe,
		int op, int sleep, u8 reg, u8 mask, u8 val, u8 *reg_val)
{
	struct m88ts2022_priv *priv = fe->tuner_priv;
	int ret, i;
	u8 u8tmp;
	struct m88ts2022_reg_val reg_vals[] = {
		{0x51, 0x1f - op},
		{0x51, 0x1f},
		{0x50, 0x00 + op},
		{0x50, 0x00},
	};

	for (i = 0; i < 2; i++) {
		dev_dbg(&priv->i2c->dev,
				"%s: i=%d op=%02x reg=%02x mask=%02x val=%02x\n",
				__func__, i, op, reg, mask, val);

		for (i = 0; i < ARRAY_SIZE(reg_vals); i++) {
			ret = m88ts2022_wr_reg(priv, reg_vals[i].reg,
					reg_vals[i].val);
			if (ret)
				goto err;
		}

		usleep_range(sleep * 1000, sleep * 10000);

		ret = m88ts2022_rd_reg(priv, reg, &u8tmp);
		if (ret)
			goto err;

		if ((u8tmp & mask) != val)
			break;
	}

	if (reg_val)
		*reg_val = u8tmp;
err:
	return ret;
}

static int m88ts2022_set_params(struct dvb_frontend *fe)
{
	struct m88ts2022_priv *priv = fe->tuner_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret = 0, div;
	u8 buf[3], u8tmp, cap_code, lpf_mxdiv, div_max, div_min;
	u16 N_reg, N, K;
	u32 lpf_gm, lpf_coeff, gdiv28, frequency_khz, frequency_offset;
	u32 freq_3db;
	dev_dbg(&priv->i2c->dev,
			"%s: frequency=%d symbol_rate=%d rolloff=%d\n",
			__func__, c->frequency, c->symbol_rate, c->rolloff);

	if (c->symbol_rate < 5000000)
		frequency_offset = 3000000; /* 3 MHz */
	else
		frequency_offset = 0;

	frequency_khz = c->frequency + (frequency_offset / 1000);

	if (frequency_khz < 1103000) {
		div = 2;
		u8tmp = 0x1b;
	} else {
		div = 1;
		u8tmp = 0x0b;
	}

	buf[0] = u8tmp;
	buf[1] = 0x40;
	ret = m88ts2022_wr_regs(priv, 0x10, buf, 2);
	if (ret)
		goto err;

	K = DIV_ROUND_CLOSEST((priv->cfg->clock / 2), 1000000);
	N = 1ul * frequency_khz * K * div * 2 / (priv->cfg->clock / 1000);
	N += N % 2;

	if (N < 4095)
		N_reg = N - 1024;
	else if (N < 6143)
		N_reg = N + 1024;
	else
		N_reg = N + 3072;

	buf[0] = (N_reg >> 8) & 0x3f;
	buf[1] = (N_reg >> 0) & 0xff;
	buf[2] = K - 8;
	ret = m88ts2022_wr_regs(priv, 0x01, buf, 3);
	if (ret)
		goto err;

	priv->frequency_khz = 1ul * N * (priv->cfg->clock / 1000) / K / div / 2;

	dev_dbg(&priv->i2c->dev,
			"%s: frequency=%d offset=%d K=%d N=%d div=%d\n",
			__func__, priv->frequency_khz,
			priv->frequency_khz - c->frequency, K, N, div);

	ret = m88ts2022_cmd(fe, 0x10, 5, 0x15, 0x40, 0x00, NULL);
	if (ret)
		goto err;

	ret = m88ts2022_rd_reg(priv, 0x14, &u8tmp);
	if (ret)
		goto err;

	u8tmp &= 0x7f;
	if (u8tmp < 64) {
		ret = m88ts2022_wr_reg_mask(priv, 0x10, 0x80, 0x80);
		if (ret)
			goto err;

		ret = m88ts2022_wr_reg(priv, 0x11, 0x6f);
		if (ret)
			goto err;

		ret = m88ts2022_cmd(fe, 0x10, 5, 0x15, 0x40, 0x00, NULL);
		if (ret)
			goto err;
	}

	ret = m88ts2022_rd_reg(priv, 0x14, &u8tmp);
	if (ret)
		goto err;

	u8tmp &= 0x1f;
	if (u8tmp > 19) {
		ret = m88ts2022_wr_reg_mask(priv, 0x10, 0x00, 0x02);
		if (ret)
			goto err;
	}

	ret = m88ts2022_cmd(fe, 0x08, 5, 0x3c, 0xff, 0x00, NULL);
	if (ret)
		goto err;

	ret = m88ts2022_wr_reg(priv, 0x25, 0x00);
	if (ret)
		goto err;

	ret = m88ts2022_wr_reg(priv, 0x27, 0x70);
	if (ret)
		goto err;

	ret = m88ts2022_wr_reg(priv, 0x41, 0x09);
	if (ret)
		goto err;

	ret = m88ts2022_wr_reg(priv, 0x08, 0x0b);
	if (ret)
		goto err;

	gdiv28 = DIV_ROUND_CLOSEST(priv->cfg->clock / 1000000 * 1694, 1000);

	ret = m88ts2022_wr_reg(priv, 0x04, gdiv28);
	if (ret)
		goto err;

	ret = m88ts2022_cmd(fe, 0x04, 2, 0x26, 0xff, 0x00, &u8tmp);
	if (ret)
		goto err;

	cap_code = u8tmp & 0x3f;

	ret = m88ts2022_wr_reg(priv, 0x41, 0x0d);
	if (ret)
		goto err;

	ret = m88ts2022_cmd(fe, 0x04, 2, 0x26, 0xff, 0x00, &u8tmp);
	if (ret)
		goto err;

	u8tmp &= 0x3f;
	cap_code = (cap_code + u8tmp) / 2;
	gdiv28 = gdiv28 * 207 / (cap_code * 2 + 151);
	div_max = gdiv28 * 135 / 100;
	div_min = gdiv28 * 78 / 100;
	if (div_max > 63)
		div_max = 63;

	freq_3db = 1ul * c->symbol_rate * 135 / 200 + 2000000;
	freq_3db += frequency_offset;
	if (freq_3db < 7000000)
		freq_3db = 7000000;
	if (freq_3db > 40000000)
		freq_3db = 40000000;

	lpf_coeff = 3200;
	lpf_gm = DIV_ROUND_CLOSEST(freq_3db * gdiv28, lpf_coeff *
			(priv->cfg->clock / 1000));
	if (lpf_gm > 23)
		lpf_gm = 23;
	if (lpf_gm < 1)
		lpf_gm = 1;

	lpf_mxdiv = DIV_ROUND_CLOSEST(lpf_gm * lpf_coeff *
			(priv->cfg->clock / 1000), freq_3db);

	if (lpf_mxdiv < div_min) {
		lpf_gm++;
		lpf_mxdiv = DIV_ROUND_CLOSEST(lpf_gm * lpf_coeff *
				(priv->cfg->clock / 1000), freq_3db);
	}

	if (lpf_mxdiv > div_max)
		lpf_mxdiv = div_max;

	ret = m88ts2022_wr_reg(priv, 0x04, lpf_mxdiv);
	if (ret)
		goto err;

	ret = m88ts2022_wr_reg(priv, 0x06, lpf_gm);
	if (ret)
		goto err;

	ret = m88ts2022_cmd(fe, 0x04, 2, 0x26, 0xff, 0x00, &u8tmp);
	if (ret)
		goto err;

	cap_code = u8tmp & 0x3f;

	ret = m88ts2022_wr_reg(priv, 0x41, 0x09);
	if (ret)
		goto err;

	ret = m88ts2022_cmd(fe, 0x04, 2, 0x26, 0xff, 0x00, &u8tmp);
	if (ret)
		goto err;

	u8tmp &= 0x3f;
	cap_code = (cap_code + u8tmp) / 2;

	u8tmp = cap_code | 0x80;
	ret = m88ts2022_wr_reg(priv, 0x25, u8tmp);
	if (ret)
		goto err;

	ret = m88ts2022_wr_reg(priv, 0x27, 0x30);
	if (ret)
		goto err;

	ret = m88ts2022_wr_reg(priv, 0x08, 0x09);
	if (ret)
		goto err;

	ret = m88ts2022_cmd(fe, 0x01, 20, 0x21, 0xff, 0x00, NULL);
	if (ret)
		goto err;
err:
	if (ret)
		dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);

	return ret;
}

static int m88ts2022_init(struct dvb_frontend *fe)
{
	struct m88ts2022_priv *priv = fe->tuner_priv;
	int ret, i;
	u8 u8tmp;
	static const struct m88ts2022_reg_val reg_vals[] = {
		{0x7d, 0x9d},
		{0x7c, 0x9a},
		{0x7a, 0x76},
		{0x3b, 0x01},
		{0x63, 0x88},
		{0x61, 0x85},
		{0x22, 0x30},
		{0x30, 0x40},
		{0x20, 0x23},
		{0x24, 0x02},
		{0x12, 0xa0},
	};
	dev_dbg(&priv->i2c->dev, "%s:\n", __func__);

	ret = m88ts2022_wr_reg(priv, 0x00, 0x01);
	if (ret)
		goto err;

	ret = m88ts2022_wr_reg(priv, 0x00, 0x03);
	if (ret)
		goto err;

	switch (priv->cfg->clock_out) {
	case M88TS2022_CLOCK_OUT_DISABLED:
		u8tmp = 0x60;
		break;
	case M88TS2022_CLOCK_OUT_ENABLED:
		u8tmp = 0x70;
		ret = m88ts2022_wr_reg(priv, 0x05, priv->cfg->clock_out_div);
		if (ret)
			goto err;
		break;
	case M88TS2022_CLOCK_OUT_ENABLED_XTALOUT:
		u8tmp = 0x6c;
		break;
	default:
		goto err;
	}

	ret = m88ts2022_wr_reg(priv, 0x42, u8tmp);
	if (ret)
		goto err;

	if (priv->cfg->loop_through)
		u8tmp = 0xec;
	else
		u8tmp = 0x6c;

	ret = m88ts2022_wr_reg(priv, 0x62, u8tmp);
	if (ret)
		goto err;

	for (i = 0; i < ARRAY_SIZE(reg_vals); i++) {
		ret = m88ts2022_wr_reg(priv, reg_vals[i].reg, reg_vals[i].val);
		if (ret)
			goto err;
	}
err:
	if (ret)
		dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int m88ts2022_sleep(struct dvb_frontend *fe)
{
	struct m88ts2022_priv *priv = fe->tuner_priv;
	int ret;
	dev_dbg(&priv->i2c->dev, "%s:\n", __func__);

	ret = m88ts2022_wr_reg(priv, 0x00, 0x00);
	if (ret)
		goto err;
err:
	if (ret)
		dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int m88ts2022_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct m88ts2022_priv *priv = fe->tuner_priv;
	dev_dbg(&priv->i2c->dev, "%s:\n", __func__);

	*frequency = priv->frequency_khz;
	return 0;
}

static int m88ts2022_get_if_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct m88ts2022_priv *priv = fe->tuner_priv;
	dev_dbg(&priv->i2c->dev, "%s:\n", __func__);

	*frequency = 0; /* Zero-IF */
	return 0;
}

static int m88ts2022_get_rf_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct m88ts2022_priv *priv = fe->tuner_priv;
	u8  u8tmp, gain1, gain2, gain3;
	u16 gain, u16tmp;
	int ret;

	ret = m88ts2022_rd_reg(priv, 0x3d, &u8tmp);
	if (ret)
		goto err;

	gain1 = (u8tmp >> 0) & 0x1f;
	if (gain1 > 15)
		gain1 = 15;

	ret = m88ts2022_rd_reg(priv, 0x21, &u8tmp);
	if (ret)
		goto err;

	gain2 = (u8tmp >> 0) & 0x1f;
	if (gain2 < 2)
		gain2 = 2;
	if (gain2 > 16)
		gain2 = 16;

	ret = m88ts2022_rd_reg(priv, 0x66, &u8tmp);
	if (ret)
		goto err;

	gain3 = (u8tmp >> 3) & 0x07;
	if (gain3 > 6)
		gain3 = 6;

	gain = gain1 * 265 + gain2 * 338 + gain3 * 285;

	/* scale value to 0x0000-0xffff */
	u16tmp = (0xffff - gain);
	if (u16tmp < 59000)
		u16tmp = 59000;
	else if (u16tmp > 61500)
		u16tmp = 61500;

	*strength = (u16tmp - 59000) * 0xffff / (61500 - 59000);
err:
	if (ret)
		dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int m88ts2022_release(struct dvb_frontend *fe)
{
	struct m88ts2022_priv *priv = fe->tuner_priv;
	dev_dbg(&priv->i2c->dev, "%s:\n", __func__);

	kfree(fe->tuner_priv);
	return 0;
}

static const struct dvb_tuner_ops m88ts2022_tuner_ops = {
	.info = {
		.name          = "Montage M88TS2022",
		.frequency_min = 950000,
		.frequency_max = 2150000,
	},

	.release = m88ts2022_release,

	.init = m88ts2022_init,
	.sleep = m88ts2022_sleep,
	.set_params = m88ts2022_set_params,

	.get_frequency = m88ts2022_get_frequency,
	.get_if_frequency = m88ts2022_get_if_frequency,
	.get_rf_strength = m88ts2022_get_rf_strength,
};

struct dvb_frontend *m88ts2022_attach(struct dvb_frontend *fe,
		struct i2c_adapter *i2c, const struct m88ts2022_config *cfg)
{
	struct m88ts2022_priv *priv;
	int ret;
	u8 chip_id, u8tmp;

	priv = kzalloc(sizeof(struct m88ts2022_priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		dev_err(&i2c->dev, "%s: kzalloc() failed\n", KBUILD_MODNAME);
		goto err;
	}

	priv->cfg = cfg;
	priv->i2c = i2c;
	priv->fe = fe;

	/* check if the tuner is there */
	ret = m88ts2022_rd_reg(priv, 0x00, &u8tmp);
	if (ret)
		goto err;

	if ((u8tmp & 0x03) == 0x00) {
		ret = m88ts2022_wr_reg(priv, 0x00, 0x01);
		if (ret < 0)
			goto err;

		usleep_range(2000, 50000);
	}

	ret = m88ts2022_wr_reg(priv, 0x00, 0x03);
	if (ret)
		goto err;

	usleep_range(2000, 50000);

	ret = m88ts2022_rd_reg(priv, 0x00, &chip_id);
	if (ret)
		goto err;

	dev_dbg(&priv->i2c->dev, "%s: chip_id=%02x\n", __func__, chip_id);

	switch (chip_id) {
	case 0xc3:
	case 0x83:
		break;
	default:
		goto err;
	}

	switch (priv->cfg->clock_out) {
	case M88TS2022_CLOCK_OUT_DISABLED:
		u8tmp = 0x60;
		break;
	case M88TS2022_CLOCK_OUT_ENABLED:
		u8tmp = 0x70;
		ret = m88ts2022_wr_reg(priv, 0x05, priv->cfg->clock_out_div);
		if (ret)
			goto err;
		break;
	case M88TS2022_CLOCK_OUT_ENABLED_XTALOUT:
		u8tmp = 0x6c;
		break;
	default:
		goto err;
	}

	ret = m88ts2022_wr_reg(priv, 0x42, u8tmp);
	if (ret)
		goto err;

	if (priv->cfg->loop_through)
		u8tmp = 0xec;
	else
		u8tmp = 0x6c;

	ret = m88ts2022_wr_reg(priv, 0x62, u8tmp);
	if (ret)
		goto err;

	/* sleep */
	ret = m88ts2022_wr_reg(priv, 0x00, 0x00);
	if (ret)
		goto err;

	dev_info(&priv->i2c->dev,
			"%s: Montage M88TS2022 successfully identified\n",
			KBUILD_MODNAME);

	fe->tuner_priv = priv;
	memcpy(&fe->ops.tuner_ops, &m88ts2022_tuner_ops,
			sizeof(struct dvb_tuner_ops));
err:
	if (ret) {
		dev_dbg(&i2c->dev, "%s: failed=%d\n", __func__, ret);
		kfree(priv);
		return NULL;
	}

	return fe;
}
EXPORT_SYMBOL(m88ts2022_attach);

MODULE_DESCRIPTION("Montage M88TS2022 silicon tuner driver");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_LICENSE("GPL");
