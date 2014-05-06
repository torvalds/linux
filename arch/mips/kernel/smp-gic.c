/*
 * Copyright (C) 2013 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * Based on smp-cmp.c:
 *  Copyright (C) 2007 MIPS Technologies, Inc.
 *  Author: Chris Dearman (chris@mips.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/printk.h>

#include <asm/gic.h>
#include <asm/smp-ops.h>

void gic_send_ipi_single(int cpu, unsigned int action)
{
	unsigned long flags;
	unsigned int intr;

	pr_debug("CPU%d: %s cpu %d action %u status %08x\n",
		 smp_processor_id(), __func__, cpu, action, read_c0_status());

	local_irq_save(flags);

	switch (action) {
	case SMP_CALL_FUNCTION:
		intr = plat_ipi_call_int_xlate(cpu);
		break;

	case SMP_RESCHEDULE_YOURSELF:
		intr = plat_ipi_resched_int_xlate(cpu);
		break;

	default:
		BUG();
	}

	gic_send_ipi(intr);
	local_irq_restore(flags);
}

void gic_send_ipi_mask(const struct cpumask *mask, unsigned int action)
{
	unsigned int i;

	for_each_cpu(i, mask)
		gic_send_ipi_single(i, action);
}
