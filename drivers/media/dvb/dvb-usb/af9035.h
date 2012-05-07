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

/* prefix for dvb-usb log writings */
#define DVB_USB_LOG_PREFIX "af9035"

#include "dvb-usb.h"
#include "af9033.h"
#include "tua9001.h"
#include "fc0011.h"
#include "mxl5007t.h"
#include "tda18218.h"

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
	bool dual_mode;
	bool hw_not_supported;

	struct af9033_config af9033_config[2];
};

u32 clock_lut[] = {
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

u32 clock_lut_it9135[] = {
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

/* EEPROM locations */
#define EEPROM_IR_MODE            0x430d
#define EEPROM_DUAL_MODE          0x4326
#define EEPROM_IR_TYPE            0x4329
#define EEPROM_1_IFFREQ_L         0x432d
#define EEPROM_1_IFFREQ_H         0x432e
#define EEPROM_1_TUNER_ID         0x4331
#define EEPROM_2_IFFREQ_L         0x433d
#define EEPROM_2_IFFREQ_H         0x433e
#define EEPROM_2_TUNER_ID         0x4341

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
