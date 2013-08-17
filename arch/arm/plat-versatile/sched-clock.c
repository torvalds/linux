/*
 *  linux/arch/arm/plat-versatile/sched-clock.c
 *
 *  Copyright (C) 1999 - 2003 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/kernel.h>
#include <linux/io.h>

#include <asm/sched_clock.h>
#include <plat/sched_clock.h>

static void __iomem *ctr;

static u32 notrace versatile_read_sched_clock(void)
{
	if (ctr)
		return readl(ctr);

	return 0;
}

void __init versatile_sched_clock_init(void __iomem *reg, unsigned long rate)
{
	ctr = reg;
	setup_sched_clock(versatile_read_sched_clock, 32, rate);
}
