/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007 MIPS Technologies, Inc.  All rights reserved.
 * Copyright (C) 2013 Imagination Technologies Ltd.
 *
 * Arbitrary Monitor Interface
 */
#include <linux/kernel.h>
#include <linux/smp.h>

#include <asm/addrspace.h>
#include <asm/mipsmtregs.h>
#include <asm/mips-boards/launch.h>
#include <asm/vpe.h>

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

int amon_cpu_start(int cpu,
		    unsigned long pc, unsigned long sp,
		    unsigned long gp, unsigned long a0)
{
	volatile struct cpulaunch *launch =
		(struct cpulaunch  *)CKSEG0ADDR(CPULAUNCH);

	if (!amon_cpu_avail(cpu))
		return -1;
	if (cpu == smp_processor_id()) {
		pr_debug("launch: I am cpu%d!\n", cpu);
		return -1;
	}
	launch += cpu;

	pr_debug("launch: starting cpu%d\n", cpu);

	launch->pc = pc;
	launch->gp = gp;
	launch->sp = sp;
	launch->a0 = a0;

	smp_wmb();		/* Target must see parameters before go */
	launch->flags |= LAUNCH_FGO;
	smp_wmb();		/* Target must see go before we poll  */

	while ((launch->flags & LAUNCH_FGONE) == 0)
		;
	smp_rmb();	/* Target will be updating flags soon */
	pr_debug("launch: cpu%d gone!\n", cpu);

	return 0;
}

#ifdef CONFIG_MIPS_VPE_LOADER
int vpe_run(struct vpe *v)
{
	struct vpe_notifications *n;

	if (amon_cpu_start(aprp_cpu_index(), v->__start, 0, 0, 0) < 0)
		return -1;

	list_for_each_entry(n, &v->notify, list)
		n->start(VPE_MODULE_MINOR);

	return 0;
}
#endif
