/*
 * cxd2841er.c
 *
 * Sony digital demodulator driver for
 *	CXD2841ER - DVB-S/S2/T/T2/C/C2
 *	CXD2854ER - DVB-S/S2/T/T2/C/C2, ISDB-T/S
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/log2.h>
#include <linux/dynamic_debug.h>
#include <linux/kernel.h>

#include <media/dvb_math.h>
#include <media/dvb_frontend.h>
#include "cxd2841er.h"
#include "cxd2841er_priv.h"

#define MAX_WRITE_REGSIZE	16
#define LOG2_E_100X 144

#define INTLOG10X100(x) ((u32) (((u64) intlog10(x) * 100) >> 24))

/* DVB-C constellation */
enum sony_dvbc_constellation_t {
	SONY_DVBC_CONSTELLATION_16QAM,
	SONY_DVBC_CONSTELLATION_32QAM,
	SONY_DVBC_CONSTELLATION_64QAM,
	SONY_DVBC_CONSTELLATION_128QAM,
	SONY_DVBC_CONSTELLATION_256QAM
};

enum cxd2841er_state {
	STATE_SHUTDOWN = 0,
	STATE_SLEEP_S,
	STATE_ACTIVE_S,
	STATE_SLEEP_TC,
	STATE_ACTIVE_TC
};

struct cxd2841er_priv {
	struct dvb_frontend		frontend;
	struct i2c_adapter		*i2c;
	u8				i2c_addr_slvx;
	u8				i2c_addr_slvt;
	const struct cxd2841er_config	*config;
	enum cxd2841er_state		state;
	u8				system;
	enum cxd2841er_xtal		xtal;
	enum fe_caps caps;
	u32				flags;
};

static const struct cxd2841er_cnr_data s_cn_data[] = {
	{ 0x033e, 0 }, { 0x0339, 100 }, { 0x0333, 200 },
	{ 0x032e, 300 }, { 0x0329, 400 }, { 0x0324, 500 },
	{ 0x031e, 600 }, { 0x0319, 700 }, { 0x0314, 800 },
	{ 0x030f, 900 }, { 0x030a, 1000 }, { 0x02ff, 1100 },
	{ 0x02f4, 1200 }, { 0x02e9, 1300 }, { 0x02de, 1400 },
	{ 0x02d4, 1500 }, { 0x02c9, 1600 }, { 0x02bf, 1700 },
	{ 0x02b5, 1800 }, { 0x02ab, 1900 }, { 0x02a1, 2000 },
	{ 0x029b, 2100 }, { 0x0295, 2200 }, { 0x0290, 2300 },
	{ 0x028a, 2400 }, { 0x0284, 2500 }, { 0x027f, 2600 },
	{ 0x0279, 2700 }, { 0x0274, 2800 }, { 0x026e, 2900 },
	{ 0x0269, 3000 }, { 0x0262, 3100 }, { 0x025c, 3200 },
	{ 0x0255, 3300 }, { 0x024f, 3400 }, { 0x0249, 3500 },
	{ 0x0242, 3600 }, { 0x023c, 3700 }, { 0x0236, 3800 },
	{ 0x0230, 3900 }, { 0x022a, 4000 }, { 0x0223, 4100 },
	{ 0x021c, 4200 }, { 0x0215, 4300 }, { 0x020e, 4400 },
	{ 0x0207, 4500 }, { 0x0201, 4600 }, { 0x01fa, 4700 },
	{ 0x01f4, 4800 }, { 0x01ed, 4900 }, { 0x01e7, 5000 },
	{ 0x01e0, 5100 }, { 0x01d9, 5200 }, { 0x01d2, 5300 },
	{ 0x01cb, 5400 }, { 0x01c4, 5500 }, { 0x01be, 5600 },
	{ 0x01b7, 5700 }, { 0x01b1, 5800 }, { 0x01aa, 5900 },
	{ 0x01a4, 6000 }, { 0x019d, 6100 }, { 0x0196, 6200 },
	{ 0x018f, 6300 }, { 0x0189, 6400 }, { 0x0182, 6500 },
	{ 0x017c, 6600 }, { 0x0175, 6700 }, { 0x016f, 6800 },
	{ 0x0169, 6900 }, { 0x0163, 7000 }, { 0x015c, 7100 },
	{ 0x0156, 7200 }, { 0x0150, 7300 }, { 0x014a, 7400 },
	{ 0x0144, 7500 }, { 0x013e, 7600 }, { 0x0138, 7700 },
	{ 0x0132, 7800 }, { 0x012d, 7900 }, { 0x0127, 8000 },
	{ 0x0121, 8100 }, { 0x011c, 8200 }, { 0x0116, 8300 },
	{ 0x0111, 8400 }, { 0x010b, 8500 }, { 0x0106, 8600 },
	{ 0x0101, 8700 }, { 0x00fc, 8800 }, { 0x00f7, 8900 },
	{ 0x00f2, 9000 }, { 0x00ee, 9100 }, { 0x00ea, 9200 },
	{ 0x00e6, 9300 }, { 0x00e2, 9400 }, { 0x00de, 9500 },
	{ 0x00da, 9600 }, { 0x00d7, 9700 }, { 0x00d3, 9800 },
	{ 0x00d0, 9900 }, { 0x00cc, 10000 }, { 0x00c7, 10100 },
	{ 0x00c3, 10200 }, { 0x00bf, 10300 }, { 0x00ba, 10400 },
	{ 0x00b6, 10500 }, { 0x00b2, 10600 }, { 0x00ae, 10700 },
	{ 0x00aa, 10800 }, { 0x00a7, 10900 }, { 0x00a3, 11000 },
	{ 0x009f, 11100 }, { 0x009c, 11200 }, { 0x0098, 11300 },
	{ 0x0094, 11400 }, { 0x0091, 11500 }, { 0x008e, 11600 },
	{ 0x008a, 11700 }, { 0x0087, 11800 }, { 0x0084, 11900 },
	{ 0x0081, 12000 }, { 0x007e, 12100 }, { 0x007b, 12200 },
	{ 0x0079, 12300 }, { 0x0076, 12400 }, { 0x0073, 12500 },
	{ 0x0071, 12600 }, { 0x006e, 12700 }, { 0x006c, 12800 },
	{ 0x0069, 12900 }, { 0x0067, 13000 }, { 0x0065, 13100 },
	{ 0x0062, 13200 }, { 0x0060, 13300 }, { 0x005e, 13400 },
	{ 0x005c, 13500 }, { 0x005a, 13600 }, { 0x0058, 13700 },
	{ 0x0056, 13800 }, { 0x0054, 13900 }, { 0x0052, 14000 },
	{ 0x0050, 14100 }, { 0x004e, 14200 }, { 0x004c, 14300 },
	{ 0x004b, 14400 }, { 0x0049, 14500 }, { 0x0047, 14600 },
	{ 0x0046, 14700 }, { 0x0044, 14800 }, { 0x0043, 14900 },
	{ 0x0041, 15000 }, { 0x003f, 15100 }, { 0x003e, 15200 },
	{ 0x003c, 15300 }, { 0x003b, 15400 }, { 0x003a, 15500 },
	{ 0x0037, 15700 }, { 0x0036, 15800 }, { 0x0034, 15900 },
	{ 0x0033, 16000 }, { 0x0032, 16100 }, { 0x0031, 16200 },
	{ 0x0030, 16300 }, { 0x002f, 16400 }, { 0x002e, 16500 },
	{ 0x002d, 16600 }, { 0x002c, 16700 }, { 0x002b, 16800 },
	{ 0x002a, 16900 }, { 0x0029, 17000 }, { 0x0028, 17100 },
	{ 0x0027, 17200 }, { 0x0026, 17300 }, { 0x0025, 17400 },
	{ 0x0024, 17500 }, { 0x0023, 17600 }, { 0x0022, 17800 },
	{ 0x0021, 17900 }, { 0x0020, 18000 }, { 0x001f, 18200 },
	{ 0x001e, 18300 }, { 0x001d, 18500 }, { 0x001c, 18700 },
	{ 0x001b, 18900 }, { 0x001a, 19000 }, { 0x0019, 19200 },
	{ 0x0018, 19300 }, { 0x0017, 19500 }, { 0x0016, 19700 },
	{ 0x0015, 19900 }, { 0x0014, 20000 },
};

static const struct cxd2841er_cnr_data s2_cn_data[] = {
	{ 0x05af, 0 }, { 0x0597, 100 }, { 0x057e, 200 },
	{ 0x0567, 300 }, { 0x0550, 400 }, { 0x0539, 500 },
	{ 0x0522, 600 }, { 0x050c, 700 }, { 0x04f6, 800 },
	{ 0x04e1, 900 }, { 0x04cc, 1000 }, { 0x04b6, 1100 },
	{ 0x04a1, 1200 }, { 0x048c, 1300 }, { 0x0477, 1400 },
	{ 0x0463, 1500 }, { 0x044f, 1600 }, { 0x043c, 1700 },
	{ 0x0428, 1800 }, { 0x0416, 1900 }, { 0x0403, 2000 },
	{ 0x03ef, 2100 }, { 0x03dc, 2200 }, { 0x03c9, 2300 },
	{ 0x03b6, 2400 }, { 0x03a4, 2500 }, { 0x0392, 2600 },
	{ 0x0381, 2700 }, { 0x036f, 2800 }, { 0x035f, 2900 },
	{ 0x034e, 3000 }, { 0x033d, 3100 }, { 0x032d, 3200 },
	{ 0x031d, 3300 }, { 0x030d, 3400 }, { 0x02fd, 3500 },
	{ 0x02ee, 3600 }, { 0x02df, 3700 }, { 0x02d0, 3800 },
	{ 0x02c2, 3900 }, { 0x02b4, 4000 }, { 0x02a6, 4100 },
	{ 0x0299, 4200 }, { 0x028c, 4300 }, { 0x027f, 4400 },
	{ 0x0272, 4500 }, { 0x0265, 4600 }, { 0x0259, 4700 },
	{ 0x024d, 4800 }, { 0x0241, 4900 }, { 0x0236, 5000 },
	{ 0x022b, 5100 }, { 0x0220, 5200 }, { 0x0215, 5300 },
	{ 0x020a, 5400 }, { 0x0200, 5500 }, { 0x01f6, 5600 },
	{ 0x01ec, 5700 }, { 0x01e2, 5800 }, { 0x01d8, 5900 },
	{ 0x01cf, 6000 }, { 0x01c6, 6100 }, { 0x01bc, 6200 },
	{ 0x01b3, 6300 }, { 0x01aa, 6400 }, { 0x01a2, 6500 },
	{ 0x0199, 6600 }, { 0x0191, 6700 }, { 0x0189, 6800 },
	{ 0x0181, 6900 }, { 0x0179, 7000 }, { 0x0171, 7100 },
	{ 0x0169, 7200 }, { 0x0161, 7300 }, { 0x015a, 7400 },
	{ 0x0153, 7500 }, { 0x014b, 7600 }, { 0x0144, 7700 },
	{ 0x013d, 7800 }, { 0x0137, 7900 }, { 0x0130, 8000 },
	{ 0x012a, 8100 }, { 0x0124, 8200 }, { 0x011e, 8300 },
	{ 0x0118, 8400 }, { 0x0112, 8500 }, { 0x010c, 8600 },
	{ 0x0107, 8700 }, { 0x0101, 8800 }, { 0x00fc, 8900 },
	{ 0x00f7, 9000 }, { 0x00f2, 9100 }, { 0x00ec, 9200 },
	{ 0x00e7, 9300 }, { 0x00e2, 9400 }, { 0x00dd, 9500 },
	{ 0x00d8, 9600 }, { 0x00d4, 9700 }, { 0x00cf, 9800 },
	{ 0x00ca, 9900 }, { 0x00c6, 10000 }, { 0x00c2, 10100 },
	{ 0x00be, 10200 }, { 0x00b9, 10300 }, { 0x00b5, 10400 },
	{ 0x00b1, 10500 }, { 0x00ae, 10600 }, { 0x00aa, 10700 },
	{ 0x00a6, 10800 }, { 0x00a3, 10900 }, { 0x009f, 11000 },
	{ 0x009b, 11100 }, { 0x0098, 11200 }, { 0x0095, 11300 },
	{ 0x0091, 11400 }, { 0x008e, 11500 }, { 0x008b, 11600 },
	{ 0x0088, 11700 }, { 0x0085, 11800 }, { 0x0082, 11900 },
	{ 0x007f, 12000 }, { 0x007c, 12100 }, { 0x007a, 12200 },
	{ 0x0077, 12300 }, { 0x0074, 12400 }, { 0x0072, 12500 },
	{ 0x006f, 12600 }, { 0x006d, 12700 }, { 0x006b, 12800 },
	{ 0x0068, 12900 }, { 0x0066, 13000 }, { 0x0064, 13100 },
	{ 0x0061, 13200 }, { 0x005f, 13300 }, { 0x005d, 13400 },
	{ 0x005b, 13500 }, { 0x0059, 13600 }, { 0x0057, 13700 },
	{ 0x0055, 13800 }, { 0x0053, 13900 }, { 0x0051, 14000 },
	{ 0x004f, 14100 }, { 0x004e, 14200 }, { 0x004c, 14300 },
	{ 0x004a, 14400 }, { 0x0049, 14500 }, { 0x0047, 14600 },
	{ 0x0045, 14700 }, { 0x0044, 14800 }, { 0x0042, 14900 },
	{ 0x0041, 15000 }, { 0x003f, 15100 }, { 0x003e, 15200 },
	{ 0x003c, 15300 }, { 0x003b, 15400 }, { 0x003a, 15500 },
	{ 0x0038, 15600 }, { 0x0037, 15700 }, { 0x0036, 15800 },
	{ 0x0034, 15900 }, { 0x0033, 16000 }, { 0x0032, 16100 },
	{ 0x0031, 16200 }, { 0x0030, 16300 }, { 0x002f, 16400 },
	{ 0x002e, 16500 }, { 0x002d, 16600 }, { 0x002c, 16700 },
	{ 0x002b, 16800 }, { 0x002a, 16900 }, { 0x0029, 17000 },
	{ 0x0028, 17100 }, { 0x0027, 17200 }, { 0x0026, 17300 },
	{ 0x0025, 17400 }, { 0x0024, 17500 }, { 0x0023, 17600 },
	{ 0x0022, 17800 }, { 0x0021, 17900 }, { 0x0020, 18000 },
	{ 0x001f, 18200 }, { 0x001e, 18300 }, { 0x001d, 18500 },
	{ 0x001c, 18700 }, { 0x001b, 18900 }, { 0x001a, 19000 },
	{ 0x0019, 19200 }, { 0x0018, 19300 }, { 0x0017, 19500 },
	{ 0x0016, 19700 }, { 0x0015, 19900 }, { 0x0014, 20000 },
};

static int cxd2841er_freeze_regs(struct cxd2841er_priv *priv);
static int cxd2841er_unfreeze_regs(struct cxd2841er_priv *priv);

static void cxd2841er_i2c_debug(struct cxd2841er_priv *priv,
				u8 addr, u8 reg, u8 write,
				const u8 *data, u32 len)
{
	dev_dbg(&priv->i2c->dev,
		"cxd2841er: I2C %s addr %02x reg 0x%02x size %d data %*ph\n",
		(write == 0 ? "read" : "write"), addr, reg, len, len, data);
}

static int cxd2841er_write_regs(struct cxd2841er_priv *priv,
				u8 addr, u8 reg, const u8 *data, u32 len)
{
	int ret;
	u8 buf[MAX_WRITE_REGSIZE + 1];
	u8 i2c_addr = (addr == I2C_SLVX ?
		priv->i2c_addr_slvx : priv->i2c_addr_slvt);
	struct i2c_msg msg[1] = {
		{
			.addr = i2c_addr,
			.flags = 0,
			.len = len + 1,
			.buf = buf,
		}
	};

	if (len + 1 >= sizeof(buf)) {
		dev_warn(&priv->i2c->dev, "wr reg=%04x: len=%d is too big!\n",
			 reg, len + 1);
		return -E2BIG;
	}

	cxd2841er_i2c_debug(priv, i2c_addr, reg, 1, data, len);
	buf[0] = reg;
	memcpy(&buf[1], data, len);

	ret = i2c_transfer(priv->i2c, msg, 1);
	if (ret >= 0 && ret != 1)
		ret = -EIO;
	if (ret < 0) {
		dev_warn(&priv->i2c->dev,
			"%s: i2c wr failed=%d addr=%02x reg=%02x len=%d\n",
			KBUILD_MODNAME, ret, i2c_addr, reg, len);
		return ret;
	}
	return 0;
}

static int cxd2841er_write_reg(struct cxd2841er_priv *priv,
			       u8 addr, u8 reg, u8 val)
{
	u8 tmp = val; /* see gcc.gnu.org/bugzilla/show_bug.cgi?id=81715 */

	return cxd2841er_write_regs(priv, addr, reg, &tmp, 1);
}

static int cxd2841er_read_regs(struct cxd2841er_priv *priv,
			       u8 addr, u8 reg, u8 *val, u32 len)
{
	int ret;
	u8 i2c_addr = (addr == I2C_SLVX ?
		priv->i2c_addr_slvx : priv->i2c_addr_slvt);
	struct i2c_msg msg[2] = {
		{
			.addr = i2c_addr,
			.flags = 0,
			.len = 1,
			.buf = &reg,
		}, {
			.addr = i2c_addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = val,
		}
	};

	ret = i2c_transfer(priv->i2c, msg, 2);
	if (ret >= 0 && ret != 2)
		ret = -EIO;
	if (ret < 0) {
		dev_warn(&priv->i2c->dev,
			"%s: i2c rd failed=%d addr=%02x reg=%02x\n",
			KBUILD_MODNAME, ret, i2c_addr, reg);
		return ret;
	}
	cxd2841er_i2c_debug(priv, i2c_addr, reg, 0, val, len);
	return 0;
}

static int cxd2841er_read_reg(struct cxd2841er_priv *priv,
			      u8 addr, u8 reg, u8 *val)
{
	return cxd2841er_read_regs(priv, addr, reg, val, 1);
}

static int cxd2841er_set_reg_bits(struct cxd2841er_priv *priv,
				  u8 addr, u8 reg, u8 data, u8 mask)
{
	int res;
	u8 rdata;

	if (mask != 0xff) {
		res = cxd2841er_read_reg(priv, addr, reg, &rdata);
		if (res)
			return res;
		data = ((data & mask) | (rdata & (mask ^ 0xFF)));
	}
	return cxd2841er_write_reg(priv, addr, reg, data);
}

static u32 cxd2841er_calc_iffreq_xtal(enum cxd2841er_xtal xtal, u32 ifhz)
{
	u64 tmp;

	tmp = (u64) ifhz * 16777216;
	do_div(tmp, ((xtal == SONY_XTAL_24000) ? 48000000 : 41000000));

	return (u32) tmp;
}

static u32 cxd2841er_calc_iffreq(u32 ifhz)
{
	return cxd2841er_calc_iffreq_xtal(SONY_XTAL_20500, ifhz);
}

static int cxd2841er_get_if_hz(struct cxd2841er_priv *priv, u32 def_hz)
{
	u32 hz;

	if (priv->frontend.ops.tuner_ops.get_if_frequency
			&& (priv->flags & CXD2841ER_AUTO_IFHZ))
		priv->frontend.ops.tuner_ops.get_if_frequency(
			&priv->frontend, &hz);
	else
		hz = def_hz;

	return hz;
}

