// SPDX-License-Identifier: GPL-2.0+
/*
 * Definitions for the PLX-9052 PCI interface chip
 *
 * Copyright (C) 2002 MEV Ltd. <http://www.mev.co.uk/>
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2000 David A. Schleef <ds@schleef.org>
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

#ifndef _PLX9052_H_
#define _PLX9052_H_

/*
 * INTCSR - Interrupt Control/Status register
 */
#define PLX9052_INTCSR			0x4c
#define PLX9052_INTCSR_LI1ENAB		BIT(0)	/* LI1 enabled */
#define PLX9052_INTCSR_LI1POL		BIT(1)	/* LI1 active high */
#define PLX9052_INTCSR_LI1STAT		BIT(2)	/* LI1 active */
#define PLX9052_INTCSR_LI2ENAB		BIT(3)	/* LI2 enabled */
#define PLX9052_INTCSR_LI2POL		BIT(4)	/* LI2 active high */
#define PLX9052_INTCSR_LI2STAT		BIT(5)	/* LI2 active */
#define PLX9052_INTCSR_PCIENAB		BIT(6)	/* PCIINT enabled */
#define PLX9052_INTCSR_SOFTINT		BIT(7)	/* generate soft int */
#define PLX9052_INTCSR_LI1SEL		BIT(8)	/* LI1 edge */
#define PLX9052_INTCSR_LI2SEL		BIT(9)	/* LI2 edge */
#define PLX9052_INTCSR_LI1CLRINT	BIT(10)	/* LI1 clear int */
#define PLX9052_INTCSR_LI2CLRINT	BIT(11)	/* LI2 clear int */
#define PLX9052_INTCSR_ISAMODE		BIT(12)	/* ISA interface mode */

/*
 * CNTRL - User I/O, Direct Slave Response, Serial EEPROM, and
 * Initialization Control register
 */
#define PLX9052_CNTRL			0x50
#define PLX9052_CNTRL_WAITO		BIT(0)	/* UIO0 or WAITO# select */
#define PLX9052_CNTRL_UIO0_DIR		BIT(1)	/* UIO0 direction */
#define PLX9052_CNTRL_UIO0_DATA		BIT(2)	/* UIO0 data */
#define PLX9052_CNTRL_LLOCKO		BIT(3)	/* UIO1 or LLOCKo# select */
#define PLX9052_CNTRL_UIO1_DIR		BIT(4)	/* UIO1 direction */
#define PLX9052_CNTRL_UIO1_DATA		BIT(5)	/* UIO1 data */
#define PLX9052_CNTRL_CS2		BIT(6)	/* UIO2 or CS2# select */
#define PLX9052_CNTRL_UIO2_DIR		BIT(7)	/* UIO2 direction */
#define PLX9052_CNTRL_UIO2_DATA		BIT(8)	/* UIO2 data */
#define PLX9052_CNTRL_CS3		BIT(9)	/* UIO3 or CS3# select */
#define PLX9052_CNTRL_UIO3_DIR		BIT(10)	/* UIO3 direction */
#define PLX9052_CNTRL_UIO3_DATA		BIT(11)	/* UIO3 data */
#define PLX9052_CNTRL_PCIBAR(x)		(((x) & 0x3) << 12)
#define PLX9052_CNTRL_PCIBAR01		PLX9052_CNTRL_PCIBAR(0)	/* mem and IO */
#define PLX9052_CNTRL_PCIBAR0		PLX9052_CNTRL_PCIBAR(1)	/* mem only */
#define PLX9052_CNTRL_PCIBAR1		PLX9052_CNTRL_PCIBAR(2)	/* IO only */
#define PLX9052_CNTRL_PCI2_1_FEATURES	BIT(14)	/* PCI v2.1 features enabled */
#define PLX9052_CNTRL_PCI_R_W_FLUSH	BIT(15)	/* read w/write flush mode */
#define PLX9052_CNTRL_PCI_R_NO_FLUSH	BIT(16)	/* read no flush mode */
#define PLX9052_CNTRL_PCI_R_NO_WRITE	BIT(17)	/* read no write mode */
#define PLX9052_CNTRL_PCI_W_RELEASE	BIT(18)	/* write release bus mode */
#define PLX9052_CNTRL_RETRY_CLKS(x)	(((x) & 0xf) << 19) /* retry clks */
#define PLX9052_CNTRL_LOCK_ENAB		BIT(23)	/* slave LOCK# enable */
#define PLX9052_CNTRL_EEPROM_MASK	(0x1f << 24) /* EEPROM bits */
#define PLX9052_CNTRL_EEPROM_CLK	BIT(24)	/* EEPROM clock */
#define PLX9052_CNTRL_EEPROM_CS		BIT(25)	/* EEPROM chip select */
#define PLX9052_CNTRL_EEPROM_DOUT	BIT(26)	/* EEPROM write bit */
#define PLX9052_CNTRL_EEPROM_DIN	BIT(27)	/* EEPROM read bit */
#define PLX9052_CNTRL_EEPROM_PRESENT	BIT(28)	/* EEPROM present */
#define PLX9052_CNTRL_RELOAD_CFG	BIT(29)	/* reload configuration */
#define PLX9052_CNTRL_PCI_RESET		BIT(30)	/* PCI adapter reset */
#define PLX9052_CNTRL_MASK_REV		BIT(31)	/* mask revision */

#endif /* _PLX9052_H_ */
