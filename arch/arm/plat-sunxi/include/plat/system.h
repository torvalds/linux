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

enum sunxi_mach_id {
	SUNXI_UNKNOWN_MACH = 0xffffffff,

	SUNXI_MACH_SUN4I = 1623,
	SUNXI_MACH_SUN5I = 1625,
	SUNXI_MACH_SUN6I = 1633,
};

/* BROM access only possible after iomap()s */
u32 sunxi_brom_chip_id(void);
int sunxi_pr_brom(void);

u32 sunxi_sramc_chip_id(void);

u32 sunxi_chip_id(void) __pure;
int sunxi_pr_chip_id(void);

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

	/* sun6i */
	SUNXI_VER_A31A = 0xA31A,
};

enum sw_ic_ver sw_get_ic_ver(void) __pure;

#define sunxi_is_sun4i()	(sunxi_chip_id() == SUNXI_MACH_SUN4I)
#define sunxi_is_sun5i()	(sunxi_chip_id() == SUNXI_MACH_SUN5I)
#define sunxi_is_sun6i()	(sunxi_chip_id() == SUNXI_MACH_SUN6I)

static inline int sunxi_is_a10(void)
{
	switch (sw_get_ic_ver()) {
	case SUNXI_VER_A10A:
	case SUNXI_VER_A10B:
	case SUNXI_VER_A10C:
		return 1;
	default:
		return 0;
	}
}

static inline int sunxi_is_a13(void)
{
	switch (sw_get_ic_ver()) {
	case SUNXI_VER_A13A:
	case SUNXI_VER_A13B:
		return 1;
	default:
		return 0;
	}
}
static inline int sunxi_is_a12(void)
{
	switch (sw_get_ic_ver()) {
	case SUNXI_VER_A12A:
	case SUNXI_VER_A12B:
		return 1;
	default:
		return 0;
	}
}
static inline int sunxi_is_a10s(void)
{
	switch (sw_get_ic_ver()) {
	case SUNXI_VER_A10SA:
	case SUNXI_VER_A10SB:
		return 1;
	default:
		return 0;
	}
}
static inline int sunxi_is_a31(void)
{
	switch (sw_get_ic_ver()) {
	case SUNXI_VER_A31A:
		return 1;
	default:
		return 0;
	}
}

struct sw_chip_id
{
	unsigned int sid_rkey0;
	unsigned int sid_rkey1;
	unsigned int sid_rkey2;
	unsigned int sid_rkey3;
};

int sw_get_chip_id(struct sw_chip_id *);

#endif
