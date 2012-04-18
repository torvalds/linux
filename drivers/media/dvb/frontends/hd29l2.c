/*
 * HDIC HD29L2 DMB-TH demodulator driver
 *
 * Copyright (C) 2011 Metropolia University of Applied Sciences, Electria R&D
 *
 * Author: Antti Palosaari <crope@iki.fi>
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

#include "hd29l2_priv.h"

int hd29l2_debug;
module_param_named(debug, hd29l2_debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

/* write multiple registers */
static int hd29l2_wr_regs(struct hd29l2_priv *priv, u8 reg, u8 *val, int len)
{
	int ret;
	u8 buf[2 + len];
	struct i2c_msg msg[1] = {
		{
			.addr = priv->cfg.i2c_addr,
			.flags = 0,
			.len = sizeof(buf),
			.buf = buf,
		}
	};

	buf[0] = 0x00;
	buf[1] = reg;
	memcpy(&buf[2], val, len);

	ret = i2c_transfer(priv->i2c, msg, 1);
	if (ret == 1) {
		ret = 0;
	} else {
		warn("i2c wr failed=%d reg=%02x len=%d", ret, reg, len);
		ret = -EREMOTEIO;
	}

	return ret;
}

/* read multiple registers */
static int hd29l2_rd_regs(struct hd29l2_priv *priv, u8 reg, u8 *val, int len)
{
	int ret;
	u8 buf[2] = { 0x00, reg };
	struct i2c_msg msg[2] = {
		{
			.addr = priv->cfg.i2c_addr,
			.flags = 0,
			.len = 2,
			.buf = buf,
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

/* write single register */
static int hd29l2_wr_reg(struct hd29l2_priv *priv, u8 reg, u8 val)
{
	return hd29l2_wr_regs(priv, reg, &val, 1);
}

/* read single register */
static int hd29l2_rd_reg(struct hd29l2_priv *priv, u8 reg, u8 *val)
{
	return hd29l2_rd_regs(priv, reg, val, 1);
}

/* write single register with mask */
static int hd29l2_wr_reg_mask(struct hd29l2_priv *priv, u8 reg, u8 val, u8 mask)
{
	int ret;
	u8 tmp;

	/* no need for read if whole reg is written */
	if (mask != 0xff) {
		ret = hd29l2_rd_regs(priv, reg, &tmp, 1);
		if (ret)
			return ret;

		val &= mask;
		tmp &= ~mask;
		val |= tmp;
	}

	return hd29l2_wr_regs(priv, reg, &val, 1);
}

/* read single register with mask */
int hd29l2_rd_reg_mask(struct hd29l2_priv *priv, u8 reg, u8 *val, u8 mask)
{
	int ret, i;
	u8 tmp;

	ret = hd29l2_rd_regs(priv, reg, &tmp, 1);
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

static int hd29l2_soft_reset(struct hd29l2_priv *priv)
{
	int ret;
	u8 tmp;

	ret = hd29l2_rd_reg(priv, 0x26, &tmp);
	if (ret)
		goto err;

	ret = hd29l2_wr_reg(priv, 0x26, 0x0d);
	if (ret)
		goto err;

	usleep_range(10000, 20000);

	ret = hd29l2_wr_reg(priv, 0x26, tmp);
	if (ret)
		goto err;

	return 0;
err:
	dbg("%s: failed=%d", __func__, ret);
	return ret;
}

static int hd29l2_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	int ret, i;
	struct hd29l2_priv *priv = fe->demodulator_priv;
	u8 tmp;

	dbg("%s: enable=%d", __func__, enable);

	/* set tuner address for demod */
	if (!priv->tuner_i2c_addr_programmed && enable) {
		/* no need to set tuner address every time, once is enough */
		ret = hd29l2_wr_reg(priv, 0x9d, priv->cfg.tuner_i2c_addr << 1);
		if (ret)
			goto err;

		priv->tuner_i2c_addr_programmed = true;
	}

	/* open / close gate */
	ret = hd29l2_wr_reg(priv, 0x9f, enable);
	if (ret)
		goto err;

	/* wait demod ready */
	for (i = 10; i; i--) {
		ret = hd29l2_rd_reg(priv, 0x9e, &tmp);
		if (ret)
			goto err;

		if (tmp == enable)
			break;

		usleep_range(5000, 10000);
	}

	dbg("%s: loop=%d", __func__, i);

	return ret;
err:
	dbg("%s: failed=%d", __func__, ret);
	return ret;
}

static int hd29l2_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	int ret;
	struct hd29l2_priv *priv = fe->demodulator_priv;
	u8 buf[2];

	*status = 0;

	ret = hd29l2_rd_reg(priv, 0x05, &buf[0]);
	if (ret)
		goto err;

	if (buf[0] & 0x01) {
		/* full lock */
		*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI |
			FE_HAS_SYNC | FE_HAS_LOCK;
	} else {
		ret = hd29l2_rd_reg(priv, 0x0d, &buf[1]);
		if (ret)
			goto err;

		if ((buf[1] & 0xfe) == 0x78)
			/* partial lock */
			*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER |
				FE_HAS_VITERBI | FE_HAS_SYNC;
	}

	priv->fe_status = *status;

	return 0;
err:
	dbg("%s: failed=%d", __func__, ret);
	return ret;
}

static int hd29l2_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	int ret;
	struct hd29l2_priv *priv = fe->demodulator_priv;
	u8 buf[2];
	u16 tmp;

	if (!(priv->fe_status & FE_HAS_LOCK)) {
		*snr = 0;
		ret = 0;
		goto err;
	}

	ret = hd29l2_rd_regs(priv, 0x0b, buf, 2);
	if (ret)
		goto err;

	tmp = (buf[0] << 8) | buf[1];

	/* report SNR in dB * 10 */
	#define LOG10_20736_24 72422627 /* log10(20736) << 24 */
	if (tmp)
		*snr = (LOG10_20736_24 - intlog10(tmp)) / ((1 << 24) / 100);
	else
		*snr = 0;

	return 0;
err:
	dbg("%s: failed=%d", __func__, ret);
	return ret;
}

static int hd29l2_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	int ret;
	struct hd29l2_priv *priv = fe->demodulator_priv;
	u8 buf[2];
	u16 tmp;

	*strength = 0;

	ret = hd29l2_rd_regs(priv, 0xd5, buf, 2);
	if (ret)
		goto err;

	tmp = buf[0] << 8 | buf[1];
	tmp = ~tmp & 0x0fff;

	/* scale value to 0x0000-0xffff from 0x0000-0x0fff */
	*strength = tmp * 0xffff / 0x0fff;

	return 0;
err:
	dbg("%s: failed=%d", __func__, ret);
	return ret;
}

