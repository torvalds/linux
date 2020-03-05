// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2000, 2001, 2004 MIPS Technologies, Inc.
 * Copyright (C) 2001 Ralf Baechle
 * Copyright (C) 2010 PMC-Sierra, Inc.
 *
 *  VSMP support for MSP platforms . Derived from malta vsmp support.
 */
#include <linux/smp.h>
#include <linux/interrupt.h>

#include <asm/setup.h>

#ifdef CONFIG_MIPS_MT_SMP
#define MIPS_CPU_IPI_RESCHED_IRQ 0	/* SW int 0 for resched */
#define MIPS_CPU_IPI_CALL_IRQ 1		/* SW int 1 for call */


static void ipi_resched_dispatch(void)
{
	do_IRQ(MIPS_CPU_IPI_RESCHED_IRQ);
}

static void ipi_call_dispatch(void)
{
	do_IRQ(MIPS_CPU_IPI_CALL_IRQ);
}

static irqreturn_t ipi_resched_interrupt(int irq, void *dev_id)
{
	return IRQ_HANDLED;
}

static irqreturn_t ipi_call_interrupt(int irq, void *dev_id)
{
	generic_smp_call_function_interrupt();

	return IRQ_HANDLED;
}

void __init arch_init_ipiirq(int irq, const char *name, irq_handler_t handler)
{
	if (request_irq(irq, handler, IRQF_PERCPU, name, NULL))
		pr_err("Failed to request irq %d (%s)\n", irq, name);
	irq_set_handler(irq, handle_percpu_irq);
}

void __init msp_vsmp_int_init(void)
{
	set_vi_handler(MIPS_CPU_IPI_RESCHED_IRQ, ipi_resched_dispatch);
	set_vi_handler(MIPS_CPU_IPI_CALL_IRQ, ipi_call_dispatch);
	arch_init_ipiirq(MIPS_CPU_IPI_RESCHED_IRQ, "IPI_resched",
			 ipi_resched_interrupt);
	arch_init_ipiirq(MIPS_CPU_IPI_CALL_IRQ, "IPI_call", ipi_call_interrupt);
}
#endif /* CONFIG_MIPS_MT_SMP */
