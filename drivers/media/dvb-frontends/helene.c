/*
 * helene.c
 *
 * Sony HELENE DVB-S/S2 DVB-T/T2 DVB-C/C2 ISDB-T/S tuner driver (CXD2858ER)
 *
 * Copyright 2012 Sony Corporation
 * Copyright (C) 2014 NetUP Inc.
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
#include "helene.h"
#include "dvb_frontend.h"

#define MAX_WRITE_REGSIZE 20

enum helene_state {
	STATE_UNKNOWN,
	STATE_SLEEP,
	STATE_ACTIVE
};

struct helene_priv {
	u32			frequency;
	u8			i2c_address;
	struct i2c_adapter	*i2c;
	enum helene_state	state;
	void			*set_tuner_data;
	int			(*set_tuner)(void *, int);
	enum helene_xtal xtal;
};

#define TERR_INTERNAL_LOOPFILTER_AVAILABLE(tv_system) \
	(((tv_system) != SONY_HELENE_DTV_DVBC_6) && \
	 ((tv_system) != SONY_HELENE_DTV_DVBC_8)\
	 && ((tv_system) != SONY_HELENE_DTV_DVBC2_6) && \
	 ((tv_system) != SONY_HELENE_DTV_DVBC2_8))

#define HELENE_AUTO		0xff
#define HELENE_OFFSET(ofs)	((u8)(ofs) & 0x1F)
#define HELENE_BW_6		0x00
#define HELENE_BW_7		0x01
#define HELENE_BW_8		0x02
#define HELENE_BW_1_7		0x03

enum helene_tv_system_t {
	SONY_HELENE_TV_SYSTEM_UNKNOWN,
	/* Terrestrial Analog */
	SONY_HELENE_ATV_MN_EIAJ,
	/**< System-M (Japan) (IF: Fp=5.75MHz in default) */
	SONY_HELENE_ATV_MN_SAP,
	/**< System-M (US)    (IF: Fp=5.75MHz in default) */
	SONY_HELENE_ATV_MN_A2,
	/**< System-M (Korea) (IF: Fp=5.9MHz in default) */
	SONY_HELENE_ATV_BG,
	/**< System-B/G       (IF: Fp=7.3MHz in default) */
	SONY_HELENE_ATV_I,
	/**< System-I         (IF: Fp=7.85MHz in default) */
	SONY_HELENE_ATV_DK,
	/**< System-D/K       (IF: Fp=7.85MHz in default) */
	SONY_HELENE_ATV_L,
	/**< System-L         (IF: Fp=7.85MHz in default) */
	SONY_HELENE_ATV_L_DASH,
	/**< System-L DASH    (IF: Fp=2.2MHz in default) */
	/* Terrestrial/Cable Digital */
	SONY_HELENE_DTV_8VSB,
	/**< ATSC 8VSB        (IF: Fc=3.7MHz in default) */
	SONY_HELENE_DTV_QAM,
	/**< US QAM           (IF: Fc=3.7MHz in default) */
	SONY_HELENE_DTV_ISDBT_6,
	/**< ISDB-T 6MHzBW    (IF: Fc=3.55MHz in default) */
	SONY_HELENE_DTV_ISDBT_7,
	/**< ISDB-T 7MHzBW    (IF: Fc=4.15MHz in default) */
	SONY_HELENE_DTV_ISDBT_8,
	/**< ISDB-T 8MHzBW    (IF: Fc=4.75MHz in default) */
	SONY_HELENE_DTV_DVBT_5,
	/**< DVB-T 5MHzBW     (IF: Fc=3.6MHz in default) */
	SONY_HELENE_DTV_DVBT_6,
	/**< DVB-T 6MHzBW     (IF: Fc=3.6MHz in default) */
	SONY_HELENE_DTV_DVBT_7,
	/**< DVB-T 7MHzBW     (IF: Fc=4.2MHz in default) */
	SONY_HELENE_DTV_DVBT_8,
	/**< DVB-T 8MHzBW     (IF: Fc=4.8MHz in default) */
	SONY_HELENE_DTV_DVBT2_1_7,
	/**< DVB-T2 1.7MHzBW  (IF: Fc=3.5MHz in default) */
	SONY_HELENE_DTV_DVBT2_5,
	/**< DVB-T2 5MHzBW    (IF: Fc=3.6MHz in default) */
	SONY_HELENE_DTV_DVBT2_6,
	/**< DVB-T2 6MHzBW    (IF: Fc=3.6MHz in default) */
	SONY_HELENE_DTV_DVBT2_7,
	/**< DVB-T2 7MHzBW    (IF: Fc=4.2MHz in default) */
	SONY_HELENE_DTV_DVBT2_8,
	/**< DVB-T2 8MHzBW    (IF: Fc=4.8MHz in default) */
	SONY_HELENE_DTV_DVBC_6,
	/**< DVB-C 6MHzBW     (IF: Fc=3.7MHz in default) */
	SONY_HELENE_DTV_DVBC_8,
	/**< DVB-C 8MHzBW     (IF: Fc=4.9MHz in default) */
	SONY_HELENE_DTV_DVBC2_6,
	/**< DVB-C2 6MHzBW    (IF: Fc=3.7MHz in default) */
	SONY_HELENE_DTV_DVBC2_8,
	/**< DVB-C2 8MHzBW    (IF: Fc=4.9MHz in default) */
	SONY_HELENE_DTV_DTMB,
	/**< DTMB             (IF: Fc=5.1MHz in default) */
	/* Satellite */
	SONY_HELENE_STV_ISDBS,
	/**< ISDB-S */
	SONY_HELENE_STV_DVBS,
	/**< DVB-S */
	SONY_HELENE_STV_DVBS2,
	/**< DVB-S2 */

	SONY_HELENE_ATV_MIN = SONY_HELENE_ATV_MN_EIAJ,
	/**< Minimum analog terrestrial system */
	SONY_HELENE_ATV_MAX = SONY_HELENE_ATV_L_DASH,
	/**< Maximum analog terrestrial system */
	SONY_HELENE_DTV_MIN = SONY_HELENE_DTV_8VSB,
	/**< Minimum digital terrestrial system */
	SONY_HELENE_DTV_MAX = SONY_HELENE_DTV_DTMB,
	/**< Maximum digital terrestrial system */
	SONY_HELENE_TERR_TV_SYSTEM_NUM,
	/**< Number of supported terrestrial broadcasting system */
	SONY_HELENE_STV_MIN = SONY_HELENE_STV_ISDBS,
	/**< Minimum satellite system */
	SONY_HELENE_STV_MAX = SONY_HELENE_STV_DVBS2
	/**< Maximum satellite system */
};