static int cxd2841er_tuner_set(struct dvb_frontend *fe)
{
	struct cxd2841er_priv *priv = fe->demodulator_priv;

	if ((priv->flags & CXD2841ER_USE_GATECTRL) && fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (fe->ops.tuner_ops.set_params)
		fe->ops.tuner_ops.set_params(fe);
	if ((priv->flags & CXD2841ER_USE_GATECTRL) && fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return 0;
}

static int cxd2841er_dvbs2_set_symbol_rate(struct cxd2841er_priv *priv,
					   u32 symbol_rate)
{
	u32 reg_value = 0;
	u8 data[3] = {0, 0, 0};

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	/*
	 * regValue = (symbolRateKSps * 2^14 / 1000) + 0.5
	 *          = ((symbolRateKSps * 2^14) + 500) / 1000
	 *          = ((symbolRateKSps * 16384) + 500) / 1000
	 */
	reg_value = DIV_ROUND_CLOSEST(symbol_rate * 16384, 1000);
	if ((reg_value == 0) || (reg_value > 0xFFFFF)) {
		dev_err(&priv->i2c->dev,
			"%s(): reg_value is out of range\n", __func__);
		return -EINVAL;
	}
	data[0] = (u8)((reg_value >> 16) & 0x0F);
	data[1] = (u8)((reg_value >>  8) & 0xFF);
	data[2] = (u8)(reg_value & 0xFF);
	/* Set SLV-T Bank : 0xAE */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xae);
	cxd2841er_write_regs(priv, I2C_SLVT, 0x20, data, 3);
	return 0;
}

static void cxd2841er_set_ts_clock_mode(struct cxd2841er_priv *priv,
					u8 system);

static int cxd2841er_sleep_s_to_active_s(struct cxd2841er_priv *priv,
					 u8 system, u32 symbol_rate)
{
	int ret;
	u8 data[4] = { 0, 0, 0, 0 };

	if (priv->state != STATE_SLEEP_S) {
		dev_err(&priv->i2c->dev, "%s(): invalid state %d\n",
			__func__, (int)priv->state);
		return -EINVAL;
	}
	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	cxd2841er_set_ts_clock_mode(priv, SYS_DVBS);
	/* Set demod mode */
	if (system == SYS_DVBS) {
		data[0] = 0x0A;
	} else if (system == SYS_DVBS2) {
		data[0] = 0x0B;
	} else {
		dev_err(&priv->i2c->dev, "%s(): invalid delsys %d\n",
			__func__, system);
		return -EINVAL;
	}
	/* Set SLV-X Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x00, 0x00);
	cxd2841er_write_reg(priv, I2C_SLVX, 0x17, data[0]);
	/* DVB-S/S2 */
	data[0] = 0x00;
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* Enable S/S2 auto detection 1 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x2d, data[0]);
	/* Set SLV-T Bank : 0xAE */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xae);
	/* Enable S/S2 auto detection 2 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x30, data[0]);
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* Enable demod clock */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x2c, 0x01);
	/* Enable ADC clock */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x31, 0x01);
	/* Enable ADC 1 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x63, 0x16);
	/* Enable ADC 2 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x65, 0x3f);
	/* Set SLV-X Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x00, 0x00);
	/* Enable ADC 3 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x18, 0x00);
	/* Set SLV-T Bank : 0xA3 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xa3);
	cxd2841er_write_reg(priv, I2C_SLVT, 0xac, 0x00);
	data[0] = 0x07;
	data[1] = 0x3B;
	data[2] = 0x08;
	data[3] = 0xC5;
	/* Set SLV-T Bank : 0xAB */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xab);
	cxd2841er_write_regs(priv, I2C_SLVT, 0x98, data, 4);
	data[0] = 0x05;
	data[1] = 0x80;
	data[2] = 0x0A;
	data[3] = 0x80;
	cxd2841er_write_regs(priv, I2C_SLVT, 0xa8, data, 4);
	data[0] = 0x0C;
	data[1] = 0xCC;
	cxd2841er_write_regs(priv, I2C_SLVT, 0xc3, data, 2);
	/* Set demod parameter */
	ret = cxd2841er_dvbs2_set_symbol_rate(priv, symbol_rate);
	if (ret != 0)
		return ret;
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* disable Hi-Z setting 1 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x80, 0x10);
	/* disable Hi-Z setting 2 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x81, 0x00);
	priv->state = STATE_ACTIVE_S;
	return 0;
}

static int cxd2841er_sleep_tc_to_active_t_band(struct cxd2841er_priv *priv,
					       u32 bandwidth);

static int cxd2841er_sleep_tc_to_active_t2_band(struct cxd2841er_priv *priv,
						u32 bandwidth);

static int cxd2841er_sleep_tc_to_active_c_band(struct cxd2841er_priv *priv,
					       u32 bandwidth);

static int cxd2841er_sleep_tc_to_active_i(struct cxd2841er_priv *priv,
		u32 bandwidth);

static int cxd2841er_active_i_to_sleep_tc(struct cxd2841er_priv *priv);

static int cxd2841er_sleep_tc_to_shutdown(struct cxd2841er_priv *priv);

static int cxd2841er_shutdown_to_sleep_tc(struct cxd2841er_priv *priv);

static int cxd2841er_sleep_tc(struct dvb_frontend *fe);

static int cxd2841er_retune_active(struct cxd2841er_priv *priv,
				   struct dtv_frontend_properties *p)
{
	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	if (priv->state != STATE_ACTIVE_S &&
			priv->state != STATE_ACTIVE_TC) {
		dev_dbg(&priv->i2c->dev, "%s(): invalid state %d\n",
			__func__, priv->state);
		return -EINVAL;
	}
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* disable TS output */
	cxd2841er_write_reg(priv, I2C_SLVT, 0xc3, 0x01);
	if (priv->state == STATE_ACTIVE_S)
		return cxd2841er_dvbs2_set_symbol_rate(
				priv, p->symbol_rate / 1000);
	else if (priv->state == STATE_ACTIVE_TC) {
		switch (priv->system) {
		case SYS_DVBT:
			return cxd2841er_sleep_tc_to_active_t_band(
					priv, p->bandwidth_hz);
		case SYS_DVBT2:
			return cxd2841er_sleep_tc_to_active_t2_band(
					priv, p->bandwidth_hz);
		case SYS_DVBC_ANNEX_A:
			return cxd2841er_sleep_tc_to_active_c_band(
					priv, p->bandwidth_hz);
		case SYS_ISDBT:
			cxd2841er_active_i_to_sleep_tc(priv);
			cxd2841er_sleep_tc_to_shutdown(priv);
			cxd2841er_shutdown_to_sleep_tc(priv);
			return cxd2841er_sleep_tc_to_active_i(
					priv, p->bandwidth_hz);
		}
	}
	dev_dbg(&priv->i2c->dev, "%s(): invalid delivery system %d\n",
		__func__, priv->system);
	return -EINVAL;
}

static int cxd2841er_active_s_to_sleep_s(struct cxd2841er_priv *priv)
{
	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	if (priv->state != STATE_ACTIVE_S) {
		dev_err(&priv->i2c->dev, "%s(): invalid state %d\n",
			__func__, priv->state);
		return -EINVAL;
	}
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* disable TS output */
	cxd2841er_write_reg(priv, I2C_SLVT, 0xc3, 0x01);
	/* enable Hi-Z setting 1 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x80, 0x1f);
	/* enable Hi-Z setting 2 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x81, 0xff);
	/* Set SLV-X Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x00, 0x00);
	/* disable ADC 1 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x18, 0x01);
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* disable ADC clock */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x31, 0x00);
	/* disable ADC 2 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x63, 0x16);
	/* disable ADC 3 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x65, 0x27);
	/* SADC Bias ON */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x69, 0x06);
	/* disable demod clock */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x2c, 0x00);
	/* Set SLV-T Bank : 0xAE */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xae);
	/* disable S/S2 auto detection1 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x30, 0x00);
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* disable S/S2 auto detection2 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x2d, 0x00);
	priv->state = STATE_SLEEP_S;
	return 0;
}

static int cxd2841er_sleep_s_to_shutdown(struct cxd2841er_priv *priv)
{
	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	if (priv->state != STATE_SLEEP_S) {
		dev_dbg(&priv->i2c->dev, "%s(): invalid demod state %d\n",
			__func__, priv->state);
		return -EINVAL;
	}
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* Disable DSQOUT */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x80, 0x3f);
	/* Disable DSQIN */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x9c, 0x00);
	/* Set SLV-X Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x00, 0x00);
	/* Disable oscillator */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x15, 0x01);
	/* Set demod mode */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x17, 0x01);
	priv->state = STATE_SHUTDOWN;
	return 0;
}

static int cxd2841er_sleep_tc_to_shutdown(struct cxd2841er_priv *priv)
{
	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	if (priv->state != STATE_SLEEP_TC) {
		dev_dbg(&priv->i2c->dev, "%s(): invalid demod state %d\n",
			__func__, priv->state);
		return -EINVAL;
	}
	/* Set SLV-X Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x00, 0x00);
	/* Disable oscillator */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x15, 0x01);
	/* Set demod mode */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x17, 0x01);
	priv->state = STATE_SHUTDOWN;
	return 0;
}

static int cxd2841er_active_t_to_sleep_tc(struct cxd2841er_priv *priv)
{
	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	if (priv->state != STATE_ACTIVE_TC) {
		dev_err(&priv->i2c->dev, "%s(): invalid state %d\n",
			__func__, priv->state);
		return -EINVAL;
	}
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* disable TS output */
	cxd2841er_write_reg(priv, I2C_SLVT, 0xc3, 0x01);
	/* enable Hi-Z setting 1 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x80, 0x3f);
	/* enable Hi-Z setting 2 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x81, 0xff);
	/* Set SLV-X Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x00, 0x00);
	/* disable ADC 1 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x18, 0x01);
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* Disable ADC 2 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x43, 0x0a);
	/* Disable ADC 3 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x41, 0x0a);
	/* Disable ADC clock */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x30, 0x00);
	/* Disable RF level monitor */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x2f, 0x00);
	/* Disable demod clock */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x2c, 0x00);
	priv->state = STATE_SLEEP_TC;
	return 0;
}

static int cxd2841er_active_t2_to_sleep_tc(struct cxd2841er_priv *priv)
{
	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	if (priv->state != STATE_ACTIVE_TC) {
		dev_err(&priv->i2c->dev, "%s(): invalid state %d\n",
			__func__, priv->state);
		return -EINVAL;
	}
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* disable TS output */
	cxd2841er_write_reg(priv, I2C_SLVT, 0xc3, 0x01);
	/* enable Hi-Z setting 1 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x80, 0x3f);
	/* enable Hi-Z setting 2 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x81, 0xff);
	/* Cancel DVB-T2 setting */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x13);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x83, 0x40);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x86, 0x21);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0x9e, 0x09, 0x0f);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x9f, 0xfb);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x2a);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0x38, 0x00, 0x0f);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x2b);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0x11, 0x00, 0x3f);
	/* Set SLV-X Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x00, 0x00);
	/* disable ADC 1 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x18, 0x01);
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* Disable ADC 2 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x43, 0x0a);
	/* Disable ADC 3 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x41, 0x0a);
	/* Disable ADC clock */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x30, 0x00);
	/* Disable RF level monitor */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x2f, 0x00);
	/* Disable demod clock */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x2c, 0x00);
	priv->state = STATE_SLEEP_TC;
	return 0;
}

static int cxd2841er_active_c_to_sleep_tc(struct cxd2841er_priv *priv)
{
	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	if (priv->state != STATE_ACTIVE_TC) {
		dev_err(&priv->i2c->dev, "%s(): invalid state %d\n",
			__func__, priv->state);
		return -EINVAL;
	}
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* disable TS output */
	cxd2841er_write_reg(priv, I2C_SLVT, 0xc3, 0x01);
	/* enable Hi-Z setting 1 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x80, 0x3f);
	/* enable Hi-Z setting 2 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x81, 0xff);
	/* Cancel DVB-C setting */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x11);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xa3, 0x00, 0x1f);
	/* Set SLV-X Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x00, 0x00);
	/* disable ADC 1 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x18, 0x01);
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* Disable ADC 2 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x43, 0x0a);
	/* Disable ADC 3 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x41, 0x0a);
	/* Disable ADC clock */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x30, 0x00);
	/* Disable RF level monitor */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x2f, 0x00);
	/* Disable demod clock */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x2c, 0x00);
	priv->state = STATE_SLEEP_TC;
	return 0;
}

static int cxd2841er_active_i_to_sleep_tc(struct cxd2841er_priv *priv)
{
	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	if (priv->state != STATE_ACTIVE_TC) {
		dev_err(&priv->i2c->dev, "%s(): invalid state %d\n",
				__func__, priv->state);
		return -EINVAL;
	}
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* disable TS output */
	cxd2841er_write_reg(priv, I2C_SLVT, 0xc3, 0x01);
	/* enable Hi-Z setting 1 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x80, 0x3f);
	/* enable Hi-Z setting 2 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x81, 0xff);

	/* TODO: Cancel demod parameter */

	/* Set SLV-X Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x00, 0x00);
	/* disable ADC 1 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x18, 0x01);
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* Disable ADC 2 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x43, 0x0a);
	/* Disable ADC 3 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x41, 0x0a);
	/* Disable ADC clock */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x30, 0x00);
	/* Disable RF level monitor */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x2f, 0x00);
	/* Disable demod clock */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x2c, 0x00);
	priv->state = STATE_SLEEP_TC;
	return 0;
}

static int cxd2841er_shutdown_to_sleep_s(struct cxd2841er_priv *priv)
{
	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	if (priv->state != STATE_SHUTDOWN) {
		dev_dbg(&priv->i2c->dev, "%s(): invalid demod state %d\n",
			__func__, priv->state);
		return -EINVAL;
	}
	/* Set SLV-X Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x00, 0x00);
	/* Clear all demodulator registers */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x02, 0x00);
	usleep_range(3000, 5000);
	/* Set SLV-X Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x00, 0x00);
	/* Set demod SW reset */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x10, 0x01);

	switch (priv->xtal) {
	case SONY_XTAL_20500:
		cxd2841er_write_reg(priv, I2C_SLVX, 0x14, 0x00);
		break;
	case SONY_XTAL_24000:
		/* Select demod frequency */
		cxd2841er_write_reg(priv, I2C_SLVX, 0x12, 0x00);
		cxd2841er_write_reg(priv, I2C_SLVX, 0x14, 0x03);
		break;
	case SONY_XTAL_41000:
		cxd2841er_write_reg(priv, I2C_SLVX, 0x14, 0x01);
		break;
	default:
		dev_dbg(&priv->i2c->dev, "%s(): invalid demod xtal %d\n",
				__func__, priv->xtal);
		return -EINVAL;
	}

	/* Set demod mode */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x17, 0x0a);
	/* Clear demod SW reset */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x10, 0x00);
	usleep_range(1000, 2000);
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* enable DSQOUT */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x80, 0x1F);
	/* enable DSQIN */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x9C, 0x40);
	/* TADC Bias On */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x43, 0x0a);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x41, 0x0a);
	/* SADC Bias On */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x63, 0x16);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x65, 0x27);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x69, 0x06);
	priv->state = STATE_SLEEP_S;
	return 0;
}

static int cxd2841er_shutdown_to_sleep_tc(struct cxd2841er_priv *priv)
{
	u8 data = 0;

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	if (priv->state != STATE_SHUTDOWN) {
		dev_dbg(&priv->i2c->dev, "%s(): invalid demod state %d\n",
			__func__, priv->state);
		return -EINVAL;
	}
	/* Set SLV-X Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x00, 0x00);
	/* Clear all demodulator registers */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x02, 0x00);
	usleep_range(3000, 5000);
	/* Set SLV-X Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x00, 0x00);
	/* Set demod SW reset */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x10, 0x01);
  /* Select ADC clock mode */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x13, 0x00);

	switch (priv->xtal) {
	case SONY_XTAL_20500:
		data = 0x0;
		break;
	case SONY_XTAL_24000:
		/* Select demod frequency */
		cxd2841er_write_reg(priv, I2C_SLVX, 0x12, 0x00);
		data = 0x3;
		break;
	case SONY_XTAL_41000:
		cxd2841er_write_reg(priv, I2C_SLVX, 0x12, 0x00);
		data = 0x1;
		break;
	}
	cxd2841er_write_reg(priv, I2C_SLVX, 0x14, data);
	/* Clear demod SW reset */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x10, 0x00);
	usleep_range(1000, 2000);
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* TADC Bias On */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x43, 0x0a);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x41, 0x0a);
	/* SADC Bias On */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x63, 0x16);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x65, 0x27);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x69, 0x06);
	priv->state = STATE_SLEEP_TC;
	return 0;
}

static int cxd2841er_tune_done(struct cxd2841er_priv *priv)
{
	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0, 0);
	/* SW Reset */
	cxd2841er_write_reg(priv, I2C_SLVT, 0xfe, 0x01);
	/* Enable TS output */
	cxd2841er_write_reg(priv, I2C_SLVT, 0xc3, 0x00);
	return 0;
}

/* Set TS parallel mode */
static void cxd2841er_set_ts_clock_mode(struct cxd2841er_priv *priv,
					u8 system)
{
	u8 serial_ts, ts_rate_ctrl_off, ts_in_off;

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	cxd2841er_read_reg(priv, I2C_SLVT, 0xc4, &serial_ts);
	cxd2841er_read_reg(priv, I2C_SLVT, 0xd3, &ts_rate_ctrl_off);
	cxd2841er_read_reg(priv, I2C_SLVT, 0xde, &ts_in_off);
	dev_dbg(&priv->i2c->dev, "%s(): ser_ts=0x%02x rate_ctrl_off=0x%02x in_off=0x%02x\n",
		__func__, serial_ts, ts_rate_ctrl_off, ts_in_off);

	/*
	 * slave    Bank    Addr    Bit    default    Name
	 * <SLV-T>  00h     C4h     [1:0]  2'b??      OSERCKMODE
	 */
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xc4,
		((priv->flags & CXD2841ER_TS_SERIAL) ? 0x01 : 0x00), 0x03);
	/*
	 * slave    Bank    Addr    Bit    default    Name
	 * <SLV-T>  00h     D1h     [1:0]  2'b??      OSERDUTYMODE
	 */
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xd1,
		((priv->flags & CXD2841ER_TS_SERIAL) ? 0x01 : 0x00), 0x03);
	/*
	 * slave    Bank    Addr    Bit    default    Name
	 * <SLV-T>  00h     D9h     [7:0]  8'h08      OTSCKPERIOD
	 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0xd9, 0x08);
	/*
	 * Disable TS IF Clock
	 * slave    Bank    Addr    Bit    default    Name
	 * <SLV-T>  00h     32h     [0]    1'b1       OREG_CK_TSIF_EN
	 */
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0x32, 0x00, 0x01);
	/*
	 * slave    Bank    Addr    Bit    default    Name
	 * <SLV-T>  00h     33h     [1:0]  2'b01      OREG_CKSEL_TSIF
	 */
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0x33,
		((priv->flags & CXD2841ER_TS_SERIAL) ? 0x01 : 0x00), 0x03);
	/*
	 * Enable TS IF Clock
	 * slave    Bank    Addr    Bit    default    Name
	 * <SLV-T>  00h     32h     [0]    1'b1       OREG_CK_TSIF_EN
	 */
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0x32, 0x01, 0x01);

	if (system == SYS_DVBT) {
		/* Enable parity period for DVB-T */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x10);
		cxd2841er_set_reg_bits(priv, I2C_SLVT, 0x66, 0x01, 0x01);
	} else if (system == SYS_DVBC_ANNEX_A) {
		/* Enable parity period for DVB-C */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x40);
		cxd2841er_set_reg_bits(priv, I2C_SLVT, 0x66, 0x01, 0x01);
	}
}

