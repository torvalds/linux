/* linux/arch/arm/mach-s5pv310/include/mach/regs-srom.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * S5PV310 - SROMC register definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_REGS_SROM_H
#define __ASM_ARCH_REGS_SROM_H __FILE__

#include <mach/map.h>

#define S5PV310_SROMREG(x)	(S5P_VA_SROMC + (x))

#define S5PV310_SROM_BW		S5PV310_SROMREG(0x0)
#define S5PV310_SROM_BC0	S5PV310_SROMREG(0x4)
#define S5PV310_SROM_BC1	S5PV310_SROMREG(0x8)
#define S5PV310_SROM_BC2	S5PV310_SROMREG(0xc)
#define S5PV310_SROM_BC3	S5PV310_SROMREG(0x10)

/* one register BW holds 4 x 4-bit packed settings for NCS0 - NCS3 */

#define S5PV310_SROM_BW__DATAWIDTH__SHIFT	0
#define S5PV310_SROM_BW__ADDRMODE__SHIFT	1
#define S5PV310_SROM_BW__WAITENABLE__SHIFT	2
#define S5PV310_SROM_BW__BYTEENABLE__SHIFT	3

#define S5PV310_SROM_BW__CS_MASK		0xf

#define S5PV310_SROM_BW__NCS0__SHIFT		0
#define S5PV310_SROM_BW__NCS1__SHIFT		4
#define S5PV310_SROM_BW__NCS2__SHIFT		8
#define S5PV310_SROM_BW__NCS3__SHIFT		12

/* applies to same to BCS0 - BCS3 */

#define S5PV310_SROM_BCX__PMC__SHIFT		0
#define S5PV310_SROM_BCX__TACP__SHIFT		4
#define S5PV310_SROM_BCX__TCAH__SHIFT		8
#define S5PV310_SROM_BCX__TCOH__SHIFT		12
#define S5PV310_SROM_BCX__TACC__SHIFT		16
#define S5PV310_SROM_BCX__TCOS__SHIFT		24
#define S5PV310_SROM_BCX__TACS__SHIFT		28

#endif /* __ASM_ARCH_REGS_SROM_H */