struct helene_terr_adjust_param_t {
	/* < Addr:0x69 Bit[6:4] : RFVGA gain.
	 * 0xFF means Auto. (RF_GAIN_SEL = 1)
	 */
	uint8_t RF_GAIN;
	/* < Addr:0x69 Bit[3:0] : IF_BPF gain.
	*/
	uint8_t IF_BPF_GC;
	/* < Addr:0x6B Bit[3:0] : RF overload
	 * RF input detect level. (FRF <= 172MHz)
	*/
	uint8_t RFOVLD_DET_LV1_VL;
	/* < Addr:0x6B Bit[3:0] : RF overload
	 * RF input detect level. (172MHz < FRF <= 464MHz)
	*/
	uint8_t RFOVLD_DET_LV1_VH;
	/* < Addr:0x6B Bit[3:0] : RF overload
	 * RF input detect level. (FRF > 464MHz)
	*/
	uint8_t RFOVLD_DET_LV1_U;
	/* < Addr:0x6C Bit[2:0] :
	 * Internal RFAGC detect level. (FRF <= 172MHz)
	*/
	uint8_t IFOVLD_DET_LV_VL;
	/* < Addr:0x6C Bit[2:0] :
	 * Internal RFAGC detect level. (172MHz < FRF <= 464MHz)
	*/
	uint8_t IFOVLD_DET_LV_VH;
	/* < Addr:0x6C Bit[2:0] :
	 * Internal RFAGC detect level. (FRF > 464MHz)
	*/
	uint8_t IFOVLD_DET_LV_U;
	/* < Addr:0x6D Bit[5:4] :
	 * IF filter center offset.
	*/
	uint8_t IF_BPF_F0;
	/* < Addr:0x6D Bit[1:0] :
	 * 6MHzBW(0x00) or 7MHzBW(0x01)
	 * or 8MHzBW(0x02) or 1.7MHzBW(0x03)
	*/
	uint8_t BW;
	/* < Addr:0x6E Bit[4:0] :
	 * 5bit signed. IF offset (kHz) = FIF_OFFSET x 50
	*/
	uint8_t FIF_OFFSET;
	/* < Addr:0x6F Bit[4:0] :
	 * 5bit signed. BW offset (kHz) =
	 * BW_OFFSET x 50 (BW_OFFSET x 10 in 1.7MHzBW)
	*/
	uint8_t BW_OFFSET;
	/* < Addr:0x9C Bit[0]   :
	 * Local polarity. (0: Upper Local, 1: Lower Local)
	*/
	uint8_t IS_LOWERLOCAL;
};

