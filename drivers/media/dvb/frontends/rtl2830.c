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

int rtl2830_debug;
module_param_named(debug, rtl2830_debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

/* write multiple hardware registers */
static int rtl2830_wr(struct rtl2830_priv *priv, u8 reg, u8 *val, int len)
{
	int ret;
	u8 buf[1+len];
	struct i2c_msg msg[1] = {
		{
			.addr = priv->cfg.i2c_addr,
			.flags = 0,
			.len = 1+len,
			.buf = buf,
		}
	};

	buf[0] = reg;
	memcpy(&buf[1], val, len);

	ret = i2c_transfer(priv->i2c, msg, 1);
	if (ret == 1) {
		ret = 0;
	} else {
		warn("i2c wr failed=%d reg=%02x len=%d", ret, reg, len);
		ret = -EREMOTEIO;
	}
	return ret;
}

/* read multiple hardware registers */
static int rtl2830_rd(struct rtl2830_priv *priv, u8 reg, u8 *val, int len)
{
	int ret;
	struct i2c_msg msg[2] = {
		{
			.addr = priv->cfg.i2c_addr,
			.flags = 0,
			.len = 1,
			.buf = &reg,
		}, {
			.addr = priv->cfg.i2c_addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = val,
		}
	};

	ret = i2c_transfer(priv->i2c, msg, 2);
	if (ret == 2) {
		ret = 0;
	} else {
		warn("i2c rd failed=%d reg=%02x len=%d", ret, reg, len);
		ret = -EREMOTEIO;
	}
	return ret;
}

/* write multiple registers */
static int rtl2830_wr_regs(struct rtl2830_priv *priv, u16 reg, u8 *val, int len)
{
	int ret;
	u8 reg2 = (reg >> 0) & 0xff;
	u8 page = (reg >> 8) & 0xff;

	/* switch bank if needed */
	if (page != priv->page) {
		ret = rtl2830_wr(priv, 0x00, &page, 1);
		if (ret)
			return ret;

		priv->page = page;
	}

	return rtl2830_wr(priv, reg2, val, len);
}

/* read multiple registers */
static int rtl2830_rd_regs(struct rtl2830_priv *priv, u16 reg, u8 *val, int len)
{
	int ret;
	u8 reg2 = (reg >> 0) & 0xff;
	u8 page = (reg >> 8) & 0xff;

	/* switch bank if needed */
	if (page != priv->page) {
		ret = rtl2830_wr(priv, 0x00, &page, 1);
		if (ret)
			return ret;

		priv->page = page;
	}

	return rtl2830_rd(priv, reg2, val, len);
}

#if 0 /* currently not used */
/* write single register */
static int rtl2830_wr_reg(struct rtl2830_priv *priv, u16 reg, u8 val)
{
	return rtl2830_wr_regs(priv, reg, &val, 1);
}
#endif

/* read single register */
static int rtl2830_rd_reg(struct rtl2830_priv *priv, u16 reg, u8 *val)
{
	return rtl2830_rd_regs(priv, reg, val, 1);
}

/* write single register with mask */
int rtl2830_wr_reg_mask(struct rtl2830_priv *priv, u16 reg, u8 val, u8 mask)
{
	int ret;
	u8 tmp;

	/* no need for read if whole reg is written */
	if (mask != 0xff) {
		ret = rtl2830_rd_regs(priv, reg, &tmp, 1);
		if (ret)
			return ret;

		val &= mask;
		tmp &= ~mask;
		val |= tmp;
	}

	return rtl2830_wr_regs(priv, reg, &val, 1);
}

/* read single register with mask */
int rtl2830_rd_reg_mask(struct rtl2830_priv *priv, u16 reg, u8 *val, u8 mask)
{
	int ret, i;
	u8 tmp;

	ret = rtl2830_rd_regs(priv, reg, &tmp, 1);
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
	struct rtl2830_priv *priv = fe->demodulator_priv;
	int ret, i;
	u64 num;
	u8 buf[3], tmp;
	u32 if_ctl;
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
		{ 0x106, priv->cfg.vtop, 0x3f },
		{ 0x107, priv->cfg.krf, 0x3f },
		{ 0x112, 0x28, 0xff },
		{ 0x103, priv->cfg.agc_targ_val, 0xff },
		{ 0x00a, 0x02, 0x07 },
		{ 0x140, 0x0c, 0x3c },
		{ 0x140, 0x40, 0xc0 },
		{ 0x15b, 0x05, 0x07 },
		{ 0x15b, 0x28, 0x38 },
		{ 0x15c, 0x05, 0x07 },
		{ 0x15c, 0x28, 0x38 },
		{ 0x115, priv->cfg.spec_inv, 0x01 },
		{ 0x16f, 0x01, 0x07 },
		{ 0x170, 0x18, 0x38 },
		{ 0x172, 0x0f, 0x0f },
		{ 0x173, 0x08, 0x38 },
		{ 0x175, 0x01, 0x07 },
		{ 0x176, 0x00, 0xc0 },
	};

	for (i = 0; i < ARRAY_SIZE(tab); i++) {
		ret = rtl2830_wr_reg_mask(priv, tab[i].reg, tab[i].val,
			tab[i].mask);
		if (ret)
			goto err;
	}

	ret = rtl2830_wr_regs(priv, 0x18f, "\x28\x00", 2);
	if (ret)
		goto err;

	ret = rtl2830_wr_regs(priv, 0x195,
		"\x04\x06\x0a\x12\x0a\x12\x1e\x28", 8);
	if (ret)
		goto err;

	num = priv->cfg.if_dvbt % priv->cfg.xtal;
	num *= 0x400000;
	num = div_u64(num, priv->cfg.xtal);
	num = -num;
	if_ctl = num & 0x3fffff;
	dbg("%s: if_ctl=%08x", __func__, if_ctl);

	ret = rtl2830_rd_reg_mask(priv, 0x119, &tmp, 0xc0); /* b[7:6] */
	if (ret)
		goto err;

	buf[0] = tmp << 6;
	buf[0] = (if_ctl >> 16) & 0x3f;
	buf[1] = (if_ctl >>  8) & 0xff;
	buf[2] = (if_ctl >>  0) & 0xff;

	ret = rtl2830_wr_regs(priv, 0x119, buf, 3);
	if (ret)
		goto err;

	/* TODO: spec init */

	/* soft reset */
	ret = rtl2830_wr_reg_mask(priv, 0x101, 0x04, 0x04);
	if (ret)
		goto err;

	ret = rtl2830_wr_reg_mask(priv, 0x101, 0x00, 0x04);
	if (ret)
		goto err;

	priv->sleeping = false;

	return ret;
err:
	dbg("%s: failed=%d", __func__, ret);
	return ret;
}

