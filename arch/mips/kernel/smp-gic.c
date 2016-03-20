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

#include <linux/irqchip/mips-gic.h>
#include <linux/printk.h>

#include <asm/mips-cpc.h>
#include <asm/smp-ops.h>

void gic_send_ipi_single(int cpu, unsigned int action)
{
	unsigned long flags;
	unsigned int intr;
	unsigned int core = cpu_data[cpu].core;

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

	if (mips_cpc_present() && (core != current_cpu_data.core)) {
		while (!cpumask_test_cpu(cpu, &cpu_coherent_mask)) {
			mips_cm_lock_other(core, 0);
			mips_cpc_lock_other(core);
			write_cpc_co_cmd(CPC_Cx_CMD_PWRUP);
			mips_cpc_unlock_other();
			mips_cm_unlock_other();
		}
	}

	local_irq_restore(flags);
}

void gic_send_ipi_mask(const struct cpumask *mask, unsigned int action)
{
	unsigned int i;

	for_each_cpu(i, mask)
		gic_send_ipi_single(i, action);
}