static u8 cxd2841er_chip_id(struct cxd2841er_priv *priv)
{
	u8 chip_id = 0;

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	if (cxd2841er_write_reg(priv, I2C_SLVT, 0, 0) == 0)
		cxd2841er_read_reg(priv, I2C_SLVT, 0xfd, &chip_id);
	else if (cxd2841er_write_reg(priv, I2C_SLVX, 0, 0) == 0)
		cxd2841er_read_reg(priv, I2C_SLVX, 0xfd, &chip_id);

	return chip_id;
}

static int cxd2841er_read_status_s(struct dvb_frontend *fe,
				   enum fe_status *status)
{
	u8 reg = 0;
	struct cxd2841er_priv *priv = fe->demodulator_priv;

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	*status = 0;
	if (priv->state != STATE_ACTIVE_S) {
		dev_err(&priv->i2c->dev, "%s(): invalid state %d\n",
			__func__, priv->state);
		return -EINVAL;
	}
	/* Set SLV-T Bank : 0xA0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xa0);
	/*
	 *  slave     Bank      Addr      Bit      Signal name
	 * <SLV-T>    A0h       11h       [2]      ITSLOCK
	 */
	cxd2841er_read_reg(priv, I2C_SLVT, 0x11, &reg);
	if (reg & 0x04) {
		*status = FE_HAS_SIGNAL
			| FE_HAS_CARRIER
			| FE_HAS_VITERBI
			| FE_HAS_SYNC
			| FE_HAS_LOCK;
	}
	dev_dbg(&priv->i2c->dev, "%s(): result 0x%x\n", __func__, *status);
	return 0;
}

static int cxd2841er_read_status_t_t2(struct cxd2841er_priv *priv,
				      u8 *sync, u8 *tslock, u8 *unlock)
{
	u8 data = 0;

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	if (priv->state != STATE_ACTIVE_TC)
		return -EINVAL;
	if (priv->system == SYS_DVBT) {
		/* Set SLV-T Bank : 0x10 */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x10);
	} else {
		/* Set SLV-T Bank : 0x20 */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x20);
	}
	cxd2841er_read_reg(priv, I2C_SLVT, 0x10, &data);
	if ((data & 0x07) == 0x07) {
		dev_dbg(&priv->i2c->dev,
			"%s(): invalid hardware state detected\n", __func__);
		*sync = 0;
		*tslock = 0;
		*unlock = 0;
	} else {
		*sync = ((data & 0x07) == 0x6 ? 1 : 0);
		*tslock = ((data & 0x20) ? 1 : 0);
		*unlock = ((data & 0x10) ? 1 : 0);
	}
	return 0;
}

static int cxd2841er_read_status_c(struct cxd2841er_priv *priv, u8 *tslock)
{
	u8 data;

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	if (priv->state != STATE_ACTIVE_TC)
		return -EINVAL;
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x40);
	cxd2841er_read_reg(priv, I2C_SLVT, 0x88, &data);
	if ((data & 0x01) == 0) {
		*tslock = 0;
	} else {
		cxd2841er_read_reg(priv, I2C_SLVT, 0x10, &data);
		*tslock = ((data & 0x20) ? 1 : 0);
	}
	return 0;
}

static int cxd2841er_read_status_i(struct cxd2841er_priv *priv,
		u8 *sync, u8 *tslock, u8 *unlock)
{
	u8 data = 0;

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	if (priv->state != STATE_ACTIVE_TC)
		return -EINVAL;
	/* Set SLV-T Bank : 0x60 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x60);
	cxd2841er_read_reg(priv, I2C_SLVT, 0x10, &data);
	dev_dbg(&priv->i2c->dev,
			"%s(): lock=0x%x\n", __func__, data);
	*sync = ((data & 0x02) ? 1 : 0);
	*tslock = ((data & 0x01) ? 1 : 0);
	*unlock = ((data & 0x10) ? 1 : 0);
	return 0;
}

static int cxd2841er_read_status_tc(struct dvb_frontend *fe,
				    enum fe_status *status)
{
	int ret = 0;
	u8 sync = 0;
	u8 tslock = 0;
	u8 unlock = 0;
	struct cxd2841er_priv *priv = fe->demodulator_priv;

	*status = 0;
	if (priv->state == STATE_ACTIVE_TC) {
		if (priv->system == SYS_DVBT || priv->system == SYS_DVBT2) {
			ret = cxd2841er_read_status_t_t2(
				priv, &sync, &tslock, &unlock);
			if (ret)
				goto done;
			if (unlock)
				goto done;
			if (sync)
				*status = FE_HAS_SIGNAL |
					FE_HAS_CARRIER |
					FE_HAS_VITERBI |
					FE_HAS_SYNC;
			if (tslock)
				*status |= FE_HAS_LOCK;
		} else if (priv->system == SYS_ISDBT) {
			ret = cxd2841er_read_status_i(
					priv, &sync, &tslock, &unlock);
			if (ret)
				goto done;
			if (unlock)
				goto done;
			if (sync)
				*status = FE_HAS_SIGNAL |
					FE_HAS_CARRIER |
					FE_HAS_VITERBI |
					FE_HAS_SYNC;
			if (tslock)
				*status |= FE_HAS_LOCK;
		} else if (priv->system == SYS_DVBC_ANNEX_A) {
			ret = cxd2841er_read_status_c(priv, &tslock);
			if (ret)
				goto done;
			if (tslock)
				*status = FE_HAS_SIGNAL |
					FE_HAS_CARRIER |
					FE_HAS_VITERBI |
					FE_HAS_SYNC |
					FE_HAS_LOCK;
		}
	}
done:
	dev_dbg(&priv->i2c->dev, "%s(): status 0x%x\n", __func__, *status);
	return ret;
}

static int cxd2841er_get_carrier_offset_s_s2(struct cxd2841er_priv *priv,
					     int *offset)
{
	u8 data[3];
	u8 is_hs_mode;
	s32 cfrl_ctrlval;
	s32 temp_div, temp_q, temp_r;

	if (priv->state != STATE_ACTIVE_S) {
		dev_dbg(&priv->i2c->dev, "%s(): invalid state %d\n",
			__func__, priv->state);
		return -EINVAL;
	}
	/*
	 * Get High Sampling Rate mode
	 *  slave     Bank      Addr      Bit      Signal name
	 * <SLV-T>    A0h       10h       [0]      ITRL_LOCK
	 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xa0);
	cxd2841er_read_reg(priv, I2C_SLVT, 0x10, &data[0]);
	if (data[0] & 0x01) {
		/*
		 *  slave     Bank      Addr      Bit      Signal name
		 * <SLV-T>    A0h       50h       [4]      IHSMODE
		 */
		cxd2841er_read_reg(priv, I2C_SLVT, 0x50, &data[0]);
		is_hs_mode = (data[0] & 0x10 ? 1 : 0);
	} else {
		dev_dbg(&priv->i2c->dev,
			"%s(): unable to detect sampling rate mode\n",
			__func__);
		return -EINVAL;
	}
	/*
	 *  slave     Bank      Addr      Bit      Signal name
	 * <SLV-T>    A0h       45h       [4:0]    ICFRL_CTRLVAL[20:16]
	 * <SLV-T>    A0h       46h       [7:0]    ICFRL_CTRLVAL[15:8]
	 * <SLV-T>    A0h       47h       [7:0]    ICFRL_CTRLVAL[7:0]
	 */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x45, data, 3);
	cfrl_ctrlval = sign_extend32((((u32)data[0] & 0x1F) << 16) |
				(((u32)data[1] & 0xFF) <<  8) |
				((u32)data[2] & 0xFF), 20);
	temp_div = (is_hs_mode ? 1048576 : 1572864);
	if (cfrl_ctrlval > 0) {
		temp_q = div_s64_rem(97375LL * cfrl_ctrlval,
			temp_div, &temp_r);
	} else {
		temp_q = div_s64_rem(-97375LL * cfrl_ctrlval,
			temp_div, &temp_r);
	}
	if (temp_r >= temp_div / 2)
		temp_q++;
	if (cfrl_ctrlval > 0)
		temp_q *= -1;
	*offset = temp_q;
	return 0;
}

static int cxd2841er_get_carrier_offset_i(struct cxd2841er_priv *priv,
					   u32 bandwidth, int *offset)
{
	u8 data[4];

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	if (priv->state != STATE_ACTIVE_TC) {
		dev_dbg(&priv->i2c->dev, "%s(): invalid state %d\n",
			__func__, priv->state);
		return -EINVAL;
	}
	if (priv->system != SYS_ISDBT) {
		dev_dbg(&priv->i2c->dev, "%s(): invalid delivery system %d\n",
			__func__, priv->system);
		return -EINVAL;
	}
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x60);
	cxd2841er_read_regs(priv, I2C_SLVT, 0x4c, data, sizeof(data));
	*offset = -1 * sign_extend32(
		((u32)(data[0] & 0x1F) << 24) | ((u32)data[1] << 16) |
		((u32)data[2] << 8) | (u32)data[3], 29);

	switch (bandwidth) {
	case 6000000:
		*offset = -1 * ((*offset) * 8/264);
		break;
	case 7000000:
		*offset = -1 * ((*offset) * 8/231);
		break;
	case 8000000:
		*offset = -1 * ((*offset) * 8/198);
		break;
	default:
		dev_dbg(&priv->i2c->dev, "%s(): invalid bandwidth %d\n",
				__func__, bandwidth);
		return -EINVAL;
	}

	dev_dbg(&priv->i2c->dev, "%s(): bandwidth %d offset %d\n",
			__func__, bandwidth, *offset);

	return 0;
}

static int cxd2841er_get_carrier_offset_t(struct cxd2841er_priv *priv,
					   u32 bandwidth, int *offset)
{
	u8 data[4];

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	if (priv->state != STATE_ACTIVE_TC) {
		dev_dbg(&priv->i2c->dev, "%s(): invalid state %d\n",
			__func__, priv->state);
		return -EINVAL;
	}
	if (priv->system != SYS_DVBT) {
		dev_dbg(&priv->i2c->dev, "%s(): invalid delivery system %d\n",
			__func__, priv->system);
		return -EINVAL;
	}
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x10);
	cxd2841er_read_regs(priv, I2C_SLVT, 0x4c, data, sizeof(data));
	*offset = -1 * sign_extend32(
		((u32)(data[0] & 0x1F) << 24) | ((u32)data[1] << 16) |
		((u32)data[2] << 8) | (u32)data[3], 29);
	*offset *= (bandwidth / 1000000);
	*offset /= 235;
	return 0;
}

static int cxd2841er_get_carrier_offset_t2(struct cxd2841er_priv *priv,
					   u32 bandwidth, int *offset)
{
	u8 data[4];

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	if (priv->state != STATE_ACTIVE_TC) {
		dev_dbg(&priv->i2c->dev, "%s(): invalid state %d\n",
			__func__, priv->state);
		return -EINVAL;
	}
	if (priv->system != SYS_DVBT2) {
		dev_dbg(&priv->i2c->dev, "%s(): invalid delivery system %d\n",
			__func__, priv->system);
		return -EINVAL;
	}
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x20);
	cxd2841er_read_regs(priv, I2C_SLVT, 0x4c, data, sizeof(data));
	*offset = -1 * sign_extend32(
		((u32)(data[0] & 0x0F) << 24) | ((u32)data[1] << 16) |
		((u32)data[2] << 8) | (u32)data[3], 27);
	switch (bandwidth) {
	case 1712000:
		*offset /= 582;
		break;
	case 5000000:
	case 6000000:
	case 7000000:
	case 8000000:
		*offset *= (bandwidth / 1000000);
		*offset /= 940;
		break;
	default:
		dev_dbg(&priv->i2c->dev, "%s(): invalid bandwidth %d\n",
			__func__, bandwidth);
		return -EINVAL;
	}
	return 0;
}

static int cxd2841er_get_carrier_offset_c(struct cxd2841er_priv *priv,
					  int *offset)
{
	u8 data[2];

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	if (priv->state != STATE_ACTIVE_TC) {
		dev_dbg(&priv->i2c->dev, "%s(): invalid state %d\n",
			__func__, priv->state);
		return -EINVAL;
	}
	if (priv->system != SYS_DVBC_ANNEX_A) {
		dev_dbg(&priv->i2c->dev, "%s(): invalid delivery system %d\n",
			__func__, priv->system);
		return -EINVAL;
	}
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x40);
	cxd2841er_read_regs(priv, I2C_SLVT, 0x15, data, sizeof(data));
	*offset = div_s64(41000LL * sign_extend32((((u32)data[0] & 0x3f) << 8)
						| (u32)data[1], 13), 16384);
	return 0;
}

static int cxd2841er_read_packet_errors_c(
		struct cxd2841er_priv *priv, u32 *penum)
{
	u8 data[3];

	*penum = 0;
	if (priv->state != STATE_ACTIVE_TC) {
		dev_dbg(&priv->i2c->dev, "%s(): invalid state %d\n",
				__func__, priv->state);
		return -EINVAL;
	}
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x40);
	cxd2841er_read_regs(priv, I2C_SLVT, 0xea, data, sizeof(data));
	if (data[2] & 0x01)
		*penum = ((u32)data[0] << 8) | (u32)data[1];
	return 0;
}

static int cxd2841er_read_packet_errors_t(
		struct cxd2841er_priv *priv, u32 *penum)
{
	u8 data[3];

	*penum = 0;
	if (priv->state != STATE_ACTIVE_TC) {
		dev_dbg(&priv->i2c->dev, "%s(): invalid state %d\n",
			__func__, priv->state);
		return -EINVAL;
	}
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x10);
	cxd2841er_read_regs(priv, I2C_SLVT, 0xea, data, sizeof(data));
	if (data[2] & 0x01)
		*penum = ((u32)data[0] << 8) | (u32)data[1];
	return 0;
}

static int cxd2841er_read_packet_errors_t2(
		struct cxd2841er_priv *priv, u32 *penum)
{
	u8 data[3];

	*penum = 0;
	if (priv->state != STATE_ACTIVE_TC) {
		dev_dbg(&priv->i2c->dev, "%s(): invalid state %d\n",
			__func__, priv->state);
		return -EINVAL;
	}
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x24);
	cxd2841er_read_regs(priv, I2C_SLVT, 0xfd, data, sizeof(data));
	if (data[0] & 0x01)
		*penum = ((u32)data[1] << 8) | (u32)data[2];
	return 0;
}

static int cxd2841er_read_packet_errors_i(
		struct cxd2841er_priv *priv, u32 *penum)
{
	u8 data[2];

	*penum = 0;
	if (priv->state != STATE_ACTIVE_TC) {
		dev_dbg(&priv->i2c->dev, "%s(): invalid state %d\n",
				__func__, priv->state);
		return -EINVAL;
	}
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x60);
	cxd2841er_read_regs(priv, I2C_SLVT, 0xA1, data, 1);

	if (!(data[0] & 0x01))
		return 0;

	/* Layer A */
	cxd2841er_read_regs(priv, I2C_SLVT, 0xA2, data, sizeof(data));
	*penum = ((u32)data[0] << 8) | (u32)data[1];

	/* Layer B */
	cxd2841er_read_regs(priv, I2C_SLVT, 0xA4, data, sizeof(data));
	*penum += ((u32)data[0] << 8) | (u32)data[1];

	/* Layer C */
	cxd2841er_read_regs(priv, I2C_SLVT, 0xA6, data, sizeof(data));
	*penum += ((u32)data[0] << 8) | (u32)data[1];

	return 0;
}

static int cxd2841er_read_ber_c(struct cxd2841er_priv *priv,
		u32 *bit_error, u32 *bit_count)
{
	u8 data[3];
	u32 bit_err, period_exp;

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	if (priv->state != STATE_ACTIVE_TC) {
		dev_dbg(&priv->i2c->dev, "%s(): invalid state %d\n",
				__func__, priv->state);
		return -EINVAL;
	}
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x40);
	cxd2841er_read_regs(priv, I2C_SLVT, 0x62, data, sizeof(data));
	if (!(data[0] & 0x80)) {
		dev_dbg(&priv->i2c->dev,
				"%s(): no valid BER data\n", __func__);
		return -EINVAL;
	}
	bit_err = ((u32)(data[0] & 0x3f) << 16) |
		((u32)data[1] << 8) |
		(u32)data[2];
	cxd2841er_read_reg(priv, I2C_SLVT, 0x60, data);
	period_exp = data[0] & 0x1f;

	if ((period_exp <= 11) && (bit_err > (1 << period_exp) * 204 * 8)) {
		dev_dbg(&priv->i2c->dev,
				"%s(): period_exp(%u) or bit_err(%u)  not in range. no valid BER data\n",
				__func__, period_exp, bit_err);
		return -EINVAL;
	}

	dev_dbg(&priv->i2c->dev,
			"%s(): period_exp(%u) or bit_err(%u) count=%d\n",
			__func__, period_exp, bit_err,
			((1 << period_exp) * 204 * 8));

	*bit_error = bit_err;
	*bit_count = ((1 << period_exp) * 204 * 8);

	return 0;
}

static int cxd2841er_read_ber_i(struct cxd2841er_priv *priv,
		u32 *bit_error, u32 *bit_count)
{
	u8 data[3];
	u8 pktnum[2];

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	if (priv->state != STATE_ACTIVE_TC) {
		dev_dbg(&priv->i2c->dev, "%s(): invalid state %d\n",
				__func__, priv->state);
		return -EINVAL;
	}

	cxd2841er_freeze_regs(priv);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x60);
	cxd2841er_read_regs(priv, I2C_SLVT, 0x5B, pktnum, sizeof(pktnum));
	cxd2841er_read_regs(priv, I2C_SLVT, 0x16, data, sizeof(data));
	cxd2841er_unfreeze_regs(priv);

	if (!pktnum[0] && !pktnum[1]) {
		dev_dbg(&priv->i2c->dev,
				"%s(): no valid BER data\n", __func__);
		return -EINVAL;
	}

	*bit_error = ((u32)(data[0] & 0x7F) << 16) |
		((u32)data[1] << 8) | data[2];
	*bit_count = ((((u32)pktnum[0] << 8) | pktnum[1]) * 204 * 8);
	dev_dbg(&priv->i2c->dev, "%s(): bit_error=%u bit_count=%u\n",
			__func__, *bit_error, *bit_count);

	return 0;
}

static int cxd2841er_mon_read_ber_s(struct cxd2841er_priv *priv,
				    u32 *bit_error, u32 *bit_count)
{
	u8 data[11];

	/* Set SLV-T Bank : 0xA0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xa0);
	/*
	 *  slave     Bank      Addr      Bit      Signal name
	 * <SLV-T>    A0h       35h       [0]      IFVBER_VALID
	 * <SLV-T>    A0h       36h       [5:0]    IFVBER_BITERR[21:16]
	 * <SLV-T>    A0h       37h       [7:0]    IFVBER_BITERR[15:8]
	 * <SLV-T>    A0h       38h       [7:0]    IFVBER_BITERR[7:0]
	 * <SLV-T>    A0h       3Dh       [5:0]    IFVBER_BITNUM[21:16]
	 * <SLV-T>    A0h       3Eh       [7:0]    IFVBER_BITNUM[15:8]
	 * <SLV-T>    A0h       3Fh       [7:0]    IFVBER_BITNUM[7:0]
	 */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x35, data, 11);
	if (data[0] & 0x01) {
		*bit_error = ((u32)(data[1]  & 0x3F) << 16) |
			     ((u32)(data[2]  & 0xFF) <<  8) |
			     (u32)(data[3]  & 0xFF);
		*bit_count = ((u32)(data[8]  & 0x3F) << 16) |
			     ((u32)(data[9]  & 0xFF) <<  8) |
			     (u32)(data[10] & 0xFF);
		if ((*bit_count == 0) || (*bit_error > *bit_count)) {
			dev_dbg(&priv->i2c->dev,
				"%s(): invalid bit_error %d, bit_count %d\n",
				__func__, *bit_error, *bit_count);
			return -EINVAL;
		}
		return 0;
	}
	dev_dbg(&priv->i2c->dev, "%s(): no data available\n", __func__);
	return -EINVAL;
}


