/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/arch/arm/mach-omap1/opp.h
 *
 *  Copyright (C) 2004 - 2005 Nokia corporation
 *  Written by Tuukka Tikkanen <tuukka.tikkanen@elektrobit.com>
 *  Based on clocks.h by Tony Lindgren, Gordon McNutt and RidgeRun, Inc
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
	u32			flags;
};

extern struct mpu_rate omap1_rate_table[];

#endif
