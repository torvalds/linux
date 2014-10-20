/*
 * ITE Tech IT9137 silicon tuner driver
 *
 *  Copyright (C) 2011 Malcolm Priestley (tvboxspy@gmail.com)
 *  IT9137 Copyright (C) ITE Tech Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.=
 */

#include "tuner_it913x_priv.h"

struct it913x_state {
	struct i2c_adapter *i2c_adap;
	u8 i2c_addr;
	u8 chip_ver;
	u8 tuner_type;
	u8 firmware_ver;
	u16 tun_xtal;
	u8 tun_fdiv;
	u8 tun_clk_mode;
	u32 tun_fn_min;
};

/* read multiple registers */
static int it913x_rd_regs(struct it913x_state *state,
		u32 reg, u8 *data, u8 count)
{
	int ret;
	u8 b[3];
	struct i2c_msg msg[2] = {
		{ .addr = state->i2c_addr, .flags = 0,
			.buf = b, .len = sizeof(b) },
		{ .addr = state->i2c_addr, .flags = I2C_M_RD,
			.buf = data, .len = count }
	};
	b[0] = (u8)(reg >> 16) & 0xff;
	b[1] = (u8)(reg >> 8) & 0xff;
	b[2] = (u8) reg & 0xff;
	b[0] |= 0x80; /* All reads from demodulator */

	ret = i2c_transfer(state->i2c_adap, msg, 2);

	return ret;
}

/* read single register */
static int it913x_rd_reg(struct it913x_state *state, u32 reg)
{
	int ret;
	u8 b[1];
	ret = it913x_rd_regs(state, reg, &b[0], sizeof(b));
	return (ret < 0) ? -ENODEV : b[0];
}

/* write multiple registers */
static int it913x_wr_regs(struct it913x_state *state,
		u8 pro, u32 reg, u8 buf[], u8 count)
{
	u8 b[256];
	struct i2c_msg msg[1] = {
		{ .addr = state->i2c_addr, .flags = 0,
		  .buf = b, .len = 3 + count }
	};
	int ret;
	b[0] = (u8)(reg >> 16) & 0xff;
	b[1] = (u8)(reg >> 8) & 0xff;
	b[2] = (u8) reg & 0xff;
	memcpy(&b[3], buf, count);

	if (pro == PRO_DMOD)
		b[0] |= 0x80;

	ret = i2c_transfer(state->i2c_adap, msg, 1);

	if (ret < 0)
		return -EIO;

	return 0;
}

/* write single register */
static int it913x_wr_reg(struct it913x_state *state,
		u8 pro, u32 reg, u32 data)
{
	int ret;
	u8 b[4];
	u8 s;

	b[0] = data >> 24;
	b[1] = (data >> 16) & 0xff;
	b[2] = (data >> 8) & 0xff;
	b[3] = data & 0xff;
	/* expand write as needed */
	if (data < 0x100)
		s = 3;
	else if (data < 0x1000)
		s = 2;
	else if (data < 0x100000)
		s = 1;
	else
		s = 0;

	ret = it913x_wr_regs(state, pro, reg, &b[s], sizeof(b) - s);

	return ret;
}

static int it913x_script_loader(struct it913x_state *state,
		struct it913xset *loadscript)
{
	int ret, i;
	if (loadscript == NULL)
		return -EINVAL;

	for (i = 0; i < 1000; ++i) {
		if (loadscript[i].pro == 0xff)
			break;
		ret = it913x_wr_regs(state, loadscript[i].pro,
			loadscript[i].address,
			loadscript[i].reg, loadscript[i].count);
		if (ret < 0)
			return -ENODEV;
	}
	return 0;
}

