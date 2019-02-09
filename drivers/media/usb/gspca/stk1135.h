/*
 * STK1135 registers
 *
 * Copyright (c) 2013 Ondrej Zary
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define STK1135_REG_GCTRL	0x000	/* GPIO control */
#define STK1135_REG_ICTRL	0x004	/* Interrupt control */
#define STK1135_REG_IDATA	0x008	/* Interrupt data */
#define STK1135_REG_RMCTL	0x00c	/* Remote wakeup control */
#define STK1135_REG_POSVA	0x010	/* Power-on strapping data */

#define STK1135_REG_SENSO	0x018	/* Sensor select options */
#define STK1135_REG_PLLFD	0x01c	/* PLL frequency divider */

#define STK1135_REG_SCTRL	0x100	/* Sensor control register */
#define STK1135_REG_DCTRL	0x104	/* Decimation control register */
#define STK1135_REG_CISPO	0x110	/* Capture image starting position */
#define STK1135_REG_CIEPO	0x114	/* Capture image ending position */
#define STK1135_REG_TCTRL	0x120	/* Test data control */

#define STK1135_REG_SICTL	0x200	/* Serial interface control register */
#define STK1135_REG_SBUSW	0x204	/* Serial bus write */
#define STK1135_REG_SBUSR	0x208	/* Serial bus read */
#define STK1135_REG_SCSI	0x20c	/* Software control serial interface */
#define STK1135_REG_GSBWP	0x210	/* General serial bus write port */
#define STK1135_REG_GSBRP	0x214	/* General serial bus read port */
#define STK1135_REG_ASIC	0x2fc	/* Alternate serial interface control */

#define STK1135_REG_TMGEN	0x300	/* Timing generator */
#define STK1135_REG_TCP1	0x350	/* Timing control parameter 1 */

struct stk1135_pkt_header {
	u8 flags;
	u8 seq;
	__le16 gpio;
} __packed;

#define STK1135_HDR_FRAME_START	(1 << 7)
#define STK1135_HDR_ODD		(1 << 6)
#define STK1135_HDR_I2C_VBLANK	(1 << 5)

#define STK1135_HDR_SEQ_MASK	0x3f
