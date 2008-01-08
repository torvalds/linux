/*
   tm6000-regs.h - driver for TM5600/TM6000 USB video capture devices

   Copyright (C) 2006-2007 Mauro Carvalho Chehab <mchehab@infradead.org>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation version 2

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * Define TV Master TM5600/TM6000 Request codes
 */
#define REQ_00_SET_IR_VALUE		0
#define REQ_01_SET_WAKEUP_IRCODE	1
#define REQ_02_GET_IR_CODE		2
#define REQ_03_SET_GET_MCU_PIN		3
#define REQ_04_EN_DISABLE_MCU_INT	4
#define REQ_05_SET_GET_USBREG		5
	/* Write: RegNum, Value, 0 */
	/* Read : RegNum, Value, 1, RegStatus */
#define REQ_06_SET_GET_USBREG_BIT	6
#define REQ_07_SET_GET_AVREG		7
	/* Write: RegNum, Value, 0 */
	/* Read : RegNum, Value, 1, RegStatus */
#define REQ_08_SET_GET_AVREG_BIT	8
#define REQ_09_SET_GET_TUNER_FQ		9
#define REQ_10_SET_TUNER_SYSTEM		10
#define REQ_11_SET_EEPROM_ADDR		11
#define REQ_12_SET_GET_EEPROMBYTE	12
#define REQ_13_GET_EEPROM_SEQREAD	13
#define REQ_14_SET_GET_I2C_WR2_RDN	14
#define REQ_15_SET_GET_I2CBYTE		15
	/* Write: Subaddr, Slave Addr, value, 0 */
	/* Read : Subaddr, Slave Addr, value, 1 */
#define REQ_16_SET_GET_I2C_WR1_RDN	16
	/* Subaddr, Slave Addr, 0, length */
#define REQ_17_SET_GET_I2CFP		17
	/* Write: Slave Addr, register, value */
	/* Read : Slave Addr, register, 2, data */

/*
 * Define TV Master TM5600/TM6000 GPIO lines
 */

#define TM6000_GPIO_CLK		0x101
#define TM6000_GPIO_DATA	0x100

#define TM6000_GPIO_1		0x102
#define TM6000_GPIO_2		0x103
#define TM6000_GPIO_3		0x104
#define TM6000_GPIO_4		0x300
#define TM6000_GPIO_5		0x301
#define TM6000_GPIO_6		0x304
#define TM6000_GPIO_7		0x305

/* tm6010 defines GPIO with different values */
#define TM6010_GPIO_0      0x0102
#define TM6010_GPIO_1      0x0103
#define TM6010_GPIO_2      0x0104
#define TM6010_GPIO_3      0x0105
#define TM6010_GPIO_4      0x0106
#define TM6010_GPIO_5      0x0107
#define TM6010_GPIO_6      0x0300
#define TM6010_GPIO_7      0x0301
#define TM6010_GPIO_9      0x0305
/*
 * Define TV Master TM5600/TM6000 URB message codes and length
 */

enum {
	TM6000_URB_MSG_VIDEO=1,
	TM6000_URB_MSG_AUDIO,
	TM6000_URB_MSG_VBI,
	TM6000_URB_MSG_PTS,
	TM6000_URB_MSG_ERR,
};