static const struct helene_terr_adjust_param_t
terr_params[SONY_HELENE_TERR_TV_SYSTEM_NUM] = {
	/*< SONY_HELENE_TV_SYSTEM_UNKNOWN */
	{HELENE_AUTO, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		HELENE_BW_6, HELENE_OFFSET(0),  HELENE_OFFSET(0),  0x00},
	/* Analog */
	/**< SONY_HELENE_ATV_MN_EIAJ   (System-M (Japan)) */
	{HELENE_AUTO, 0x05, 0x03, 0x06, 0x03, 0x01, 0x01, 0x01, 0x00,
		HELENE_BW_6,  HELENE_OFFSET(0),  HELENE_OFFSET(1),  0x00},
	/**< SONY_HELENE_ATV_MN_SAP    (System-M (US)) */
	{HELENE_AUTO, 0x05, 0x03, 0x06, 0x03, 0x01, 0x01, 0x01, 0x00,
		HELENE_BW_6,  HELENE_OFFSET(0),  HELENE_OFFSET(1),  0x00},
	{HELENE_AUTO, 0x05, 0x03, 0x06, 0x03, 0x01, 0x01, 0x01, 0x00,
		HELENE_BW_6,  HELENE_OFFSET(3),  HELENE_OFFSET(1),  0x00},
	/**< SONY_HELENE_ATV_MN_A2     (System-M (Korea)) */
	{HELENE_AUTO, 0x05, 0x03, 0x06, 0x03, 0x01, 0x01, 0x01, 0x00,
		HELENE_BW_7,  HELENE_OFFSET(11), HELENE_OFFSET(5),  0x00},
	/**< SONY_HELENE_ATV_BG        (System-B/G) */
	{HELENE_AUTO, 0x05, 0x03, 0x06, 0x03, 0x01, 0x01, 0x01, 0x00,
		HELENE_BW_8,  HELENE_OFFSET(2),  HELENE_OFFSET(-3), 0x00},
	/**< SONY_HELENE_ATV_I         (System-I) */
	{HELENE_AUTO, 0x05, 0x03, 0x06, 0x03, 0x01, 0x01, 0x01, 0x00,
		HELENE_BW_8,  HELENE_OFFSET(2),  HELENE_OFFSET(-3), 0x00},
	/**< SONY_HELENE_ATV_DK        (System-D/K) */
	{HELENE_AUTO, 0x03, 0x04, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x00,
		HELENE_BW_8,  HELENE_OFFSET(2),  HELENE_OFFSET(-3), 0x00},
	/**< SONY_HELENE_ATV_L         (System-L) */
	{HELENE_AUTO, 0x03, 0x04, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x00,
		HELENE_BW_8,  HELENE_OFFSET(-1), HELENE_OFFSET(4),  0x00},
	/**< SONY_HELENE_ATV_L_DASH    (System-L DASH) */
	/* Digital */
	{HELENE_AUTO, 0x09, 0x0B, 0x0B, 0x0B, 0x03, 0x03, 0x03, 0x00,
		HELENE_BW_6,  HELENE_OFFSET(-6), HELENE_OFFSET(-3), 0x00},
	/**< SONY_HELENE_DTV_8VSB      (ATSC 8VSB) */
	{HELENE_AUTO, 0x09, 0x0B, 0x0B, 0x0B, 0x02, 0x02, 0x02, 0x00,
		HELENE_BW_6,  HELENE_OFFSET(-6), HELENE_OFFSET(-3), 0x00},
	/**< SONY_HELENE_DTV_QAM       (US QAM) */
	{HELENE_AUTO, 0x09, 0x0B, 0x0B, 0x0B, 0x02, 0x02, 0x02, 0x00,
		HELENE_BW_6,  HELENE_OFFSET(-9), HELENE_OFFSET(-5), 0x00},
	/**< SONY_HELENE_DTV_ISDBT_6   (ISDB-T 6MHzBW) */
	{HELENE_AUTO, 0x09, 0x0B, 0x0B, 0x0B, 0x02, 0x02, 0x02, 0x00,
		HELENE_BW_7,  HELENE_OFFSET(-7), HELENE_OFFSET(-6), 0x00},
	/**< SONY_HELENE_DTV_ISDBT_7   (ISDB-T 7MHzBW) */
	{HELENE_AUTO, 0x09, 0x0B, 0x0B, 0x0B, 0x02, 0x02, 0x02, 0x00,
		HELENE_BW_8,  HELENE_OFFSET(-5), HELENE_OFFSET(-7), 0x00},
	/**< SONY_HELENE_DTV_ISDBT_8   (ISDB-T 8MHzBW) */
	{HELENE_AUTO, 0x09, 0x0B, 0x0B, 0x0B, 0x02, 0x02, 0x02, 0x00,
		HELENE_BW_6,  HELENE_OFFSET(-8), HELENE_OFFSET(-3), 0x00},
	/**< SONY_HELENE_DTV_DVBT_5    (DVB-T 5MHzBW) */
	{HELENE_AUTO, 0x09, 0x0B, 0x0B, 0x0B, 0x02, 0x02, 0x02, 0x00,
		HELENE_BW_6,  HELENE_OFFSET(-8), HELENE_OFFSET(-3), 0x00},
	/**< SONY_HELENE_DTV_DVBT_6    (DVB-T 6MHzBW) */
	{HELENE_AUTO, 0x09, 0x0B, 0x0B, 0x0B, 0x02, 0x02, 0x02, 0x00,
		HELENE_BW_7,  HELENE_OFFSET(-6), HELENE_OFFSET(-5), 0x00},
	/**< SONY_HELENE_DTV_DVBT_7    (DVB-T 7MHzBW) */
	{HELENE_AUTO, 0x09, 0x0B, 0x0B, 0x0B, 0x02, 0x02, 0x02, 0x00,
		HELENE_BW_8,  HELENE_OFFSET(-4), HELENE_OFFSET(-6), 0x00},
	/**< SONY_HELENE_DTV_DVBT_8    (DVB-T 8MHzBW) */
	{HELENE_AUTO, 0x09, 0x0B, 0x0B, 0x0B, 0x02, 0x02, 0x02, 0x00,
		HELENE_BW_1_7, HELENE_OFFSET(-10), HELENE_OFFSET(-10), 0x00},
	/**< SONY_HELENE_DTV_DVBT2_1_7 (DVB-T2 1.7MHzBW) */
	{HELENE_AUTO, 0x09, 0x0B, 0x0B, 0x0B, 0x02, 0x02, 0x02, 0x00,
		HELENE_BW_6,  HELENE_OFFSET(-8), HELENE_OFFSET(-3), 0x00},
	/**< SONY_HELENE_DTV_DVBT2_5   (DVB-T2 5MHzBW) */
	{HELENE_AUTO, 0x09, 0x0B, 0x0B, 0x0B, 0x02, 0x02, 0x02, 0x00,
		HELENE_BW_6,  HELENE_OFFSET(-8), HELENE_OFFSET(-3), 0x00},
	/**< SONY_HELENE_DTV_DVBT2_6   (DVB-T2 6MHzBW) */
	{HELENE_AUTO, 0x09, 0x0B, 0x0B, 0x0B, 0x02, 0x02, 0x02, 0x00,
		HELENE_BW_7,  HELENE_OFFSET(-6), HELENE_OFFSET(-5), 0x00},
	/**< SONY_HELENE_DTV_DVBT2_7   (DVB-T2 7MHzBW) */
	{HELENE_AUTO, 0x09, 0x0B, 0x0B, 0x0B, 0x02, 0x02, 0x02, 0x00,
		HELENE_BW_8,  HELENE_OFFSET(-4), HELENE_OFFSET(-6), 0x00},
	/**< SONY_HELENE_DTV_DVBT2_8   (DVB-T2 8MHzBW) */
	{HELENE_AUTO, 0x05, 0x02, 0x02, 0x02, 0x01, 0x01, 0x01, 0x00,
		HELENE_BW_6,  HELENE_OFFSET(-6), HELENE_OFFSET(-4), 0x00},
	/**< SONY_HELENE_DTV_DVBC_6    (DVB-C 6MHzBW) */
	{HELENE_AUTO, 0x05, 0x02, 0x02, 0x02, 0x01, 0x01, 0x01, 0x00,
		HELENE_BW_8,  HELENE_OFFSET(-2), HELENE_OFFSET(-3), 0x00},
	/**< SONY_HELENE_DTV_DVBC_8    (DVB-C 8MHzBW) */
	{HELENE_AUTO, 0x03, 0x09, 0x09, 0x09, 0x02, 0x02, 0x02, 0x00,
		HELENE_BW_6,  HELENE_OFFSET(-6), HELENE_OFFSET(-2), 0x00},
	/**< SONY_HELENE_DTV_DVBC2_6   (DVB-C2 6MHzBW) */
	{HELENE_AUTO, 0x03, 0x09, 0x09, 0x09, 0x02, 0x02, 0x02, 0x00,
		HELENE_BW_8,  HELENE_OFFSET(-2), HELENE_OFFSET(0),  0x00},
	/**< SONY_HELENE_DTV_DVBC2_8   (DVB-C2 8MHzBW) */
	{HELENE_AUTO, 0x04, 0x0B, 0x0B, 0x0B, 0x02, 0x02, 0x02, 0x00,
		HELENE_BW_8,  HELENE_OFFSET(2),  HELENE_OFFSET(1),  0x00}
	/**< SONY_HELENE_DTV_DTMB      (DTMB) */
};

