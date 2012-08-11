/*
 * STK1160 driver
 *
 * Copyright (C) 2012 Ezequiel Garcia
 * <elezegarcia--a.t--gmail.com>
 *
 * Based on Easycap driver by R.M. Thomas
 *	Copyright (C) 2010 R.M. Thomas
 *	<rmthomas--a.t--sciolus.org>
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
 *
 */

/* GPIO Control */
#define STK1160_GCTRL			0x000

/* Remote Wakup Control */
#define STK1160_RMCTL			0x00c

/*
 * Decoder Control Register:
 * This byte controls capture start/stop
 * with bit #7 (0x?? OR 0x80 to activate).
 */
#define STK1160_DCTRL			0x100

/* Capture Frame Start Position */
#define STK116_CFSPO			0x110
#define STK116_CFSPO_STX_L		0x110
#define STK116_CFSPO_STX_H		0x111
#define STK116_CFSPO_STY_L		0x112
#define STK116_CFSPO_STY_H		0x113

/* Capture Frame End Position */
#define STK116_CFEPO			0x114
#define STK116_CFEPO_ENX_L		0x114
#define STK116_CFEPO_ENX_H		0x115
#define STK116_CFEPO_ENY_L		0x116
#define STK116_CFEPO_ENY_H		0x117

/* Serial Interface Control  */
#define STK1160_SICTL			0x200
#define STK1160_SICTL_CD		0x202
#define STK1160_SICTL_SDA		0x203

/* Serial Bus Write */
#define STK1160_SBUSW			0x204
#define STK1160_SBUSW_WA		0x204
#define STK1160_SBUSW_WD		0x205

/* Serial Bus Read */
#define STK1160_SBUSR			0x208
#define STK1160_SBUSR_RA		0x208
#define STK1160_SBUSR_RD		0x209

/* Alternate Serial Inteface Control */
#define STK1160_ASIC			0x2fc

/* PLL Select Options */
#define STK1160_PLLSO			0x018

/* PLL Frequency Divider */
#define STK1160_PLLFD			0x01c

/* Timing Generator */
#define STK1160_TIGEN			0x300

/* Timing Control Parameter */
#define STK1160_TICTL			0x350

/* AC97 Audio Control */
#define STK1160_AC97CTL_0		0x500
#define STK1160_AC97CTL_1		0x504

/* Use [0:6] bits of register 0x504 to set codec command address */
#define STK1160_AC97_ADDR		0x504
/* Use [16:31] bits of register 0x500 to set codec command data */
#define STK1160_AC97_CMD		0x502

/* Audio I2S Interface */
#define STK1160_I2SCTL			0x50c

/* EEPROM Interface */
#define STK1160_EEPROM_SZ		0x5f0
