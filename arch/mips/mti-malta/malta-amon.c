/*
 * Copyright (C) 2007  MIPS Technologies, Inc.
 *	All rights reserved.

 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Arbitrary Monitor interface
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/smp.h>

#include <asm/addrspace.h>
#include <asm/mips-boards/launch.h>
#include <asm/mipsmtregs.h>

int amon_cpu_avail(int cpu)
{
	struct cpulaunch *launch = (struct cpulaunch *)CKSEG0ADDR(CPULAUNCH);

	if (cpu < 0 || cpu >= NCPULAUNCH) {
		pr_debug("avail: cpu%d is out of range\n", cpu);
		return 0;
	}

	launch += cpu;
	if (!(launch->flags & LAUNCH_FREADY)) {
		pr_debug("avail: cpu%d is not ready\n", cpu);
		return 0;
	}
	if (launch->flags & (LAUNCH_FGO|LAUNCH_FGONE)) {
		pr_debug("avail: too late.. cpu%d is already gone\n", cpu);
		return 0;
	}

	return 1;
}

void amon_cpu_start(int cpu,
		    unsigned long pc, unsigned long sp,
		    unsigned long gp, unsigned long a0)
{
	volatile struct cpulaunch *launch =
		(struct cpulaunch  *)CKSEG0ADDR(CPULAUNCH);

	if (!amon_cpu_avail(cpu))
		return;
	if (cpu == smp_processor_id()) {
		pr_debug("launch: I am cpu%d!\n", cpu);
		return;
	}
	launch += cpu;

	pr_debug("launch: starting cpu%d\n", cpu);

	launch->pc = pc;
	launch->gp = gp;
	launch->sp = sp;
	launch->a0 = a0;

	/* Make sure target sees parameters before the go bit */
	smp_mb();

	launch->flags |= LAUNCH_FGO;
	while ((launch->flags & LAUNCH_FGONE) == 0)
		;
	pr_debug("launch: cpu%d gone!\n", cpu);
}
