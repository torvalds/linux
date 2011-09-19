/*
 * SMP support for PowerNV machines.
 *
 * Copyright 2011 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/cpu.h>

#include <asm/irq.h>
#include <asm/smp.h>
#include <asm/paca.h>
#include <asm/machdep.h>
#include <asm/cputable.h>
#include <asm/firmware.h>
#include <asm/system.h>
#include <asm/rtas.h>
#include <asm/vdso_datapage.h>
#include <asm/cputhreads.h>
#include <asm/xics.h>

#include "powernv.h"

static void __devinit pnv_smp_setup_cpu(int cpu)
{
	if (cpu != boot_cpuid)
		xics_setup_cpu();
}

static int pnv_smp_cpu_bootable(unsigned int nr)
{
	/* Special case - we inhibit secondary thread startup
	 * during boot if the user requests it.
	 */
	if (system_state < SYSTEM_RUNNING && cpu_has_feature(CPU_FTR_SMT)) {
		if (!smt_enabled_at_boot && cpu_thread_in_core(nr) != 0)
			return 0;
		if (smt_enabled_at_boot
		    && cpu_thread_in_core(nr) >= smt_enabled_at_boot)
			return 0;
	}

	return 1;
}

static struct smp_ops_t pnv_smp_ops = {
	.message_pass	= smp_muxed_ipi_message_pass,
	.cause_ipi	= NULL,	/* Filled at runtime by xics_smp_probe() */
	.probe		= xics_smp_probe,
	.kick_cpu	= smp_generic_kick_cpu,
	.setup_cpu	= pnv_smp_setup_cpu,
	.cpu_bootable	= pnv_smp_cpu_bootable,
};

/* This is called very early during platform setup_arch */
void __init pnv_smp_init(void)
{
	smp_ops = &pnv_smp_ops;

	/* XXX We don't yet have a proper entry point from HAL, for
	 * now we rely on kexec-style entry from BML
	 */

#ifdef CONFIG_PPC_RTAS
	/* Non-lpar has additional take/give timebase */
	if (rtas_token("freeze-time-base") != RTAS_UNKNOWN_SERVICE) {
		smp_ops->give_timebase = rtas_give_timebase;
		smp_ops->take_timebase = rtas_take_timebase;
	}
#endif /* CONFIG_PPC_RTAS */
}