static int rtl2830_sleep(struct dvb_frontend *fe)
{
	struct rtl2830_priv *priv = fe->demodulator_priv;
	priv->sleeping = true;
	return 0;
}

int rtl2830_get_tune_settings(struct dvb_frontend *fe,
	struct dvb_frontend_tune_settings *s)
{
	s->min_delay_ms = 500;
	s->step_size = fe->ops.info.frequency_stepsize * 2;
	s->max_drift = (fe->ops.info.frequency_stepsize * 2) + 1;

	return 0;
}

static int rtl2830_set_frontend(struct dvb_frontend *fe)
{
	struct rtl2830_priv *priv = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i;
	static u8 bw_params1[3][34] = {
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
	static u8 bw_params2[3][6] = {
		{0xc3, 0x0c, 0x44, 0x33, 0x33, 0x30,}, /* 6 MHz */
		{0xb8, 0xe3, 0x93, 0x99, 0x99, 0x98,}, /* 7 MHz */
		{0xae, 0xba, 0xf3, 0x26, 0x66, 0x64,}, /* 8 MHz */
	};


	dbg("%s: frequency=%d bandwidth_hz=%d inversion=%d", __func__,
		c->frequency, c->bandwidth_hz, c->inversion);

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
		dbg("invalid bandwidth");
		return -EINVAL;
	}

	ret = rtl2830_wr_reg_mask(priv, 0x008, i << 1, 0x06);
	if (ret)
		goto err;

	/* 1/2 split I2C write */
	ret = rtl2830_wr_regs(priv, 0x11c, &bw_params1[i][0], 17);
	if (ret)
		goto err;

	/* 2/2 split I2C write */
	ret = rtl2830_wr_regs(priv, 0x12d, &bw_params1[i][17], 17);
	if (ret)
		goto err;

	ret = rtl2830_wr_regs(priv, 0x19d, bw_params2[i], 6);
	if (ret)
		goto err;

	return ret;
err:
	dbg("%s: failed=%d", __func__, ret);
	return ret;
}

static int rtl2830_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct rtl2830_priv *priv = fe->demodulator_priv;
	int ret;
	u8 tmp;
	*status = 0;

	if (priv->sleeping)
		return 0;

	ret = rtl2830_rd_reg_mask(priv, 0x351, &tmp, 0x78); /* [6:3] */
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
	dbg("%s: failed=%d", __func__, ret);
	return ret;
}

