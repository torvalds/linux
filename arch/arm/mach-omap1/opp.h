/*
 *  linux/arch/arm/mach-omap1/opp.h
 *
 *  Copyright (C) 2004 - 2005 Nokia corporation
 *  Written by Tuukka Tikkanen <tuukka.tikkanen@elektrobit.com>
 *  Based on clocks.h by Tony Lindgren, Gordon McNutt and RidgeRun, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_MACH_OMAP1_OPP_H
#define __ARCH_ARM_MACH_OMAP1_OPP_H

#include <linux/types.h>

struct mpu_rate {
	unsigned long		rate;
	unsigned long		xtal;
	unsigned long		pll_rate;
	__u16			ckctl_val;
	__u16			dpllctl_val;
};

extern struct mpu_rate omap1_rate_table[];

#endif