static void helene_i2c_debug(struct helene_priv *priv,
		u8 reg, u8 write, const u8 *data, u32 len)
{
	dev_dbg(&priv->i2c->dev, "helene: I2C %s reg 0x%02x size %d\n",
			(write == 0 ? "read" : "write"), reg, len);
	print_hex_dump_bytes("helene: I2C data: ",
			DUMP_PREFIX_OFFSET, data, len);
}

static int helene_write_regs(struct helene_priv *priv,
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
		dev_warn(&priv->i2c->dev,
				"wr reg=%04x: len=%d vs %zu is too big!\n",
				reg, len + 1, sizeof(buf));
		return -E2BIG;
	}

	helene_i2c_debug(priv, reg, 1, data, len);
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

static int helene_write_reg(struct helene_priv *priv, u8 reg, u8 val)
{
	return helene_write_regs(priv, reg, &val, 1);
}

static int helene_read_regs(struct helene_priv *priv,
		u8 reg, u8 *val, u32 len)
{
	int ret;
	struct i2c_msg msg[2] = {
		{
			.addr = priv->i2c_address,
			.flags = 0,
			.len = 1,
			.buf = &reg,
		}, {
			.addr = priv->i2c_address,
			.flags = I2C_M_RD,
			.len = len,
			.buf = val,
		}
	};

	ret = i2c_transfer(priv->i2c, &msg[0], 1);
	if (ret >= 0 && ret != 1)
		ret = -EREMOTEIO;
	if (ret < 0) {
		dev_warn(&priv->i2c->dev,
				"%s: I2C rw failed=%d addr=%02x reg=%02x\n",
				KBUILD_MODNAME, ret, priv->i2c_address, reg);
		return ret;
	}
	ret = i2c_transfer(priv->i2c, &msg[1], 1);
	if (ret >= 0 && ret != 1)
		ret = -EREMOTEIO;
	if (ret < 0) {
		dev_warn(&priv->i2c->dev,
				"%s: i2c rd failed=%d addr=%02x reg=%02x\n",
				KBUILD_MODNAME, ret, priv->i2c_address, reg);
		return ret;
	}
	helene_i2c_debug(priv, reg, 0, val, len);
	return 0;
}

static int helene_read_reg(struct helene_priv *priv, u8 reg, u8 *val)
{
	return helene_read_regs(priv, reg, val, 1);
}

static int helene_set_reg_bits(struct helene_priv *priv,
		u8 reg, u8 data, u8 mask)
{
	int res;
	u8 rdata;

	if (mask != 0xff) {
		res = helene_read_reg(priv, reg, &rdata);
		if (res != 0)
			return res;
		data = ((data & mask) | (rdata & (mask ^ 0xFF)));
	}
	return helene_write_reg(priv, reg, data);
}

static int helene_enter_power_save(struct helene_priv *priv)
{
	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	if (priv->state == STATE_SLEEP)
		return 0;

	/* Standby setting for CPU */
	helene_write_reg(priv, 0x88, 0x0);

	/* Standby setting for internal logic block */
	helene_write_reg(priv, 0x87, 0xC0);

	priv->state = STATE_SLEEP;
	return 0;
}

static int helene_leave_power_save(struct helene_priv *priv)
{
	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	if (priv->state == STATE_ACTIVE)
		return 0;

	/* Standby setting for internal logic block */
	helene_write_reg(priv, 0x87, 0xC4);

	/* Standby setting for CPU */
	helene_write_reg(priv, 0x88, 0x40);

	priv->state = STATE_ACTIVE;
	return 0;
}

static int helene_init(struct dvb_frontend *fe)
{
	struct helene_priv *priv = fe->tuner_priv;

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	return helene_leave_power_save(priv);
}

static void helene_release(struct dvb_frontend *fe)
{
	struct helene_priv *priv = fe->tuner_priv;

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
}