static int it913x_init(struct dvb_frontend *fe)
{
	struct it913x_state *state = fe->tuner_priv;
	int ret, i, reg;
	u8 val, nv_val;
	u8 nv[] = {48, 32, 24, 16, 12, 8, 6, 4, 2};
	u8 b[2];

	reg = it913x_rd_reg(state, 0xec86);
	switch (reg) {
	case 0:
		state->tun_clk_mode = reg;
		state->tun_xtal = 2000;
		state->tun_fdiv = 3;
		val = 16;
		break;
	case -ENODEV:
		return -ENODEV;
	case 1:
	default:
		state->tun_clk_mode = reg;
		state->tun_xtal = 640;
		state->tun_fdiv = 1;
		val = 6;
		break;
	}

	reg = it913x_rd_reg(state, 0xed03);

	if (reg < 0)
		return -ENODEV;
	else if (reg < ARRAY_SIZE(nv))
		nv_val = nv[reg];
	else
		nv_val = 2;

	for (i = 0; i < 50; i++) {
		ret = it913x_rd_regs(state, 0xed23, &b[0], sizeof(b));
		reg = (b[1] << 8) + b[0];
		if (reg > 0)
			break;
		if (ret < 0)
			return -ENODEV;
		udelay(2000);
	}
	state->tun_fn_min = state->tun_xtal * reg;
	state->tun_fn_min /= (state->tun_fdiv * nv_val);
	dev_dbg(&state->i2c_adap->dev, "%s: Tuner fn_min %d\n", __func__,
			state->tun_fn_min);

	if (state->chip_ver > 1)
		msleep(50);
	else {
		for (i = 0; i < 50; i++) {
			reg = it913x_rd_reg(state, 0xec82);
			if (reg > 0)
				break;
			if (reg < 0)
				return -ENODEV;
			udelay(2000);
		}
	}

	/* Power Up Tuner - common all versions */
	ret = it913x_wr_reg(state, PRO_DMOD, 0xec40, 0x1);
	ret |= it913x_wr_reg(state, PRO_DMOD, 0xfba8, 0x0);
	ret |= it913x_wr_reg(state, PRO_DMOD, 0xec57, 0x0);
	ret |= it913x_wr_reg(state, PRO_DMOD, 0xec58, 0x0);

	return it913x_wr_reg(state, PRO_DMOD, 0xed81, val);
}

static int it9137_set_params(struct dvb_frontend *fe)
{
	struct it913x_state *state = fe->tuner_priv;
	struct it913xset *set_tuner = set_it9137_template;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	u32 bandwidth = p->bandwidth_hz;
	u32 frequency_m = p->frequency;
	int ret, reg;
	u32 frequency = frequency_m / 1000;
	u32 freq, temp_f, tmp;
	u16 iqik_m_cal;
	u16 n_div;
	u8 n;
	u8 l_band;
	u8 lna_band;
	u8 bw;

	if (state->firmware_ver == 1)
		set_tuner = set_it9135_template;
	else
		set_tuner = set_it9137_template;

	dev_dbg(&state->i2c_adap->dev, "%s: Tuner Frequency %d Bandwidth %d\n",
			__func__, frequency, bandwidth);

	if (frequency >= 51000 && frequency <= 440000) {
		l_band = 0;
		lna_band = 0;
	} else if (frequency > 440000 && frequency <= 484000) {
		l_band = 1;
		lna_band = 1;
	} else if (frequency > 484000 && frequency <= 533000) {
		l_band = 1;
		lna_band = 2;
	} else if (frequency > 533000 && frequency <= 587000) {
		l_band = 1;
		lna_band = 3;
	} else if (frequency > 587000 && frequency <= 645000) {
		l_band = 1;
		lna_band = 4;
	} else if (frequency > 645000 && frequency <= 710000) {
		l_band = 1;
		lna_band = 5;
	} else if (frequency > 710000 && frequency <= 782000) {
		l_band = 1;
		lna_band = 6;
	} else if (frequency > 782000 && frequency <= 860000) {
		l_band = 1;
		lna_band = 7;
	} else if (frequency > 1450000 && frequency <= 1492000) {
		l_band = 1;
		lna_band = 0;
	} else if (frequency > 1660000 && frequency <= 1685000) {
		l_band = 1;
		lna_band = 1;
	} else
		return -EINVAL;
	set_tuner[0].reg[0] = lna_band;

	switch (bandwidth) {
	case 5000000:
		bw = 0;
		break;
	case 6000000:
		bw = 2;
		break;
	case 7000000:
		bw = 4;
		break;
	default:
	case 8000000:
		bw = 6;
		break;
	}

	set_tuner[1].reg[0] = bw;
	set_tuner[2].reg[0] = 0xa0 | (l_band << 3);

	if (frequency > 53000 && frequency <= 74000) {
		n_div = 48;
		n = 0;
	} else if (frequency > 74000 && frequency <= 111000) {
		n_div = 32;
		n = 1;
	} else if (frequency > 111000 && frequency <= 148000) {
		n_div = 24;
		n = 2;
	} else if (frequency > 148000 && frequency <= 222000) {
		n_div = 16;
		n = 3;
	} else if (frequency > 222000 && frequency <= 296000) {
		n_div = 12;
		n = 4;
	} else if (frequency > 296000 && frequency <= 445000) {
		n_div = 8;
		n = 5;
	} else if (frequency > 445000 && frequency <= state->tun_fn_min) {
		n_div = 6;
		n = 6;
	} else if (frequency > state->tun_fn_min && frequency <= 950000) {
		n_div = 4;
		n = 7;
	} else if (frequency > 1450000 && frequency <= 1680000) {
		n_div = 2;
		n = 0;
	} else
		return -EINVAL;

	reg = it913x_rd_reg(state, 0xed81);
	iqik_m_cal = (u16)reg * n_div;

	if (reg < 0x20) {
		if (state->tun_clk_mode == 0)
			iqik_m_cal = (iqik_m_cal * 9) >> 5;
		else
			iqik_m_cal >>= 1;
	} else {
		iqik_m_cal = 0x40 - iqik_m_cal;
		if (state->tun_clk_mode == 0)
			iqik_m_cal = ~((iqik_m_cal * 9) >> 5);
		else
			iqik_m_cal = ~(iqik_m_cal >> 1);
	}

	temp_f = frequency * (u32)n_div * (u32)state->tun_fdiv;
	freq = temp_f / state->tun_xtal;
	tmp = freq * state->tun_xtal;

	if ((temp_f - tmp) >= (state->tun_xtal >> 1))
		freq++;

	freq += (u32) n << 13;
	/* Frequency OMEGA_IQIK_M_CAL_MID*/
	temp_f = freq + (u32)iqik_m_cal;

	set_tuner[3].reg[0] =  temp_f & 0xff;
	set_tuner[4].reg[0] =  (temp_f >> 8) & 0xff;

	dev_dbg(&state->i2c_adap->dev, "%s: High Frequency = %04x\n",
			__func__, temp_f);

	/* Lower frequency */
	set_tuner[5].reg[0] =  freq & 0xff;
	set_tuner[6].reg[0] =  (freq >> 8) & 0xff;

	dev_dbg(&state->i2c_adap->dev, "%s: low Frequency = %04x\n",
			__func__, freq);

	ret = it913x_script_loader(state, set_tuner);

	return (ret < 0) ? -ENODEV : 0;
}