static int cxd2841er_mon_read_ber_s2(struct cxd2841er_priv *priv,
				     u32 *bit_error, u32 *bit_count)
{
	u8 data[5];
	u32 period;

	/* Set SLV-T Bank : 0xB2 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xb2);
	/*
	 *  slave     Bank      Addr      Bit      Signal name
	 * <SLV-T>    B2h       30h       [0]      IFLBER_VALID
	 * <SLV-T>    B2h       31h       [3:0]    IFLBER_BITERR[27:24]
	 * <SLV-T>    B2h       32h       [7:0]    IFLBER_BITERR[23:16]
	 * <SLV-T>    B2h       33h       [7:0]    IFLBER_BITERR[15:8]
	 * <SLV-T>    B2h       34h       [7:0]    IFLBER_BITERR[7:0]
	 */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x30, data, 5);
	if (data[0] & 0x01) {
		/* Bit error count */
		*bit_error = ((u32)(data[1] & 0x0F) << 24) |
			     ((u32)(data[2] & 0xFF) << 16) |
			     ((u32)(data[3] & 0xFF) <<  8) |
			     (u32)(data[4] & 0xFF);

		/* Set SLV-T Bank : 0xA0 */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xa0);
		cxd2841er_read_reg(priv, I2C_SLVT, 0x7a, data);
		/* Measurement period */
		period = (u32)(1 << (data[0] & 0x0F));
		if (period == 0) {
			dev_dbg(&priv->i2c->dev,
				"%s(): period is 0\n", __func__);
			return -EINVAL;
		}
		if (*bit_error > (period * 64800)) {
			dev_dbg(&priv->i2c->dev,
				"%s(): invalid bit_err 0x%x period 0x%x\n",
				__func__, *bit_error, period);
			return -EINVAL;
		}
		*bit_count = period * 64800;

		return 0;
	} else {
		dev_dbg(&priv->i2c->dev,
			"%s(): no data available\n", __func__);
	}
	return -EINVAL;
}

static int cxd2841er_read_ber_t2(struct cxd2841er_priv *priv,
				 u32 *bit_error, u32 *bit_count)
{
	u8 data[4];
	u32 period_exp, n_ldpc;

	if (priv->state != STATE_ACTIVE_TC) {
		dev_dbg(&priv->i2c->dev,
			"%s(): invalid state %d\n", __func__, priv->state);
		return -EINVAL;
	}
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x20);
	cxd2841er_read_regs(priv, I2C_SLVT, 0x39, data, sizeof(data));
	if (!(data[0] & 0x10)) {
		dev_dbg(&priv->i2c->dev,
			"%s(): no valid BER data\n", __func__);
		return -EINVAL;
	}
	*bit_error = ((u32)(data[0] & 0x0f) << 24) |
		     ((u32)data[1] << 16) |
		     ((u32)data[2] << 8) |
		     (u32)data[3];
	cxd2841er_read_reg(priv, I2C_SLVT, 0x6f, data);
	period_exp = data[0] & 0x0f;
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x22);
	cxd2841er_read_reg(priv, I2C_SLVT, 0x5e, data);
	n_ldpc = ((data[0] & 0x03) == 0 ? 16200 : 64800);
	if (*bit_error > ((1U << period_exp) * n_ldpc)) {
		dev_dbg(&priv->i2c->dev,
			"%s(): invalid BER value\n", __func__);
		return -EINVAL;
	}

	/*
	 * FIXME: the right thing would be to return bit_error untouched,
	 * but, as we don't know the scale returned by the counters, let's
	 * at least preserver BER = bit_error/bit_count.
	 */
	if (period_exp >= 4) {
		*bit_count = (1U << (period_exp - 4)) * (n_ldpc / 200);
		*bit_error *= 3125ULL;
	} else {
		*bit_count = (1U << period_exp) * (n_ldpc / 200);
		*bit_error *= 50000ULL;
	}
	return 0;
}

static int cxd2841er_read_ber_t(struct cxd2841er_priv *priv,
				u32 *bit_error, u32 *bit_count)
{
	u8 data[2];
	u32 period;

	if (priv->state != STATE_ACTIVE_TC) {
		dev_dbg(&priv->i2c->dev,
			"%s(): invalid state %d\n", __func__, priv->state);
		return -EINVAL;
	}
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x10);
	cxd2841er_read_reg(priv, I2C_SLVT, 0x39, data);
	if (!(data[0] & 0x01)) {
		dev_dbg(&priv->i2c->dev,
			"%s(): no valid BER data\n", __func__);
		return 0;
	}
	cxd2841er_read_regs(priv, I2C_SLVT, 0x22, data, sizeof(data));
	*bit_error = ((u32)data[0] << 8) | (u32)data[1];
	cxd2841er_read_reg(priv, I2C_SLVT, 0x6f, data);
	period = ((data[0] & 0x07) == 0) ? 256 : (4096 << (data[0] & 0x07));

	/*
	 * FIXME: the right thing would be to return bit_error untouched,
	 * but, as we don't know the scale returned by the counters, let's
	 * at least preserver BER = bit_error/bit_count.
	 */
	*bit_count = period / 128;
	*bit_error *= 78125ULL;
	return 0;
}

static int cxd2841er_freeze_regs(struct cxd2841er_priv *priv)
{
	/*
	 * Freeze registers: ensure multiple separate register reads
	 * are from the same snapshot
	 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x01, 0x01);
	return 0;
}

static int cxd2841er_unfreeze_regs(struct cxd2841er_priv *priv)
{
	/*
	 * un-freeze registers
	 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x01, 0x00);
	return 0;
}

static u32 cxd2841er_dvbs_read_snr(struct cxd2841er_priv *priv,
		u8 delsys, u32 *snr)
{
	u8 data[3];
	u32 res = 0, value;
	int min_index, max_index, index;
	static const struct cxd2841er_cnr_data *cn_data;

	cxd2841er_freeze_regs(priv);
	/* Set SLV-T Bank : 0xA1 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xa1);
	/*
	 *  slave     Bank      Addr      Bit     Signal name
	 * <SLV-T>    A1h       10h       [0]     ICPM_QUICKRDY
	 * <SLV-T>    A1h       11h       [4:0]   ICPM_QUICKCNDT[12:8]
	 * <SLV-T>    A1h       12h       [7:0]   ICPM_QUICKCNDT[7:0]
	 */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x10, data, 3);
	cxd2841er_unfreeze_regs(priv);

	if (data[0] & 0x01) {
		value = ((u32)(data[1] & 0x1F) << 8) | (u32)(data[2] & 0xFF);
		min_index = 0;
		if (delsys == SYS_DVBS) {
			cn_data = s_cn_data;
			max_index = ARRAY_SIZE(s_cn_data) - 1;
		} else {
			cn_data = s2_cn_data;
			max_index = ARRAY_SIZE(s2_cn_data) - 1;
		}
		if (value >= cn_data[min_index].value) {
			res = cn_data[min_index].cnr_x1000;
			goto done;
		}
		if (value <= cn_data[max_index].value) {
			res = cn_data[max_index].cnr_x1000;
			goto done;
		}
		while ((max_index - min_index) > 1) {
			index = (max_index + min_index) / 2;
			if (value == cn_data[index].value) {
				res = cn_data[index].cnr_x1000;
				goto done;
			} else if (value > cn_data[index].value)
				max_index = index;
			else
				min_index = index;
			if ((max_index - min_index) <= 1) {
				if (value == cn_data[max_index].value) {
					res = cn_data[max_index].cnr_x1000;
					goto done;
				} else {
					res = cn_data[min_index].cnr_x1000;
					goto done;
				}
			}
		}
	} else {
		dev_dbg(&priv->i2c->dev,
			"%s(): no data available\n", __func__);
		return -EINVAL;
	}
done:
	*snr = res;
	return 0;
}

static uint32_t sony_log(uint32_t x)
{
	return (((10000>>8)*(intlog2(x)>>16) + LOG2_E_100X/2)/LOG2_E_100X);
}

static int cxd2841er_read_snr_c(struct cxd2841er_priv *priv, u32 *snr)
{
	u32 reg;
	u8 data[2];
	enum sony_dvbc_constellation_t qam = SONY_DVBC_CONSTELLATION_16QAM;

	*snr = 0;
	if (priv->state != STATE_ACTIVE_TC) {
		dev_dbg(&priv->i2c->dev,
				"%s(): invalid state %d\n",
				__func__, priv->state);
		return -EINVAL;
	}

	cxd2841er_freeze_regs(priv);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x40);
	cxd2841er_read_regs(priv, I2C_SLVT, 0x19, data, 1);
	qam = (enum sony_dvbc_constellation_t) (data[0] & 0x07);
	cxd2841er_read_regs(priv, I2C_SLVT, 0x4C, data, 2);
	cxd2841er_unfreeze_regs(priv);

	reg = ((u32)(data[0]&0x1f) << 8) | (u32)data[1];
	if (reg == 0) {
		dev_dbg(&priv->i2c->dev,
				"%s(): reg value out of range\n", __func__);
		return 0;
	}

	switch (qam) {
	case SONY_DVBC_CONSTELLATION_16QAM:
	case SONY_DVBC_CONSTELLATION_64QAM:
	case SONY_DVBC_CONSTELLATION_256QAM:
		/* SNR(dB) = -9.50 * ln(IREG_SNR_ESTIMATE / (24320)) */
		if (reg < 126)
			reg = 126;
		*snr = -95 * (int32_t)sony_log(reg) + 95941;
		break;
	case SONY_DVBC_CONSTELLATION_32QAM:
	case SONY_DVBC_CONSTELLATION_128QAM:
		/* SNR(dB) = -8.75 * ln(IREG_SNR_ESTIMATE / (20800)) */
		if (reg < 69)
			reg = 69;
		*snr = -88 * (int32_t)sony_log(reg) + 86999;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cxd2841er_read_snr_t(struct cxd2841er_priv *priv, u32 *snr)
{
	u32 reg;
	u8 data[2];

	*snr = 0;
	if (priv->state != STATE_ACTIVE_TC) {
		dev_dbg(&priv->i2c->dev,
			"%s(): invalid state %d\n", __func__, priv->state);
		return -EINVAL;
	}

	cxd2841er_freeze_regs(priv);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x10);
	cxd2841er_read_regs(priv, I2C_SLVT, 0x28, data, sizeof(data));
	cxd2841er_unfreeze_regs(priv);

	reg = ((u32)data[0] << 8) | (u32)data[1];
	if (reg == 0) {
		dev_dbg(&priv->i2c->dev,
			"%s(): reg value out of range\n", __func__);
		return 0;
	}
	if (reg > 4996)
		reg = 4996;
	*snr = 100 * ((INTLOG10X100(reg) - INTLOG10X100(5350 - reg)) + 285);
	return 0;
}

static int cxd2841er_read_snr_t2(struct cxd2841er_priv *priv, u32 *snr)
{
	u32 reg;
	u8 data[2];

	*snr = 0;
	if (priv->state != STATE_ACTIVE_TC) {
		dev_dbg(&priv->i2c->dev,
			"%s(): invalid state %d\n", __func__, priv->state);
		return -EINVAL;
	}

	cxd2841er_freeze_regs(priv);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x20);
	cxd2841er_read_regs(priv, I2C_SLVT, 0x28, data, sizeof(data));
	cxd2841er_unfreeze_regs(priv);

	reg = ((u32)data[0] << 8) | (u32)data[1];
	if (reg == 0) {
		dev_dbg(&priv->i2c->dev,
			"%s(): reg value out of range\n", __func__);
		return 0;
	}
	if (reg > 10876)
		reg = 10876;
	*snr = 100 * ((INTLOG10X100(reg) - INTLOG10X100(12600 - reg)) + 320);
	return 0;
}

static int cxd2841er_read_snr_i(struct cxd2841er_priv *priv, u32 *snr)
{
	u32 reg;
	u8 data[2];

	*snr = 0;
	if (priv->state != STATE_ACTIVE_TC) {
		dev_dbg(&priv->i2c->dev,
				"%s(): invalid state %d\n", __func__,
				priv->state);
		return -EINVAL;
	}

	cxd2841er_freeze_regs(priv);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x60);
	cxd2841er_read_regs(priv, I2C_SLVT, 0x28, data, sizeof(data));
	cxd2841er_unfreeze_regs(priv);

	reg = ((u32)data[0] << 8) | (u32)data[1];
	if (reg == 0) {
		dev_dbg(&priv->i2c->dev,
				"%s(): reg value out of range\n", __func__);
		return 0;
	}
	*snr = 10000 * (intlog10(reg) >> 24) - 9031;
	return 0;
}

static u16 cxd2841er_read_agc_gain_c(struct cxd2841er_priv *priv,
					u8 delsys)
{
	u8 data[2];

	cxd2841er_write_reg(
		priv, I2C_SLVT, 0x00, 0x40);
	cxd2841er_read_regs(priv, I2C_SLVT, 0x49, data, 2);
	dev_dbg(&priv->i2c->dev,
			"%s(): AGC value=%u\n",
			__func__, (((u16)data[0] & 0x0F) << 8) |
			(u16)(data[1] & 0xFF));
	return ((((u16)data[0] & 0x0F) << 8) | (u16)(data[1] & 0xFF)) << 4;
}

static u16 cxd2841er_read_agc_gain_t_t2(struct cxd2841er_priv *priv,
					u8 delsys)
{
	u8 data[2];

	cxd2841er_write_reg(
		priv, I2C_SLVT, 0x00, (delsys == SYS_DVBT ? 0x10 : 0x20));
	cxd2841er_read_regs(priv, I2C_SLVT, 0x26, data, 2);
	dev_dbg(&priv->i2c->dev,
			"%s(): AGC value=%u\n",
			__func__, (((u16)data[0] & 0x0F) << 8) |
			(u16)(data[1] & 0xFF));
	return ((((u16)data[0] & 0x0F) << 8) | (u16)(data[1] & 0xFF)) << 4;
}

static u16 cxd2841er_read_agc_gain_i(struct cxd2841er_priv *priv,
		u8 delsys)
{
	u8 data[2];

	cxd2841er_write_reg(
			priv, I2C_SLVT, 0x00, 0x60);
	cxd2841er_read_regs(priv, I2C_SLVT, 0x26, data, 2);

	dev_dbg(&priv->i2c->dev,
			"%s(): AGC value=%u\n",
			__func__, (((u16)data[0] & 0x0F) << 8) |
			(u16)(data[1] & 0xFF));
	return ((((u16)data[0] & 0x0F) << 8) | (u16)(data[1] & 0xFF)) << 4;
}

static u16 cxd2841er_read_agc_gain_s(struct cxd2841er_priv *priv)
{
	u8 data[2];

	/* Set SLV-T Bank : 0xA0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xa0);
	/*
	 *  slave     Bank      Addr      Bit       Signal name
	 * <SLV-T>    A0h       1Fh       [4:0]     IRFAGC_GAIN[12:8]
	 * <SLV-T>    A0h       20h       [7:0]     IRFAGC_GAIN[7:0]
	 */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x1f, data, 2);
	return ((((u16)data[0] & 0x1F) << 8) | (u16)(data[1] & 0xFF)) << 3;
}

static void cxd2841er_read_ber(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct cxd2841er_priv *priv = fe->demodulator_priv;
	u32 ret, bit_error = 0, bit_count = 0;

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	switch (p->delivery_system) {
	case SYS_DVBC_ANNEX_A:
	case SYS_DVBC_ANNEX_B:
	case SYS_DVBC_ANNEX_C:
		ret = cxd2841er_read_ber_c(priv, &bit_error, &bit_count);
		break;
	case SYS_ISDBT:
		ret = cxd2841er_read_ber_i(priv, &bit_error, &bit_count);
		break;
	case SYS_DVBS:
		ret = cxd2841er_mon_read_ber_s(priv, &bit_error, &bit_count);
		break;
	case SYS_DVBS2:
		ret = cxd2841er_mon_read_ber_s2(priv, &bit_error, &bit_count);
		break;
	case SYS_DVBT:
		ret = cxd2841er_read_ber_t(priv, &bit_error, &bit_count);
		break;
	case SYS_DVBT2:
		ret = cxd2841er_read_ber_t2(priv, &bit_error, &bit_count);
		break;
	default:
		p->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		p->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		return;
	}

	if (!ret) {
		p->post_bit_error.stat[0].scale = FE_SCALE_COUNTER;
		p->post_bit_error.stat[0].uvalue += bit_error;
		p->post_bit_count.stat[0].scale = FE_SCALE_COUNTER;
		p->post_bit_count.stat[0].uvalue += bit_count;
	} else {
		p->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		p->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	}
}

static void cxd2841er_read_signal_strength(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct cxd2841er_priv *priv = fe->demodulator_priv;
	s32 strength;

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	switch (p->delivery_system) {
	case SYS_DVBT:
	case SYS_DVBT2:
		strength = cxd2841er_read_agc_gain_t_t2(priv,
							p->delivery_system);
		p->strength.stat[0].scale = FE_SCALE_DECIBEL;
		/* Formula was empirically determinated @ 410 MHz */
		p->strength.stat[0].uvalue = strength * 366 / 100 - 89520;
		break;	/* Code moved out of the function */
	case SYS_DVBC_ANNEX_A:
	case SYS_DVBC_ANNEX_B:
	case SYS_DVBC_ANNEX_C:
		strength = cxd2841er_read_agc_gain_c(priv,
							p->delivery_system);
		p->strength.stat[0].scale = FE_SCALE_DECIBEL;
		/*
		 * Formula was empirically determinated via linear regression,
		 * using frequencies: 175 MHz, 410 MHz and 800 MHz, and a
		 * stream modulated with QAM64
		 */
		p->strength.stat[0].uvalue = strength * 4045 / 1000 - 85224;
		break;
	case SYS_ISDBT:
		strength = cxd2841er_read_agc_gain_i(priv, p->delivery_system);
		p->strength.stat[0].scale = FE_SCALE_DECIBEL;
		/*
		 * Formula was empirically determinated via linear regression,
		 * using frequencies: 175 MHz, 410 MHz and 800 MHz.
		 */
		p->strength.stat[0].uvalue = strength * 3775 / 1000 - 90185;
		break;
	case SYS_DVBS:
	case SYS_DVBS2:
		strength = 65535 - cxd2841er_read_agc_gain_s(priv);
		p->strength.stat[0].scale = FE_SCALE_RELATIVE;
		p->strength.stat[0].uvalue = strength;
		break;
	default:
		p->strength.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		break;
	}
}