static int hd29l2_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	int ret;
	struct hd29l2_priv *priv = fe->demodulator_priv;
	u8 buf[2];

	if (!(priv->fe_status & FE_HAS_SYNC)) {
		*ber = 0;
		ret = 0;
		goto err;
	}

	ret = hd29l2_rd_regs(priv, 0xd9, buf, 2);
	if (ret) {
		*ber = 0;
		goto err;
	}

	/* LDPC BER */
	*ber = ((buf[0] & 0x0f) << 8) | buf[1];

	return 0;
err:
	dbg("%s: failed=%d", __func__, ret);
	return ret;
}

static int hd29l2_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	/* no way to read? */
	*ucblocks = 0;
	return 0;
}

static enum dvbfe_search hd29l2_search(struct dvb_frontend *fe)
{
	int ret, i;
	struct hd29l2_priv *priv = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u8 tmp, buf[3];
	u8 modulation, carrier, guard_interval, interleave, code_rate;
	u64 num64;
	u32 if_freq, if_ctl;
	bool auto_mode;

	dbg("%s: delivery_system=%d frequency=%d bandwidth_hz=%d " \
		"modulation=%d inversion=%d fec_inner=%d guard_interval=%d",
		 __func__,
		c->delivery_system, c->frequency, c->bandwidth_hz,
		c->modulation, c->inversion, c->fec_inner, c->guard_interval);