/* Power sequence */
/* Power Up	Tuner on -> Frontend suspend off -> Tuner clk on */
/* Power Down	Frontend suspend on -> Tuner clk off -> Tuner off */

static int it913x_sleep(struct dvb_frontend *fe)
{
	struct it913x_state *state = fe->tuner_priv;
	return it913x_script_loader(state, it9137_tuner_off);
}

static int it913x_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	return 0;
}

static const struct dvb_tuner_ops it913x_tuner_ops = {
	.info = {
		.name           = "ITE Tech IT913X",
		.frequency_min  = 174000000,
		.frequency_max  = 862000000,
	},

	.release = it913x_release,

	.init = it913x_init,
	.sleep = it913x_sleep,
	.set_params = it9137_set_params,
};

struct dvb_frontend *it913x_attach(struct dvb_frontend *fe,
		struct i2c_adapter *i2c_adap, u8 i2c_addr, u8 config)
{
	struct it913x_state *state = NULL;
	int ret;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct it913x_state), GFP_KERNEL);
	if (state == NULL)
		return NULL;

	state->i2c_adap = i2c_adap;
	state->i2c_addr = i2c_addr;

	switch (config) {
	case AF9033_TUNER_IT9135_38:
	case AF9033_TUNER_IT9135_51:
	case AF9033_TUNER_IT9135_52:
		state->chip_ver = 0x01;
		break;
	case AF9033_TUNER_IT9135_60:
	case AF9033_TUNER_IT9135_61:
	case AF9033_TUNER_IT9135_62:
		state->chip_ver = 0x02;
		break;
	default:
		dev_dbg(&i2c_adap->dev,
				"%s: invalid config=%02x\n", __func__, config);
		goto error;
	}

	state->tuner_type = config;
	state->firmware_ver = 1;

	/* tuner RF initial */
	ret = it913x_wr_reg(state, PRO_DMOD, 0xec4c, 0x68);
	if (ret < 0)
		goto error;

	fe->tuner_priv = state;
	memcpy(&fe->ops.tuner_ops, &it913x_tuner_ops,
			sizeof(struct dvb_tuner_ops));

	dev_info(&i2c_adap->dev,
			"%s: ITE Tech IT913X successfully attached\n",
			KBUILD_MODNAME);
	dev_dbg(&i2c_adap->dev, "%s: config=%02x chip_ver=%02x\n",
			__func__, config, state->chip_ver);

	return fe;
error:
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL(it913x_attach);

MODULE_DESCRIPTION("ITE Tech IT913X silicon tuner driver");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_LICENSE("GPL");