static void cxd2841er_read_snr(struct dvb_frontend *fe)
{
	u32 tmp = 0;
	int ret = 0;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct cxd2841er_priv *priv = fe->demodulator_priv;

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	switch (p->delivery_system) {
	case SYS_DVBC_ANNEX_A:
	case SYS_DVBC_ANNEX_B:
	case SYS_DVBC_ANNEX_C:
		ret = cxd2841er_read_snr_c(priv, &tmp);
		break;
	case SYS_DVBT:
		ret = cxd2841er_read_snr_t(priv, &tmp);
		break;
	case SYS_DVBT2:
		ret = cxd2841er_read_snr_t2(priv, &tmp);
		break;
	case SYS_ISDBT:
		ret = cxd2841er_read_snr_i(priv, &tmp);
		break;
	case SYS_DVBS:
	case SYS_DVBS2:
		ret = cxd2841er_dvbs_read_snr(priv, p->delivery_system, &tmp);
		break;
	default:
		dev_dbg(&priv->i2c->dev, "%s(): unknown delivery system %d\n",
			__func__, p->delivery_system);
		p->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		return;
	}

	dev_dbg(&priv->i2c->dev, "%s(): snr=%d\n",
			__func__, (int32_t)tmp);

	if (!ret) {
		p->cnr.stat[0].scale = FE_SCALE_DECIBEL;
		p->cnr.stat[0].svalue = tmp;
	} else {
		p->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	}
}

static void cxd2841er_read_ucblocks(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct cxd2841er_priv *priv = fe->demodulator_priv;
	u32 ucblocks = 0;

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	switch (p->delivery_system) {
	case SYS_DVBC_ANNEX_A:
	case SYS_DVBC_ANNEX_B:
	case SYS_DVBC_ANNEX_C:
		cxd2841er_read_packet_errors_c(priv, &ucblocks);
		break;
	case SYS_DVBT:
		cxd2841er_read_packet_errors_t(priv, &ucblocks);
		break;
	case SYS_DVBT2:
		cxd2841er_read_packet_errors_t2(priv, &ucblocks);
		break;
	case SYS_ISDBT:
		cxd2841er_read_packet_errors_i(priv, &ucblocks);
		break;
	default:
		p->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		return;
	}
	dev_dbg(&priv->i2c->dev, "%s() ucblocks=%u\n", __func__, ucblocks);

	p->block_error.stat[0].scale = FE_SCALE_COUNTER;
	p->block_error.stat[0].uvalue = ucblocks;
}

static int cxd2841er_dvbt2_set_profile(
	struct cxd2841er_priv *priv, enum cxd2841er_dvbt2_profile_t profile)
{
	u8 tune_mode;
	u8 seq_not2d_time;

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	switch (profile) {
	case DVBT2_PROFILE_BASE:
		tune_mode = 0x01;
		/* Set early unlock time */
		seq_not2d_time = (priv->xtal == SONY_XTAL_24000)?0x0E:0x0C;
		break;
	case DVBT2_PROFILE_LITE:
		tune_mode = 0x05;
		/* Set early unlock time */
		seq_not2d_time = (priv->xtal == SONY_XTAL_24000)?0x2E:0x28;
		break;
	case DVBT2_PROFILE_ANY:
		tune_mode = 0x00;
		/* Set early unlock time */
		seq_not2d_time = (priv->xtal == SONY_XTAL_24000)?0x2E:0x28;
		break;
	default:
		return -EINVAL;
	}
	/* Set SLV-T Bank : 0x2E */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x2e);
	/* Set profile and tune mode */
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0x10, tune_mode, 0x07);
	/* Set SLV-T Bank : 0x2B */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x2b);
	/* Set early unlock detection time */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x9d, seq_not2d_time);
	return 0;
}

static int cxd2841er_dvbt2_set_plp_config(struct cxd2841er_priv *priv,
					  u8 is_auto, u8 plp_id)
{
	if (is_auto) {
		dev_dbg(&priv->i2c->dev,
			"%s() using auto PLP selection\n", __func__);
	} else {
		dev_dbg(&priv->i2c->dev,
			"%s() using manual PLP selection, ID %d\n",
			__func__, plp_id);
	}
	/* Set SLV-T Bank : 0x23 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x23);
	if (!is_auto) {
		/* Manual PLP selection mode. Set the data PLP Id. */
		cxd2841er_write_reg(priv, I2C_SLVT, 0xaf, plp_id);
	}
	/* Auto PLP select (Scanning mode = 0x00). Data PLP select = 0x01. */
	cxd2841er_write_reg(priv, I2C_SLVT, 0xad, (is_auto ? 0x00 : 0x01));
	return 0;
}

static int cxd2841er_sleep_tc_to_active_t2_band(struct cxd2841er_priv *priv,
						u32 bandwidth)
{
	u32 iffreq, ifhz;
	u8 data[MAX_WRITE_REGSIZE];

	static const uint8_t nominalRate8bw[3][5] = {
		/* TRCG Nominal Rate [37:0] */
		{0x11, 0xF0, 0x00, 0x00, 0x00}, /* 20.5MHz XTal */
		{0x15, 0x00, 0x00, 0x00, 0x00}, /* 24MHz XTal */
		{0x11, 0xF0, 0x00, 0x00, 0x00}  /* 41MHz XTal */
	};

	static const uint8_t nominalRate7bw[3][5] = {
		/* TRCG Nominal Rate [37:0] */
		{0x14, 0x80, 0x00, 0x00, 0x00}, /* 20.5MHz XTal */
		{0x18, 0x00, 0x00, 0x00, 0x00}, /* 24MHz XTal */
		{0x14, 0x80, 0x00, 0x00, 0x00}  /* 41MHz XTal */
	};

	static const uint8_t nominalRate6bw[3][5] = {
		/* TRCG Nominal Rate [37:0] */
		{0x17, 0xEA, 0xAA, 0xAA, 0xAA}, /* 20.5MHz XTal */
		{0x1C, 0x00, 0x00, 0x00, 0x00}, /* 24MHz XTal */
		{0x17, 0xEA, 0xAA, 0xAA, 0xAA}  /* 41MHz XTal */
	};

	static const uint8_t nominalRate5bw[3][5] = {
		/* TRCG Nominal Rate [37:0] */
		{0x1C, 0xB3, 0x33, 0x33, 0x33}, /* 20.5MHz XTal */
		{0x21, 0x99, 0x99, 0x99, 0x99}, /* 24MHz XTal */
		{0x1C, 0xB3, 0x33, 0x33, 0x33}  /* 41MHz XTal */
	};

	static const uint8_t nominalRate17bw[3][5] = {
		/* TRCG Nominal Rate [37:0] */
		{0x58, 0xE2, 0xAF, 0xE0, 0xBC}, /* 20.5MHz XTal */
		{0x68, 0x0F, 0xA2, 0x32, 0xD0}, /* 24MHz XTal */
		{0x58, 0xE2, 0xAF, 0xE0, 0xBC}  /* 41MHz XTal */
	};

	static const uint8_t itbCoef8bw[3][14] = {
		{0x26, 0xAF, 0x06, 0xCD, 0x13, 0xBB, 0x28, 0xBA,
			0x23, 0xA9, 0x1F, 0xA8, 0x2C, 0xC8}, /* 20.5MHz XTal */
		{0x2F, 0xBA, 0x28, 0x9B, 0x28, 0x9D, 0x28, 0xA1,
			0x29, 0xA5, 0x2A, 0xAC, 0x29, 0xB5}, /* 24MHz XTal   */
		{0x26, 0xAF, 0x06, 0xCD, 0x13, 0xBB, 0x28, 0xBA,
			0x23, 0xA9, 0x1F, 0xA8, 0x2C, 0xC8}  /* 41MHz XTal   */
	};

	static const uint8_t itbCoef7bw[3][14] = {
		{0x2C, 0xBD, 0x02, 0xCF, 0x04, 0xF8, 0x23, 0xA6,
			0x29, 0xB0, 0x26, 0xA9, 0x21, 0xA5}, /* 20.5MHz XTal */
		{0x30, 0xB1, 0x29, 0x9A, 0x28, 0x9C, 0x28, 0xA0,
			0x29, 0xA2, 0x2B, 0xA6, 0x2B, 0xAD}, /* 24MHz XTal   */
		{0x2C, 0xBD, 0x02, 0xCF, 0x04, 0xF8, 0x23, 0xA6,
			0x29, 0xB0, 0x26, 0xA9, 0x21, 0xA5}  /* 41MHz XTal   */
	};

	static const uint8_t itbCoef6bw[3][14] = {
		{0x27, 0xA7, 0x28, 0xB3, 0x02, 0xF0, 0x01, 0xE8,
			0x00, 0xCF, 0x00, 0xE6, 0x23, 0xA4}, /* 20.5MHz XTal */
		{0x31, 0xA8, 0x29, 0x9B, 0x27, 0x9C, 0x28, 0x9E,
			0x29, 0xA4, 0x29, 0xA2, 0x29, 0xA8}, /* 24MHz XTal   */
		{0x27, 0xA7, 0x28, 0xB3, 0x02, 0xF0, 0x01, 0xE8,
			0x00, 0xCF, 0x00, 0xE6, 0x23, 0xA4}  /* 41MHz XTal   */
	};

	static const uint8_t itbCoef5bw[3][14] = {
		{0x27, 0xA7, 0x28, 0xB3, 0x02, 0xF0, 0x01, 0xE8,
			0x00, 0xCF, 0x00, 0xE6, 0x23, 0xA4}, /* 20.5MHz XTal */
		{0x31, 0xA8, 0x29, 0x9B, 0x27, 0x9C, 0x28, 0x9E,
			0x29, 0xA4, 0x29, 0xA2, 0x29, 0xA8}, /* 24MHz XTal   */
		{0x27, 0xA7, 0x28, 0xB3, 0x02, 0xF0, 0x01, 0xE8,
			0x00, 0xCF, 0x00, 0xE6, 0x23, 0xA4}  /* 41MHz XTal   */
	};

	static const uint8_t itbCoef17bw[3][14] = {
		{0x25, 0xA0, 0x36, 0x8D, 0x2E, 0x94, 0x28, 0x9B,
			0x32, 0x90, 0x2C, 0x9D, 0x29, 0x99}, /* 20.5MHz XTal */
		{0x33, 0x8E, 0x2B, 0x97, 0x2D, 0x95, 0x37, 0x8B,
			0x30, 0x97, 0x2D, 0x9A, 0x21, 0xA4}, /* 24MHz XTal   */
		{0x25, 0xA0, 0x36, 0x8D, 0x2E, 0x94, 0x28, 0x9B,
			0x32, 0x90, 0x2C, 0x9D, 0x29, 0x99}  /* 41MHz XTal   */
	};

	/* Set SLV-T Bank : 0x20 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x20);

	switch (bandwidth) {
	case 8000000:
		/* <Timing Recovery setting> */
		cxd2841er_write_regs(priv, I2C_SLVT,
				0x9F, nominalRate8bw[priv->xtal], 5);

		/* Set SLV-T Bank : 0x27 */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x27);
		cxd2841er_set_reg_bits(priv, I2C_SLVT,
				0x7a, 0x00, 0x0f);

		/* Set SLV-T Bank : 0x10 */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x10);

		/* Group delay equaliser settings for
		 * ASCOT2D, ASCOT2E and ASCOT3 tuners
		 */
		if (priv->flags & CXD2841ER_ASCOT)
			cxd2841er_write_regs(priv, I2C_SLVT,
				0xA6, itbCoef8bw[priv->xtal], 14);
		/* <IF freq setting> */
		ifhz = cxd2841er_get_if_hz(priv, 4800000);
		iffreq = cxd2841er_calc_iffreq_xtal(priv->xtal, ifhz);
		data[0] = (u8) ((iffreq >> 16) & 0xff);
		data[1] = (u8)((iffreq >> 8) & 0xff);
		data[2] = (u8)(iffreq & 0xff);
		cxd2841er_write_regs(priv, I2C_SLVT, 0xB6, data, 3);
		/* System bandwidth setting */
		cxd2841er_set_reg_bits(
				priv, I2C_SLVT, 0xD7, 0x00, 0x07);
		break;
	case 7000000:
		/* <Timing Recovery setting> */
		cxd2841er_write_regs(priv, I2C_SLVT,
				0x9F, nominalRate7bw[priv->xtal], 5);

		/* Set SLV-T Bank : 0x27 */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x27);
		cxd2841er_set_reg_bits(priv, I2C_SLVT,
				0x7a, 0x00, 0x0f);

		/* Set SLV-T Bank : 0x10 */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x10);

		/* Group delay equaliser settings for
		 * ASCOT2D, ASCOT2E and ASCOT3 tuners
		 */
		if (priv->flags & CXD2841ER_ASCOT)
			cxd2841er_write_regs(priv, I2C_SLVT,
				0xA6, itbCoef7bw[priv->xtal], 14);
		/* <IF freq setting> */
		ifhz = cxd2841er_get_if_hz(priv, 4200000);
		iffreq = cxd2841er_calc_iffreq_xtal(priv->xtal, ifhz);
		data[0] = (u8) ((iffreq >> 16) & 0xff);
		data[1] = (u8)((iffreq >> 8) & 0xff);
		data[2] = (u8)(iffreq & 0xff);
		cxd2841er_write_regs(priv, I2C_SLVT, 0xB6, data, 3);
		/* System bandwidth setting */
		cxd2841er_set_reg_bits(
				priv, I2C_SLVT, 0xD7, 0x02, 0x07);
		break;
	case 6000000:
		/* <Timing Recovery setting> */
		cxd2841er_write_regs(priv, I2C_SLVT,
				0x9F, nominalRate6bw[priv->xtal], 5);

		/* Set SLV-T Bank : 0x27 */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x27);
		cxd2841er_set_reg_bits(priv, I2C_SLVT,
				0x7a, 0x00, 0x0f);

		/* Set SLV-T Bank : 0x10 */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x10);

		/* Group delay equaliser settings for
		 * ASCOT2D, ASCOT2E and ASCOT3 tuners
		 */
		if (priv->flags & CXD2841ER_ASCOT)
			cxd2841er_write_regs(priv, I2C_SLVT,
				0xA6, itbCoef6bw[priv->xtal], 14);
		/* <IF freq setting> */
		ifhz = cxd2841er_get_if_hz(priv, 3600000);
		iffreq = cxd2841er_calc_iffreq_xtal(priv->xtal, ifhz);
		data[0] = (u8) ((iffreq >> 16) & 0xff);
		data[1] = (u8)((iffreq >> 8) & 0xff);
		data[2] = (u8)(iffreq & 0xff);
		cxd2841er_write_regs(priv, I2C_SLVT, 0xB6, data, 3);
		/* System bandwidth setting */
		cxd2841er_set_reg_bits(
				priv, I2C_SLVT, 0xD7, 0x04, 0x07);
		break;
	case 5000000:
		/* <Timing Recovery setting> */
		cxd2841er_write_regs(priv, I2C_SLVT,
				0x9F, nominalRate5bw[priv->xtal], 5);

		/* Set SLV-T Bank : 0x27 */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x27);
		cxd2841er_set_reg_bits(priv, I2C_SLVT,
				0x7a, 0x00, 0x0f);

		/* Set SLV-T Bank : 0x10 */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x10);

		/* Group delay equaliser settings for
		 * ASCOT2D, ASCOT2E and ASCOT3 tuners
		 */
		if (priv->flags & CXD2841ER_ASCOT)
			cxd2841er_write_regs(priv, I2C_SLVT,
				0xA6, itbCoef5bw[priv->xtal], 14);
		/* <IF freq setting> */
		ifhz = cxd2841er_get_if_hz(priv, 3600000);
		iffreq = cxd2841er_calc_iffreq_xtal(priv->xtal, ifhz);
		data[0] = (u8) ((iffreq >> 16) & 0xff);
		data[1] = (u8)((iffreq >> 8) & 0xff);
		data[2] = (u8)(iffreq & 0xff);
		cxd2841er_write_regs(priv, I2C_SLVT, 0xB6, data, 3);
		/* System bandwidth setting */
		cxd2841er_set_reg_bits(
				priv, I2C_SLVT, 0xD7, 0x06, 0x07);
		break;
	case 1712000:
		/* <Timing Recovery setting> */
		cxd2841er_write_regs(priv, I2C_SLVT,
				0x9F, nominalRate17bw[priv->xtal], 5);

		/* Set SLV-T Bank : 0x27 */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x27);
		cxd2841er_set_reg_bits(priv, I2C_SLVT,
				0x7a, 0x03, 0x0f);

		/* Set SLV-T Bank : 0x10 */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x10);

		/* Group delay equaliser settings for
		 * ASCOT2D, ASCOT2E and ASCOT3 tuners
		 */
		if (priv->flags & CXD2841ER_ASCOT)
			cxd2841er_write_regs(priv, I2C_SLVT,
				0xA6, itbCoef17bw[priv->xtal], 14);
		/* <IF freq setting> */
		ifhz = cxd2841er_get_if_hz(priv, 3500000);
		iffreq = cxd2841er_calc_iffreq_xtal(priv->xtal, ifhz);
		data[0] = (u8) ((iffreq >> 16) & 0xff);
		data[1] = (u8)((iffreq >> 8) & 0xff);
		data[2] = (u8)(iffreq & 0xff);
		cxd2841er_write_regs(priv, I2C_SLVT, 0xB6, data, 3);
		/* System bandwidth setting */
		cxd2841er_set_reg_bits(
				priv, I2C_SLVT, 0xD7, 0x03, 0x07);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int cxd2841er_sleep_tc_to_active_t_band(
		struct cxd2841er_priv *priv, u32 bandwidth)
{
	u8 data[MAX_WRITE_REGSIZE];
	u32 iffreq, ifhz;
	static const u8 nominalRate8bw[3][5] = {
		/* TRCG Nominal Rate [37:0] */
		{0x11, 0xF0, 0x00, 0x00, 0x00}, /* 20.5MHz XTal */
		{0x15, 0x00, 0x00, 0x00, 0x00}, /* 24MHz XTal */
		{0x11, 0xF0, 0x00, 0x00, 0x00}  /* 41MHz XTal */
	};
	static const u8 nominalRate7bw[3][5] = {
		/* TRCG Nominal Rate [37:0] */
		{0x14, 0x80, 0x00, 0x00, 0x00}, /* 20.5MHz XTal */
		{0x18, 0x00, 0x00, 0x00, 0x00}, /* 24MHz XTal */
		{0x14, 0x80, 0x00, 0x00, 0x00}  /* 41MHz XTal */
	};
	static const u8 nominalRate6bw[3][5] = {
		/* TRCG Nominal Rate [37:0] */
		{0x17, 0xEA, 0xAA, 0xAA, 0xAA}, /* 20.5MHz XTal */
		{0x1C, 0x00, 0x00, 0x00, 0x00}, /* 24MHz XTal */
		{0x17, 0xEA, 0xAA, 0xAA, 0xAA}  /* 41MHz XTal */
	};
	static const u8 nominalRate5bw[3][5] = {
		/* TRCG Nominal Rate [37:0] */
		{0x1C, 0xB3, 0x33, 0x33, 0x33}, /* 20.5MHz XTal */
		{0x21, 0x99, 0x99, 0x99, 0x99}, /* 24MHz XTal */
		{0x1C, 0xB3, 0x33, 0x33, 0x33}  /* 41MHz XTal */
	};