static int helene_sleep(struct dvb_frontend *fe)
{
	struct helene_priv *priv = fe->tuner_priv;

	dev_dbg(&priv->i2c->dev, "%s()\n", __func__);
	helene_enter_power_save(priv);
	return 0;
}

static enum helene_tv_system_t helene_get_tv_system(struct dvb_frontend *fe)
{
	enum helene_tv_system_t system = SONY_HELENE_TV_SYSTEM_UNKNOWN;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct helene_priv *priv = fe->tuner_priv;

	if (p->delivery_system == SYS_DVBT) {
		if (p->bandwidth_hz <= 5000000)
			system = SONY_HELENE_DTV_DVBT_5;
		else if (p->bandwidth_hz <= 6000000)
			system = SONY_HELENE_DTV_DVBT_6;
		else if (p->bandwidth_hz <= 7000000)
			system = SONY_HELENE_DTV_DVBT_7;
		else if (p->bandwidth_hz <= 8000000)
			system = SONY_HELENE_DTV_DVBT_8;
		else {
			system = SONY_HELENE_DTV_DVBT_8;
			p->bandwidth_hz = 8000000;
		}
	} else if (p->delivery_system == SYS_DVBT2) {
		if (p->bandwidth_hz <= 5000000)
			system = SONY_HELENE_DTV_DVBT2_5;
		else if (p->bandwidth_hz <= 6000000)
			system = SONY_HELENE_DTV_DVBT2_6;
		else if (p->bandwidth_hz <= 7000000)
			system = SONY_HELENE_DTV_DVBT2_7;
		else if (p->bandwidth_hz <= 8000000)
			system = SONY_HELENE_DTV_DVBT2_8;
		else {
			system = SONY_HELENE_DTV_DVBT2_8;
			p->bandwidth_hz = 8000000;
		}
	} else if (p->delivery_system == SYS_DVBS) {
		system = SONY_HELENE_STV_DVBS;
	} else if (p->delivery_system == SYS_DVBS2) {
		system = SONY_HELENE_STV_DVBS2;
	} else if (p->delivery_system == SYS_ISDBS) {
		system = SONY_HELENE_STV_ISDBS;
	} else if (p->delivery_system == SYS_ISDBT) {
		if (p->bandwidth_hz <= 6000000)
			system = SONY_HELENE_DTV_ISDBT_6;
		else if (p->bandwidth_hz <= 7000000)
			system = SONY_HELENE_DTV_ISDBT_7;
		else if (p->bandwidth_hz <= 8000000)
			system = SONY_HELENE_DTV_ISDBT_8;
		else {
			system = SONY_HELENE_DTV_ISDBT_8;
			p->bandwidth_hz = 8000000;
		}
	} else if (p->delivery_system == SYS_DVBC_ANNEX_A) {
		if (p->bandwidth_hz <= 6000000)
			system = SONY_HELENE_DTV_DVBC_6;
		else if (p->bandwidth_hz <= 8000000)
			system = SONY_HELENE_DTV_DVBC_8;
	}
	dev_dbg(&priv->i2c->dev,
			"%s(): HELENE DTV system %d (delsys %d, bandwidth %d)\n",
			__func__, (int)system, p->delivery_system,
			p->bandwidth_hz);
	return system;
}

