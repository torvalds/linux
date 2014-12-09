/*
 * Realtek RTL2830 DVB-T demodulator driver
 *
 * Copyright (C) 2011 Antti Palosaari <crope@iki.fi>
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


/*
 * Driver implements own I2C-adapter for tuner I2C access. That's since chip
 * have unusual I2C-gate control which closes gate automatically after each
 * I2C transfer. Using own I2C adapter we can workaround that.
 */

#include "rtl2830_priv.h"

/* Max transfer size done by I2C transfer functions */
#define MAX_XFER_SIZE  64

/* write multiple hardware registers */
static int rtl2830_wr(struct i2c_client *client, u8 reg, const u8 *val, int len)
{
	struct rtl2830_dev *dev = i2c_get_clientdata(client);
	int ret;
	u8 buf[MAX_XFER_SIZE];
	struct i2c_msg msg[1] = {
		{
			.addr = dev->cfg.i2c_addr,
			.flags = 0,
			.len = 1 + len,
			.buf = buf,
		}
	};

	if (1 + len > sizeof(buf)) {
		dev_warn(&dev->i2c->dev,
			 "%s: i2c wr reg=%04x: len=%d is too big!\n",
			 KBUILD_MODNAME, reg, len);
		return -EINVAL;
	}

	buf[0] = reg;
	memcpy(&buf[1], val, len);

	ret = i2c_transfer(dev->i2c, msg, 1);
	if (ret == 1) {
		ret = 0;
	} else {
		dev_warn(&dev->i2c->dev, "%s: i2c wr failed=%d reg=%02x " \
				"len=%d\n", KBUILD_MODNAME, ret, reg, len);
		ret = -EREMOTEIO;
	}
	return ret;
}

/* read multiple hardware registers */
static int rtl2830_rd(struct i2c_client *client, u8 reg, u8 *val, int len)
{
	struct rtl2830_dev *dev = i2c_get_clientdata(client);
	int ret;
	struct i2c_msg msg[2] = {
		{
			.addr = dev->cfg.i2c_addr,
			.flags = 0,
			.len = 1,
			.buf = &reg,
		}, {
			.addr = dev->cfg.i2c_addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = val,
		}
	};

	ret = i2c_transfer(dev->i2c, msg, 2);
	if (ret == 2) {
		ret = 0;
	} else {
		dev_warn(&dev->i2c->dev, "%s: i2c rd failed=%d reg=%02x " \
				"len=%d\n", KBUILD_MODNAME, ret, reg, len);
		ret = -EREMOTEIO;
	}
	return ret;
}

/* write multiple registers */
static int rtl2830_wr_regs(struct i2c_client *client, u16 reg, const u8 *val, int len)
{
	struct rtl2830_dev *dev = i2c_get_clientdata(client);
	int ret;
	u8 reg2 = (reg >> 0) & 0xff;
	u8 page = (reg >> 8) & 0xff;

	/* switch bank if needed */
	if (page != dev->page) {
		ret = rtl2830_wr(client, 0x00, &page, 1);
		if (ret)
			return ret;

		dev->page = page;
	}

	return rtl2830_wr(client, reg2, val, len);
}

/* read multiple registers */
static int rtl2830_rd_regs(struct i2c_client *client, u16 reg, u8 *val, int len)
{
	struct rtl2830_dev *dev = i2c_get_clientdata(client);
	int ret;
	u8 reg2 = (reg >> 0) & 0xff;
	u8 page = (reg >> 8) & 0xff;

	/* switch bank if needed */
	if (page != dev->page) {
		ret = rtl2830_wr(client, 0x00, &page, 1);
		if (ret)
			return ret;

		dev->page = page;
	}

	return rtl2830_rd(client, reg2, val, len);
}

/* read single register */
static int rtl2830_rd_reg(struct i2c_client *client, u16 reg, u8 *val)
{
	return rtl2830_rd_regs(client, reg, val, 1);
}