	/* as for now we detect always params automatically */
	auto_mode = true;

	/* program tuner */
	if (fe->ops.tuner_ops.set_params)
		fe->ops.tuner_ops.set_params(fe);

	/* get and program IF */
	if (fe->ops.tuner_ops.get_if_frequency)
		fe->ops.tuner_ops.get_if_frequency(fe, &if_freq);
	else
		if_freq = 0;

	if (if_freq) {
		/* normal IF */

		/* calc IF control value */
		num64 = if_freq;
		num64 *= 0x800000;
		num64 = div_u64(num64, HD29L2_XTAL);
		num64 -= 0x800000;
		if_ctl = num64;

		tmp = 0xfc; /* tuner type normal */
	} else {
		/* zero IF */
		if_ctl = 0;
		tmp = 0xfe; /* tuner type Zero-IF */
	}

	buf[0] = ((if_ctl >>  0) & 0xff);
	buf[1] = ((if_ctl >>  8) & 0xff);
	buf[2] = ((if_ctl >> 16) & 0xff);

	/* program IF control */
	ret = hd29l2_wr_regs(priv, 0x14, buf, 3);
	if (ret)
		goto err;

	/* program tuner type */
	ret = hd29l2_wr_reg(priv, 0xab, tmp);
	if (ret)
		goto err;

	dbg("%s: if_freq=%d if_ctl=%x", __func__, if_freq, if_ctl);

	if (auto_mode) {
		/*
		 * use auto mode
		 */

		/* disable quick mode */
		ret = hd29l2_wr_reg_mask(priv, 0xac, 0 << 7, 0x80);
		if (ret)
			goto err;

		ret = hd29l2_wr_reg_mask(priv, 0x82, 1 << 1, 0x02);
		if (ret)
			goto err;

		/* enable auto mode */
		ret = hd29l2_wr_reg_mask(priv, 0x7d, 1 << 6, 0x40);
		if (ret)
			goto err;

		ret = hd29l2_wr_reg_mask(priv, 0x81, 1 << 3, 0x08);
		if (ret)
			goto err;

		/* soft reset */
		ret = hd29l2_soft_reset(priv);
		if (ret)
			goto err;

		/* detect modulation */
		for (i = 30; i; i--) {
			msleep(100);

			ret = hd29l2_rd_reg(priv, 0x0d, &tmp);
			if (ret)
				goto err;

			if ((((tmp & 0xf0) >= 0x10) &&
				((tmp & 0x0f) == 0x08)) || (tmp >= 0x2c))
				break;
		}

		dbg("%s: loop=%d", __func__, i);

		if (i == 0)
			/* detection failed */
			return DVBFE_ALGO_SEARCH_FAILED;

		/* read modulation */
		ret = hd29l2_rd_reg_mask(priv, 0x7d, &modulation, 0x07);
		if (ret)
			goto err;
	} else {
		/*
		 * use manual mode
		 */

		modulation = HD29L2_QAM64;
		carrier = HD29L2_CARRIER_MULTI;
		guard_interval = HD29L2_PN945;
		interleave = HD29L2_INTERLEAVER_420;
		code_rate = HD29L2_CODE_RATE_08;

		tmp = (code_rate << 3) | modulation;
		ret = hd29l2_wr_reg_mask(priv, 0x7d, tmp, 0x5f);
		if (ret)
			goto err;

		tmp = (carrier << 2) | guard_interval;
		ret = hd29l2_wr_reg_mask(priv, 0x81, tmp, 0x0f);
		if (ret)
			goto err;

		tmp = interleave;
		ret = hd29l2_wr_reg_mask(priv, 0x82, tmp, 0x03);
		if (ret)
			goto err;
	}

