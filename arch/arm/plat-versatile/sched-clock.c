// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  linux/arch/arm/plat-versatile/sched-clock.c
 *
 *  Copyright (C) 1999 - 2003 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 */
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/sched_clock.h>

#include <plat/sched_clock.h>

static void __iomem *ctr;

static u64 notrace versatile_read_sched_clock(void)
{
	if (ctr)
		return readl(ctr);

	return 0;
}

void __init versatile_sched_clock_init(void __iomem *reg, unsigned long rate)
{
	ctr = reg;
	sched_clock_register(versatile_read_sched_clock, 32, rate);
}
