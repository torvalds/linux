/*
 * NXP TDA10071 + Conexant CX24118A DVB-S/S2 demodulator + tuner driver
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

#ifndef TDA10071_PRIV
#define TDA10071_PRIV

#include "dvb_frontend.h"
#include "tda10071.h"
#include <linux/firmware.h>
#include <linux/regmap.h>

struct tda10071_dev {
	struct dvb_frontend fe;
	struct i2c_client *client;
	struct regmap *regmap;
	struct mutex cmd_execute_mutex;
	u32 clk;
	u16 i2c_wr_max;
	u8 ts_mode;
	bool spec_inv;
	u8 pll_multiplier;
	u8 tuner_i2c_addr;

	u8 meas_count;
	u32 dvbv3_ber;
	enum fe_status fe_status;
	enum fe_delivery_system delivery_system;
	bool warm; /* FW running */
	u64 post_bit_error;
	u64 block_error;
};

static struct tda10071_modcod {
	enum fe_delivery_system delivery_system;
	enum fe_modulation modulation;
	enum fe_code_rate fec;
	u8 val;
} TDA10071_MODCOD[] = {
	/* NBC-QPSK */
	{ SYS_DVBS2, QPSK,  FEC_AUTO, 0x00 },
	{ SYS_DVBS2, QPSK,  FEC_1_2,  0x04 },
	{ SYS_DVBS2, QPSK,  FEC_3_5,  0x05 },
	{ SYS_DVBS2, QPSK,  FEC_2_3,  0x06 },
	{ SYS_DVBS2, QPSK,  FEC_3_4,  0x07 },
	{ SYS_DVBS2, QPSK,  FEC_4_5,  0x08 },
	{ SYS_DVBS2, QPSK,  FEC_5_6,  0x09 },
	{ SYS_DVBS2, QPSK,  FEC_8_9,  0x0a },
	{ SYS_DVBS2, QPSK,  FEC_9_10, 0x0b },
	/* 8PSK */
	{ SYS_DVBS2, PSK_8, FEC_AUTO, 0x00 },
	{ SYS_DVBS2, PSK_8, FEC_3_5,  0x0c },
	{ SYS_DVBS2, PSK_8, FEC_2_3,  0x0d },
	{ SYS_DVBS2, PSK_8, FEC_3_4,  0x0e },
	{ SYS_DVBS2, PSK_8, FEC_5_6,  0x0f },
	{ SYS_DVBS2, PSK_8, FEC_8_9,  0x10 },
	{ SYS_DVBS2, PSK_8, FEC_9_10, 0x11 },
	/* QPSK */
	{ SYS_DVBS,  QPSK,  FEC_AUTO, 0x2d },
	{ SYS_DVBS,  QPSK,  FEC_1_2,  0x2e },
	{ SYS_DVBS,  QPSK,  FEC_2_3,  0x2f },
	{ SYS_DVBS,  QPSK,  FEC_3_4,  0x30 },
	{ SYS_DVBS,  QPSK,  FEC_5_6,  0x31 },
	{ SYS_DVBS,  QPSK,  FEC_7_8,  0x32 },
};

struct tda10071_reg_val_mask {
	u8 reg;
	u8 val;
	u8 mask;
};

/* firmware filename */
#define TDA10071_FIRMWARE "dvb-fe-tda10071.fw"

/* firmware commands */
#define CMD_DEMOD_INIT          0x10
#define CMD_CHANGE_CHANNEL      0x11
#define CMD_MPEG_CONFIG         0x13
#define CMD_TUNER_INIT          0x15
#define CMD_GET_AGCACC          0x1a

#define CMD_LNB_CONFIG          0x20
#define CMD_LNB_SEND_DISEQC     0x21
#define CMD_LNB_SET_DC_LEVEL    0x22
#define CMD_LNB_PCB_CONFIG      0x23
#define CMD_LNB_SEND_TONEBURST  0x24
#define CMD_LNB_UPDATE_REPLY    0x25

#define CMD_GET_FW_VERSION      0x35
#define CMD_SET_SLEEP_MODE      0x36
#define CMD_BER_CONTROL         0x3e
#define CMD_BER_UPDATE_COUNTERS 0x3f

/* firmware command struct */
#define TDA10071_ARGLEN      30
struct tda10071_cmd {
	u8 args[TDA10071_ARGLEN];
	u8 len;
};


#endif /* TDA10071_PRIV */
