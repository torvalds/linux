/*
 *  arch/arm/mach-pxa/include/mach/lubbock.h
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define LUBBOCK_ETH_PHYS	PXA_CS3_PHYS

#define LUBBOCK_FPGA_PHYS	PXA_CS2_PHYS
#define LUBBOCK_FPGA_VIRT	(0xf0000000)
#define LUB_P2V(x)		((x) - LUBBOCK_FPGA_PHYS + LUBBOCK_FPGA_VIRT)
#define LUB_V2P(x)		((x) - LUBBOCK_FPGA_VIRT + LUBBOCK_FPGA_PHYS)

#ifndef __ASSEMBLY__
#  define __LUB_REG(x)		(*((volatile unsigned long *)LUB_P2V(x)))
#else
#  define __LUB_REG(x)		LUB_P2V(x)
#endif

/* FPGA register virtual addresses */
#define LUB_WHOAMI		__LUB_REG(LUBBOCK_FPGA_PHYS + 0x000)
#define LUB_HEXLED		__LUB_REG(LUBBOCK_FPGA_PHYS + 0x010)
#define LUB_DISC_BLNK_LED	__LUB_REG(LUBBOCK_FPGA_PHYS + 0x040)
#define LUB_CONF_SWITCHES	__LUB_REG(LUBBOCK_FPGA_PHYS + 0x050)
#define LUB_USER_SWITCHES	__LUB_REG(LUBBOCK_FPGA_PHYS + 0x060)
#define LUB_MISC_WR		__LUB_REG(LUBBOCK_FPGA_PHYS + 0x080)
#define LUB_MISC_RD		__LUB_REG(LUBBOCK_FPGA_PHYS + 0x090)
#define LUB_IRQ_MASK_EN		__LUB_REG(LUBBOCK_FPGA_PHYS + 0x0c0)
#define LUB_IRQ_SET_CLR		__LUB_REG(LUBBOCK_FPGA_PHYS + 0x0d0)
#define LUB_GP			__LUB_REG(LUBBOCK_FPGA_PHYS + 0x100)

#ifndef __ASSEMBLY__
extern void lubbock_set_misc_wr(unsigned int mask, unsigned int set);
#endif
