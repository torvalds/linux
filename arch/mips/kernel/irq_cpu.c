/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * Copyright (C) 2001 Ralf Baechle
 * Copyright (C) 2005  MIPS Technologies, Inc.	All rights reserved.
 *	Author: Maciej W. Rozycki <macro@mips.com>
 *
 * This file define the irq handler for MIPS CPU interrupts.
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

/*
 * Almost all MIPS CPUs define 8 interrupt sources.  They are typically
 * level triggered (i.e., cannot be cleared from CPU; must be cleared from
 * device).  The first two are software interrupts which we don't really
 * use or support.  The last one is usually the CPU timer interrupt if
 * counter register is present or, for CPUs with an external FPU, by
 * convention it's the FPU exception interrupt.
 *
 * Don't even think about using this on SMP.  You have been warned.
 *
 * This file exports one global function:
 *	void mips_cpu_irq_init(void);
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>

#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>
#include <asm/mipsmtregs.h>
#include <asm/setup.h>

static inline void unmask_mips_irq(struct irq_data *d)
{
	set_c0_status(0x100 << (d->irq - MIPS_CPU_IRQ_BASE));
	irq_enable_hazard();
}

static inline void mask_mips_irq(struct irq_data *d)
{
	clear_c0_status(0x100 << (d->irq - MIPS_CPU_IRQ_BASE));
	irq_disable_hazard();
}

static struct irq_chip mips_cpu_irq_controller = {
	.name		= "MIPS",
	.irq_ack	= mask_mips_irq,
	.irq_mask	= mask_mips_irq,
	.irq_mask_ack	= mask_mips_irq,
	.irq_unmask	= unmask_mips_irq,
	.irq_eoi	= unmask_mips_irq,
	.irq_disable	= mask_mips_irq,
	.irq_enable	= unmask_mips_irq,
};

/*
 * Basically the same as above but taking care of all the MT stuff
 */

static unsigned int mips_mt_cpu_irq_startup(struct irq_data *d)
{
	unsigned int vpflags = dvpe();

	clear_c0_cause(0x100 << (d->irq - MIPS_CPU_IRQ_BASE));
	evpe(vpflags);
	unmask_mips_irq(d);
	return 0;
}

/*
 * While we ack the interrupt interrupts are disabled and thus we don't need
 * to deal with concurrency issues.  Same for mips_cpu_irq_end.
 */
static void mips_mt_cpu_irq_ack(struct irq_data *d)
{
	unsigned int vpflags = dvpe();
	clear_c0_cause(0x100 << (d->irq - MIPS_CPU_IRQ_BASE));
	evpe(vpflags);
	mask_mips_irq(d);
}

static struct irq_chip mips_mt_cpu_irq_controller = {
	.name		= "MIPS",
	.irq_startup	= mips_mt_cpu_irq_startup,
	.irq_ack	= mips_mt_cpu_irq_ack,
	.irq_mask	= mask_mips_irq,
	.irq_mask_ack	= mips_mt_cpu_irq_ack,
	.irq_unmask	= unmask_mips_irq,
	.irq_eoi	= unmask_mips_irq,
	.irq_disable	= mask_mips_irq,
	.irq_enable	= unmask_mips_irq,
};

asmlinkage void __weak plat_irq_dispatch(void)
{
	unsigned long pending = read_c0_cause() & read_c0_status() & ST0_IM;
	int irq;

	if (!pending) {
		spurious_interrupt();
		return;
	}

	pending >>= CAUSEB_IP;
	while (pending) {
		irq = fls(pending) - 1;
		do_IRQ(MIPS_CPU_IRQ_BASE + irq);
		pending &= ~BIT(irq);
	}
}

static int mips_cpu_intc_map(struct irq_domain *d, unsigned int irq,
			     irq_hw_number_t hw)
{
	static struct irq_chip *chip;

	if (hw < 2 && cpu_has_mipsmt) {
		/* Software interrupts are used for MT/CMT IPI */
		chip = &mips_mt_cpu_irq_controller;
	} else {
		chip = &mips_cpu_irq_controller;
	}

	if (cpu_has_vint)
		set_vi_handler(hw, plat_irq_dispatch);

	irq_set_chip_and_handler(irq, chip, handle_percpu_irq);

	return 0;
}

static const struct irq_domain_ops mips_cpu_intc_irq_domain_ops = {
	.map = mips_cpu_intc_map,
	.xlate = irq_domain_xlate_onecell,
};

static void __init __mips_cpu_irq_init(struct device_node *of_node)
{
	struct irq_domain *domain;

	/* Mask interrupts. */
	clear_c0_status(ST0_IM);
	clear_c0_cause(CAUSEF_IP);

	domain = irq_domain_add_legacy(of_node, 8, MIPS_CPU_IRQ_BASE, 0,
				       &mips_cpu_intc_irq_domain_ops, NULL);
	if (!domain)
		panic("Failed to add irqdomain for MIPS CPU");
}

void __init mips_cpu_irq_init(void)
{
	__mips_cpu_irq_init(NULL);
}

int __init mips_cpu_irq_of_init(struct device_node *of_node,
				struct device_node *parent)
{
	__mips_cpu_irq_init(of_node);
	return 0;
}
