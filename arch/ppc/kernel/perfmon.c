/* kernel/perfmon.c
 * PPC 32 Performance Monitor Infrastructure
 *
 * Author: Andy Fleming
 * Copyright (c) 2004 Freescale Semiconductor, Inc
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/interrupt.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/prctl.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/reg.h>
#include <asm/xmon.h>

/* A lock to regulate grabbing the interrupt */
DEFINE_SPINLOCK(perfmon_lock);

#ifdef CONFIG_FSL_BOOKE
static void dummy_perf(struct pt_regs *regs)
{
	unsigned int pmgc0 = mfpmr(PMRN_PMGC0);

	pmgc0 &= ~PMGC0_PMIE;
	mtpmr(PMRN_PMGC0, pmgc0);
}

#else
/* Ensure exceptions are disabled */

static void dummy_perf(struct pt_regs *regs)
{
	unsigned int mmcr0 = mfspr(SPRN_MMCR0);

	mmcr0 &= ~MMCR0_PMXE;
	mtspr(SPRN_MMCR0, mmcr0);
}
#endif

void (*perf_irq)(struct pt_regs *) = dummy_perf;

/* Grab the interrupt, if it's free.
 * Returns 0 on success, -1 if the interrupt is taken already */
int request_perfmon_irq(void (*handler)(struct pt_regs *))
{
	int err = 0;

	spin_lock(&perfmon_lock);

	if (perf_irq == dummy_perf)
		perf_irq = handler;
	else {
		pr_info("perfmon irq already handled by %p\n", perf_irq);
		err = -1;
	}

	spin_unlock(&perfmon_lock);

	return err;
}

void free_perfmon_irq(void)
{
	spin_lock(&perfmon_lock);

	perf_irq = dummy_perf;

	spin_unlock(&perfmon_lock);
}

EXPORT_SYMBOL(perf_irq);
EXPORT_SYMBOL(request_perfmon_irq);
EXPORT_SYMBOL(free_perfmon_irq);