static int helene_set_params_s(struct dvb_frontend *fe)
{
	u8 data[MAX_WRITE_REGSIZE];
	u32 frequency;
	enum helene_tv_system_t tv_system;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct helene_priv *priv = fe->tuner_priv;
	int frequencykHz = p->frequency;
	uint32_t frequency4kHz = 0;
	u32 symbol_rate = p->symbol_rate/1000;

	dev_dbg(&priv->i2c->dev, "%s(): tune frequency %dkHz sr=%uKsps\n",
			__func__, frequencykHz, symbol_rate);
	tv_system = helene_get_tv_system(fe);

	if (tv_system == SONY_HELENE_TV_SYSTEM_UNKNOWN) {
		dev_err(&priv->i2c->dev, "%s(): unknown DTV system\n",
				__func__);
		return -EINVAL;
	}
	/* RF switch turn to satellite */
	if (priv->set_tuner)
		priv->set_tuner(priv->set_tuner_data, 0);
	frequency = roundup(p->frequency / 1000, 1);

	/* Disable IF signal output */
	helene_write_reg(priv, 0x15, 0x02);

	/* RFIN matching in power save (Sat) reset */
	helene_write_reg(priv, 0x43, 0x06);

	/* Analog block setting (0x6A, 0x6B) */
	data[0] = 0x00;
	data[1] = 0x00;
	helene_write_regs(priv, 0x6A, data, 2);
	helene_write_reg(priv, 0x75, 0x99);
	helene_write_reg(priv, 0x9D, 0x00);

	/* Tuning setting for CPU (0x61) */
	helene_write_reg(priv, 0x61, 0x07);

	/* Satellite mode select (0x01) */
	helene_write_reg(priv, 0x01, 0x01);

	/* Clock enable for internal logic block, CPU wake-up (0x04, 0x05) */
	data[0] = 0xC4;
	data[1] = 0x40;

	switch (priv->xtal) {
	case SONY_HELENE_XTAL_16000:
		data[2] = 0x02;
		break;
	case SONY_HELENE_XTAL_20500:
		data[2] = 0x02;
		break;
	case SONY_HELENE_XTAL_24000:
		data[2] = 0x03;
		break;
	case SONY_HELENE_XTAL_41000:
		data[2] = 0x05;
		break;
	default:
		dev_err(&priv->i2c->dev, "%s(): unknown xtal %d\n",
				__func__, priv->xtal);
		return -EINVAL;
	}

	/* Setting for analog block (0x07). LOOPFILTER INTERNAL */
	data[3] = 0x80;

	/* Tuning setting for analog block
	 * (0x08, 0x09, 0x0A, 0x0B). LOOPFILTER INTERNAL
	*/
	if (priv->xtal == SONY_HELENE_XTAL_20500)
		data[4] = 0x58;
	else
		data[4] = 0x70;

	data[5] = 0x1E;
	data[6] = 0x02;
	data[7] = 0x24;

	/* Enable for analog block (0x0C, 0x0D, 0x0E). SAT LNA ON */
	data[8] = 0x0F;
	data[8] |= 0xE0; /* POWERSAVE_TERR_RF_ACTIVE */
	data[9]  = 0x02;
	data[10] = 0x1E;

	/* Setting for LPF cutoff frequency (0x0F) */
	switch (tv_system) {
	case SONY_HELENE_STV_ISDBS:
		data[11] = 0x22; /* 22MHz */
		break;
	case SONY_HELENE_STV_DVBS:
		if (symbol_rate <= 4000)
			data[11] = 0x05;
		else if (symbol_rate <= 10000)
			data[11] = (uint8_t)((symbol_rate * 47
						+ (40000-1)) / 40000);
		else
			data[11] = (uint8_t)((symbol_rate * 27
						+ (40000-1)) / 40000 + 5);

		if (data[11] > 36)
			data[11] = 36; /* 5 <= lpf_cutoff <= 36 is valid */
		break;
	case SONY_HELENE_STV_DVBS2:
		if (symbol_rate <= 4000)
			data[11] = 0x05;
		else if (symbol_rate <= 10000)
			data[11] = (uint8_t)((symbol_rate * 11
						+ (10000-1)) / 10000);
		else
			data[11] = (uint8_t)((symbol_rate * 3
						+ (5000-1)) / 5000 + 5);

		if (data[11] > 36)
			data[11] = 36; /* 5 <= lpf_cutoff <= 36 is valid */
		break;
	default:
		dev_err(&priv->i2c->dev, "%s(): unknown standard %d\n",
				__func__, tv_system);
		return -EINVAL;
	}

	/* RF tuning frequency setting (0x10, 0x11, 0x12) */
	frequency4kHz = (frequencykHz + 2) / 4;
	data[12] = (uint8_t)(frequency4kHz & 0xFF);         /* FRF_L */
	data[13] = (uint8_t)((frequency4kHz >> 8) & 0xFF);  /* FRF_M */
	/* FRF_H (bit[3:0]) */
	data[14] = (uint8_t)((frequency4kHz >> 16) & 0x0F);

	/* Tuning command (0x13) */
	data[15] = 0xFF;

	/* Setting for IQOUT_LIMIT (0x14) 0.75Vpp */
	data[16] = 0x00;

	/* Enable IQ output (0x15) */
	data[17] = 0x01;

	helene_write_regs(priv, 0x04, data, 18);

	dev_dbg(&priv->i2c->dev, "%s(): tune done\n",
			__func__);

	priv->frequency = frequency;
	return 0;
}

