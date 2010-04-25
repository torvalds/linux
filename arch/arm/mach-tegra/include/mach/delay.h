/*
 * arch/arm/mach-tegra/include/mach/delay.h
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *  Colin Cross <ccross@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef __MACH_TEGRA_DELAY_H
#define __MACH_TEGRA_DELAY_H

/* needed by loops_per_jiffy calculations */
extern void __delay(int loops);

extern void __udelay(unsigned long usecs);
extern void __const_udelay(unsigned long usecs);

/* we don't have any restrictions on maximum udelay length, but we'll enforce
 * the same restriction as the ARM default so we don't introduce any
 * incompatibilties in drivers.
 */
extern void __bad_udelay(void);

#define MAX_UDELAY_MS 2

#define udelay(n)							\
	((__builtin_constant_p(n) && (n) > (MAX_UDELAY_MS * 1000)) ?	\
		__bad_udelay() :					\
		__udelay(n))

#endif /* defined(__MACH_TEGRA_DELAY_H) */
