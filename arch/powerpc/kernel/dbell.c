/*
 * Author: Kumar Gala <galak@kernel.crashing.org>
 *
 * Copyright 2009 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/threads.h>
#include <linux/hardirq.h>

#include <asm/dbell.h>
#include <asm/irq_regs.h>

#ifdef CONFIG_SMP
void doorbell_setup_this_cpu(void)
{
	unsigned long tag = mfspr(SPRN_PIR) & 0x3fff;

	smp_muxed_ipi_set_data(smp_processor_id(), tag);
}

void doorbell_cause_ipi(int cpu, unsigned long data)
{
	ppc_msgsnd(PPC_DBELL, 0, data);
}

void doorbell_exception(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	irq_enter();

	smp_ipi_demux();

	irq_exit();
	set_irq_regs(old_regs);
}
#else /* CONFIG_SMP */
void doorbell_exception(struct pt_regs *regs)
{
	printk(KERN_WARNING "Received doorbell on non-smp system\n");
}
#endif /* CONFIG_SMP */