	/* ensure modulation validy */
	/* 0=QAM4_NR, 1=QAM4, 2=QAM16, 3=QAM32, 4=QAM64 */
	if (modulation > (ARRAY_SIZE(reg_mod_vals_tab[0].val) - 1)) {
		dbg("%s: modulation=%d not valid", __func__, modulation);
		goto err;
	}

	/* program registers according to modulation */
	for (i = 0; i < ARRAY_SIZE(reg_mod_vals_tab); i++) {
		ret = hd29l2_wr_reg(priv, reg_mod_vals_tab[i].reg,
			reg_mod_vals_tab[i].val[modulation]);
		if (ret)
			goto err;
	}

	/* read guard interval */
	ret = hd29l2_rd_reg_mask(priv, 0x81, &guard_interval, 0x03);
	if (ret)
		goto err;

	/* read carrier mode */
	ret = hd29l2_rd_reg_mask(priv, 0x81, &carrier, 0x04);
	if (ret)
		goto err;

	dbg("%s: modulation=%d guard_interval=%d carrier=%d",
		__func__, modulation, guard_interval, carrier);

	if ((carrier == HD29L2_CARRIER_MULTI) && (modulation == HD29L2_QAM64) &&
		(guard_interval == HD29L2_PN945)) {
		dbg("%s: C=3780 && QAM64 && PN945", __func__);

		ret = hd29l2_wr_reg(priv, 0x42, 0x33);
		if (ret)
			goto err;

		ret = hd29l2_wr_reg(priv, 0xdd, 0x01);
		if (ret)
			goto err;
	}

	usleep_range(10000, 20000);

	/* soft reset */
	ret = hd29l2_soft_reset(priv);
	if (ret)
		goto err;

	/* wait demod lock */
	for (i = 30; i; i--) {
		msleep(100);

		/* read lock bit */
		ret = hd29l2_rd_reg_mask(priv, 0x05, &tmp, 0x01);
		if (ret)
			goto err;

		if (tmp)
			break;
	}

	dbg("%s: loop=%d", __func__, i);

	if (i == 0)
		return DVBFE_ALGO_SEARCH_AGAIN;

	return DVBFE_ALGO_SEARCH_SUCCESS;
err:
	dbg("%s: failed=%d", __func__, ret);
	return DVBFE_ALGO_SEARCH_ERROR;
}

static int hd29l2_get_frontend_algo(struct dvb_frontend *fe)
{
	return DVBFE_ALGO_CUSTOM;
}