static int helene_set_params(struct dvb_frontend *fe)
{
	u8 data[MAX_WRITE_REGSIZE];
	u32 frequency;
	enum helene_tv_system_t tv_system;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct helene_priv *priv = fe->tuner_priv;
	int frequencykHz = p->frequency / 1000;

	dev_dbg(&priv->i2c->dev, "%s(): tune frequency %dkHz\n",
			__func__, frequencykHz);
	tv_system = helene_get_tv_system(fe);

	if (tv_system == SONY_HELENE_TV_SYSTEM_UNKNOWN) {
		dev_dbg(&priv->i2c->dev, "%s(): unknown DTV system\n",
				__func__);
		return -EINVAL;
	}
	if (priv->set_tuner)
		priv->set_tuner(priv->set_tuner_data, 1);
	frequency = roundup(p->frequency / 1000, 25);

	/* mode select */
	helene_write_reg(priv, 0x01, 0x00);

	/* Disable IF signal output */
	helene_write_reg(priv, 0x74, 0x02);

	if (priv->state == STATE_SLEEP)
		helene_leave_power_save(priv);

	/* Initial setting for internal analog block (0x91, 0x92) */
	if ((tv_system == SONY_HELENE_DTV_DVBC_6) ||
			(tv_system == SONY_HELENE_DTV_DVBC_8)) {
		data[0] = 0x16;
		data[1] = 0x26;
	} else {
		data[0] = 0x10;
		data[1] = 0x20;
	}
	helene_write_regs(priv, 0x91, data, 2);

	/* Setting for analog block */
	if (TERR_INTERNAL_LOOPFILTER_AVAILABLE(tv_system))
		data[0] = 0x90;
	else
		data[0] = 0x00;

	/* Setting for local polarity (0x9D) */
	data[1] = (uint8_t)(terr_params[tv_system].IS_LOWERLOCAL & 0x01);
	helene_write_regs(priv, 0x9C, data, 2);

	/* Enable for analog block */
	data[0] = 0xEE;
	data[1] = 0x02;
	data[2] = 0x1E;
	data[3] = 0x67; /* Tuning setting for CPU */

	/* Setting for PLL reference divider for xtal=24MHz */
	if ((tv_system == SONY_HELENE_DTV_DVBC_6) ||
			(tv_system == SONY_HELENE_DTV_DVBC_8))
		data[4] = 0x18;
	else
		data[4] = 0x03;

	/* Tuning setting for analog block */
	if (TERR_INTERNAL_LOOPFILTER_AVAILABLE(tv_system)) {
		data[5] = 0x38;
		data[6] = 0x1E;
		data[7] = 0x02;
		data[8] = 0x24;
	} else if ((tv_system == SONY_HELENE_DTV_DVBC_6) ||
			(tv_system == SONY_HELENE_DTV_DVBC_8)) {
		data[5] = 0x1C;
		data[6] = 0x78;
		data[7] = 0x08;
		data[8] = 0x1C;
	} else {
		data[5] = 0xB4;
		data[6] = 0x78;
		data[7] = 0x08;
		data[8] = 0x30;
	}
	helene_write_regs(priv, 0x5E, data, 9);

	/* LT_AMP_EN should be 0 */
	helene_set_reg_bits(priv, 0x67, 0x0, 0x02);

	/* Setting for IFOUT_LIMIT */
	data[0] = 0x00; /* 1.5Vpp */

	/* RF_GAIN setting */
	if (terr_params[tv_system].RF_GAIN == HELENE_AUTO)
		data[1] = 0x80; /* RF_GAIN_SEL = 1 */
	else
		data[1] = (uint8_t)((terr_params[tv_system].RF_GAIN
					<< 4) & 0x70);

	/* IF_BPF_GC setting */
	data[1] |= (uint8_t)(terr_params[tv_system].IF_BPF_GC & 0x0F);

	/* Setting for internal RFAGC (0x6A, 0x6B, 0x6C) */
	data[2] = 0x00;
	if (frequencykHz <= 172000) {
		data[3] = (uint8_t)(terr_params[tv_system].RFOVLD_DET_LV1_VL
				& 0x0F);
		data[4] = (uint8_t)(terr_params[tv_system].IFOVLD_DET_LV_VL
				& 0x07);
	} else if (frequencykHz <= 464000) {
		data[3] = (uint8_t)(terr_params[tv_system].RFOVLD_DET_LV1_VH
				& 0x0F);
		data[4] = (uint8_t)(terr_params[tv_system].IFOVLD_DET_LV_VH
				& 0x07);
	} else {
		data[3] = (uint8_t)(terr_params[tv_system].RFOVLD_DET_LV1_U
				& 0x0F);
		data[4] = (uint8_t)(terr_params[tv_system].IFOVLD_DET_LV_U
				& 0x07);
	}
	data[4] |= 0x20;

	/* Setting for IF frequency and bandwidth */

	/* IF filter center frequency offset (IF_BPF_F0) (0x6D) */
	data[5] = (uint8_t)((terr_params[tv_system].IF_BPF_F0 << 4) & 0x30);

	/* IF filter band width (BW) (0x6D) */
	data[5] |= (uint8_t)(terr_params[tv_system].BW & 0x03);

	/* IF frequency offset value (FIF_OFFSET) (0x6E) */
	data[6] = (uint8_t)(terr_params[tv_system].FIF_OFFSET & 0x1F);

	/* IF band width offset value (BW_OFFSET) (0x6F) */
	data[7] = (uint8_t)(terr_params[tv_system].BW_OFFSET & 0x1F);

	/* RF tuning frequency setting (0x70, 0x71, 0x72) */
	data[8]  = (uint8_t)(frequencykHz & 0xFF);         /* FRF_L */
	data[9]  = (uint8_t)((frequencykHz >> 8) & 0xFF);  /* FRF_M */
	data[10] = (uint8_t)((frequencykHz >> 16)
			& 0x0F); /* FRF_H (bit[3:0]) */

	/* Tuning command */
	data[11] = 0xFF;

	/* Enable IF output, AGC and IFOUT pin selection (0x74) */
	data[12] = 0x01;

	if ((tv_system == SONY_HELENE_DTV_DVBC_6) ||
			(tv_system == SONY_HELENE_DTV_DVBC_8)) {
		data[13] = 0xD9;
		data[14] = 0x0F;
		data[15] = 0x24;
		data[16] = 0x87;
	} else {
		data[13] = 0x99;
		data[14] = 0x00;
		data[15] = 0x24;
		data[16] = 0x87;
	}

	helene_write_regs(priv, 0x68, data, 17);

	dev_dbg(&priv->i2c->dev, "%s(): tune done\n",
			__func__);

	priv->frequency = frequency;
	return 0;
}

static int helene_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct helene_priv *priv = fe->tuner_priv;

	*frequency = priv->frequency * 1000;
	return 0;
}

static const struct dvb_tuner_ops helene_tuner_ops = {
	.info = {
		.name = "Sony HELENE Ter tuner",
		.frequency_min = 1000000,
		.frequency_max = 1200000000,
		.frequency_step = 25000,
	},
	.init = helene_init,
	.release = helene_release,
	.sleep = helene_sleep,
	.set_params = helene_set_params,
	.get_frequency = helene_get_frequency,
};

static const struct dvb_tuner_ops helene_tuner_ops_s = {
	.info = {
		.name = "Sony HELENE Sat tuner",
		.frequency_min = 500000,
		.frequency_max = 2500000,
		.frequency_step = 1000,
	},
	.init = helene_init,
	.release = helene_release,
	.sleep = helene_sleep,
	.set_params = helene_set_params_s,
	.get_frequency = helene_get_frequency,
};

/* power-on tuner
 * call once after reset
 */
