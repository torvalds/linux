/*
 * arch/arm/plat-sunxi/include/plat/system.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Benn Huang <benn@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef __SW_SYSTEM_H
#define __SW_SYSTEM_H

#include <asm/proc-fns.h>

extern unsigned long fb_start;
extern unsigned long fb_size;
extern unsigned long g2d_start;
extern unsigned long g2d_size;

static inline void arch_idle(void)
{
	cpu_do_idle();
}

/* BROM access only possible after iomap()s */
u32 sunxi_chip_id(void);
int sunxi_pr_brom(void);

enum sw_ic_ver {
	SUNXI_VER_UNKNOWN = 0xffffffff,

	/* sun4i */
	SUNXI_VER_A10A = 0xA100,
	SUNXI_VER_A10B,
	SUNXI_VER_A10C,

	/* sun5i */
	SUNXI_VER_A13A = 0xA13A,
	SUNXI_VER_A13B,
	SUNXI_VER_A12A = 0xA12A,
	SUNXI_VER_A12B,
	SUNXI_VER_A10SA = 0xA10A,
	SUNXI_VER_A10SB,
};

enum sw_ic_ver sw_get_ic_ver(void);

#ifdef CONFIG_ARCH_SUN5I
struct sw_chip_id
{
    unsigned int sid_rkey0;
    unsigned int sid_rkey1;
    unsigned int sid_rkey2;
    unsigned int sid_rkey3;
};

int sw_get_chip_id(struct sw_chip_id *);
#endif

#endif
