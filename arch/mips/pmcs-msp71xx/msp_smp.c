/*
 * Copyright (C) 2000, 2001, 2004 MIPS Technologies, Inc.
 * Copyright (C) 2001 Ralf Baechle
 * Copyright (C) 2010 PMC-Sierra, Inc.
 *
 *  VSMP support for MSP platforms . Derived from malta vsmp support.
 *
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
 */
#include <linux/smp.h>
#include <linux/interrupt.h>

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
	smp_call_function_interrupt();

	return IRQ_HANDLED;
}

static struct irqaction irq_resched = {
	.handler	= ipi_resched_interrupt,
	.flags		= IRQF_PERCPU,
	.name		= "IPI_resched"
};

static struct irqaction irq_call = {
	.handler	= ipi_call_interrupt,
	.flags		= IRQF_PERCPU,
	.name		= "IPI_call"
};

void __init arch_init_ipiirq(int irq, struct irqaction *action)
{
	setup_irq(irq, action);
	irq_set_handler(irq, handle_percpu_irq);
}

void __init msp_vsmp_int_init(void)
{
	set_vi_handler(MIPS_CPU_IPI_RESCHED_IRQ, ipi_resched_dispatch);
	set_vi_handler(MIPS_CPU_IPI_CALL_IRQ, ipi_call_dispatch);
	arch_init_ipiirq(MIPS_CPU_IPI_RESCHED_IRQ, &irq_resched);
	arch_init_ipiirq(MIPS_CPU_IPI_CALL_IRQ, &irq_call);
}
#endif /* CONFIG_MIPS_MT_SMP */
