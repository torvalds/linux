/*
 * horus3a.h
 *
 * Sony Horus3A DVB-S/S2 tuner driver
 *
 * Copyright 2012 Sony Corporation
 * Copyright (C) 2014 NetUP Inc.
 * Copyright (C) 2014 Sergey Kozlov <serjk@netup.ru>
 * Copyright (C) 2014 Abylay Ospan <aospan@netup.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/dvb/frontend.h>
#include <linux/types.h>
#include "horus3a.h"
#include "dvb_frontend.h"

#define MAX_WRITE_REGSIZE      5

enum horus3a_state {
	STATE_UNKNOWN,
	STATE_SLEEP,
	STATE_ACTIVE
};

struct horus3a_priv {
	u32			frequency;
	u8			i2c_address;
	struct i2c_adapter	*i2c;
	enum horus3a_state	state;
	void			*set_tuner_data;
	int			(*set_tuner)(void *, int);
};

static void horus3a_i2c_debug(struct horus3a_priv *priv,
			      u8 reg, u8 write, const u8 *data, u32 len)
{
	dev_dbg(&priv->i2c->dev, "horus3a: I2C %s reg 0x%02x size %d\n",
		(write == 0 ? "read" : "write"), reg, len);
	print_hex_dump_bytes("horus3a: I2C data: ",
		DUMP_PREFIX_OFFSET, data, len);
}

static int horus3a_write_regs(struct horus3a_priv *priv,
			      u8 reg, const u8 *data, u32 len)
{
	int ret;
	u8 buf[MAX_WRITE_REGSIZE + 1];
	struct i2c_msg msg[1] = {
		{
			.addr = priv->i2c_address,
			.flags = 0,
			.len = len + 1,
			.buf = buf,
		}
	};

	if (len + 1 > sizeof(buf)) {
		dev_warn(&priv->i2c->dev,"wr reg=%04x: len=%d is too big!\n",
			 reg, len + 1);
		return -E2BIG;
	}

	horus3a_i2c_debug(priv, reg, 1, data, len);
	buf[0] = reg;
	memcpy(&buf[1], data, len);
	ret = i2c_transfer(priv->i2c, msg, 1);
	if (ret >= 0 && ret != 1)
		ret = -EREMOTEIO;
	if (ret < 0) {
		dev_warn(&priv->i2c->dev,
			"%s: i2c wr failed=%d reg=%02x len=%d\n",
			KBUILD_MODNAME, ret, reg, len);
		return ret;
	}
	return 0;
}

static int horus3a_write_reg(struct horus3a_priv *priv, u8 reg, u8 val)
{
	return horus3a_write_regs(priv, reg, &val, 1);
}

static int horus3a_enter_power_save(struct horus3a_priv *priv)
{
	u8 data[2];

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	if (priv->state == STATE_SLEEP)
		return 0;
	/* IQ Generator disable */
	horus3a_write_reg(priv, 0x2a, 0x79);
	/* MDIV_EN = 0 */
	horus3a_write_reg(priv, 0x29, 0x70);
	/* VCO disable preparation */
	horus3a_write_reg(priv, 0x28, 0x3e);
	/* VCO buffer disable */
	horus3a_write_reg(priv, 0x2a, 0x19);
	/* VCO calibration disable */
	horus3a_write_reg(priv, 0x1c, 0x00);
	/* Power save setting (xtal is not stopped) */
	data[0] = 0xC0;
	/* LNA is Disabled */
	data[1] = 0xA7;
	/* 0x11 - 0x12 */
	horus3a_write_regs(priv, 0x11, data, sizeof(data));
	priv->state = STATE_SLEEP;
	return 0;
}

static int horus3a_leave_power_save(struct horus3a_priv *priv)
{
	u8 data[2];

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	if (priv->state == STATE_ACTIVE)
		return 0;
	/* Leave power save */
	data[0] = 0x00;
	/* LNA is Disabled */
	data[1] = 0xa7;
	/* 0x11 - 0x12 */
	horus3a_write_regs(priv, 0x11, data, sizeof(data));
	/* VCO buffer enable */
	horus3a_write_reg(priv, 0x2a, 0x79);
	/* VCO calibration enable */
	horus3a_write_reg(priv, 0x1c, 0xc0);
	/* MDIV_EN = 1 */
	horus3a_write_reg(priv, 0x29, 0x71);
	usleep_range(5000, 7000);
	priv->state = STATE_ACTIVE;
	return 0;
}

static int horus3a_init(struct dvb_frontend *fe)
{
	struct horus3a_priv *priv = fe->tuner_priv;

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	return 0;
}