static int helene_x_pon(struct helene_priv *priv)
{
	/* RFIN matching in power save (terrestrial) = ACTIVE */
	/* RFIN matching in power save (satellite) = ACTIVE */
	u8 dataT[] = { 0x06, 0x00, 0x02, 0x00 };
	/* SAT_RF_ACTIVE = true, lnaOff = false, terrRfActive = true */
	u8 dataS[] = { 0x05, 0x06 };
	u8 cdata[] = {0x7A, 0x01};
	u8 data[20];
	u8 rdata[2];

	/* mode select */
	helene_write_reg(priv, 0x01, 0x00);

	helene_write_reg(priv, 0x67, dataT[3]);
	helene_write_reg(priv, 0x43, dataS[1]);
	helene_write_regs(priv, 0x5E, dataT, 3);
	helene_write_reg(priv, 0x0C, dataS[0]);

	/* Initial setting for internal logic block */
	helene_write_regs(priv, 0x99, cdata, sizeof(cdata));

	/* 0x81 - 0x94 */
	data[0] = 0x18; /* xtal 24 MHz */
	data[1] = (uint8_t)(0x80 | (0x04 & 0x1F)); /* 4 x 25 = 100uA */
	data[2] = (uint8_t)(0x80 | (0x26 & 0x7F)); /* 38 x 0.25 = 9.5pF */
	data[3] = 0x80; /* REFOUT signal output 500mVpp */
	data[4] = 0x00; /* GPIO settings */
	data[5] = 0x00; /* GPIO settings */
	data[6] = 0xC4; /* Clock enable for internal logic block */
	data[7] = 0x40; /* Start CPU boot-up */
	data[8] = 0x10; /* For burst-write */

	/* Setting for internal RFAGC */
	data[9] = 0x00;
	data[10] = 0x45;
	data[11] = 0x75;

	data[12] = 0x07; /* Setting for analog block */

	/* Initial setting for internal analog block */
	data[13] = 0x1C;
	data[14] = 0x3F;
	data[15] = 0x02;
	data[16] = 0x10;
	data[17] = 0x20;
	data[18] = 0x0A;
	data[19] = 0x00;

	helene_write_regs(priv, 0x81, data, sizeof(data));

	/* Setting for internal RFAGC */
	helene_write_reg(priv, 0x9B, 0x00);

	msleep(20);

	/* Check CPU_STT/CPU_ERR */
	helene_read_regs(priv, 0x1A, rdata, sizeof(rdata));

	if (rdata[0] != 0x00) {
		dev_err(&priv->i2c->dev,
				"HELENE tuner CPU error 0x%x\n", rdata[0]);
		return -EIO;
	}

	/* VCO current setting */
	cdata[0] = 0x90;
	cdata[1] = 0x06;
	helene_write_regs(priv, 0x17, cdata, sizeof(cdata));
	msleep(20);
	helene_read_reg(priv, 0x19, data);
	helene_write_reg(priv, 0x95, (uint8_t)((data[0] >> 4) & 0x0F));

	/* Disable IF signal output */
	helene_write_reg(priv, 0x74, 0x02);

	/* Standby setting for CPU */
	helene_write_reg(priv, 0x88, 0x00);

	/* Standby setting for internal logic block */
	helene_write_reg(priv, 0x87, 0xC0);

	/* Load capacitance control setting for crystal oscillator */
	helene_write_reg(priv, 0x80, 0x01);

	/* Satellite initial setting */
	cdata[0] = 0x07;
	cdata[1] = 0x00;
	helene_write_regs(priv, 0x41, cdata, sizeof(cdata));

	dev_info(&priv->i2c->dev,
			"HELENE tuner x_pon done\n");

	return 0;
}

struct dvb_frontend *helene_attach_s(struct dvb_frontend *fe,
		const struct helene_config *config,
		struct i2c_adapter *i2c)
{
	struct helene_priv *priv = NULL;

	priv = kzalloc(sizeof(struct helene_priv), GFP_KERNEL);
	if (priv == NULL)
		return NULL;
	priv->i2c_address = (config->i2c_address >> 1);
	priv->i2c = i2c;
	priv->set_tuner_data = config->set_tuner_priv;
	priv->set_tuner = config->set_tuner_callback;
	priv->xtal = config->xtal;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	if (helene_x_pon(priv) != 0) {
		kfree(priv);
		return NULL;
	}

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	memcpy(&fe->ops.tuner_ops, &helene_tuner_ops_s,
			sizeof(struct dvb_tuner_ops));
	fe->tuner_priv = priv;
	dev_info(&priv->i2c->dev,
			"Sony HELENE Sat attached on addr=%x at I2C adapter %p\n",
			priv->i2c_address, priv->i2c);
	return fe;
}
EXPORT_SYMBOL(helene_attach_s);

struct dvb_frontend *helene_attach(struct dvb_frontend *fe,
		const struct helene_config *config,
		struct i2c_adapter *i2c)
{
	struct helene_priv *priv = NULL;

	priv = kzalloc(sizeof(struct helene_priv), GFP_KERNEL);
	if (priv == NULL)
		return NULL;
	priv->i2c_address = (config->i2c_address >> 1);
	priv->i2c = i2c;
	priv->set_tuner_data = config->set_tuner_priv;
	priv->set_tuner = config->set_tuner_callback;
	priv->xtal = config->xtal;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	if (helene_x_pon(priv) != 0) {
		kfree(priv);
		return NULL;
	}

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	memcpy(&fe->ops.tuner_ops, &helene_tuner_ops,
			sizeof(struct dvb_tuner_ops));
	fe->tuner_priv = priv;
	dev_info(&priv->i2c->dev,
			"Sony HELENE Ter attached on addr=%x at I2C adapter %p\n",
			priv->i2c_address, priv->i2c);
	return fe;
}
EXPORT_SYMBOL(helene_attach);

MODULE_DESCRIPTION("Sony HELENE Sat/Ter tuner driver");
MODULE_AUTHOR("Abylay Ospan <aospan@netup.ru>");
MODULE_LICENSE("GPL");