static int hd29l2_get_frontend(struct dvb_frontend *fe)
{
	int ret;
	struct hd29l2_priv *priv = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u8 buf[3];
	u32 if_ctl;
	char *str_constellation, *str_code_rate, *str_constellation_code_rate,
		*str_guard_interval, *str_carrier, *str_guard_interval_carrier,
		*str_interleave, *str_interleave_;

	ret = hd29l2_rd_reg(priv, 0x7d, &buf[0]);
	if (ret)
		goto err;

	ret = hd29l2_rd_regs(priv, 0x81, &buf[1], 2);
	if (ret)
		goto err;

	/* constellation, 0x7d[2:0] */
	switch ((buf[0] >> 0) & 0x07) {
	case 0: /* QAM4NR */
		str_constellation = "QAM4NR";
		c->modulation = QAM_AUTO; /* FIXME */
		break;
	case 1: /* QAM4 */
		str_constellation = "QAM4";
		c->modulation = QPSK; /* FIXME */
		break;
	case 2:
		str_constellation = "QAM16";
		c->modulation = QAM_16;
		break;
	case 3:
		str_constellation = "QAM32";
		c->modulation = QAM_32;
		break;
	case 4:
		str_constellation = "QAM64";
		c->modulation = QAM_64;
		break;
	default:
		str_constellation = "?";
	}

	/* LDPC code rate, 0x7d[4:3] */
	switch ((buf[0] >> 3) & 0x03) {
	case 0: /* 0.4 */
		str_code_rate = "0.4";
		c->fec_inner = FEC_AUTO; /* FIXME */
		break;
	case 1: /* 0.6 */
		str_code_rate = "0.6";
		c->fec_inner = FEC_3_5;
		break;
	case 2: /* 0.8 */
		str_code_rate = "0.8";
		c->fec_inner = FEC_4_5;
		break;
	default:
		str_code_rate = "?";
	}

	/* constellation & code rate set, 0x7d[6] */
	switch ((buf[0] >> 6) & 0x01) {
	case 0:
		str_constellation_code_rate = "manual";
		break;
	case 1:
		str_constellation_code_rate = "auto";
		break;
	default:
		str_constellation_code_rate = "?";
	}

	/* frame header, 0x81[1:0] */
	switch ((buf[1] >> 0) & 0x03) {
	case 0: /* PN945 */
		str_guard_interval = "PN945";
		c->guard_interval = GUARD_INTERVAL_AUTO; /* FIXME */
		break;
	case 1: /* PN595 */
		str_guard_interval = "PN595";
		c->guard_interval = GUARD_INTERVAL_AUTO; /* FIXME */
		break;
	case 2: /* PN420 */
		str_guard_interval = "PN420";
		c->guard_interval = GUARD_INTERVAL_AUTO; /* FIXME */
		break;
	default:
		str_guard_interval = "?";
	}

	/* carrier, 0x81[2] */
	switch ((buf[1] >> 2) & 0x01) {
	case 0:
		str_carrier = "C=1";
		break;
	case 1:
		str_carrier = "C=3780";
		break;
	default:
		str_carrier = "?";
	}

	/* frame header & carrier set, 0x81[3] */
	switch ((buf[1] >> 3) & 0x01) {
	case 0:
		str_guard_interval_carrier = "manual";
		break;
	case 1:
		str_guard_interval_carrier = "auto";
		break;
	default:
		str_guard_interval_carrier = "?";
	}

	/* interleave, 0x82[0] */
	switch ((buf[2] >> 0) & 0x01) {
	case 0:
		str_interleave = "M=720";
		break;
	case 1:
		str_interleave = "M=240";
		break;
	default:
		str_interleave = "?";
	}

	/* interleave set, 0x82[1] */
	switch ((buf[2] >> 1) & 0x01) {
	case 0:
		str_interleave_ = "manual";
		break;
	case 1:
		str_interleave_ = "auto";
		break;
	default:
		str_interleave_ = "?";
	}

	/*
	 * We can read out current detected NCO and use that value next
	 * time instead of calculating new value from targed IF.
	 * I think it will not effect receiver sensitivity but gaining lock
	 * after tune could be easier...
	 */
	ret = hd29l2_rd_regs(priv, 0xb1, &buf[0], 3);
	if (ret)
		goto err;

	if_ctl = (buf[0] << 16) | ((buf[1] - 7) << 8) | buf[2];

	dbg("%s: %s %s %s | %s %s %s | %s %s | NCO=%06x", __func__,
		str_constellation, str_code_rate, str_constellation_code_rate,
		str_guard_interval, str_carrier, str_guard_interval_carrier,
		str_interleave, str_interleave_, if_ctl);

	return 0;
err:
	dbg("%s: failed=%d", __func__, ret);
	return ret;
}