	static const u8 itbCoef8bw[3][14] = {
		{0x26, 0xAF, 0x06, 0xCD, 0x13, 0xBB, 0x28, 0xBA, 0x23, 0xA9,
			0x1F, 0xA8, 0x2C, 0xC8}, /* 20.5MHz XTal */
		{0x2F, 0xBA, 0x28, 0x9B, 0x28, 0x9D, 0x28, 0xA1, 0x29, 0xA5,
			0x2A, 0xAC, 0x29, 0xB5}, /* 24MHz XTal   */
		{0x26, 0xAF, 0x06, 0xCD, 0x13, 0xBB, 0x28, 0xBA, 0x23, 0xA9,
			0x1F, 0xA8, 0x2C, 0xC8}  /* 41MHz XTal   */
	};
	static const u8 itbCoef7bw[3][14] = {
		{0x2C, 0xBD, 0x02, 0xCF, 0x04, 0xF8, 0x23, 0xA6, 0x29, 0xB0,
			0x26, 0xA9, 0x21, 0xA5}, /* 20.5MHz XTal */
		{0x30, 0xB1, 0x29, 0x9A, 0x28, 0x9C, 0x28, 0xA0, 0x29, 0xA2,
			0x2B, 0xA6, 0x2B, 0xAD}, /* 24MHz XTal   */
		{0x2C, 0xBD, 0x02, 0xCF, 0x04, 0xF8, 0x23, 0xA6, 0x29, 0xB0,
			0x26, 0xA9, 0x21, 0xA5}  /* 41MHz XTal   */
	};
	static const u8 itbCoef6bw[3][14] = {
		{0x27, 0xA7, 0x28, 0xB3, 0x02, 0xF0, 0x01, 0xE8, 0x00, 0xCF,
			0x00, 0xE6, 0x23, 0xA4}, /* 20.5MHz XTal */
		{0x31, 0xA8, 0x29, 0x9B, 0x27, 0x9C, 0x28, 0x9E, 0x29, 0xA4,
			0x29, 0xA2, 0x29, 0xA8}, /* 24MHz XTal   */
		{0x27, 0xA7, 0x28, 0xB3, 0x02, 0xF0, 0x01, 0xE8, 0x00, 0xCF,
			0x00, 0xE6, 0x23, 0xA4}  /* 41MHz XTal   */
	};
	static const u8 itbCoef5bw[3][14] = {
		{0x27, 0xA7, 0x28, 0xB3, 0x02, 0xF0, 0x01, 0xE8, 0x00, 0xCF,
			0x00, 0xE6, 0x23, 0xA4}, /* 20.5MHz XTal */
		{0x31, 0xA8, 0x29, 0x9B, 0x27, 0x9C, 0x28, 0x9E, 0x29, 0xA4,
			0x29, 0xA2, 0x29, 0xA8}, /* 24MHz XTal   */
		{0x27, 0xA7, 0x28, 0xB3, 0x02, 0xF0, 0x01, 0xE8, 0x00, 0xCF,
			0x00, 0xE6, 0x23, 0xA4}  /* 41MHz XTal   */
	};

	/* Set SLV-T Bank : 0x13 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x13);
	/* Echo performance optimization setting */
	data[0] = 0x01;
	data[1] = 0x14;
	cxd2841er_write_regs(priv, I2C_SLVT, 0x9C, data, 2);

	/* Set SLV-T Bank : 0x10 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x10);

	switch (bandwidth) {
	case 8000000:
		/* <Timing Recovery setting> */
		cxd2841er_write_regs(priv, I2C_SLVT,
				0x9F, nominalRate8bw[priv->xtal], 5);
		/* Group delay equaliser settings for
		 * ASCOT2D, ASCOT2E and ASCOT3 tuners
		*/
		if (priv->flags & CXD2841ER_ASCOT)
			cxd2841er_write_regs(priv, I2C_SLVT,
				0xA6, itbCoef8bw[priv->xtal], 14);
		/* <IF freq setting> */
		ifhz = cxd2841er_get_if_hz(priv, 4800000);
		iffreq = cxd2841er_calc_iffreq_xtal(priv->xtal, ifhz);
		data[0] = (u8) ((iffreq >> 16) & 0xff);
		data[1] = (u8)((iffreq >> 8) & 0xff);
		data[2] = (u8)(iffreq & 0xff);
		cxd2841er_write_regs(priv, I2C_SLVT, 0xB6, data, 3);
		/* System bandwidth setting */
		cxd2841er_set_reg_bits(
			priv, I2C_SLVT, 0xD7, 0x00, 0x07);

		/* Demod core latency setting */
		if (priv->xtal == SONY_XTAL_24000) {
			data[0] = 0x15;
			data[1] = 0x28;
		} else {
			data[0] = 0x01;
			data[1] = 0xE0;
		}
		cxd2841er_write_regs(priv, I2C_SLVT, 0xD9, data, 2);

		/* Notch filter setting */
		data[0] = 0x01;
		data[1] = 0x02;
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x17);
		cxd2841er_write_regs(priv, I2C_SLVT, 0x38, data, 2);
		break;
	case 7000000:
		/* <Timing Recovery setting> */
		cxd2841er_write_regs(priv, I2C_SLVT,
				0x9F, nominalRate7bw[priv->xtal], 5);
		/* Group delay equaliser settings for
		 * ASCOT2D, ASCOT2E and ASCOT3 tuners
		*/
		if (priv->flags & CXD2841ER_ASCOT)
			cxd2841er_write_regs(priv, I2C_SLVT,
				0xA6, itbCoef7bw[priv->xtal], 14);
		/* <IF freq setting> */
		ifhz = cxd2841er_get_if_hz(priv, 4200000);
		iffreq = cxd2841er_calc_iffreq_xtal(priv->xtal, ifhz);
		data[0] = (u8) ((iffreq >> 16) & 0xff);
		data[1] = (u8)((iffreq >> 8) & 0xff);
		data[2] = (u8)(iffreq & 0xff);
		cxd2841er_write_regs(priv, I2C_SLVT, 0xB6, data, 3);
		/* System bandwidth setting */
		cxd2841er_set_reg_bits(
			priv, I2C_SLVT, 0xD7, 0x02, 0x07);

		/* Demod core latency setting */
		if (priv->xtal == SONY_XTAL_24000) {
			data[0] = 0x1F;
			data[1] = 0xF8;
		} else {
			data[0] = 0x12;
			data[1] = 0xF8;
		}
		cxd2841er_write_regs(priv, I2C_SLVT, 0xD9, data, 2);

		/* Notch filter setting */
		data[0] = 0x00;
		data[1] = 0x03;
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x17);
		cxd2841er_write_regs(priv, I2C_SLVT, 0x38, data, 2);
		break;
	case 6000000:
		/* <Timing Recovery setting> */
		cxd2841er_write_regs(priv, I2C_SLVT,
				0x9F, nominalRate6bw[priv->xtal], 5);
		/* Group delay equaliser settings for
		 * ASCOT2D, ASCOT2E and ASCOT3 tuners
		*/
		if (priv->flags & CXD2841ER_ASCOT)
			cxd2841er_write_regs(priv, I2C_SLVT,
				0xA6, itbCoef6bw[priv->xtal], 14);
		/* <IF freq setting> */
		ifhz = cxd2841er_get_if_hz(priv, 3600000);
		iffreq = cxd2841er_calc_iffreq_xtal(priv->xtal, ifhz);
		data[0] = (u8) ((iffreq >> 16) & 0xff);
		data[1] = (u8)((iffreq >> 8) & 0xff);
		data[2] = (u8)(iffreq & 0xff);
		cxd2841er_write_regs(priv, I2C_SLVT, 0xB6, data, 3);
		/* System bandwidth setting */
		cxd2841er_set_reg_bits(
			priv, I2C_SLVT, 0xD7, 0x04, 0x07);

		/* Demod core latency setting */
		if (priv->xtal == SONY_XTAL_24000) {
			data[0] = 0x25;
			data[1] = 0x4C;
		} else {
			data[0] = 0x1F;
			data[1] = 0xDC;
		}
		cxd2841er_write_regs(priv, I2C_SLVT, 0xD9, data, 2);

		/* Notch filter setting */
		data[0] = 0x00;
		data[1] = 0x03;
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x17);
		cxd2841er_write_regs(priv, I2C_SLVT, 0x38, data, 2);
		break;
	case 5000000:
		/* <Timing Recovery setting> */
		cxd2841er_write_regs(priv, I2C_SLVT,
				0x9F, nominalRate5bw[priv->xtal], 5);
		/* Group delay equaliser settings for
		 * ASCOT2D, ASCOT2E and ASCOT3 tuners
		*/
		if (priv->flags & CXD2841ER_ASCOT)
			cxd2841er_write_regs(priv, I2C_SLVT,
				0xA6, itbCoef5bw[priv->xtal], 14);
		/* <IF freq setting> */
		ifhz = cxd2841er_get_if_hz(priv, 3600000);
		iffreq = cxd2841er_calc_iffreq_xtal(priv->xtal, ifhz);
		data[0] = (u8) ((iffreq >> 16) & 0xff);
		data[1] = (u8)((iffreq >> 8) & 0xff);
		data[2] = (u8)(iffreq & 0xff);
		cxd2841er_write_regs(priv, I2C_SLVT, 0xB6, data, 3);
		/* System bandwidth setting */
		cxd2841er_set_reg_bits(
			priv, I2C_SLVT, 0xD7, 0x06, 0x07);

		/* Demod core latency setting */
		if (priv->xtal == SONY_XTAL_24000) {
			data[0] = 0x2C;
			data[1] = 0xC2;
		} else {
			data[0] = 0x26;
			data[1] = 0x3C;
		}
		cxd2841er_write_regs(priv, I2C_SLVT, 0xD9, data, 2);

		/* Notch filter setting */
		data[0] = 0x00;
		data[1] = 0x03;
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x17);
		cxd2841er_write_regs(priv, I2C_SLVT, 0x38, data, 2);
		break;
	}

	return 0;
}

static int cxd2841er_sleep_tc_to_active_i_band(
		struct cxd2841er_priv *priv, u32 bandwidth)
{
	u32 iffreq, ifhz;
	u8 data[3];

	/* TRCG Nominal Rate */
	static const u8 nominalRate8bw[3][5] = {
		{0x00, 0x00, 0x00, 0x00, 0x00}, /* 20.5MHz XTal */
		{0x11, 0xB8, 0x00, 0x00, 0x00}, /* 24MHz XTal */
		{0x00, 0x00, 0x00, 0x00, 0x00}  /* 41MHz XTal */
	};

	static const u8 nominalRate7bw[3][5] = {
		{0x00, 0x00, 0x00, 0x00, 0x00}, /* 20.5MHz XTal */
		{0x14, 0x40, 0x00, 0x00, 0x00}, /* 24MHz XTal */
		{0x00, 0x00, 0x00, 0x00, 0x00}  /* 41MHz XTal */
	};

	static const u8 nominalRate6bw[3][5] = {
		{0x14, 0x2E, 0x00, 0x00, 0x00}, /* 20.5MHz XTal */
		{0x17, 0xA0, 0x00, 0x00, 0x00}, /* 24MHz XTal */
		{0x14, 0x2E, 0x00, 0x00, 0x00}  /* 41MHz XTal */
	};

	static const u8 itbCoef8bw[3][14] = {
		{0x00}, /* 20.5MHz XTal */
		{0x2F, 0xBA, 0x28, 0x9B, 0x28, 0x9D, 0x28, 0xA1, 0x29,
			0xA5, 0x2A, 0xAC, 0x29, 0xB5}, /* 24MHz Xtal */
		{0x0}, /* 41MHz XTal   */
	};

	static const u8 itbCoef7bw[3][14] = {
		{0x00}, /* 20.5MHz XTal */
		{0x30, 0xB1, 0x29, 0x9A, 0x28, 0x9C, 0x28, 0xA0, 0x29,
			0xA2, 0x2B, 0xA6, 0x2B, 0xAD}, /* 24MHz Xtal */
		{0x00}, /* 41MHz XTal   */
	};

	static const u8 itbCoef6bw[3][14] = {
		{0x27, 0xA7, 0x28, 0xB3, 0x02, 0xF0, 0x01, 0xE8, 0x00,
			0xCF, 0x00, 0xE6, 0x23, 0xA4}, /* 20.5MHz XTal */
		{0x31, 0xA8, 0x29, 0x9B, 0x27, 0x9C, 0x28, 0x9E, 0x29,
			0xA4, 0x29, 0xA2, 0x29, 0xA8}, /* 24MHz Xtal   */
		{0x27, 0xA7, 0x28, 0xB3, 0x02, 0xF0, 0x01, 0xE8, 0x00,
			0xCF, 0x00, 0xE6, 0x23, 0xA4}, /* 41MHz XTal   */
	};

	dev_dbg(&priv->i2c->dev, "%s() bandwidth=%u\n", __func__, bandwidth);
	/* Set SLV-T Bank : 0x10 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x10);

	/*  20.5/41MHz Xtal support is not available
	 *  on ISDB-T 7MHzBW and 8MHzBW
	*/
	if (priv->xtal != SONY_XTAL_24000 && bandwidth > 6000000) {
		dev_err(&priv->i2c->dev,
			"%s(): bandwidth %d supported only for 24MHz xtal\n",
			__func__, bandwidth);
		return -EINVAL;
	}

	switch (bandwidth) {
	case 8000000:
		/* TRCG Nominal Rate */
		cxd2841er_write_regs(priv, I2C_SLVT,
				0x9F, nominalRate8bw[priv->xtal], 5);
		/*  Group delay equaliser settings for ASCOT tuners optimized */
		if (priv->flags & CXD2841ER_ASCOT)
			cxd2841er_write_regs(priv, I2C_SLVT,
				0xA6, itbCoef8bw[priv->xtal], 14);

		/* IF freq setting */
		ifhz = cxd2841er_get_if_hz(priv, 4750000);
		iffreq = cxd2841er_calc_iffreq_xtal(priv->xtal, ifhz);
		data[0] = (u8) ((iffreq >> 16) & 0xff);
		data[1] = (u8)((iffreq >> 8) & 0xff);
		data[2] = (u8)(iffreq & 0xff);
		cxd2841er_write_regs(priv, I2C_SLVT, 0xB6, data, 3);

		/* System bandwidth setting */
		cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xd7, 0x0, 0x7);

		/* Demod core latency setting */
		data[0] = 0x13;
		data[1] = 0xFC;
		cxd2841er_write_regs(priv, I2C_SLVT, 0xD9, data, 2);

		/* Acquisition optimization setting */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x12);
		cxd2841er_set_reg_bits(priv, I2C_SLVT, 0x71, 0x03, 0x07);
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x15);
		cxd2841er_write_reg(priv, I2C_SLVT, 0xBE, 0x03);
		break;
	case 7000000:
		/* TRCG Nominal Rate */
		cxd2841er_write_regs(priv, I2C_SLVT,
				0x9F, nominalRate7bw[priv->xtal], 5);
		/*  Group delay equaliser settings for ASCOT tuners optimized */
		if (priv->flags & CXD2841ER_ASCOT)
			cxd2841er_write_regs(priv, I2C_SLVT,
				0xA6, itbCoef7bw[priv->xtal], 14);

		/* IF freq setting */
		ifhz = cxd2841er_get_if_hz(priv, 4150000);
		iffreq = cxd2841er_calc_iffreq_xtal(priv->xtal, ifhz);
		data[0] = (u8) ((iffreq >> 16) & 0xff);
		data[1] = (u8)((iffreq >> 8) & 0xff);
		data[2] = (u8)(iffreq & 0xff);
		cxd2841er_write_regs(priv, I2C_SLVT, 0xB6, data, 3);

		/* System bandwidth setting */
		cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xd7, 0x02, 0x7);

		/* Demod core latency setting */
		data[0] = 0x1A;
		data[1] = 0xFA;
		cxd2841er_write_regs(priv, I2C_SLVT, 0xD9, data, 2);

		/* Acquisition optimization setting */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x12);
		cxd2841er_set_reg_bits(priv, I2C_SLVT, 0x71, 0x03, 0x07);
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x15);
		cxd2841er_write_reg(priv, I2C_SLVT, 0xBE, 0x02);
		break;
	case 6000000:
		/* TRCG Nominal Rate */
		cxd2841er_write_regs(priv, I2C_SLVT,
				0x9F, nominalRate6bw[priv->xtal], 5);
		/*  Group delay equaliser settings for ASCOT tuners optimized */
		if (priv->flags & CXD2841ER_ASCOT)
			cxd2841er_write_regs(priv, I2C_SLVT,
				0xA6, itbCoef6bw[priv->xtal], 14);

		/* IF freq setting */
		ifhz = cxd2841er_get_if_hz(priv, 3550000);
		iffreq = cxd2841er_calc_iffreq_xtal(priv->xtal, ifhz);
		data[0] = (u8) ((iffreq >> 16) & 0xff);
		data[1] = (u8)((iffreq >> 8) & 0xff);
		data[2] = (u8)(iffreq & 0xff);
		cxd2841er_write_regs(priv, I2C_SLVT, 0xB6, data, 3);

		/* System bandwidth setting */
		cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xd7, 0x04, 0x7);

		/* Demod core latency setting */
		if (priv->xtal == SONY_XTAL_24000) {
			data[0] = 0x1F;
			data[1] = 0x79;
		} else {
			data[0] = 0x1A;
			data[1] = 0xE2;
		}
		cxd2841er_write_regs(priv, I2C_SLVT, 0xD9, data, 2);

		/* Acquisition optimization setting */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x12);
		cxd2841er_set_reg_bits(priv, I2C_SLVT, 0x71, 0x07, 0x07);
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x15);
		cxd2841er_write_reg(priv, I2C_SLVT, 0xBE, 0x02);
		break;
	default:
		dev_dbg(&priv->i2c->dev, "%s(): invalid bandwidth %d\n",
				__func__, bandwidth);
		return -EINVAL;
	}
	return 0;
}

