/*
 * Montage M88DS3103 demodulator driver
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
 */

#include "m88ds3103_priv.h"

static struct dvb_frontend_ops m88ds3103_ops;

/* write multiple registers */
static int m88ds3103_wr_regs(struct m88ds3103_priv *priv,
		u8 reg, const u8 *val, int len)
{
#define MAX_WR_LEN 32
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

	mutex_lock(&priv->i2c_mutex);
	ret = i2c_transfer(priv->i2c, msg, 1);
	mutex_unlock(&priv->i2c_mutex);
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
static int m88ds3103_rd_regs(struct m88ds3103_priv *priv,
		u8 reg, u8 *val, int len)
{
#define MAX_RD_LEN 3
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

	mutex_lock(&priv->i2c_mutex);
	ret = i2c_transfer(priv->i2c, msg, 2);
	mutex_unlock(&priv->i2c_mutex);
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
static int m88ds3103_wr_reg(struct m88ds3103_priv *priv, u8 reg, u8 val)
{
	return m88ds3103_wr_regs(priv, reg, &val, 1);
}

/* read single register */
static int m88ds3103_rd_reg(struct m88ds3103_priv *priv, u8 reg, u8 *val)
{
	return m88ds3103_rd_regs(priv, reg, val, 1);
}

/* write single register with mask */
static int m88ds3103_wr_reg_mask(struct m88ds3103_priv *priv,
		u8 reg, u8 val, u8 mask)
{
	int ret;
	u8 u8tmp;

	/* no need for read if whole reg is written */
	if (mask != 0xff) {
		ret = m88ds3103_rd_regs(priv, reg, &u8tmp, 1);
		if (ret)
			return ret;

		val &= mask;
		u8tmp &= ~mask;
		val |= u8tmp;
	}

	return m88ds3103_wr_regs(priv, reg, &val, 1);
}

/* read single register with mask */
static int m88ds3103_rd_reg_mask(struct m88ds3103_priv *priv,
		u8 reg, u8 *val, u8 mask)
{
	int ret, i;
	u8 u8tmp;

	ret = m88ds3103_rd_regs(priv, reg, &u8tmp, 1);
	if (ret)
		return ret;

	u8tmp &= mask;

	/* find position of the first bit */
	for (i = 0; i < 8; i++) {
		if ((mask >> i) & 0x01)
			break;
	}
	*val = u8tmp >> i;

	return 0;
}

/* write reg val table using reg addr auto increment */
static int m88ds3103_wr_reg_val_tab(struct m88ds3103_priv *priv,
		const struct m88ds3103_reg_val *tab, int tab_len)
{
	int ret, i, j;
	u8 buf[83];

	dev_dbg(&priv->i2c->dev, "%s: tab_len=%d\n", __func__, tab_len);

	if (tab_len > 83) {
		ret = -EINVAL;
		goto err;
	}

	for (i = 0, j = 0; i < tab_len; i++, j++) {
		buf[j] = tab[i].val;

		if (i == tab_len - 1 || tab[i].reg != tab[i + 1].reg - 1 ||
				!((j + 1) % (priv->cfg->i2c_wr_max - 1))) {
			ret = m88ds3103_wr_regs(priv, tab[i].reg - j, buf, j + 1);
			if (ret)
				goto err;

			j = -1;
		}
	}

	return 0;
err:
	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int m88ds3103_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct m88ds3103_priv *priv = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;
	u8 u8tmp;

	*status = 0;

	if (!priv->warm) {
		ret = -EAGAIN;
		goto err;
	}

	switch (c->delivery_system) {
	case SYS_DVBS:
		ret = m88ds3103_rd_reg_mask(priv, 0xd1, &u8tmp, 0x07);
		if (ret)
			goto err;

		if (u8tmp == 0x07)
			*status = FE_HAS_SIGNAL | FE_HAS_CARRIER |
					FE_HAS_VITERBI | FE_HAS_SYNC |
					FE_HAS_LOCK;
		break;
	case SYS_DVBS2:
		ret = m88ds3103_rd_reg_mask(priv, 0x0d, &u8tmp, 0x8f);
		if (ret)
			goto err;

		if (u8tmp == 0x8f)
			*status = FE_HAS_SIGNAL | FE_HAS_CARRIER |
					FE_HAS_VITERBI | FE_HAS_SYNC |
					FE_HAS_LOCK;
		break;
	default:
		dev_dbg(&priv->i2c->dev, "%s: invalid delivery_system\n",
				__func__);
		ret = -EINVAL;
		goto err;
	}

	priv->fe_status = *status;

	dev_dbg(&priv->i2c->dev, "%s: lock=%02x status=%02x\n",
			__func__, u8tmp, *status);

	return 0;
err:
	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int m88ds3103_set_frontend(struct dvb_frontend *fe)
{
	struct m88ds3103_priv *priv = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, len;
	const struct m88ds3103_reg_val *init;
	u8 u8tmp, u8tmp1, u8tmp2;
	u8 buf[2];
	u16 u16tmp, divide_ratio;
	u32 tuner_frequency, target_mclk;
	s32 s32tmp;

	dev_dbg(&priv->i2c->dev,
			"%s: delivery_system=%d modulation=%d frequency=%d symbol_rate=%d inversion=%d pilot=%d rolloff=%d\n",
			__func__, c->delivery_system,
			c->modulation, c->frequency, c->symbol_rate,
			c->inversion, c->pilot, c->rolloff);

	if (!priv->warm) {
		ret = -EAGAIN;
		goto err;
	}

	/* program tuner */
	if (fe->ops.tuner_ops.set_params) {
		ret = fe->ops.tuner_ops.set_params(fe);
		if (ret)
			goto err;
	}

	if (fe->ops.tuner_ops.get_frequency) {
		ret = fe->ops.tuner_ops.get_frequency(fe, &tuner_frequency);
		if (ret)
			goto err;
	} else {
		/*
		 * Use nominal target frequency as tuner driver does not provide
		 * actual frequency used. Carrier offset calculation is not
		 * valid.
		 */
		tuner_frequency = c->frequency;
	}

	/* reset */
	ret = m88ds3103_wr_reg(priv, 0x07, 0x80);
	if (ret)
		goto err;

	ret = m88ds3103_wr_reg(priv, 0x07, 0x00);
	if (ret)
		goto err;

	ret = m88ds3103_wr_reg(priv, 0xb2, 0x01);
	if (ret)
		goto err;

	ret = m88ds3103_wr_reg(priv, 0x00, 0x01);
	if (ret)
		goto err;

	switch (c->delivery_system) {
	case SYS_DVBS:
		len = ARRAY_SIZE(m88ds3103_dvbs_init_reg_vals);
		init = m88ds3103_dvbs_init_reg_vals;
		target_mclk = 96000;
		break;
	case SYS_DVBS2:
		len = ARRAY_SIZE(m88ds3103_dvbs2_init_reg_vals);
		init = m88ds3103_dvbs2_init_reg_vals;

		switch (priv->cfg->ts_mode) {
		case M88DS3103_TS_SERIAL:
		case M88DS3103_TS_SERIAL_D7:
			if (c->symbol_rate < 18000000)
				target_mclk = 96000;
			else
				target_mclk = 144000;
			break;
		case M88DS3103_TS_PARALLEL:
		case M88DS3103_TS_CI:
			if (c->symbol_rate < 18000000)
				target_mclk = 96000;
			else if (c->symbol_rate < 28000000)
				target_mclk = 144000;
			else
				target_mclk = 192000;
			break;
		default:
			dev_dbg(&priv->i2c->dev, "%s: invalid ts_mode\n",
					__func__);
			ret = -EINVAL;
			goto err;
		}
		break;
	default:
		dev_dbg(&priv->i2c->dev, "%s: invalid delivery_system\n",
				__func__);
		ret = -EINVAL;
		goto err;
	}

	/* program init table */
	if (c->delivery_system != priv->delivery_system) {
		ret = m88ds3103_wr_reg_val_tab(priv, init, len);
		if (ret)
			goto err;
	}

	u8tmp1 = 0; /* silence compiler warning */
	switch (priv->cfg->ts_mode) {
	case M88DS3103_TS_SERIAL:
		u8tmp1 = 0x00;
		u8tmp = 0x06;
		break;
	case M88DS3103_TS_SERIAL_D7:
		u8tmp1 = 0x20;
		u8tmp = 0x06;
		break;
	case M88DS3103_TS_PARALLEL:
		u8tmp = 0x02;
		break;
	case M88DS3103_TS_CI:
		u8tmp = 0x03;
		break;
	default:
		dev_dbg(&priv->i2c->dev, "%s: invalid ts_mode\n", __func__);
		ret = -EINVAL;
		goto err;
	}

	if (priv->cfg->ts_clk_pol)
		u8tmp |= 0x40;

	/* TS mode */
	ret = m88ds3103_wr_reg(priv, 0xfd, u8tmp);
	if (ret)
		goto err;

	switch (priv->cfg->ts_mode) {
	case M88DS3103_TS_SERIAL:
	case M88DS3103_TS_SERIAL_D7:
		ret = m88ds3103_wr_reg_mask(priv, 0x29, u8tmp1, 0x20);
		if (ret)
			goto err;
	}

	if (priv->cfg->ts_clk) {
		divide_ratio = DIV_ROUND_UP(target_mclk, priv->cfg->ts_clk);
		u8tmp1 = divide_ratio / 2;
		u8tmp2 = DIV_ROUND_UP(divide_ratio, 2);
	} else {
		divide_ratio = 0;
		u8tmp1 = 0;
		u8tmp2 = 0;
	}

	dev_dbg(&priv->i2c->dev,
			"%s: target_mclk=%d ts_clk=%d divide_ratio=%d\n",
			__func__, target_mclk, priv->cfg->ts_clk, divide_ratio);

	u8tmp1--;
	u8tmp2--;
	/* u8tmp1[5:2] => fe[3:0], u8tmp1[1:0] => ea[7:6] */
	u8tmp1 &= 0x3f;
	/* u8tmp2[5:0] => ea[5:0] */
	u8tmp2 &= 0x3f;

	ret = m88ds3103_rd_reg(priv, 0xfe, &u8tmp);
	if (ret)
		goto err;

	u8tmp = ((u8tmp  & 0xf0) << 0) | u8tmp1 >> 2;
	ret = m88ds3103_wr_reg(priv, 0xfe, u8tmp);
	if (ret)
		goto err;

	u8tmp = ((u8tmp1 & 0x03) << 6) | u8tmp2 >> 0;
	ret = m88ds3103_wr_reg(priv, 0xea, u8tmp);
	if (ret)
		goto err;

	switch (target_mclk) {
	case 96000:
		u8tmp1 = 0x02; /* 0b10 */
		u8tmp2 = 0x01; /* 0b01 */
		break;
	case 144000:
		u8tmp1 = 0x00; /* 0b00 */
		u8tmp2 = 0x01; /* 0b01 */
		break;
	case 192000:
		u8tmp1 = 0x03; /* 0b11 */
		u8tmp2 = 0x00; /* 0b00 */
		break;
	}

	ret = m88ds3103_wr_reg_mask(priv, 0x22, u8tmp1 << 6, 0xc0);
	if (ret)
		goto err;

	ret = m88ds3103_wr_reg_mask(priv, 0x24, u8tmp2 << 6, 0xc0);
	if (ret)
		goto err;

	if (c->symbol_rate <= 3000000)
		u8tmp = 0x20;
	else if (c->symbol_rate <= 10000000)
		u8tmp = 0x10;
	else
		u8tmp = 0x06;

	ret = m88ds3103_wr_reg(priv, 0xc3, 0x08);
	if (ret)
		goto err;

	ret = m88ds3103_wr_reg(priv, 0xc8, u8tmp);
	if (ret)
		goto err;

	ret = m88ds3103_wr_reg(priv, 0xc4, 0x08);
	if (ret)
		goto err;

	ret = m88ds3103_wr_reg(priv, 0xc7, 0x00);
	if (ret)
		goto err;

	u16tmp = DIV_ROUND_CLOSEST((c->symbol_rate / 1000) << 15, M88DS3103_MCLK_KHZ / 2);
	buf[0] = (u16tmp >> 0) & 0xff;
	buf[1] = (u16tmp >> 8) & 0xff;
	ret = m88ds3103_wr_regs(priv, 0x61, buf, 2);
	if (ret)
		goto err;

	ret = m88ds3103_wr_reg_mask(priv, 0x4d, priv->cfg->spec_inv << 1, 0x02);
	if (ret)
		goto err;

	ret = m88ds3103_wr_reg_mask(priv, 0x30, priv->cfg->agc_inv << 4, 0x10);
	if (ret)
		goto err;

	ret = m88ds3103_wr_reg(priv, 0x33, priv->cfg->agc);
	if (ret)
		goto err;

	dev_dbg(&priv->i2c->dev, "%s: carrier offset=%d\n", __func__,
			(tuner_frequency - c->frequency));

	s32tmp = 0x10000 * (tuner_frequency - c->frequency);
	s32tmp = DIV_ROUND_CLOSEST(s32tmp, M88DS3103_MCLK_KHZ);
	if (s32tmp < 0)
		s32tmp += 0x10000;

	buf[0] = (s32tmp >> 0) & 0xff;
	buf[1] = (s32tmp >> 8) & 0xff;
	ret = m88ds3103_wr_regs(priv, 0x5e, buf, 2);
	if (ret)
		goto err;

	ret = m88ds3103_wr_reg(priv, 0x00, 0x00);
	if (ret)
		goto err;

	ret = m88ds3103_wr_reg(priv, 0xb2, 0x00);
	if (ret)
		goto err;

	priv->delivery_system = c->delivery_system;

	return 0;
err:
	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int m88ds3103_init(struct dvb_frontend *fe)
{
	struct m88ds3103_priv *priv = fe->demodulator_priv;
	int ret, len, remaining;
	const struct firmware *fw = NULL;
	u8 *fw_file = M88DS3103_FIRMWARE;
	u8 u8tmp;

	dev_dbg(&priv->i2c->dev, "%s:\n", __func__);

	/* set cold state by default */
	priv->warm = false;

	/* wake up device from sleep */
	ret = m88ds3103_wr_reg_mask(priv, 0x08, 0x01, 0x01);
	if (ret)
		goto err;

	ret = m88ds3103_wr_reg_mask(priv, 0x04, 0x00, 0x01);
	if (ret)
		goto err;

	ret = m88ds3103_wr_reg_mask(priv, 0x23, 0x00, 0x10);
	if (ret)
		goto err;

	/* reset */
	ret = m88ds3103_wr_reg(priv, 0x07, 0x60);
	if (ret)
		goto err;

	ret = m88ds3103_wr_reg(priv, 0x07, 0x00);
	if (ret)
		goto err;

	/* firmware status */
	ret = m88ds3103_rd_reg(priv, 0xb9, &u8tmp);
	if (ret)
		goto err;

	dev_dbg(&priv->i2c->dev, "%s: firmware=%02x\n", __func__, u8tmp);

	if (u8tmp)
		goto skip_fw_download;

	/* cold state - try to download firmware */
	dev_info(&priv->i2c->dev, "%s: found a '%s' in cold state\n",
			KBUILD_MODNAME, m88ds3103_ops.info.name);

	/* request the firmware, this will block and timeout */
	ret = request_firmware(&fw, fw_file, priv->i2c->dev.parent);
	if (ret) {
		dev_err(&priv->i2c->dev, "%s: firmare file '%s' not found\n",
				KBUILD_MODNAME, fw_file);
		goto err;
	}

	dev_info(&priv->i2c->dev, "%s: downloading firmware from file '%s'\n",
			KBUILD_MODNAME, fw_file);

	ret = m88ds3103_wr_reg(priv, 0xb2, 0x01);
	if (ret)
		goto err;

	for (remaining = fw->size; remaining > 0;
			remaining -= (priv->cfg->i2c_wr_max - 1)) {
		len = remaining;
		if (len > (priv->cfg->i2c_wr_max - 1))
			len = (priv->cfg->i2c_wr_max - 1);

		ret = m88ds3103_wr_regs(priv, 0xb0,
				&fw->data[fw->size - remaining], len);
		if (ret) {
			dev_err(&priv->i2c->dev,
					"%s: firmware download failed=%d\n",
					KBUILD_MODNAME, ret);
			goto err;
		}
	}

	ret = m88ds3103_wr_reg(priv, 0xb2, 0x00);
	if (ret)
		goto err;

	release_firmware(fw);
	fw = NULL;

	ret = m88ds3103_rd_reg(priv, 0xb9, &u8tmp);
	if (ret)
		goto err;

	if (!u8tmp) {
		dev_info(&priv->i2c->dev, "%s: firmware did not run\n",
				KBUILD_MODNAME);
		ret = -EFAULT;
		goto err;
	}

	dev_info(&priv->i2c->dev, "%s: found a '%s' in warm state\n",
			KBUILD_MODNAME, m88ds3103_ops.info.name);
	dev_info(&priv->i2c->dev, "%s: firmware version %X.%X\n",
			KBUILD_MODNAME, (u8tmp >> 4) & 0xf, (u8tmp >> 0 & 0xf));

skip_fw_download:
	/* warm state */
	priv->warm = true;

	return 0;
err:
	if (fw)
		release_firmware(fw);

	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int m88ds3103_sleep(struct dvb_frontend *fe)
{
	struct m88ds3103_priv *priv = fe->demodulator_priv;
	int ret;

	dev_dbg(&priv->i2c->dev, "%s:\n", __func__);

	priv->delivery_system = SYS_UNDEFINED;

	/* TS Hi-Z */
	ret = m88ds3103_wr_reg_mask(priv, 0x27, 0x00, 0x01);
	if (ret)
		goto err;

	/* sleep */
	ret = m88ds3103_wr_reg_mask(priv, 0x08, 0x00, 0x01);
	if (ret)
		goto err;

	ret = m88ds3103_wr_reg_mask(priv, 0x04, 0x01, 0x01);
	if (ret)
		goto err;

	ret = m88ds3103_wr_reg_mask(priv, 0x23, 0x10, 0x10);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int m88ds3103_get_frontend(struct dvb_frontend *fe)
{
	struct m88ds3103_priv *priv = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;
	u8 buf[3];

	dev_dbg(&priv->i2c->dev, "%s:\n", __func__);

	if (!priv->warm || !(priv->fe_status & FE_HAS_LOCK)) {
		ret = -EAGAIN;
		goto err;
	}

	switch (c->delivery_system) {
	case SYS_DVBS:
		ret = m88ds3103_rd_reg(priv, 0xe0, &buf[0]);
		if (ret)
			goto err;

		ret = m88ds3103_rd_reg(priv, 0xe6, &buf[1]);
		if (ret)
			goto err;

		switch ((buf[0] >> 2) & 0x01) {
		case 0:
			c->inversion = INVERSION_OFF;
			break;
		case 1:
			c->inversion = INVERSION_ON;
			break;
		}

		switch ((buf[1] >> 5) & 0x07) {
		case 0:
			c->fec_inner = FEC_7_8;
			break;
		case 1:
			c->fec_inner = FEC_5_6;
			break;
		case 2:
			c->fec_inner = FEC_3_4;
			break;
		case 3:
			c->fec_inner = FEC_2_3;
			break;
		case 4:
			c->fec_inner = FEC_1_2;
			break;
		default:
			dev_dbg(&priv->i2c->dev, "%s: invalid fec_inner\n",
					__func__);
		}

		c->modulation = QPSK;

		break;
	case SYS_DVBS2:
		ret = m88ds3103_rd_reg(priv, 0x7e, &buf[0]);
		if (ret)
			goto err;

		ret = m88ds3103_rd_reg(priv, 0x89, &buf[1]);
		if (ret)
			goto err;

		ret = m88ds3103_rd_reg(priv, 0xf2, &buf[2]);
		if (ret)
			goto err;

		switch ((buf[0] >> 0) & 0x0f) {
		case 2:
			c->fec_inner = FEC_2_5;
			break;
		case 3:
			c->fec_inner = FEC_1_2;
			break;
		case 4:
			c->fec_inner = FEC_3_5;
			break;
		case 5:
			c->fec_inner = FEC_2_3;
			break;
		case 6:
			c->fec_inner = FEC_3_4;
			break;
		case 7:
			c->fec_inner = FEC_4_5;
			break;
		case 8:
			c->fec_inner = FEC_5_6;
			break;
		case 9:
			c->fec_inner = FEC_8_9;
			break;
		case 10:
			c->fec_inner = FEC_9_10;
			break;
		default:
			dev_dbg(&priv->i2c->dev, "%s: invalid fec_inner\n",
					__func__);
		}

		switch ((buf[0] >> 5) & 0x01) {
		case 0:
			c->pilot = PILOT_OFF;
			break;
		case 1:
			c->pilot = PILOT_ON;
			break;
		}

		switch ((buf[0] >> 6) & 0x07) {
		case 0:
			c->modulation = QPSK;
			break;
		case 1:
			c->modulation = PSK_8;
			break;
		case 2:
			c->modulation = APSK_16;
			break;
		case 3:
			c->modulation = APSK_32;
			break;
		default:
			dev_dbg(&priv->i2c->dev, "%s: invalid modulation\n",
					__func__);
		}

		switch ((buf[1] >> 7) & 0x01) {
		case 0:
			c->inversion = INVERSION_OFF;
			break;
		case 1:
			c->inversion = INVERSION_ON;
			break;
		}

		switch ((buf[2] >> 0) & 0x03) {
		case 0:
			c->rolloff = ROLLOFF_35;
			break;
		case 1:
			c->rolloff = ROLLOFF_25;
			break;
		case 2:
			c->rolloff = ROLLOFF_20;
			break;
		default:
			dev_dbg(&priv->i2c->dev, "%s: invalid rolloff\n",
					__func__);
		}
		break;
	default:
		dev_dbg(&priv->i2c->dev, "%s: invalid delivery_system\n",
				__func__);
		ret = -EINVAL;
		goto err;
	}

	ret = m88ds3103_rd_regs(priv, 0x6d, buf, 2);
	if (ret)
		goto err;

	c->symbol_rate = 1ull * ((buf[1] << 8) | (buf[0] << 0)) *
			M88DS3103_MCLK_KHZ * 1000 / 0x10000;

	return 0;
err:
	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int m88ds3103_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct m88ds3103_priv *priv = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i, tmp;
	u8 buf[3];
	u16 noise, signal;
	u32 noise_tot, signal_tot;

	dev_dbg(&priv->i2c->dev, "%s:\n", __func__);
	/* reports SNR in resolution of 0.1 dB */

	/* more iterations for more accurate estimation */
	#define M88DS3103_SNR_ITERATIONS 3

	switch (c->delivery_system) {
	case SYS_DVBS:
		tmp = 0;

		for (i = 0; i < M88DS3103_SNR_ITERATIONS; i++) {
			ret = m88ds3103_rd_reg(priv, 0xff, &buf[0]);
			if (ret)
				goto err;

			tmp += buf[0];
		}

		/* use of one register limits max value to 15 dB */
		/* SNR(X) dB = 10 * ln(X) / ln(10) dB */
		tmp = DIV_ROUND_CLOSEST(tmp, 8 * M88DS3103_SNR_ITERATIONS);
		if (tmp)
			*snr = div_u64((u64) 100 * intlog2(tmp), intlog2(10));
		else
			*snr = 0;
		break;
	case SYS_DVBS2:
		noise_tot = 0;
		signal_tot = 0;

		for (i = 0; i < M88DS3103_SNR_ITERATIONS; i++) {
			ret = m88ds3103_rd_regs(priv, 0x8c, buf, 3);
			if (ret)
				goto err;

			noise = buf[1] << 6;    /* [13:6] */
			noise |= buf[0] & 0x3f; /*  [5:0] */
			noise >>= 2;
			signal = buf[2] * buf[2];
			signal >>= 1;

			noise_tot += noise;
			signal_tot += signal;
		}

		noise = noise_tot / M88DS3103_SNR_ITERATIONS;
		signal = signal_tot / M88DS3103_SNR_ITERATIONS;

		/* SNR(X) dB = 10 * log10(X) dB */
		if (signal > noise) {
			tmp = signal / noise;
			*snr = div_u64((u64) 100 * intlog10(tmp), (1 << 24));
		} else {
			*snr = 0;
		}
		break;
	default:
		dev_dbg(&priv->i2c->dev, "%s: invalid delivery_system\n",
				__func__);
		ret = -EINVAL;
		goto err;
	}

	return 0;
err:
	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int m88ds3103_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct m88ds3103_priv *priv = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;
	unsigned int utmp;
	u8 buf[3], u8tmp;

	dev_dbg(&priv->i2c->dev, "%s:\n", __func__);

	switch (c->delivery_system) {
	case SYS_DVBS:
		ret = m88ds3103_wr_reg(priv, 0xf9, 0x04);
		if (ret)
			goto err;

		ret = m88ds3103_rd_reg(priv, 0xf8, &u8tmp);
		if (ret)
			goto err;

		if (!(u8tmp & 0x10)) {
			u8tmp |= 0x10;

			ret = m88ds3103_rd_regs(priv, 0xf6, buf, 2);
			if (ret)
				goto err;

			priv->ber = (buf[1] << 8) | (buf[0] << 0);

			/* restart counters */
			ret = m88ds3103_wr_reg(priv, 0xf8, u8tmp);
			if (ret)
				goto err;
		}
		break;
	case SYS_DVBS2:
		ret = m88ds3103_rd_regs(priv, 0xd5, buf, 3);
		if (ret)
			goto err;

		utmp = (buf[2] << 16) | (buf[1] << 8) | (buf[0] << 0);

		if (utmp > 3000) {
			ret = m88ds3103_rd_regs(priv, 0xf7, buf, 2);
			if (ret)
				goto err;

			priv->ber = (buf[1] << 8) | (buf[0] << 0);

			/* restart counters */
			ret = m88ds3103_wr_reg(priv, 0xd1, 0x01);
			if (ret)
				goto err;

			ret = m88ds3103_wr_reg(priv, 0xf9, 0x01);
			if (ret)
				goto err;

			ret = m88ds3103_wr_reg(priv, 0xf9, 0x00);
			if (ret)
				goto err;

			ret = m88ds3103_wr_reg(priv, 0xd1, 0x00);
			if (ret)
				goto err;
		}
		break;
	default:
		dev_dbg(&priv->i2c->dev, "%s: invalid delivery_system\n",
				__func__);
		ret = -EINVAL;
		goto err;
	}

	*ber = priv->ber;

	return 0;
err:
	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int m88ds3103_set_tone(struct dvb_frontend *fe,
	fe_sec_tone_mode_t fe_sec_tone_mode)
{
	struct m88ds3103_priv *priv = fe->demodulator_priv;
	int ret;
	u8 u8tmp, tone, reg_a1_mask;

	dev_dbg(&priv->i2c->dev, "%s: fe_sec_tone_mode=%d\n", __func__,
			fe_sec_tone_mode);

	if (!priv->warm) {
		ret = -EAGAIN;
		goto err;
	}

	switch (fe_sec_tone_mode) {
	case SEC_TONE_ON:
		tone = 0;
		reg_a1_mask = 0x47;
		break;
	case SEC_TONE_OFF:
		tone = 1;
		reg_a1_mask = 0x00;
		break;
	default:
		dev_dbg(&priv->i2c->dev, "%s: invalid fe_sec_tone_mode\n",
				__func__);
		ret = -EINVAL;
		goto err;
	}

	u8tmp = tone << 7 | priv->cfg->envelope_mode << 5;
	ret = m88ds3103_wr_reg_mask(priv, 0xa2, u8tmp, 0xe0);
	if (ret)
		goto err;

	u8tmp = 1 << 2;
	ret = m88ds3103_wr_reg_mask(priv, 0xa1, u8tmp, reg_a1_mask);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int m88ds3103_set_voltage(struct dvb_frontend *fe,
	fe_sec_voltage_t fe_sec_voltage)
{
	struct m88ds3103_priv *priv = fe->demodulator_priv;
	int ret;
	u8 u8tmp;
	bool voltage_sel, voltage_dis;

	dev_dbg(&priv->i2c->dev, "%s: fe_sec_voltage=%d\n", __func__,
			fe_sec_voltage);

	if (!priv->warm) {
		ret = -EAGAIN;
		goto err;
	}

	switch (fe_sec_voltage) {
	case SEC_VOLTAGE_18:
		voltage_sel = true;
		voltage_dis = false;
		break;
	case SEC_VOLTAGE_13:
		voltage_sel = false;
		voltage_dis = false;
		break;
	case SEC_VOLTAGE_OFF:
		voltage_sel = false;
		voltage_dis = true;
		break;
	default:
		dev_dbg(&priv->i2c->dev, "%s: invalid fe_sec_voltage\n",
				__func__);
		ret = -EINVAL;
		goto err;
	}

	/* output pin polarity */
	voltage_sel ^= priv->cfg->lnb_hv_pol;
	voltage_dis ^= priv->cfg->lnb_en_pol;

	u8tmp = voltage_dis << 1 | voltage_sel << 0;
	ret = m88ds3103_wr_reg_mask(priv, 0xa2, u8tmp, 0x03);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int m88ds3103_diseqc_send_master_cmd(struct dvb_frontend *fe,
		struct dvb_diseqc_master_cmd *diseqc_cmd)
{
	struct m88ds3103_priv *priv = fe->demodulator_priv;
	int ret, i;
	u8 u8tmp;

	dev_dbg(&priv->i2c->dev, "%s: msg=%*ph\n", __func__,
			diseqc_cmd->msg_len, diseqc_cmd->msg);

	if (!priv->warm) {
		ret = -EAGAIN;
		goto err;
	}

	if (diseqc_cmd->msg_len < 3 || diseqc_cmd->msg_len > 6) {
		ret = -EINVAL;
		goto err;
	}

	u8tmp = priv->cfg->envelope_mode << 5;
	ret = m88ds3103_wr_reg_mask(priv, 0xa2, u8tmp, 0xe0);
	if (ret)
		goto err;

	ret = m88ds3103_wr_regs(priv, 0xa3, diseqc_cmd->msg,
			diseqc_cmd->msg_len);
	if (ret)
		goto err;

	ret = m88ds3103_wr_reg(priv, 0xa1,
			(diseqc_cmd->msg_len - 1) << 3 | 0x07);
	if (ret)
		goto err;

	/* DiSEqC message typical period is 54 ms */
	usleep_range(40000, 60000);

	/* wait DiSEqC TX ready */
	for (i = 20, u8tmp = 1; i && u8tmp; i--) {
		usleep_range(5000, 10000);

		ret = m88ds3103_rd_reg_mask(priv, 0xa1, &u8tmp, 0x40);
		if (ret)
			goto err;
	}

	dev_dbg(&priv->i2c->dev, "%s: loop=%d\n", __func__, i);

	if (i == 0) {
		dev_dbg(&priv->i2c->dev, "%s: diseqc tx timeout\n", __func__);

		ret = m88ds3103_wr_reg_mask(priv, 0xa1, 0x40, 0xc0);
		if (ret)
			goto err;
	}

	ret = m88ds3103_wr_reg_mask(priv, 0xa2, 0x80, 0xc0);
	if (ret)
		goto err;

	if (i == 0) {
		ret = -ETIMEDOUT;
		goto err;
	}

	return 0;
err:
	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int m88ds3103_diseqc_send_burst(struct dvb_frontend *fe,
	fe_sec_mini_cmd_t fe_sec_mini_cmd)
{
	struct m88ds3103_priv *priv = fe->demodulator_priv;
	int ret, i;
	u8 u8tmp, burst;

	dev_dbg(&priv->i2c->dev, "%s: fe_sec_mini_cmd=%d\n", __func__,
			fe_sec_mini_cmd);

	if (!priv->warm) {
		ret = -EAGAIN;
		goto err;
	}

	u8tmp = priv->cfg->envelope_mode << 5;
	ret = m88ds3103_wr_reg_mask(priv, 0xa2, u8tmp, 0xe0);
	if (ret)
		goto err;

	switch (fe_sec_mini_cmd) {
	case SEC_MINI_A:
		burst = 0x02;
		break;
	case SEC_MINI_B:
		burst = 0x01;
		break;
	default:
		dev_dbg(&priv->i2c->dev, "%s: invalid fe_sec_mini_cmd\n",
				__func__);
		ret = -EINVAL;
		goto err;
	}

	ret = m88ds3103_wr_reg(priv, 0xa1, burst);
	if (ret)
		goto err;

	/* DiSEqC ToneBurst period is 12.5 ms */
	usleep_range(11000, 20000);

	/* wait DiSEqC TX ready */
	for (i = 5, u8tmp = 1; i && u8tmp; i--) {
		usleep_range(800, 2000);

		ret = m88ds3103_rd_reg_mask(priv, 0xa1, &u8tmp, 0x40);
		if (ret)
			goto err;
	}

	dev_dbg(&priv->i2c->dev, "%s: loop=%d\n", __func__, i);

	ret = m88ds3103_wr_reg_mask(priv, 0xa2, 0x80, 0xc0);
	if (ret)
		goto err;

	if (i == 0) {
		dev_dbg(&priv->i2c->dev, "%s: diseqc tx timeout\n", __func__);
		ret = -ETIMEDOUT;
		goto err;
	}

	return 0;
err:
	dev_dbg(&priv->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int m88ds3103_get_tune_settings(struct dvb_frontend *fe,
	struct dvb_frontend_tune_settings *s)
{
	s->min_delay_ms = 3000;

	return 0;
}

static void m88ds3103_release(struct dvb_frontend *fe)
{
	struct m88ds3103_priv *priv = fe->demodulator_priv;

	i2c_del_mux_adapter(priv->i2c_adapter);
	kfree(priv);
}

static int m88ds3103_select(struct i2c_adapter *adap, void *mux_priv, u32 chan)
{
	struct m88ds3103_priv *priv = mux_priv;
	int ret;
	struct i2c_msg gate_open_msg[1] = {
		{
			.addr = priv->cfg->i2c_addr,
			.flags = 0,
			.len = 2,
			.buf = "\x03\x11",
		}
	};

	mutex_lock(&priv->i2c_mutex);

	/* open tuner I2C repeater for 1 xfer, closes automatically */
	ret = __i2c_transfer(priv->i2c, gate_open_msg, 1);
	if (ret != 1) {
		dev_warn(&priv->i2c->dev, "%s: i2c wr failed=%d\n",
				KBUILD_MODNAME, ret);
		if (ret >= 0)
			ret = -EREMOTEIO;

		return ret;
	}

	return 0;
}

static int m88ds3103_deselect(struct i2c_adapter *adap, void *mux_priv,
		u32 chan)
{
	struct m88ds3103_priv *priv = mux_priv;

	mutex_unlock(&priv->i2c_mutex);

	return 0;
}

struct dvb_frontend *m88ds3103_attach(const struct m88ds3103_config *cfg,
		struct i2c_adapter *i2c, struct i2c_adapter **tuner_i2c_adapter)
{
	int ret;
	struct m88ds3103_priv *priv;
	u8 chip_id, u8tmp;

	/* allocate memory for the internal priv */
	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		dev_err(&i2c->dev, "%s: kzalloc() failed\n", KBUILD_MODNAME);
		goto err;
	}

	priv->cfg = cfg;
	priv->i2c = i2c;
	mutex_init(&priv->i2c_mutex);

	ret = m88ds3103_rd_reg(priv, 0x01, &chip_id);
	if (ret)
		goto err;

	dev_dbg(&priv->i2c->dev, "%s: chip_id=%02x\n", __func__, chip_id);

	switch (chip_id) {
	case 0xd0:
		break;
	default:
		goto err;
	}

	switch (priv->cfg->clock_out) {
	case M88DS3103_CLOCK_OUT_DISABLED:
		u8tmp = 0x80;
		break;
	case M88DS3103_CLOCK_OUT_ENABLED:
		u8tmp = 0x00;
		break;
	case M88DS3103_CLOCK_OUT_ENABLED_DIV2:
		u8tmp = 0x10;
		break;
	default:
		goto err;
	}

	ret = m88ds3103_wr_reg(priv, 0x29, u8tmp);
	if (ret)
		goto err;

	/* sleep */
	ret = m88ds3103_wr_reg_mask(priv, 0x08, 0x00, 0x01);
	if (ret)
		goto err;

	ret = m88ds3103_wr_reg_mask(priv, 0x04, 0x01, 0x01);
	if (ret)
		goto err;

	ret = m88ds3103_wr_reg_mask(priv, 0x23, 0x10, 0x10);
	if (ret)
		goto err;

	/* create mux i2c adapter for tuner */
	priv->i2c_adapter = i2c_add_mux_adapter(i2c, &i2c->dev, priv, 0, 0, 0,
			m88ds3103_select, m88ds3103_deselect);
	if (priv->i2c_adapter == NULL)
		goto err;

	*tuner_i2c_adapter = priv->i2c_adapter;

	/* create dvb_frontend */
	memcpy(&priv->fe.ops, &m88ds3103_ops, sizeof(struct dvb_frontend_ops));
	priv->fe.demodulator_priv = priv;

	return &priv->fe;
err:
	dev_dbg(&i2c->dev, "%s: failed=%d\n", __func__, ret);
	kfree(priv);
	return NULL;
}
EXPORT_SYMBOL(m88ds3103_attach);

static struct dvb_frontend_ops m88ds3103_ops = {
	.delsys = { SYS_DVBS, SYS_DVBS2 },
	.info = {
		.name = "Montage M88DS3103",
		.frequency_min =  950000,
		.frequency_max = 2150000,
		.frequency_tolerance = 5000,
		.symbol_rate_min =  1000000,
		.symbol_rate_max = 45000000,
		.caps = FE_CAN_INVERSION_AUTO |
			FE_CAN_FEC_1_2 |
			FE_CAN_FEC_2_3 |
			FE_CAN_FEC_3_4 |
			FE_CAN_FEC_4_5 |
			FE_CAN_FEC_5_6 |
			FE_CAN_FEC_6_7 |
			FE_CAN_FEC_7_8 |
			FE_CAN_FEC_8_9 |
			FE_CAN_FEC_AUTO |
			FE_CAN_QPSK |
			FE_CAN_RECOVER |
			FE_CAN_2G_MODULATION
	},

	.release = m88ds3103_release,

	.get_tune_settings = m88ds3103_get_tune_settings,

	.init = m88ds3103_init,
	.sleep = m88ds3103_sleep,

	.set_frontend = m88ds3103_set_frontend,
	.get_frontend = m88ds3103_get_frontend,

	.read_status = m88ds3103_read_status,
	.read_snr = m88ds3103_read_snr,
	.read_ber = m88ds3103_read_ber,

	.diseqc_send_master_cmd = m88ds3103_diseqc_send_master_cmd,
	.diseqc_send_burst = m88ds3103_diseqc_send_burst,

	.set_tone = m88ds3103_set_tone,
	.set_voltage = m88ds3103_set_voltage,
};

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Montage M88DS3103 DVB-S/S2 demodulator driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(M88DS3103_FIRMWARE);