static int horus3a_release(struct dvb_frontend *fe)
{
	struct horus3a_priv *priv = fe->tuner_priv;

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
	return 0;
}

static int horus3a_sleep(struct dvb_frontend *fe)
{
	struct horus3a_priv *priv = fe->tuner_priv;

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	horus3a_enter_power_save(priv);
	return 0;
}

static int horus3a_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct horus3a_priv *priv = fe->tuner_priv;
	u32 frequency = p->frequency;
	u32 symbol_rate = p->symbol_rate/1000;
	u8 mixdiv = 0;
	u8 mdiv = 0;
	u32 ms = 0;
	u8 f_ctl = 0;
	u8 g_ctl = 0;
	u8 fc_lpf = 0;
	u8 data[5];

	dev_dbg(&priv->i2c->dev, "%s(): frequency %dkHz symbol_rate %dksps\n",
		__func__, frequency, symbol_rate);
	if (priv->set_tuner)
		priv->set_tuner(priv->set_tuner_data, 0);
	if (priv->state == STATE_SLEEP)
		horus3a_leave_power_save(priv);

	/* frequency should be X MHz (X : integer) */
	frequency = DIV_ROUND_CLOSEST(frequency, 1000) * 1000;
	if (frequency <= 1155000) {
		mixdiv = 4;
		mdiv = 1;
	} else {
		mixdiv = 2;
		mdiv = 0;
	}
	/* Assumed that fREF == 1MHz (1000kHz) */
	ms = DIV_ROUND_CLOSEST((frequency * mixdiv) / 2, 1000);
	if (ms > 0x7FFF) { /* 15 bit */
		dev_err(&priv->i2c->dev, "horus3a: invalid frequency %d\n",
			frequency);
		return -EINVAL;
	}
	if (frequency < 975000) {
		/* F_CTL=11100 G_CTL=001 */
		f_ctl = 0x1C;
		g_ctl = 0x01;
	} else if (frequency < 1050000) {
		/* F_CTL=11000 G_CTL=010 */
		f_ctl = 0x18;
		g_ctl = 0x02;
	} else if (frequency < 1150000) {
		/* F_CTL=10100 G_CTL=010 */
		f_ctl = 0x14;
		g_ctl = 0x02;
	} else if (frequency < 1250000) {
		/* F_CTL=10000 G_CTL=011 */
		f_ctl = 0x10;
		g_ctl = 0x03;
	} else if (frequency < 1350000) {
		/* F_CTL=01100 G_CTL=100 */
		f_ctl = 0x0C;
		g_ctl = 0x04;
	} else if (frequency < 1450000) {
		/* F_CTL=01010 G_CTL=100 */
		f_ctl = 0x0A;
		g_ctl = 0x04;
	} else if (frequency < 1600000) {
		/* F_CTL=00111 G_CTL=101 */
		f_ctl = 0x07;
		g_ctl = 0x05;
	} else if (frequency < 1800000) {
		/* F_CTL=00100 G_CTL=010 */
		f_ctl = 0x04;
		g_ctl = 0x02;
	} else if (frequency < 2000000) {
		/* F_CTL=00010 G_CTL=001 */
		f_ctl = 0x02;
		g_ctl = 0x01;
	} else {
		/* F_CTL=00000 G_CTL=000 */
		f_ctl = 0x00;
		g_ctl = 0x00;
	}
	/* LPF cutoff frequency setting */
	if (p->delivery_system == SYS_DVBS) {
		/*
		 * rolloff = 0.35
		 * SR <= 4.3
		 * fc_lpf = 5
		 * 4.3 < SR <= 10
		 * fc_lpf = SR * (1 + rolloff) / 2 + SR / 2 =
		 *	SR * 1.175 = SR * (47/40)
		 * 10 < SR
		 * fc_lpf = SR * (1 + rolloff) / 2 + 5 =
		 *	SR * 0.675 + 5 = SR * (27/40) + 5
		 * NOTE: The result should be round up.
		 */
		if (symbol_rate <= 4300)
			fc_lpf = 5;
		else if (symbol_rate <= 10000)
			fc_lpf = (u8)DIV_ROUND_UP(symbol_rate * 47, 40000);
		else
			fc_lpf = (u8)DIV_ROUND_UP(symbol_rate * 27, 40000) + 5;
		/* 5 <= fc_lpf <= 36 */
		if (fc_lpf > 36)
			fc_lpf = 36;
	} else if (p->delivery_system == SYS_DVBS2) {
		/*
		 * SR <= 4.5:
		 * fc_lpf = 5
		 * 4.5 < SR <= 10:
		 * fc_lpf = SR * (1 + rolloff) / 2 + SR / 2
		 * 10 < SR:
		 * fc_lpf = SR * (1 + rolloff) / 2 + 5
		 * NOTE: The result should be round up.
		 */
		if (symbol_rate <= 4500)
			fc_lpf = 5;
		else if (symbol_rate <= 10000)
			fc_lpf = (u8)((symbol_rate * 11 + (10000-1)) / 10000);
		else
			fc_lpf = (u8)((symbol_rate * 3 + (5000-1)) / 5000 + 5);
		/* 5 <= fc_lpf <= 36 is valid */
		if (fc_lpf > 36)
			fc_lpf = 36;
	} else {
		dev_err(&priv->i2c->dev,
			"horus3a: invalid delivery system %d\n",
			p->delivery_system);
		return -EINVAL;
	}
	/* 0x00 - 0x04 */
	data[0] = (u8)((ms >> 7) & 0xFF);
	data[1] = (u8)((ms << 1) & 0xFF);
	data[2] = 0x00;
	data[3] = 0x00;
	data[4] = (u8)(mdiv << 7);
	horus3a_write_regs(priv, 0x00, data, sizeof(data));
	/* Write G_CTL, F_CTL */
	horus3a_write_reg(priv, 0x09, (u8)((g_ctl << 5) | f_ctl));
	/* Write LPF cutoff frequency */
	horus3a_write_reg(priv, 0x37, (u8)(0x80 | (fc_lpf << 1)));
	/* Start Calibration */
	horus3a_write_reg(priv, 0x05, 0x80);
	/* IQ Generator enable */
	horus3a_write_reg(priv, 0x2a, 0x7b);
	/* tuner stabilization time */
	msleep(60);
	/* Store tuned frequency to the struct */
	priv->frequency = ms * 2 * 1000 / mixdiv;
	return 0;
}