static int cxd2841er_sleep_tc_to_active_c_band(struct cxd2841er_priv *priv,
					       u32 bandwidth)
{
	u8 bw7_8mhz_b10_a6[] = {
		0x2D, 0xC7, 0x04, 0xF4, 0x07, 0xC5, 0x2A, 0xB8,
		0x27, 0x9E, 0x27, 0xA4, 0x29, 0xAB };
	u8 bw6mhz_b10_a6[] = {
		0x27, 0xA7, 0x28, 0xB3, 0x02, 0xF0, 0x01, 0xE8,
		0x00, 0xCF, 0x00, 0xE6, 0x23, 0xA4 };
	u8 b10_b6[3];
	u32 iffreq, ifhz;

	if (bandwidth != 6000000 &&
			bandwidth != 7000000 &&
			bandwidth != 8000000) {
		dev_info(&priv->i2c->dev, "%s(): unsupported bandwidth %d. Forcing 8Mhz!\n",
				__func__, bandwidth);
		bandwidth = 8000000;
	}

	dev_dbg(&priv->i2c->dev, "%s() bw=%d\n", __func__, bandwidth);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x10);
	switch (bandwidth) {
	case 8000000:
	case 7000000:
		if (priv->flags & CXD2841ER_ASCOT)
			cxd2841er_write_regs(
				priv, I2C_SLVT, 0xa6,
				bw7_8mhz_b10_a6, sizeof(bw7_8mhz_b10_a6));
		ifhz = cxd2841er_get_if_hz(priv, 4900000);
		iffreq = cxd2841er_calc_iffreq(ifhz);
		break;
	case 6000000:
		if (priv->flags & CXD2841ER_ASCOT)
			cxd2841er_write_regs(
				priv, I2C_SLVT, 0xa6,
				bw6mhz_b10_a6, sizeof(bw6mhz_b10_a6));
		ifhz = cxd2841er_get_if_hz(priv, 3700000);
		iffreq = cxd2841er_calc_iffreq(ifhz);
		break;
	default:
		dev_err(&priv->i2c->dev, "%s(): unsupported bandwidth %d\n",
			__func__, bandwidth);
		return -EINVAL;
	}
	/* <IF freq setting> */
	b10_b6[0] = (u8) ((iffreq >> 16) & 0xff);
	b10_b6[1] = (u8)((iffreq >> 8) & 0xff);
	b10_b6[2] = (u8)(iffreq & 0xff);
	cxd2841er_write_regs(priv, I2C_SLVT, 0xb6, b10_b6, sizeof(b10_b6));
	/* Set SLV-T Bank : 0x11 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x11);
	switch (bandwidth) {
	case 8000000:
	case 7000000:
		cxd2841er_set_reg_bits(
			priv, I2C_SLVT, 0xa3, 0x00, 0x1f);
		break;
	case 6000000:
		cxd2841er_set_reg_bits(
			priv, I2C_SLVT, 0xa3, 0x14, 0x1f);
		break;
	}
	/* Set SLV-T Bank : 0x40 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x40);
	switch (bandwidth) {
	case 8000000:
		cxd2841er_set_reg_bits(
			priv, I2C_SLVT, 0x26, 0x0b, 0x0f);
		cxd2841er_write_reg(priv, I2C_SLVT,  0x27, 0x3e);
		break;
	case 7000000:
		cxd2841er_set_reg_bits(
			priv, I2C_SLVT, 0x26, 0x09, 0x0f);
		cxd2841er_write_reg(priv, I2C_SLVT,  0x27, 0xd6);
		break;
	case 6000000:
		cxd2841er_set_reg_bits(
			priv, I2C_SLVT, 0x26, 0x08, 0x0f);
		cxd2841er_write_reg(priv, I2C_SLVT,  0x27, 0x6e);
		break;
	}
	return 0;
}

static int cxd2841er_sleep_tc_to_active_t(struct cxd2841er_priv *priv,
					  u32 bandwidth)
{
	u8 data[2] = { 0x09, 0x54 };
	u8 data24m[3] = {0xDC, 0x6C, 0x00};

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	cxd2841er_set_ts_clock_mode(priv, SYS_DVBT);
	/* Set SLV-X Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x00, 0x00);
	/* Set demod mode */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x17, 0x01);
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* Enable demod clock */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x2c, 0x01);
	/* Disable RF level monitor */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x2f, 0x00);
	/* Enable ADC clock */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x30, 0x00);
	/* Enable ADC 1 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x41, 0x1a);
	/* Enable ADC 2 & 3 */
	if (priv->xtal == SONY_XTAL_41000) {
		data[0] = 0x0A;
		data[1] = 0xD4;
	}
	cxd2841er_write_regs(priv, I2C_SLVT, 0x43, data, 2);
	/* Enable ADC 4 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x18, 0x00);
	/* Set SLV-T Bank : 0x10 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x10);
	/* IFAGC gain settings */
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xd2, 0x0c, 0x1f);
	/* Set SLV-T Bank : 0x11 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x11);
	/* BBAGC TARGET level setting */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x6a, 0x50);
	/* Set SLV-T Bank : 0x10 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x10);
	/* ASCOT setting */
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xa5,
		((priv->flags & CXD2841ER_ASCOT) ? 0x01 : 0x00), 0x01);
	/* Set SLV-T Bank : 0x18 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x18);
	/* Pre-RS BER moniter setting */
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0x36, 0x40, 0x07);
	/* FEC Auto Recovery setting */
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0x30, 0x01, 0x01);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0x31, 0x01, 0x01);
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* TSIF setting */
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xce, 0x01, 0x01);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xcf, 0x01, 0x01);

	if (priv->xtal == SONY_XTAL_24000) {
		/* Set SLV-T Bank : 0x10 */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x10);
		cxd2841er_write_reg(priv, I2C_SLVT, 0xBF, 0x60);
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x18);
		cxd2841er_write_regs(priv, I2C_SLVT, 0x24, data24m, 3);
	}

	cxd2841er_sleep_tc_to_active_t_band(priv, bandwidth);
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* Disable HiZ Setting 1 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x80, 0x28);
	/* Disable HiZ Setting 2 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x81, 0x00);
	priv->state = STATE_ACTIVE_TC;
	return 0;
}

static int cxd2841er_sleep_tc_to_active_t2(struct cxd2841er_priv *priv,
					   u32 bandwidth)
{
	u8 data[MAX_WRITE_REGSIZE];

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	cxd2841er_set_ts_clock_mode(priv, SYS_DVBT2);
	/* Set SLV-X Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x00, 0x00);
	/* Set demod mode */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x17, 0x02);
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* Enable demod clock */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x2c, 0x01);
	/* Disable RF level monitor */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x59, 0x00);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x2f, 0x00);
	/* Enable ADC clock */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x30, 0x00);
	/* Enable ADC 1 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x41, 0x1a);

	if (priv->xtal == SONY_XTAL_41000) {
		data[0] = 0x0A;
		data[1] = 0xD4;
	} else {
		data[0] = 0x09;
		data[1] = 0x54;
	}

	cxd2841er_write_regs(priv, I2C_SLVT, 0x43, data, 2);
	/* Enable ADC 4 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x18, 0x00);
	/* Set SLV-T Bank : 0x10 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x10);
	/* IFAGC gain settings */
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xd2, 0x0c, 0x1f);
	/* Set SLV-T Bank : 0x11 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x11);
	/* BBAGC TARGET level setting */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x6a, 0x50);
	/* Set SLV-T Bank : 0x10 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x10);
	/* ASCOT setting */
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xa5,
		((priv->flags & CXD2841ER_ASCOT) ? 0x01 : 0x00), 0x01);
	/* Set SLV-T Bank : 0x20 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x20);
	/* Acquisition optimization setting */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x8b, 0x3c);
	/* Set SLV-T Bank : 0x2b */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x2b);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0x76, 0x20, 0x70);
	/* Set SLV-T Bank : 0x23 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x23);
	/* L1 Control setting */
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xE6, 0x00, 0x03);
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* TSIF setting */
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xce, 0x01, 0x01);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xcf, 0x01, 0x01);
	/* DVB-T2 initial setting */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x13);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x83, 0x10);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x86, 0x34);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0x9e, 0x09, 0x0f);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x9f, 0xd8);
	/* Set SLV-T Bank : 0x2a */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x2a);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0x38, 0x04, 0x0f);
	/* Set SLV-T Bank : 0x2b */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x2b);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0x11, 0x20, 0x3f);

	/* 24MHz Xtal setting */
	if (priv->xtal == SONY_XTAL_24000) {
		/* Set SLV-T Bank : 0x11 */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x11);
		data[0] = 0xEB;
		data[1] = 0x03;
		data[2] = 0x3B;
		cxd2841er_write_regs(priv, I2C_SLVT, 0x33, data, 3);

		/* Set SLV-T Bank : 0x20 */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x20);
		data[0] = 0x5E;
		data[1] = 0x5E;
		data[2] = 0x47;
		cxd2841er_write_regs(priv, I2C_SLVT, 0x95, data, 3);

		cxd2841er_write_reg(priv, I2C_SLVT, 0x99, 0x18);

		data[0] = 0x3F;
		data[1] = 0xFF;
		cxd2841er_write_regs(priv, I2C_SLVT, 0xD9, data, 2);

		/* Set SLV-T Bank : 0x24 */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x24);
		data[0] = 0x0B;
		data[1] = 0x72;
		cxd2841er_write_regs(priv, I2C_SLVT, 0x34, data, 2);

		data[0] = 0x93;
		data[1] = 0xF3;
		data[2] = 0x00;
		cxd2841er_write_regs(priv, I2C_SLVT, 0xD2, data, 3);

		data[0] = 0x05;
		data[1] = 0xB8;
		data[2] = 0xD8;
		cxd2841er_write_regs(priv, I2C_SLVT, 0xDD, data, 3);

		cxd2841er_write_reg(priv, I2C_SLVT, 0xE0, 0x00);

		/* Set SLV-T Bank : 0x25 */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x25);
		cxd2841er_write_reg(priv, I2C_SLVT, 0xED, 0x60);

		/* Set SLV-T Bank : 0x27 */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x27);
		cxd2841er_write_reg(priv, I2C_SLVT, 0xFA, 0x34);

		/* Set SLV-T Bank : 0x2B */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x2B);
		cxd2841er_write_reg(priv, I2C_SLVT, 0x4B, 0x2F);
		cxd2841er_write_reg(priv, I2C_SLVT, 0x9E, 0x0E);

		/* Set SLV-T Bank : 0x2D */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x2D);
		data[0] = 0x89;
		data[1] = 0x89;
		cxd2841er_write_regs(priv, I2C_SLVT, 0x24, data, 2);

		/* Set SLV-T Bank : 0x5E */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x5E);
		data[0] = 0x24;
		data[1] = 0x95;
		cxd2841er_write_regs(priv, I2C_SLVT, 0x8C, data, 2);
	}

	cxd2841er_sleep_tc_to_active_t2_band(priv, bandwidth);

	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* Disable HiZ Setting 1 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x80, 0x28);
	/* Disable HiZ Setting 2 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x81, 0x00);
	priv->state = STATE_ACTIVE_TC;
	return 0;
}

/* ISDB-Tb part */
static int cxd2841er_sleep_tc_to_active_i(struct cxd2841er_priv *priv,
		u32 bandwidth)
{
	u8 data[2] = { 0x09, 0x54 };
	u8 data24m[2] = {0x60, 0x00};
	u8 data24m2[3] = {0xB7, 0x1B, 0x00};

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	cxd2841er_set_ts_clock_mode(priv, SYS_DVBT);
	/* Set SLV-X Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x00, 0x00);
	/* Set demod mode */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x17, 0x06);
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* Enable demod clock */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x2c, 0x01);
	/* Enable RF level monitor */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x2f, 0x01);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x59, 0x01);
	/* Enable ADC clock */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x30, 0x00);
	/* Enable ADC 1 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x41, 0x1a);
	/* xtal freq 20.5MHz or 24M */
	cxd2841er_write_regs(priv, I2C_SLVT, 0x43, data, 2);
	/* Enable ADC 4 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x18, 0x00);
	/* ASCOT setting */
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xa5,
		((priv->flags & CXD2841ER_ASCOT) ? 0x01 : 0x00), 0x01);
	/* FEC Auto Recovery setting */
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0x30, 0x01, 0x01);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0x31, 0x00, 0x01);
	/* ISDB-T initial setting */
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xce, 0x00, 0x01);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xcf, 0x00, 0x01);
	/* Set SLV-T Bank : 0x10 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x10);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0x69, 0x04, 0x07);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0x6B, 0x03, 0x07);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0x9D, 0x50, 0xFF);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xD3, 0x06, 0x1F);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xED, 0x00, 0x01);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xE2, 0xCE, 0x80);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xF2, 0x13, 0x10);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xDE, 0x2E, 0x3F);
	/* Set SLV-T Bank : 0x15 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x15);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xDE, 0x02, 0x03);
	/* Set SLV-T Bank : 0x1E */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x1E);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0x73, 0x68, 0xFF);
	/* Set SLV-T Bank : 0x63 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x63);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0x81, 0x00, 0x01);

	/* for xtal 24MHz */
	/* Set SLV-T Bank : 0x10 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x10);
	cxd2841er_write_regs(priv, I2C_SLVT, 0xBF, data24m, 2);
	/* Set SLV-T Bank : 0x60 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x60);
	cxd2841er_write_regs(priv, I2C_SLVT, 0xA8, data24m2, 3);

	cxd2841er_sleep_tc_to_active_i_band(priv, bandwidth);
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* Disable HiZ Setting 1 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x80, 0x28);
	/* Disable HiZ Setting 2 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x81, 0x00);
	priv->state = STATE_ACTIVE_TC;
	return 0;
}

static int cxd2841er_sleep_tc_to_active_c(struct cxd2841er_priv *priv,
					  u32 bandwidth)
{
	u8 data[2] = { 0x09, 0x54 };

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	cxd2841er_set_ts_clock_mode(priv, SYS_DVBC_ANNEX_A);
	/* Set SLV-X Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x00, 0x00);
	/* Set demod mode */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x17, 0x04);
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* Enable demod clock */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x2c, 0x01);
	/* Disable RF level monitor */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x59, 0x00);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x2f, 0x00);
	/* Enable ADC clock */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x30, 0x00);
	/* Enable ADC 1 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x41, 0x1a);
	/* xtal freq 20.5MHz */
	cxd2841er_write_regs(priv, I2C_SLVT, 0x43, data, 2);
	/* Enable ADC 4 */
	cxd2841er_write_reg(priv, I2C_SLVX, 0x18, 0x00);
	/* Set SLV-T Bank : 0x10 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x10);
	/* IFAGC gain settings */
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xd2, 0x09, 0x1f);
	/* Set SLV-T Bank : 0x11 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x11);
	/* BBAGC TARGET level setting */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x6a, 0x48);
	/* Set SLV-T Bank : 0x10 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x10);
	/* ASCOT setting */
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xa5,
		((priv->flags & CXD2841ER_ASCOT) ? 0x01 : 0x00), 0x01);
	/* Set SLV-T Bank : 0x40 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x40);
	/* Demod setting */
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xc3, 0x00, 0x04);
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* TSIF setting */
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xce, 0x01, 0x01);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xcf, 0x01, 0x01);

	cxd2841er_sleep_tc_to_active_c_band(priv, bandwidth);
	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* Disable HiZ Setting 1 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x80, 0x28);
	/* Disable HiZ Setting 2 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x81, 0x00);
	priv->state = STATE_ACTIVE_TC;
	return 0;
}

static int cxd2841er_get_frontend(struct dvb_frontend *fe,
				  struct dtv_frontend_properties *p)
{
	enum fe_status status = 0;
	struct cxd2841er_priv *priv = fe->demodulator_priv;

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	if (priv->state == STATE_ACTIVE_S)
		cxd2841er_read_status_s(fe, &status);
	else if (priv->state == STATE_ACTIVE_TC)
		cxd2841er_read_status_tc(fe, &status);

	if (priv->state == STATE_ACTIVE_TC || priv->state == STATE_ACTIVE_S)
		cxd2841er_read_signal_strength(fe);
	else
		p->strength.stat[0].scale = FE_SCALE_NOT_AVAILABLE;

	if (status & FE_HAS_LOCK) {
		cxd2841er_read_snr(fe);
		cxd2841er_read_ucblocks(fe);

		cxd2841er_read_ber(fe);
	} else {
		p->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		p->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		p->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		p->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	}
	return 0;
}

static int cxd2841er_set_frontend_s(struct dvb_frontend *fe)
{
	int ret = 0, i, timeout, carr_offset;
	enum fe_status status;
	struct cxd2841er_priv *priv = fe->demodulator_priv;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	u32 symbol_rate = p->symbol_rate/1000;

	dev_dbg(&priv->i2c->dev, "%s(): %s frequency=%d symbol_rate=%d xtal=%d\n",
		__func__,
		(p->delivery_system == SYS_DVBS ? "DVB-S" : "DVB-S2"),
		 p->frequency, symbol_rate, priv->xtal);

	if (priv->flags & CXD2841ER_EARLY_TUNE)
		cxd2841er_tuner_set(fe);

	switch (priv->state) {
	case STATE_SLEEP_S:
		ret = cxd2841er_sleep_s_to_active_s(
			priv, p->delivery_system, symbol_rate);
		break;
	case STATE_ACTIVE_S:
		ret = cxd2841er_retune_active(priv, p);
		break;
	default:
		dev_dbg(&priv->i2c->dev, "%s(): invalid state %d\n",
			__func__, priv->state);
		ret = -EINVAL;
		goto done;
	}
	if (ret) {
		dev_dbg(&priv->i2c->dev, "%s(): tune failed\n", __func__);
		goto done;
	}

	if (!(priv->flags & CXD2841ER_EARLY_TUNE))
		cxd2841er_tuner_set(fe);

	cxd2841er_tune_done(priv);
	timeout = ((3000000 + (symbol_rate - 1)) / symbol_rate) + 150;

	i = 0;
	do {
		usleep_range(CXD2841ER_DVBS_POLLING_INVL*1000,
			(CXD2841ER_DVBS_POLLING_INVL + 2) * 1000);
		cxd2841er_read_status_s(fe, &status);
		if (status & FE_HAS_LOCK)
			break;
		i++;
	} while (i < timeout / CXD2841ER_DVBS_POLLING_INVL);

	if (status & FE_HAS_LOCK) {
		if (cxd2841er_get_carrier_offset_s_s2(
				priv, &carr_offset)) {
			ret = -EINVAL;
			goto done;
		}
		dev_dbg(&priv->i2c->dev, "%s(): carrier_offset=%d\n",
			__func__, carr_offset);
	}
done:
	/* Reset stats */
	p->strength.stat[0].scale = FE_SCALE_RELATIVE;
	p->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;

	return ret;
}

static int cxd2841er_set_frontend_tc(struct dvb_frontend *fe)
{
	int ret = 0, timeout;
	enum fe_status status;
	struct cxd2841er_priv *priv = fe->demodulator_priv;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;

	dev_dbg(&priv->i2c->dev, "%s() delivery_system=%d bandwidth_hz=%d\n",
		 __func__, p->delivery_system, p->bandwidth_hz);

	if (priv->flags & CXD2841ER_EARLY_TUNE)
		cxd2841er_tuner_set(fe);

	/* deconfigure/put demod to sleep on delsys switch if active */
	if (priv->state == STATE_ACTIVE_TC &&
	    priv->system != p->delivery_system) {
		dev_dbg(&priv->i2c->dev, "%s(): old_delsys=%d, new_delsys=%d -> sleep\n",
			 __func__, priv->system, p->delivery_system);
		cxd2841er_sleep_tc(fe);
	}

	if (p->delivery_system == SYS_DVBT) {
		priv->system = SYS_DVBT;
		switch (priv->state) {
		case STATE_SLEEP_TC:
			ret = cxd2841er_sleep_tc_to_active_t(
				priv, p->bandwidth_hz);
			break;
		case STATE_ACTIVE_TC:
			ret = cxd2841er_retune_active(priv, p);
			break;
		default:
			dev_dbg(&priv->i2c->dev, "%s(): invalid state %d\n",
				__func__, priv->state);
			ret = -EINVAL;
		}
	} else if (p->delivery_system == SYS_DVBT2) {
		priv->system = SYS_DVBT2;
		cxd2841er_dvbt2_set_plp_config(priv,
			(int)(p->stream_id > 255), p->stream_id);
		cxd2841er_dvbt2_set_profile(priv, DVBT2_PROFILE_BASE);
		switch (priv->state) {
		case STATE_SLEEP_TC:
			ret = cxd2841er_sleep_tc_to_active_t2(priv,
				p->bandwidth_hz);
			break;
		case STATE_ACTIVE_TC:
			ret = cxd2841er_retune_active(priv, p);
			break;
		default:
			dev_dbg(&priv->i2c->dev, "%s(): invalid state %d\n",
				__func__, priv->state);
			ret = -EINVAL;
		}
	} else if (p->delivery_system == SYS_ISDBT) {
		priv->system = SYS_ISDBT;
		switch (priv->state) {
		case STATE_SLEEP_TC:
			ret = cxd2841er_sleep_tc_to_active_i(
					priv, p->bandwidth_hz);
			break;
		case STATE_ACTIVE_TC:
			ret = cxd2841er_retune_active(priv, p);
			break;
		default:
			dev_dbg(&priv->i2c->dev, "%s(): invalid state %d\n",
					__func__, priv->state);
			ret = -EINVAL;
		}
	} else if (p->delivery_system == SYS_DVBC_ANNEX_A ||
			p->delivery_system == SYS_DVBC_ANNEX_C) {
		priv->system = SYS_DVBC_ANNEX_A;
		/* correct bandwidth */
		if (p->bandwidth_hz != 6000000 &&
				p->bandwidth_hz != 7000000 &&
				p->bandwidth_hz != 8000000) {
			p->bandwidth_hz = 8000000;
			dev_dbg(&priv->i2c->dev, "%s(): forcing bandwidth to %d\n",
					__func__, p->bandwidth_hz);
		}

		switch (priv->state) {
		case STATE_SLEEP_TC:
			ret = cxd2841er_sleep_tc_to_active_c(
				priv, p->bandwidth_hz);
			break;
		case STATE_ACTIVE_TC:
			ret = cxd2841er_retune_active(priv, p);
			break;
		default:
			dev_dbg(&priv->i2c->dev, "%s(): invalid state %d\n",
				__func__, priv->state);
			ret = -EINVAL;
		}
	} else {
		dev_dbg(&priv->i2c->dev,
			"%s(): invalid delivery system %d\n",
			__func__, p->delivery_system);
		ret = -EINVAL;
	}
	if (ret)
		goto done;

	if (!(priv->flags & CXD2841ER_EARLY_TUNE))
		cxd2841er_tuner_set(fe);

	cxd2841er_tune_done(priv);

	if (priv->flags & CXD2841ER_NO_WAIT_LOCK)
		goto done;

	timeout = 2500;
	while (timeout > 0) {
		ret = cxd2841er_read_status_tc(fe, &status);
		if (ret)
			goto done;
		if (status & FE_HAS_LOCK)
			break;
		msleep(20);
		timeout -= 20;
	}
	if (timeout < 0)
		dev_dbg(&priv->i2c->dev,
			"%s(): LOCK wait timeout\n", __func__);
done:
	return ret;
}