/* write single register with mask */
static int rtl2830_wr_reg_mask(struct i2c_client *client, u16 reg, u8 val, u8 mask)
{
	int ret;
	u8 tmp;

	/* no need for read if whole reg is written */
	if (mask != 0xff) {
		ret = rtl2830_rd_regs(client, reg, &tmp, 1);
		if (ret)
			return ret;

		val &= mask;
		tmp &= ~mask;
		val |= tmp;
	}

	return rtl2830_wr_regs(client, reg, &val, 1);
}

/* read single register with mask */
static int rtl2830_rd_reg_mask(struct i2c_client *client, u16 reg, u8 *val, u8 mask)
{
	int ret, i;
	u8 tmp;

	ret = rtl2830_rd_regs(client, reg, &tmp, 1);
	if (ret)
		return ret;

	tmp &= mask;

	/* find position of the first bit */
	for (i = 0; i < 8; i++) {
		if ((mask >> i) & 0x01)
			break;
	}
	*val = tmp >> i;

	return 0;
}

static int rtl2830_init(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct rtl2830_dev *dev = i2c_get_clientdata(client);
	int ret, i;
	struct rtl2830_reg_val_mask tab[] = {
		{ 0x00d, 0x01, 0x03 },
		{ 0x00d, 0x10, 0x10 },
		{ 0x104, 0x00, 0x1e },
		{ 0x105, 0x80, 0x80 },
		{ 0x110, 0x02, 0x03 },
		{ 0x110, 0x08, 0x0c },
		{ 0x17b, 0x00, 0x40 },
		{ 0x17d, 0x05, 0x0f },
		{ 0x17d, 0x50, 0xf0 },
		{ 0x18c, 0x08, 0x0f },
		{ 0x18d, 0x00, 0xc0 },
		{ 0x188, 0x05, 0x0f },
		{ 0x189, 0x00, 0xfc },
		{ 0x2d5, 0x02, 0x02 },
		{ 0x2f1, 0x02, 0x06 },
		{ 0x2f1, 0x20, 0xf8 },
		{ 0x16d, 0x00, 0x01 },
		{ 0x1a6, 0x00, 0x80 },
		{ 0x106, dev->cfg.vtop, 0x3f },
		{ 0x107, dev->cfg.krf, 0x3f },
		{ 0x112, 0x28, 0xff },
		{ 0x103, dev->cfg.agc_targ_val, 0xff },
		{ 0x00a, 0x02, 0x07 },
		{ 0x140, 0x0c, 0x3c },
		{ 0x140, 0x40, 0xc0 },
		{ 0x15b, 0x05, 0x07 },
		{ 0x15b, 0x28, 0x38 },
		{ 0x15c, 0x05, 0x07 },
		{ 0x15c, 0x28, 0x38 },
		{ 0x115, dev->cfg.spec_inv, 0x01 },
		{ 0x16f, 0x01, 0x07 },
		{ 0x170, 0x18, 0x38 },
		{ 0x172, 0x0f, 0x0f },
		{ 0x173, 0x08, 0x38 },
		{ 0x175, 0x01, 0x07 },
		{ 0x176, 0x00, 0xc0 },
	};

	for (i = 0; i < ARRAY_SIZE(tab); i++) {
		ret = rtl2830_wr_reg_mask(client, tab[i].reg, tab[i].val,
			tab[i].mask);
		if (ret)
			goto err;
	}

	ret = rtl2830_wr_regs(client, 0x18f, "\x28\x00", 2);
	if (ret)
		goto err;

	ret = rtl2830_wr_regs(client, 0x195,
		"\x04\x06\x0a\x12\x0a\x12\x1e\x28", 8);
	if (ret)
		goto err;

	/* TODO: spec init */

	/* soft reset */
	ret = rtl2830_wr_reg_mask(client, 0x101, 0x04, 0x04);
	if (ret)
		goto err;

	ret = rtl2830_wr_reg_mask(client, 0x101, 0x00, 0x04);
	if (ret)
		goto err;

	dev->sleeping = false;

	return ret;
err:
	dev_dbg(&dev->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int rtl2830_sleep(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct rtl2830_dev *dev = i2c_get_clientdata(client);
	dev->sleeping = true;
	return 0;
}

static int rtl2830_get_tune_settings(struct dvb_frontend *fe,
	struct dvb_frontend_tune_settings *s)
{
	s->min_delay_ms = 500;
	s->step_size = fe->ops.info.frequency_stepsize * 2;
	s->max_drift = (fe->ops.info.frequency_stepsize * 2) + 1;

	return 0;
}

static int rtl2830_set_frontend(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct rtl2830_dev *dev = i2c_get_clientdata(client);
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i;
	u64 num;
	u8 buf[3], tmp;
	u32 if_ctl, if_frequency;
	static const u8 bw_params1[3][34] = {
		{
		0x1f, 0xf0, 0x1f, 0xf0, 0x1f, 0xfa, 0x00, 0x17, 0x00, 0x41,
		0x00, 0x64, 0x00, 0x67, 0x00, 0x38, 0x1f, 0xde, 0x1f, 0x7a,
		0x1f, 0x47, 0x1f, 0x7c, 0x00, 0x30, 0x01, 0x4b, 0x02, 0x82,
		0x03, 0x73, 0x03, 0xcf, /* 6 MHz */
		}, {
		0x1f, 0xfa, 0x1f, 0xda, 0x1f, 0xc1, 0x1f, 0xb3, 0x1f, 0xca,
		0x00, 0x07, 0x00, 0x4d, 0x00, 0x6d, 0x00, 0x40, 0x1f, 0xca,
		0x1f, 0x4d, 0x1f, 0x2a, 0x1f, 0xb2, 0x00, 0xec, 0x02, 0x7e,
		0x03, 0xd0, 0x04, 0x53, /* 7 MHz */
		}, {
		0x00, 0x10, 0x00, 0x0e, 0x1f, 0xf7, 0x1f, 0xc9, 0x1f, 0xa0,
		0x1f, 0xa6, 0x1f, 0xec, 0x00, 0x4e, 0x00, 0x7d, 0x00, 0x3a,
		0x1f, 0x98, 0x1f, 0x10, 0x1f, 0x40, 0x00, 0x75, 0x02, 0x5f,
		0x04, 0x24, 0x04, 0xdb, /* 8 MHz */
		},
	};
	static const u8 bw_params2[3][6] = {
		{0xc3, 0x0c, 0x44, 0x33, 0x33, 0x30}, /* 6 MHz */
		{0xb8, 0xe3, 0x93, 0x99, 0x99, 0x98}, /* 7 MHz */
		{0xae, 0xba, 0xf3, 0x26, 0x66, 0x64}, /* 8 MHz */
	};

	dev_dbg(&dev->i2c->dev,
			"%s: frequency=%d bandwidth_hz=%d inversion=%d\n",
			__func__, c->frequency, c->bandwidth_hz, c->inversion);

	/* program tuner */
	if (fe->ops.tuner_ops.set_params)
		fe->ops.tuner_ops.set_params(fe);

	switch (c->bandwidth_hz) {
	case 6000000:
		i = 0;
		break;
	case 7000000:
		i = 1;
		break;
	case 8000000:
		i = 2;
		break;
	default:
		dev_dbg(&dev->i2c->dev, "%s: invalid bandwidth\n", __func__);
		return -EINVAL;
	}

	ret = rtl2830_wr_reg_mask(client, 0x008, i << 1, 0x06);
	if (ret)
		goto err;

	/* program if frequency */
	if (fe->ops.tuner_ops.get_if_frequency)
		ret = fe->ops.tuner_ops.get_if_frequency(fe, &if_frequency);
	else
		ret = -EINVAL;

	if (ret < 0)
		goto err;

	num = if_frequency % dev->cfg.xtal;
	num *= 0x400000;
	num = div_u64(num, dev->cfg.xtal);
	num = -num;
	if_ctl = num & 0x3fffff;
	dev_dbg(&dev->i2c->dev, "%s: if_frequency=%d if_ctl=%08x\n",
			__func__, if_frequency, if_ctl);

	ret = rtl2830_rd_reg_mask(client, 0x119, &tmp, 0xc0); /* b[7:6] */
	if (ret)
		goto err;

	buf[0] = tmp << 6;
	buf[0] |= (if_ctl >> 16) & 0x3f;
	buf[1] = (if_ctl >>  8) & 0xff;
	buf[2] = (if_ctl >>  0) & 0xff;

	ret = rtl2830_wr_regs(client, 0x119, buf, 3);
	if (ret)
		goto err;

	/* 1/2 split I2C write */
	ret = rtl2830_wr_regs(client, 0x11c, &bw_params1[i][0], 17);
	if (ret)
		goto err;

	/* 2/2 split I2C write */
	ret = rtl2830_wr_regs(client, 0x12d, &bw_params1[i][17], 17);
	if (ret)
		goto err;

	ret = rtl2830_wr_regs(client, 0x19d, bw_params2[i], 6);
	if (ret)
		goto err;

	return ret;
err:
	dev_dbg(&dev->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int rtl2830_get_frontend(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct rtl2830_dev *dev = i2c_get_clientdata(client);
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;
	u8 buf[3];

	if (dev->sleeping)
		return 0;

	ret = rtl2830_rd_regs(client, 0x33c, buf, 2);
	if (ret)
		goto err;

	ret = rtl2830_rd_reg(client, 0x351, &buf[2]);
	if (ret)
		goto err;

	dev_dbg(&dev->i2c->dev, "%s: TPS=%*ph\n", __func__, 3, buf);

	switch ((buf[0] >> 2) & 3) {
	case 0:
		c->modulation = QPSK;
		break;
	case 1:
		c->modulation = QAM_16;
		break;
	case 2:
		c->modulation = QAM_64;
		break;
	}

	switch ((buf[2] >> 2) & 1) {
	case 0:
		c->transmission_mode = TRANSMISSION_MODE_2K;
		break;
	case 1:
		c->transmission_mode = TRANSMISSION_MODE_8K;
	}

	switch ((buf[2] >> 0) & 3) {
	case 0:
		c->guard_interval = GUARD_INTERVAL_1_32;
		break;
	case 1:
		c->guard_interval = GUARD_INTERVAL_1_16;
		break;
	case 2:
		c->guard_interval = GUARD_INTERVAL_1_8;
		break;
	case 3:
		c->guard_interval = GUARD_INTERVAL_1_4;
		break;
	}

	switch ((buf[0] >> 4) & 7) {
	case 0:
		c->hierarchy = HIERARCHY_NONE;
		break;
	case 1:
		c->hierarchy = HIERARCHY_1;
		break;
	case 2:
		c->hierarchy = HIERARCHY_2;
		break;
	case 3:
		c->hierarchy = HIERARCHY_4;
		break;
	}

	switch ((buf[1] >> 3) & 7) {
	case 0:
		c->code_rate_HP = FEC_1_2;
		break;
	case 1:
		c->code_rate_HP = FEC_2_3;
		break;
	case 2:
		c->code_rate_HP = FEC_3_4;
		break;
	case 3:
		c->code_rate_HP = FEC_5_6;
		break;
	case 4:
		c->code_rate_HP = FEC_7_8;
		break;
	}

	switch ((buf[1] >> 0) & 7) {
	case 0:
		c->code_rate_LP = FEC_1_2;
		break;
	case 1:
		c->code_rate_LP = FEC_2_3;
		break;
	case 2:
		c->code_rate_LP = FEC_3_4;
		break;
	case 3:
		c->code_rate_LP = FEC_5_6;
		break;
	case 4:
		c->code_rate_LP = FEC_7_8;
		break;
	}

	return 0;
err:
	dev_dbg(&dev->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int rtl2830_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct rtl2830_dev *dev = fe->demodulator_priv;
	int ret;
	u8 tmp;
	*status = 0;

	if (dev->sleeping)
		return 0;

	ret = rtl2830_rd_reg_mask(client, 0x351, &tmp, 0x78); /* [6:3] */
	if (ret)
		goto err;

	if (tmp == 11) {
		*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER |
			FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
	} else if (tmp == 10) {
		*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER |
			FE_HAS_VITERBI;
	}

	return ret;
err:
	dev_dbg(&dev->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int rtl2830_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct rtl2830_dev *dev = i2c_get_clientdata(client);
	int ret, hierarchy, constellation;
	u8 buf[2], tmp;
	u16 tmp16;
#define CONSTELLATION_NUM 3
#define HIERARCHY_NUM 4
	static const u32 snr_constant[CONSTELLATION_NUM][HIERARCHY_NUM] = {
		{ 70705899, 70705899, 70705899, 70705899 },
		{ 82433173, 82433173, 87483115, 94445660 },
		{ 92888734, 92888734, 95487525, 99770748 },
	};

	if (dev->sleeping)
		return 0;

	/* reports SNR in resolution of 0.1 dB */

	ret = rtl2830_rd_reg(client, 0x33c, &tmp);
	if (ret)
		goto err;

	constellation = (tmp >> 2) & 0x03; /* [3:2] */
	if (constellation > CONSTELLATION_NUM - 1)
		goto err;

	hierarchy = (tmp >> 4) & 0x07; /* [6:4] */
	if (hierarchy > HIERARCHY_NUM - 1)
		goto err;

	ret = rtl2830_rd_regs(client, 0x40c, buf, 2);
	if (ret)
		goto err;

	tmp16 = buf[0] << 8 | buf[1];

	if (tmp16)
		*snr = (snr_constant[constellation][hierarchy] -
				intlog10(tmp16)) / ((1 << 24) / 100);
	else
		*snr = 0;

	return 0;
err:
	dev_dbg(&dev->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int rtl2830_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct rtl2830_dev *dev = i2c_get_clientdata(client);
	int ret;
	u8 buf[2];

	if (dev->sleeping)
		return 0;

	ret = rtl2830_rd_regs(client, 0x34e, buf, 2);
	if (ret)
		goto err;

	*ber = buf[0] << 8 | buf[1];

	return 0;
err:
	dev_dbg(&dev->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int rtl2830_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	*ucblocks = 0;
	return 0;
}

static int rtl2830_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct rtl2830_dev *dev = i2c_get_clientdata(client);
	int ret;
	u8 buf[2];
	u16 if_agc_raw, if_agc;

	if (dev->sleeping)
		return 0;

	ret = rtl2830_rd_regs(client, 0x359, buf, 2);
	if (ret)
		goto err;

	if_agc_raw = (buf[0] << 8 | buf[1]) & 0x3fff;

	if (if_agc_raw & (1 << 9))
		if_agc = -(~(if_agc_raw - 1) & 0x1ff);
	else
		if_agc = if_agc_raw;

	*strength = (u8) (55 - if_agc / 182);
	*strength |= *strength << 8;

	return 0;
err:
	dev_dbg(&dev->i2c->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static struct dvb_frontend_ops rtl2830_ops = {
	.delsys = { SYS_DVBT },
	.info = {
		.name = "Realtek RTL2830 (DVB-T)",
		.caps = FE_CAN_FEC_1_2 |
			FE_CAN_FEC_2_3 |
			FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 |
			FE_CAN_FEC_7_8 |
			FE_CAN_FEC_AUTO |
			FE_CAN_QPSK |
			FE_CAN_QAM_16 |
			FE_CAN_QAM_64 |
			FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_HIERARCHY_AUTO |
			FE_CAN_RECOVER |
			FE_CAN_MUTE_TS
	},

	.init = rtl2830_init,
	.sleep = rtl2830_sleep,

	.get_tune_settings = rtl2830_get_tune_settings,

	.set_frontend = rtl2830_set_frontend,
	.get_frontend = rtl2830_get_frontend,

	.read_status = rtl2830_read_status,
	.read_snr = rtl2830_read_snr,
	.read_ber = rtl2830_read_ber,
	.read_ucblocks = rtl2830_read_ucblocks,
	.read_signal_strength = rtl2830_read_signal_strength,
};

/*
 * I2C gate/repeater logic
 * We must use unlocked i2c_transfer() here because I2C lock is already taken
 * by tuner driver. Gate is closed automatically after single I2C xfer.
 */
static int rtl2830_select(struct i2c_adapter *adap, void *mux_priv, u32 chan_id)
{
	struct i2c_client *client = mux_priv;
	struct rtl2830_dev *dev = i2c_get_clientdata(client);
	struct i2c_msg select_reg_page_msg[1] = {
		{
			.addr = dev->cfg.i2c_addr,
			.flags = 0,
			.len = 2,
			.buf = "\x00\x01",
		}
	};
	struct i2c_msg gate_open_msg[1] = {
		{
			.addr = dev->cfg.i2c_addr,
			.flags = 0,
			.len = 2,
			.buf = "\x01\x08",
		}
	};
	int ret;

	/* select register page */
	ret = __i2c_transfer(adap, select_reg_page_msg, 1);
	if (ret != 1) {
		dev_warn(&client->dev, "i2c write failed %d\n", ret);
		if (ret >= 0)
			ret = -EREMOTEIO;
		goto err;
	}

	dev->page = 1;

	/* open tuner I2C repeater for 1 xfer, closes automatically */
	ret = __i2c_transfer(adap, gate_open_msg, 1);
	if (ret != 1) {
		dev_warn(&client->dev, "i2c write failed %d\n", ret);
		if (ret >= 0)
			ret = -EREMOTEIO;
		goto err;
	}

	return 0;

err:
	dev_dbg(&client->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static struct dvb_frontend *rtl2830_get_dvb_frontend(struct i2c_client *client)
{
	struct rtl2830_dev *dev = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "\n");

	return &dev->fe;
}

static struct i2c_adapter *rtl2830_get_i2c_adapter(struct i2c_client *client)
{
	struct rtl2830_dev *dev = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "\n");

	return dev->adapter;
}

static int rtl2830_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct rtl2830_platform_data *pdata = client->dev.platform_data;
	struct i2c_adapter *i2c = client->adapter;
	struct rtl2830_dev *dev;
	int ret;
	u8 u8tmp;

	dev_dbg(&client->dev, "\n");

	if (pdata == NULL) {
		ret = -EINVAL;
		goto err;
	}

	/* allocate memory for the internal state */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	/* setup the state */
	i2c_set_clientdata(client, dev);
	dev->i2c = i2c;
	dev->sleeping = true;
	dev->cfg.i2c_addr = client->addr;
	dev->cfg.xtal = pdata->clk;
	dev->cfg.spec_inv = pdata->spec_inv;
	dev->cfg.vtop = pdata->vtop;
	dev->cfg.krf = pdata->krf;
	dev->cfg.agc_targ_val = pdata->agc_targ_val;

	/* check if the demod is there */
	ret = rtl2830_rd_reg(client, 0x000, &u8tmp);
	if (ret)
		goto err_kfree;

	/* create muxed i2c adapter for tuner */
	dev->adapter = i2c_add_mux_adapter(client->adapter, &client->dev,
			client, 0, 0, 0, rtl2830_select, NULL);
	if (dev->adapter == NULL) {
		ret = -ENODEV;
		goto err_kfree;
	}

	/* create dvb frontend */
	memcpy(&dev->fe.ops, &rtl2830_ops, sizeof(dev->fe.ops));
	dev->fe.demodulator_priv = client;

	/* setup callbacks */
	pdata->get_dvb_frontend = rtl2830_get_dvb_frontend;
	pdata->get_i2c_adapter = rtl2830_get_i2c_adapter;

	dev_info(&client->dev, "Realtek RTL2830 successfully attached\n");
	return 0;

err_kfree:
	kfree(dev);
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int rtl2830_remove(struct i2c_client *client)
{
	struct rtl2830_dev *dev = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "\n");

	i2c_del_mux_adapter(dev->adapter);
	kfree(dev);
	return 0;
}

static const struct i2c_device_id rtl2830_id_table[] = {
	{"rtl2830", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, rtl2830_id_table);

static struct i2c_driver rtl2830_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "rtl2830",
	},
	.probe		= rtl2830_probe,
	.remove		= rtl2830_remove,
	.id_table	= rtl2830_id_table,
};

module_i2c_driver(rtl2830_driver);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Realtek RTL2830 DVB-T demodulator driver");
MODULE_LICENSE("GPL");
