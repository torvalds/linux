/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * STK1160 driver
 *
 * Copyright (C) 2012 Ezequiel Garcia
 * <elezegarcia--a.t--gmail.com>
 *
 * Based on Easycap driver by R.M. Thomas
 *	Copyright (C) 2010 R.M. Thomas
 *	<rmthomas--a.t--sciolus.org>
 */

/* GPIO Control */
#define STK1160_GCTRL			0x000

/* Remote Wakeup Control */
#define STK1160_RMCTL			0x00c

/* Power-on Strapping Data */
#define STK1160_POSVA			0x010
#define STK1160_POSV_L			0x010
#define STK1160_POSV_M			0x011
#define STK1160_POSV_H			0x012
#define  STK1160_POSV_L_ACDOUT		BIT(3)
#define  STK1160_POSV_L_ACSYNC		BIT(2)

/*
 * Decoder Control Register:
 * This byte controls capture start/stop
 * with bit #7 (0x?? OR 0x80 to activate).
 */
#define STK1160_DCTRL			0x100

/*
 * Decimation Control Register:
 * Byte 104: Horizontal Decimation Line Unit Count
 * Byte 105: Vertical Decimation Line Unit Count
 * Byte 106: Decimation Control
 * Bit 0 - Horizontal Decimation Control
 *   0 Horizontal decimation is disabled.
 *   1 Horizontal decimation is enabled.
 * Bit 1 - Decimates Half or More Column
 *   0 Decimates less than half from original column,
 *     send count unit (0x105) before each unit skipped.
 *   1 Decimates half or more from original column,
 *     skip count unit (0x105) before each unit sent.
 * Bit 2 - Vertical Decimation Control
 *   0 Vertical decimation is disabled.
 *   1 Vertical decimation is enabled.
 * Bit 3 - Vertical Greater or Equal to Half
 *   0 Decimates less than half from original row,
 *     send count unit (0x105) before each unit skipped.
 *   1 Decimates half or more from original row,
 *     skip count unit (0x105) before each unit sent.
 * Bit 4 - Decimation Unit
 *  0 Decimation will work with 2 rows or columns per unit.
 *  1 Decimation will work with 4 rows or columns per unit.
 */
#define STK1160_DMCTRL_H_UNITS		0x104
#define STK1160_DMCTRL_V_UNITS		0x105
#define STK1160_DMCTRL			0x106
#define  STK1160_H_DEC_EN		BIT(0)
#define  STK1160_H_DEC_MODE		BIT(1)
#define  STK1160_V_DEC_EN		BIT(2)
#define  STK1160_V_DEC_MODE		BIT(3)
#define  STK1160_DEC_UNIT_SIZE		BIT(4)

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

/* Alternate Serial Interface Control */
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
#define  STK1160_AC97CTL_0_CR		BIT(1)
#define  STK1160_AC97CTL_0_CW		BIT(2)

/* Use [0:6] bits of register 0x504 to set codec command address */
#define STK1160_AC97_ADDR		0x504
/* Use [16:31] bits of register 0x500 to set codec command data */
#define STK1160_AC97_CMD		0x502

/* Audio I2S Interface */
#define STK1160_I2SCTL			0x50c

/* EEPROM Interface */
#define STK1160_EEPROM_SZ		0x5f0
