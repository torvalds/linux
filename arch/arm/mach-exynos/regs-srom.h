/*
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * S5P SROMC register definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __PLAT_SAMSUNG_REGS_SROM_H
#define __PLAT_SAMSUNG_REGS_SROM_H __FILE__

#include <mach/map.h>

#define S5P_SROMREG(x)		(S5P_VA_SROMC + (x))

#define S5P_SROM_BW		S5P_SROMREG(0x0)
#define S5P_SROM_BC0		S5P_SROMREG(0x4)
#define S5P_SROM_BC1		S5P_SROMREG(0x8)
#define S5P_SROM_BC2		S5P_SROMREG(0xc)
#define S5P_SROM_BC3		S5P_SROMREG(0x10)
#define S5P_SROM_BC4		S5P_SROMREG(0x14)
#define S5P_SROM_BC5		S5P_SROMREG(0x18)

/* one register BW holds 4 x 4-bit packed settings for NCS0 - NCS3 */

#define S5P_SROM_BW__DATAWIDTH__SHIFT		0
#define S5P_SROM_BW__ADDRMODE__SHIFT		1
#define S5P_SROM_BW__WAITENABLE__SHIFT		2
#define S5P_SROM_BW__BYTEENABLE__SHIFT		3

#define S5P_SROM_BW__CS_MASK			0xf

#define S5P_SROM_BW__NCS0__SHIFT		0
#define S5P_SROM_BW__NCS1__SHIFT		4
#define S5P_SROM_BW__NCS2__SHIFT		8
#define S5P_SROM_BW__NCS3__SHIFT		12
#define S5P_SROM_BW__NCS4__SHIFT		16
#define S5P_SROM_BW__NCS5__SHIFT		20

/* applies to same to BCS0 - BCS3 */

#define S5P_SROM_BCX__PMC__SHIFT		0
#define S5P_SROM_BCX__TACP__SHIFT		4
#define S5P_SROM_BCX__TCAH__SHIFT		8
#define S5P_SROM_BCX__TCOH__SHIFT		12
#define S5P_SROM_BCX__TACC__SHIFT		16
#define S5P_SROM_BCX__TCOS__SHIFT		24
#define S5P_SROM_BCX__TACS__SHIFT		28

#endif /* __PLAT_SAMSUNG_REGS_SROM_H */
