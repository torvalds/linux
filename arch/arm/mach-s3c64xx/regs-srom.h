/*
 * Copyright 2009 Andy Green <andy@warmcat.com>
 *
 * S3C64XX SROM definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __MACH_S3C64XX_REGS_SROM_H
#define __MACH_S3C64XX_REGS_SROM_H __FILE__

#define S3C64XX_SROMREG(x)	(S3C_VA_MEM + (x))

#define S3C64XX_SROM_BW		S3C64XX_SROMREG(0)
#define S3C64XX_SROM_BC0	S3C64XX_SROMREG(4)
#define S3C64XX_SROM_BC1	S3C64XX_SROMREG(8)
#define S3C64XX_SROM_BC2	S3C64XX_SROMREG(0xc)
#define S3C64XX_SROM_BC3	S3C64XX_SROMREG(0x10)
#define S3C64XX_SROM_BC4	S3C64XX_SROMREG(0x14)
#define S3C64XX_SROM_BC5	S3C64XX_SROMREG(0x18)

/*
 * one register BW holds 5 x 4-bit packed settings for NCS0 - NCS4
 */

#define S3C64XX_SROM_BW__DATAWIDTH__SHIFT	0
#define S3C64XX_SROM_BW__WAITENABLE__SHIFT	2
#define S3C64XX_SROM_BW__BYTEENABLE__SHIFT	3
#define S3C64XX_SROM_BW__CS_MASK		0xf

#define S3C64XX_SROM_BW__NCS0__SHIFT	0
#define S3C64XX_SROM_BW__NCS1__SHIFT	4
#define S3C64XX_SROM_BW__NCS2__SHIFT	8
#define S3C64XX_SROM_BW__NCS3__SHIFT	0xc
#define S3C64XX_SROM_BW__NCS4__SHIFT	0x10

/*
 * applies to same to BCS0 - BCS4
 */

#define S3C64XX_SROM_BCX__PMC__SHIFT	0
#define S3C64XX_SROM_BCX__PMC__MASK	3
#define S3C64XX_SROM_BCX__TACP__SHIFT	4
#define S3C64XX_SROM_BCX__TACP__MASK	0xf
#define S3C64XX_SROM_BCX__TCAH__SHIFT	8
#define S3C64XX_SROM_BCX__TCAH__MASK	0xf
#define S3C64XX_SROM_BCX__TCOH__SHIFT	12
#define S3C64XX_SROM_BCX__TCOH__MASK	0xf
#define S3C64XX_SROM_BCX__TACC__SHIFT	16
#define S3C64XX_SROM_BCX__TACC__MASK	0x1f
#define S3C64XX_SROM_BCX__TCOS__SHIFT	24
#define S3C64XX_SROM_BCX__TCOS__MASK	0xf
#define S3C64XX_SROM_BCX__TACS__SHIFT	28
#define S3C64XX_SROM_BCX__TACS__MASK	0xf

#endif /* __MACH_S3C64XX_REGS_SROM_H */