static int cxd2841er_tune_s(struct dvb_frontend *fe,
			    bool re_tune,
			    unsigned int mode_flags,
			    unsigned int *delay,
			    enum fe_status *status)
{
	int ret, carrier_offset;
	struct cxd2841er_priv *priv = fe->demodulator_priv;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;

	dev_dbg(&priv->i2c->dev, "%s() re_tune=%d\n", __func__, re_tune);
	if (re_tune) {
		ret = cxd2841er_set_frontend_s(fe);
		if (ret)
			return ret;
		cxd2841er_read_status_s(fe, status);
		if (*status & FE_HAS_LOCK) {
			if (cxd2841er_get_carrier_offset_s_s2(
					priv, &carrier_offset))
				return -EINVAL;
			p->frequency += carrier_offset;
			ret = cxd2841er_set_frontend_s(fe);
			if (ret)
				return ret;
		}
	}
	*delay = HZ / 5;
	return cxd2841er_read_status_s(fe, status);
}

static int cxd2841er_tune_tc(struct dvb_frontend *fe,
			     bool re_tune,
			     unsigned int mode_flags,
			     unsigned int *delay,
			     enum fe_status *status)
{
	int ret, carrier_offset;
	struct cxd2841er_priv *priv = fe->demodulator_priv;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;

	dev_dbg(&priv->i2c->dev, "%s(): re_tune %d bandwidth=%d\n", __func__,
			re_tune, p->bandwidth_hz);
	if (re_tune) {
		ret = cxd2841er_set_frontend_tc(fe);
		if (ret)
			return ret;
		cxd2841er_read_status_tc(fe, status);
		if (*status & FE_HAS_LOCK) {
			switch (priv->system) {
			case SYS_ISDBT:
				ret = cxd2841er_get_carrier_offset_i(
						priv, p->bandwidth_hz,
						&carrier_offset);
				if (ret)
					return ret;
				break;
			case SYS_DVBT:
				ret = cxd2841er_get_carrier_offset_t(
					priv, p->bandwidth_hz,
					&carrier_offset);
				if (ret)
					return ret;
				break;
			case SYS_DVBT2:
				ret = cxd2841er_get_carrier_offset_t2(
					priv, p->bandwidth_hz,
					&carrier_offset);
				if (ret)
					return ret;
				break;
			case SYS_DVBC_ANNEX_A:
				ret = cxd2841er_get_carrier_offset_c(
					priv, &carrier_offset);
				if (ret)
					return ret;
				break;
			default:
				dev_dbg(&priv->i2c->dev,
					"%s(): invalid delivery system %d\n",
					__func__, priv->system);
				return -EINVAL;
			}
			dev_dbg(&priv->i2c->dev, "%s(): carrier offset %d\n",
				__func__, carrier_offset);
			p->frequency += carrier_offset;
			ret = cxd2841er_set_frontend_tc(fe);
			if (ret)
				return ret;
		}
	}
	*delay = HZ / 5;
	return cxd2841er_read_status_tc(fe, status);
}

static int cxd2841er_sleep_s(struct dvb_frontend *fe)
{
	struct cxd2841er_priv *priv = fe->demodulator_priv;

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	cxd2841er_active_s_to_sleep_s(fe->demodulator_priv);
	cxd2841er_sleep_s_to_shutdown(fe->demodulator_priv);
	return 0;
}

static int cxd2841er_sleep_tc(struct dvb_frontend *fe)
{
	struct cxd2841er_priv *priv = fe->demodulator_priv;

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);

	if (priv->state == STATE_ACTIVE_TC) {
		switch (priv->system) {
		case SYS_DVBT:
			cxd2841er_active_t_to_sleep_tc(priv);
			break;
		case SYS_DVBT2:
			cxd2841er_active_t2_to_sleep_tc(priv);
			break;
		case SYS_ISDBT:
			cxd2841er_active_i_to_sleep_tc(priv);
			break;
		case SYS_DVBC_ANNEX_A:
			cxd2841er_active_c_to_sleep_tc(priv);
			break;
		default:
			dev_warn(&priv->i2c->dev,
				"%s(): unknown delivery system %d\n",
				__func__, priv->system);
		}
	}
	if (priv->state != STATE_SLEEP_TC) {
		dev_err(&priv->i2c->dev, "%s(): invalid state %d\n",
			__func__, priv->state);
		return -EINVAL;
	}
	return 0;
}

static int cxd2841er_shutdown_tc(struct dvb_frontend *fe)
{
	struct cxd2841er_priv *priv = fe->demodulator_priv;

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);

	if (!cxd2841er_sleep_tc(fe))
		cxd2841er_sleep_tc_to_shutdown(priv);
	return 0;
}

static int cxd2841er_send_burst(struct dvb_frontend *fe,
				enum fe_sec_mini_cmd burst)
{
	u8 data;
	struct cxd2841er_priv *priv  = fe->demodulator_priv;

	dev_dbg(&priv->i2c->dev, "%s(): burst mode %s\n", __func__,
		(burst == SEC_MINI_A ? "A" : "B"));
	if (priv->state != STATE_SLEEP_S &&
			priv->state != STATE_ACTIVE_S) {
		dev_err(&priv->i2c->dev, "%s(): invalid demod state %d\n",
			__func__, priv->state);
		return -EINVAL;
	}
	data = (burst == SEC_MINI_A ? 0 : 1);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xbb);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x34, 0x01);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x35, data);
	return 0;
}

static int cxd2841er_set_tone(struct dvb_frontend *fe,
			      enum fe_sec_tone_mode tone)
{
	u8 data;
	struct cxd2841er_priv *priv  = fe->demodulator_priv;

	dev_dbg(&priv->i2c->dev, "%s(): tone %s\n", __func__,
		(tone == SEC_TONE_ON ? "On" : "Off"));
	if (priv->state != STATE_SLEEP_S &&
			priv->state != STATE_ACTIVE_S) {
		dev_err(&priv->i2c->dev, "%s(): invalid demod state %d\n",
			__func__, priv->state);
		return -EINVAL;
	}
	data = (tone == SEC_TONE_ON ? 1 : 0);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xbb);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x36, data);
	return 0;
}

static int cxd2841er_send_diseqc_msg(struct dvb_frontend *fe,
				     struct dvb_diseqc_master_cmd *cmd)
{
	int i;
	u8 data[12];
	struct cxd2841er_priv *priv  = fe->demodulator_priv;

	if (priv->state != STATE_SLEEP_S &&
			priv->state != STATE_ACTIVE_S) {
		dev_err(&priv->i2c->dev, "%s(): invalid demod state %d\n",
			__func__, priv->state);
		return -EINVAL;
	}
	dev_dbg(&priv->i2c->dev,
		"%s(): cmd->len %d\n", __func__, cmd->msg_len);
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xbb);
	/* DiDEqC enable */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x33, 0x01);
	/* cmd1 length & data */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x3d, cmd->msg_len);
	memset(data, 0, sizeof(data));
	for (i = 0; i < cmd->msg_len && i < sizeof(data); i++)
		data[i] = cmd->msg[i];
	cxd2841er_write_regs(priv, I2C_SLVT, 0x3e, data, sizeof(data));
	/* repeat count for cmd1 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x37, 1);
	/* repeat count for cmd2: always 0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x38, 0);
	/* start transmit */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x32, 0x01);
	/* wait for 1 sec timeout */
	for (i = 0; i < 50; i++) {
		cxd2841er_read_reg(priv, I2C_SLVT, 0x10, data);
		if (!data[0]) {
			dev_dbg(&priv->i2c->dev,
				"%s(): DiSEqC cmd has been sent\n", __func__);
			return 0;
		}
		msleep(20);
	}
	dev_dbg(&priv->i2c->dev,
		"%s(): DiSEqC cmd transmit timeout\n", __func__);
	return -ETIMEDOUT;
}

static void cxd2841er_release(struct dvb_frontend *fe)
{
	struct cxd2841er_priv *priv  = fe->demodulator_priv;

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	kfree(priv);
}

static int cxd2841er_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct cxd2841er_priv *priv = fe->demodulator_priv;

	dev_dbg(&priv->i2c->dev, "%s(): enable=%d\n", __func__, enable);
	cxd2841er_set_reg_bits(
		priv, I2C_SLVX, 0x8, (enable ? 0x01 : 0x00), 0x01);
	return 0;
}

static enum dvbfe_algo cxd2841er_get_algo(struct dvb_frontend *fe)
{
	struct cxd2841er_priv *priv = fe->demodulator_priv;

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	return DVBFE_ALGO_HW;
}

static void cxd2841er_init_stats(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;

	p->strength.len = 1;
	p->strength.stat[0].scale = FE_SCALE_RELATIVE;
	p->cnr.len = 1;
	p->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->block_error.len = 1;
	p->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->post_bit_error.len = 1;
	p->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->post_bit_count.len = 1;
	p->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
}


static int cxd2841er_init_s(struct dvb_frontend *fe)
{
	struct cxd2841er_priv *priv = fe->demodulator_priv;

	/* sanity. force demod to SHUTDOWN state */
	if (priv->state == STATE_SLEEP_S) {
		dev_dbg(&priv->i2c->dev, "%s() forcing sleep->shutdown\n",
				__func__);
		cxd2841er_sleep_s_to_shutdown(priv);
	} else if (priv->state == STATE_ACTIVE_S) {
		dev_dbg(&priv->i2c->dev, "%s() forcing active->sleep->shutdown\n",
				__func__);
		cxd2841er_active_s_to_sleep_s(priv);
		cxd2841er_sleep_s_to_shutdown(priv);
	}

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	cxd2841er_shutdown_to_sleep_s(priv);
	/* SONY_DEMOD_CONFIG_SAT_IFAGCNEG set to 1 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xa0);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xb9, 0x01, 0x01);

	cxd2841er_init_stats(fe);

	return 0;
}

static int cxd2841er_init_tc(struct dvb_frontend *fe)
{
	struct cxd2841er_priv *priv = fe->demodulator_priv;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;

	dev_dbg(&priv->i2c->dev, "%s() bandwidth_hz=%d\n",
			__func__, p->bandwidth_hz);
	cxd2841er_shutdown_to_sleep_tc(priv);
	/* SONY_DEMOD_CONFIG_IFAGCNEG = 1 (0 for NO_AGCNEG */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x10);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xcb,
		((priv->flags & CXD2841ER_NO_AGCNEG) ? 0x00 : 0x40), 0x40);
	/* SONY_DEMOD_CONFIG_IFAGC_ADC_FS = 0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0xcd, 0x50);
	/* SONY_DEMOD_CONFIG_PARALLEL_SEL = 1 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xc4,
		((priv->flags & CXD2841ER_TS_SERIAL) ? 0x80 : 0x00), 0x80);

	/* clear TSCFG bits 3+4 */
	if (priv->flags & CXD2841ER_TSBITS)
		cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xc4, 0x00, 0x18);

	cxd2841er_init_stats(fe);

	return 0;
}

static const struct dvb_frontend_ops cxd2841er_dvbs_s2_ops;
static struct dvb_frontend_ops cxd2841er_t_c_ops;

static struct dvb_frontend *cxd2841er_attach(struct cxd2841er_config *cfg,
					     struct i2c_adapter *i2c,
					     u8 system)
{
	u8 chip_id = 0;
	const char *type;
	const char *name;
	struct cxd2841er_priv *priv = NULL;

	/* allocate memory for the internal state */
	priv = kzalloc(sizeof(struct cxd2841er_priv), GFP_KERNEL);
	if (!priv)
		return NULL;
	priv->i2c = i2c;
	priv->config = cfg;
	priv->i2c_addr_slvx = (cfg->i2c_addr + 4) >> 1;
	priv->i2c_addr_slvt = (cfg->i2c_addr) >> 1;
	priv->xtal = cfg->xtal;
	priv->flags = cfg->flags;
	priv->frontend.demodulator_priv = priv;
	dev_info(&priv->i2c->dev,
		"%s(): I2C adapter %p SLVX addr %x SLVT addr %x\n",
		__func__, priv->i2c,
		priv->i2c_addr_slvx, priv->i2c_addr_slvt);
	chip_id = cxd2841er_chip_id(priv);
	switch (chip_id) {
	case CXD2837ER_CHIP_ID:
		snprintf(cxd2841er_t_c_ops.info.name, 128,
				"Sony CXD2837ER DVB-T/T2/C demodulator");
		name = "CXD2837ER";
		type = "C/T/T2";
		break;
	case CXD2838ER_CHIP_ID:
		snprintf(cxd2841er_t_c_ops.info.name, 128,
				"Sony CXD2838ER ISDB-T demodulator");
		cxd2841er_t_c_ops.delsys[0] = SYS_ISDBT;
		cxd2841er_t_c_ops.delsys[1] = SYS_UNDEFINED;
		cxd2841er_t_c_ops.delsys[2] = SYS_UNDEFINED;
		name = "CXD2838ER";
		type = "ISDB-T";
		break;
	case CXD2841ER_CHIP_ID:
		snprintf(cxd2841er_t_c_ops.info.name, 128,
				"Sony CXD2841ER DVB-T/T2/C demodulator");
		name = "CXD2841ER";
		type = "T/T2/C/ISDB-T";
		break;
	case CXD2843ER_CHIP_ID:
		snprintf(cxd2841er_t_c_ops.info.name, 128,
				"Sony CXD2843ER DVB-T/T2/C/C2 demodulator");
		name = "CXD2843ER";
		type = "C/C2/T/T2";
		break;
	case CXD2854ER_CHIP_ID:
		snprintf(cxd2841er_t_c_ops.info.name, 128,
				"Sony CXD2854ER DVB-T/T2/C and ISDB-T demodulator");
		cxd2841er_t_c_ops.delsys[3] = SYS_ISDBT;
		name = "CXD2854ER";
		type = "C/C2/T/T2/ISDB-T";
		break;
	default:
		dev_err(&priv->i2c->dev, "%s(): invalid chip ID 0x%02x\n",
				__func__, chip_id);
		priv->frontend.demodulator_priv = NULL;
		kfree(priv);
		return NULL;
	}

	/* create dvb_frontend */
	if (system == SYS_DVBS) {
		memcpy(&priv->frontend.ops,
			&cxd2841er_dvbs_s2_ops,
			sizeof(struct dvb_frontend_ops));
		type = "S/S2";
	} else {
		memcpy(&priv->frontend.ops,
			&cxd2841er_t_c_ops,
			sizeof(struct dvb_frontend_ops));
	}

	dev_info(&priv->i2c->dev,
		"%s(): attaching %s DVB-%s frontend\n",
		__func__, name, type);
	dev_info(&priv->i2c->dev, "%s(): chip ID 0x%02x OK.\n",
		__func__, chip_id);
	return &priv->frontend;
}

struct dvb_frontend *cxd2841er_attach_s(struct cxd2841er_config *cfg,
					struct i2c_adapter *i2c)
{
	return cxd2841er_attach(cfg, i2c, SYS_DVBS);
}
EXPORT_SYMBOL(cxd2841er_attach_s);

struct dvb_frontend *cxd2841er_attach_t_c(struct cxd2841er_config *cfg,
					struct i2c_adapter *i2c)
{
	return cxd2841er_attach(cfg, i2c, 0);
}
EXPORT_SYMBOL(cxd2841er_attach_t_c);

static const struct dvb_frontend_ops cxd2841er_dvbs_s2_ops = {
	.delsys = { SYS_DVBS, SYS_DVBS2 },
	.info = {
		.name		= "Sony CXD2841ER DVB-S/S2 demodulator",
		.frequency_min	= 500000,
		.frequency_max	= 2500000,
		.frequency_stepsize	= 0,
		.symbol_rate_min = 1000000,
		.symbol_rate_max = 45000000,
		.symbol_rate_tolerance = 500,
		.caps = FE_CAN_INVERSION_AUTO |
			FE_CAN_FEC_AUTO |
			FE_CAN_QPSK,
	},
	.init = cxd2841er_init_s,
	.sleep = cxd2841er_sleep_s,
	.release = cxd2841er_release,
	.set_frontend = cxd2841er_set_frontend_s,
	.get_frontend = cxd2841er_get_frontend,
	.read_status = cxd2841er_read_status_s,
	.i2c_gate_ctrl = cxd2841er_i2c_gate_ctrl,
	.get_frontend_algo = cxd2841er_get_algo,
	.set_tone = cxd2841er_set_tone,
	.diseqc_send_burst = cxd2841er_send_burst,
	.diseqc_send_master_cmd = cxd2841er_send_diseqc_msg,
	.tune = cxd2841er_tune_s
};

static struct dvb_frontend_ops cxd2841er_t_c_ops = {
	.delsys = { SYS_DVBT, SYS_DVBT2, SYS_DVBC_ANNEX_A },
	.info = {
		.name	= "", /* will set in attach function */
		.caps = FE_CAN_FEC_1_2 |
			FE_CAN_FEC_2_3 |
			FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 |
			FE_CAN_FEC_7_8 |
			FE_CAN_FEC_AUTO |
			FE_CAN_QPSK |
			FE_CAN_QAM_16 |
			FE_CAN_QAM_32 |
			FE_CAN_QAM_64 |
			FE_CAN_QAM_128 |
			FE_CAN_QAM_256 |
			FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_HIERARCHY_AUTO |
			FE_CAN_MUTE_TS |
			FE_CAN_2G_MODULATION,
		.frequency_min = 42000000,
		.frequency_max = 1002000000,
		.symbol_rate_min = 870000,
		.symbol_rate_max = 11700000
	},
	.init = cxd2841er_init_tc,
	.sleep = cxd2841er_shutdown_tc,
	.release = cxd2841er_release,
	.set_frontend = cxd2841er_set_frontend_tc,
	.get_frontend = cxd2841er_get_frontend,
	.read_status = cxd2841er_read_status_tc,
	.tune = cxd2841er_tune_tc,
	.i2c_gate_ctrl = cxd2841er_i2c_gate_ctrl,
	.get_frontend_algo = cxd2841er_get_algo
};

MODULE_DESCRIPTION("Sony CXD2837/38/41/43/54ER DVB-C/C2/T/T2/S/S2 demodulator driver");
MODULE_AUTHOR("Sergey Kozlov <serjk@netup.ru>, Abylay Ospan <aospan@netup.ru>");
MODULE_LICENSE("GPL");
