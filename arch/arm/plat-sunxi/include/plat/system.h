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

enum sunxi_chip_id {
	SUNXI_UNKNOWN_MACH = 0xffffffff,

	SUNXI_MACH_SUN4I = 1623,
	SUNXI_MACH_SUN5I = 1625,
	SUNXI_MACH_SUN6I = 1633,
	SUNXI_MACH_SUN7I = 1651,
};

enum {
	SUNXI_BIT_SUN4I = BIT(30),
	SUNXI_BIT_SUN5I = BIT(29),
	SUNXI_BIT_SUN6I = BIT(28),
	SUNXI_BIT_SUN7I = BIT(27),

	/* SUNXI_BIT_UNKNOWN can't OR anything known */
	SUNXI_BIT_UNKNOWN = BIT(20),

	/* sun4i */
	SUNXI_SOC_A10  = SUNXI_BIT_SUN4I | BIT(4),

	/* sun5i */
	SUNXI_SOC_A13  = SUNXI_BIT_SUN5I | BIT(4),
	SUNXI_SOC_A12  = SUNXI_BIT_SUN5I | BIT(5),
	SUNXI_SOC_A10S = SUNXI_BIT_SUN5I | BIT(6),

	/* sun6i */
	SUNXI_SOC_A31  = SUNXI_BIT_SUN6I | BIT(4),

	/* sun7i */
	SUNXI_SOC_A20  = SUNXI_BIT_SUN7I | BIT(4),

	SUNXI_REV_UNKNOWN = 0,
	SUNXI_REV_A,
	SUNXI_REV_B,
	SUNXI_REV_C,
};

/* BROM access only possible after iomap()s */
u32 sunxi_brom_chip_id(void);
int sunxi_pr_brom(void);

u32 sunxi_sc_chip_id(void);

u32 sunxi_chip_id(void) __pure;
int sunxi_pr_chip_id(void);

enum sw_ic_ver {
	SUNXI_VER_UNKNOWN = SUNXI_BIT_UNKNOWN,

	/* sun4i */
	SUNXI_VER_A10A = SUNXI_SOC_A10 + SUNXI_REV_A,
	SUNXI_VER_A10B,
	SUNXI_VER_A10C,

	/* sun5i */
	SUNXI_VER_A13 = SUNXI_SOC_A13,
	SUNXI_VER_A13A,
	SUNXI_VER_A13B,
	SUNXI_VER_A12 = SUNXI_SOC_A12,
	SUNXI_VER_A12A,
	SUNXI_VER_A12B,
	SUNXI_VER_A10S = SUNXI_SOC_A10S,
	SUNXI_VER_A10SA,
	SUNXI_VER_A10SB,

	/* sun6i */
	SUNXI_VER_A31 = SUNXI_SOC_A31,

	/* sun7i */
	SUNXI_VER_A20 = SUNXI_SOC_A20,
};

enum sw_ic_ver sw_get_ic_ver(void) __pure;

#define _sunxi_is(M)		((sw_get_ic_ver()&M) == M)

#if defined(CONFIG_SUNXI_MULTIPLATFORM)
/* sunxi_is_sunNi() could also be implemented ORing the ic_ver */
#define sunxi_is_sun4i()	(sunxi_chip_id() == SUNXI_MACH_SUN4I)
#define sunxi_is_sun5i()	(sunxi_chip_id() == SUNXI_MACH_SUN5I)
#define sunxi_is_sun6i()	(sunxi_chip_id() == SUNXI_MACH_SUN6I)
#define sunxi_is_sun7i()	(sunxi_chip_id() == SUNXI_MACH_SUN7I)
#define sunxi_is_a10()		_sunxi_is(SUNXI_SOC_A10)
#define sunxi_is_a13()		_sunxi_is(SUNXI_SOC_A13)
#define sunxi_is_a12()		_sunxi_is(SUNXI_SOC_A12)
#define sunxi_is_a10s()		_sunxi_is(SUNXI_SOC_A10S)
#define sunxi_is_a31()		_sunxi_is(SUNXI_SOC_A31)
#define sunxi_is_a20()		_sunxi_is(SUNXI_SOC_A20)

#elif defined(CONFIG_ARCH_SUN4I)
#define sunxi_is_sun4i()	(sunxi_chip_id() == SUNXI_MACH_SUN4I)
#define sunxi_is_sun5i()	(0)
#define sunxi_is_sun6i()	(0)
#define sunxi_is_sun7i()	(0)
#define sunxi_is_a10()		_sunxi_is(SUNXI_SOC_A10)
#define sunxi_is_a13()		(0)
#define sunxi_is_a12()		(0)
#define sunxi_is_a10s()		(0)
#define sunxi_is_a31()		(0)
#define sunxi_is_a20()		(0)

#elif defined(CONFIG_ARCH_SUN5I)
#define sunxi_is_sun4i()	(0)
#define sunxi_is_sun5i()	(sunxi_chip_id() == SUNXI_MACH_SUN5I)
#define sunxi_is_sun6i()	(0)
#define sunxi_is_sun7i()	(0)
#define sunxi_is_a10()		(0)
#define sunxi_is_a13()		_sunxi_is(SUNXI_SOC_A13)
#define sunxi_is_a12()		_sunxi_is(SUNXI_SOC_A12)
#define sunxi_is_a10s()		_sunxi_is(SUNXI_SOC_A10S)
#define sunxi_is_a31()		(0)
#define sunxi_is_a20()		(0)

#elif defined(CONFIG_ARCH_SUN7I)
#define sunxi_is_sun4i()	(0)
#define sunxi_is_sun5i()	(0)
#define sunxi_is_sun6i()	(0)
#define sunxi_is_sun7i()	(sunxi_chip_id() == SUNXI_MACH_SUN7I)
#define sunxi_is_a10()		(0)
#define sunxi_is_a13()		(0)
#define sunxi_is_a12()		(0)
#define sunxi_is_a10s()		(0)
#define sunxi_is_a31()		(0)
#define sunxi_is_a20()		_sunxi_is(SUNXI_SOC_A20)

#endif

#define sunxi_soc_rev()		(sw_get_ic_ver() & 0xf)

struct sw_chip_id
{
	unsigned int sid_rkey0;
	unsigned int sid_rkey1;
	unsigned int sid_rkey2;
	unsigned int sid_rkey3;
};

int sw_get_chip_id(struct sw_chip_id *);

#endif