static int rtl2830_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct rtl2830_priv *priv = fe->demodulator_priv;
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

	/* reports SNR in resolution of 0.1 dB */

	ret = rtl2830_rd_reg(priv, 0x33c, &tmp);
	if (ret)
		goto err;

	constellation = (tmp >> 2) & 0x03; /* [3:2] */
	if (constellation > CONSTELLATION_NUM - 1)
		goto err;

	hierarchy = (tmp >> 4) & 0x07; /* [6:4] */
	if (hierarchy > HIERARCHY_NUM - 1)
		goto err;

	ret = rtl2830_rd_regs(priv, 0x40c, buf, 2);
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
	dbg("%s: failed=%d", __func__, ret);
	return ret;
}

static int rtl2830_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct rtl2830_priv *priv = fe->demodulator_priv;
	int ret;
	u8 buf[2];

	ret = rtl2830_rd_regs(priv, 0x34e, buf, 2);
	if (ret)
		goto err;

	*ber = buf[0] << 8 | buf[1];

	return 0;
err:
	dbg("%s: failed=%d", __func__, ret);
	return ret;
}

static int rtl2830_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	*ucblocks = 0;
	return 0;
}

static int rtl2830_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	*strength = 0;
	return 0;
}

static struct dvb_frontend_ops rtl2830_ops;

static u32 rtl2830_tuner_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static int rtl2830_tuner_i2c_xfer(struct i2c_adapter *i2c_adap,
	struct i2c_msg msg[], int num)
{
	struct rtl2830_priv *priv = i2c_get_adapdata(i2c_adap);
	int ret;

	/* open i2c-gate */
	ret = rtl2830_wr_reg_mask(priv, 0x101, 0x08, 0x08);
	if (ret)
		goto err;

	ret = i2c_transfer(priv->i2c, msg, num);
	if (ret < 0)
		warn("tuner i2c failed=%d", ret);

	return ret;
err:
	dbg("%s: failed=%d", __func__, ret);
	return ret;
}

static struct i2c_algorithm rtl2830_tuner_i2c_algo = {
	.master_xfer   = rtl2830_tuner_i2c_xfer,
	.functionality = rtl2830_tuner_i2c_func,
};

struct i2c_adapter *rtl2830_get_tuner_i2c_adapter(struct dvb_frontend *fe)
{
	struct rtl2830_priv *priv = fe->demodulator_priv;
	return &priv->tuner_i2c_adapter;
}
EXPORT_SYMBOL(rtl2830_get_tuner_i2c_adapter);

static void rtl2830_release(struct dvb_frontend *fe)
{
	struct rtl2830_priv *priv = fe->demodulator_priv;

	i2c_del_adapter(&priv->tuner_i2c_adapter);
	kfree(priv);
}

struct dvb_frontend *rtl2830_attach(const struct rtl2830_config *cfg,
	struct i2c_adapter *i2c)
{
	struct rtl2830_priv *priv = NULL;
	int ret = 0;
	u8 tmp;

	/* allocate memory for the internal state */
	priv = kzalloc(sizeof(struct rtl2830_priv), GFP_KERNEL);
	if (priv == NULL)
		goto err;

	/* setup the priv */
	priv->i2c = i2c;
	memcpy(&priv->cfg, cfg, sizeof(struct rtl2830_config));

	/* check if the demod is there */
	ret = rtl2830_rd_reg(priv, 0x000, &tmp);
	if (ret)
		goto err;

	/* create dvb_frontend */
	memcpy(&priv->fe.ops, &rtl2830_ops, sizeof(struct dvb_frontend_ops));
	priv->fe.demodulator_priv = priv;

	/* create tuner i2c adapter */
	strlcpy(priv->tuner_i2c_adapter.name, "RTL2830 tuner I2C adapter",
		sizeof(priv->tuner_i2c_adapter.name));
	priv->tuner_i2c_adapter.algo = &rtl2830_tuner_i2c_algo;
	priv->tuner_i2c_adapter.algo_data = NULL;
	i2c_set_adapdata(&priv->tuner_i2c_adapter, priv);
	if (i2c_add_adapter(&priv->tuner_i2c_adapter) < 0) {
		err("tuner I2C bus could not be initialized");
		goto err;
	}

	priv->sleeping = true;

	return &priv->fe;
err:
	dbg("%s: failed=%d", __func__, ret);
	kfree(priv);
	return NULL;
}
EXPORT_SYMBOL(rtl2830_attach);

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

	.release = rtl2830_release,

	.init = rtl2830_init,
	.sleep = rtl2830_sleep,

	.get_tune_settings = rtl2830_get_tune_settings,

	.set_frontend = rtl2830_set_frontend,

	.read_status = rtl2830_read_status,
	.read_snr = rtl2830_read_snr,
	.read_ber = rtl2830_read_ber,
	.read_ucblocks = rtl2830_read_ucblocks,
	.read_signal_strength = rtl2830_read_signal_strength,
};

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Realtek RTL2830 DVB-T demodulator driver");
MODULE_LICENSE("GPL");