static int horus3a_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct horus3a_priv *priv = fe->tuner_priv;

	*frequency = priv->frequency;
	return 0;
}

static const struct dvb_tuner_ops horus3a_tuner_ops = {
	.info = {
		.name = "Sony Horus3a",
		.frequency_min = 950000,
		.frequency_max = 2150000,
		.frequency_step = 1000,
	},
	.init = horus3a_init,
	.release = horus3a_release,
	.sleep = horus3a_sleep,
	.set_params = horus3a_set_params,
	.get_frequency = horus3a_get_frequency,
};

struct dvb_frontend *horus3a_attach(struct dvb_frontend *fe,
				    const struct horus3a_config *config,
				    struct i2c_adapter *i2c)
{
	u8 buf[3], val;
	struct horus3a_priv *priv = NULL;

	priv = kzalloc(sizeof(struct horus3a_priv), GFP_KERNEL);
	if (priv == NULL)
		return NULL;
	priv->i2c_address = (config->i2c_address >> 1);
	priv->i2c = i2c;
	priv->set_tuner_data = config->set_tuner_priv;
	priv->set_tuner = config->set_tuner_callback;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	/* wait 4ms after power on */
	usleep_range(4000, 6000);
	/* IQ Generator disable */
	horus3a_write_reg(priv, 0x2a, 0x79);
	/* REF_R = Xtal Frequency */
	buf[0] = config->xtal_freq_mhz;
	buf[1] = config->xtal_freq_mhz;
	buf[2] = 0;
	/* 0x6 - 0x8 */
	horus3a_write_regs(priv, 0x6, buf, 3);
	/* IQ Out = Single Ended */
	horus3a_write_reg(priv, 0x0a, 0x40);
	switch (config->xtal_freq_mhz) {
	case 27:
		val = 0x1f;
		break;
	case 24:
		val = 0x10;
		break;
	case 16:
		val = 0xc;
		break;
	default:
		val = 0;
		dev_warn(&priv->i2c->dev,
			"horus3a: invalid xtal frequency %dMHz\n",
			config->xtal_freq_mhz);
		break;
	}
	val <<= 2;
	horus3a_write_reg(priv, 0x0e, val);
	horus3a_enter_power_save(priv);
	usleep_range(3000, 5000);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	memcpy(&fe->ops.tuner_ops, &horus3a_tuner_ops,
				sizeof(struct dvb_tuner_ops));
	fe->tuner_priv = priv;
	dev_info(&priv->i2c->dev,
		"Sony HORUS3A attached on addr=%x at I2C adapter %p\n",
		priv->i2c_address, priv->i2c);
	return fe;
}
EXPORT_SYMBOL(horus3a_attach);

MODULE_DESCRIPTION("Sony HORUS3A sattelite tuner driver");
MODULE_AUTHOR("Sergey Kozlov <serjk@netup.ru>");
MODULE_LICENSE("GPL");