static int hd29l2_init(struct dvb_frontend *fe)
{
	int ret, i;
	struct hd29l2_priv *priv = fe->demodulator_priv;
	u8 tmp;
	static const struct reg_val tab[] = {
		{ 0x3a, 0x06 },
		{ 0x3b, 0x03 },
		{ 0x3c, 0x04 },
		{ 0xaf, 0x06 },
		{ 0xb0, 0x1b },
		{ 0x80, 0x64 },
		{ 0x10, 0x38 },
	};

	dbg("%s:", __func__);

	/* reset demod */
	/* it is recommended to HW reset chip using RST_N pin */
	if (fe->callback) {
		ret = fe->callback(fe, DVB_FRONTEND_COMPONENT_DEMOD, 0, 0);
		if (ret)
			goto err;

		/* reprogramming needed because HW reset clears registers */
		priv->tuner_i2c_addr_programmed = false;
	}

	/* init */
	for (i = 0; i < ARRAY_SIZE(tab); i++) {
		ret = hd29l2_wr_reg(priv, tab[i].reg, tab[i].val);
		if (ret)
			goto err;
	}

	/* TS params */
	ret = hd29l2_rd_reg(priv, 0x36, &tmp);
	if (ret)
		goto err;

	tmp &= 0x1b;
	tmp |= priv->cfg.ts_mode;
	ret = hd29l2_wr_reg(priv, 0x36, tmp);
	if (ret)
		goto err;

	ret = hd29l2_rd_reg(priv, 0x31, &tmp);
	tmp &= 0xef;

	if (!(priv->cfg.ts_mode >> 7))
		/* set b4 for serial TS */
		tmp |= 0x10;

	ret = hd29l2_wr_reg(priv, 0x31, tmp);
	if (ret)
		goto err;

	return ret;
err:
	dbg("%s: failed=%d", __func__, ret);
	return ret;
}

static void hd29l2_release(struct dvb_frontend *fe)
{
	struct hd29l2_priv *priv = fe->demodulator_priv;
	kfree(priv);
}

static struct dvb_frontend_ops hd29l2_ops;

struct dvb_frontend *hd29l2_attach(const struct hd29l2_config *config,
	struct i2c_adapter *i2c)
{
	int ret;
	struct hd29l2_priv *priv = NULL;
	u8 tmp;

	/* allocate memory for the internal state */
	priv = kzalloc(sizeof(struct hd29l2_priv), GFP_KERNEL);
	if (priv == NULL)
		goto err;

	/* setup the state */
	priv->i2c = i2c;
	memcpy(&priv->cfg, config, sizeof(struct hd29l2_config));


	/* check if the demod is there */
	ret = hd29l2_rd_reg(priv, 0x00, &tmp);
	if (ret)
		goto err;

	/* create dvb_frontend */
	memcpy(&priv->fe.ops, &hd29l2_ops, sizeof(struct dvb_frontend_ops));
	priv->fe.demodulator_priv = priv;

	return &priv->fe;
err:
	kfree(priv);
	return NULL;
}
EXPORT_SYMBOL(hd29l2_attach);

static struct dvb_frontend_ops hd29l2_ops = {
	.delsys = { SYS_DVBT },
	.info = {
		.name = "HDIC HD29L2 DMB-TH",
		.frequency_min = 474000000,
		.frequency_max = 858000000,
		.frequency_stepsize = 10000,
		.caps = FE_CAN_FEC_AUTO |
			FE_CAN_QPSK |
			FE_CAN_QAM_16 |
			FE_CAN_QAM_32 |
			FE_CAN_QAM_64 |
			FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_BANDWIDTH_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_HIERARCHY_AUTO |
			FE_CAN_RECOVER
	},

	.release = hd29l2_release,

	.init = hd29l2_init,

	.get_frontend_algo = hd29l2_get_frontend_algo,
	.search = hd29l2_search,
	.get_frontend = hd29l2_get_frontend,

	.read_status = hd29l2_read_status,
	.read_snr = hd29l2_read_snr,
	.read_signal_strength = hd29l2_read_signal_strength,
	.read_ber = hd29l2_read_ber,
	.read_ucblocks = hd29l2_read_ucblocks,

	.i2c_gate_ctrl = hd29l2_i2c_gate_ctrl,
};

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("HDIC HD29L2 DMB-TH demodulator driver");
MODULE_LICENSE("GPL");
