/*
 * Afatech AF9035 DVB USB driver
 *
 * Copyright (C) 2009 Antti Palosaari <crope@iki.fi>
 * Copyright (C) 2012 Antti Palosaari <crope@iki.fi>
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

#ifndef AF9035_H
#define AF9035_H

#include "dvb_usb.h"
#include "af9033.h"
#include "tua9001.h"
#include "fc0011.h"
#include "fc0012.h"
#include "mxl5007t.h"
#include "tda18218.h"
#include "fc2580.h"
#include "tuner_it913x.h"

struct reg_val {
	u32 reg;
	u8  val;
};

struct reg_val_mask {
	u32 reg;
	u8  val;
	u8  mask;
};

struct usb_req {
	u8  cmd;
	u8  mbox;
	u8  wlen;
	u8  *wbuf;
	u8  rlen;
	u8  *rbuf;
};

struct state {
#define BUF_LEN 64
	u8 buf[BUF_LEN];
	u8 seq; /* packet sequence number */
	u8 prechip_version;
	u8 chip_version;
	u16 chip_type;
	u8 dual_mode:1;
	u16 eeprom_addr;
	struct af9033_config af9033_config[2];
};

static const u32 clock_lut_af9035[] = {
	20480000, /*      FPGA */
	16384000, /* 16.38 MHz */
	20480000, /* 20.48 MHz */
	36000000, /* 36.00 MHz */
	30000000, /* 30.00 MHz */
	26000000, /* 26.00 MHz */
	28000000, /* 28.00 MHz */
	32000000, /* 32.00 MHz */
	34000000, /* 34.00 MHz */
	24000000, /* 24.00 MHz */
	22000000, /* 22.00 MHz */
	12000000, /* 12.00 MHz */
};

static const u32 clock_lut_it9135[] = {
	12000000, /* 12.00 MHz */
	20480000, /* 20.48 MHz */
	36000000, /* 36.00 MHz */
	30000000, /* 30.00 MHz */
	26000000, /* 26.00 MHz */
	28000000, /* 28.00 MHz */
	32000000, /* 32.00 MHz */
	34000000, /* 34.00 MHz */
	24000000, /* 24.00 MHz */
	22000000, /* 22.00 MHz */
};

#define AF9035_FIRMWARE_AF9035 "dvb-usb-af9035-02.fw"
#define AF9035_FIRMWARE_IT9135_V1 "dvb-usb-it9135-01.fw"
#define AF9035_FIRMWARE_IT9135_V2 "dvb-usb-it9135-02.fw"

/*
 * eeprom is memory mapped as read only. Writing that memory mapped address
 * will not corrupt eeprom.
 *
 * TS mode:
 * 0  TS
 * 1  DCA + PIP
 * 3  PIP
 * n  DCA
 *
 * Values 0 and 3 are seen to this day. 0 for single TS and 3 for dual TS.
 */

#define EEPROM_BASE_AF9035        0x42fd
#define EEPROM_BASE_IT9135        0x499c
#define EEPROM_SHIFT                0x10

#define EEPROM_IR_MODE              0x10
#define EEPROM_TS_MODE              0x29
#define EEPROM_2ND_DEMOD_ADDR       0x2a
#define EEPROM_IR_TYPE              0x2c
#define EEPROM_1_IF_L               0x30
#define EEPROM_1_IF_H               0x31
#define EEPROM_1_TUNER_ID           0x34
#define EEPROM_2_IF_L               0x40
#define EEPROM_2_IF_H               0x41
#define EEPROM_2_TUNER_ID           0x44

/* USB commands */
#define CMD_MEM_RD                  0x00
#define CMD_MEM_WR                  0x01
#define CMD_I2C_RD                  0x02
#define CMD_I2C_WR                  0x03
#define CMD_IR_GET                  0x18
#define CMD_FW_DL                   0x21
#define CMD_FW_QUERYINFO            0x22
#define CMD_FW_BOOT                 0x23
#define CMD_FW_DL_BEGIN             0x24
#define CMD_FW_DL_END               0x25
#define CMD_FW_SCATTER_WR           0x29

#endif
