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
 * device).
 *
 * The first two are software interrupts (i.e. not exposed as pins) which
 * may be used for IPIs in multi-threaded single-core systems.
 *
 * The last one is usually the CPU timer interrupt if the counter register
 * is present, or for old CPUs with an external FPU by convention it's the
 * FPU exception interrupt.
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>

#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>
#include <asm/mipsmtregs.h>
#include <asm/setup.h>

static struct irq_domain *irq_domain;
static struct irq_domain *ipi_domain;

static inline void unmask_mips_irq(struct irq_data *d)
{
	set_c0_status(IE_SW0 << d->hwirq);
	irq_enable_hazard();
}

static inline void mask_mips_irq(struct irq_data *d)
{
	clear_c0_status(IE_SW0 << d->hwirq);
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

	clear_c0_cause(C_SW0 << d->hwirq);
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
	clear_c0_cause(C_SW0 << d->hwirq);
	evpe(vpflags);
	mask_mips_irq(d);
}

#ifdef CONFIG_GENERIC_IRQ_IPI

static void mips_mt_send_ipi(struct irq_data *d, unsigned int cpu)
{
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	unsigned long flags;
	int vpflags;

	local_irq_save(flags);

	/* We can only send IPIs to VPEs within the local core */
	WARN_ON(!cpus_are_siblings(smp_processor_id(), cpu));

	vpflags = dvpe();
	settc(cpu_vpe_id(&cpu_data[cpu]));
	write_vpe_c0_cause(read_vpe_c0_cause() | (C_SW0 << hwirq));
	evpe(vpflags);

	local_irq_restore(flags);
}

#endif /* CONFIG_GENERIC_IRQ_IPI */

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
#ifdef CONFIG_GENERIC_IRQ_IPI
	.ipi_send_single = mips_mt_send_ipi,
#endif
};

asmlinkage void __weak plat_irq_dispatch(void)
{
	unsigned long pending = read_c0_cause() & read_c0_status() & ST0_IM;
	unsigned int virq;
	int irq;

	if (!pending) {
		spurious_interrupt();
		return;
	}

	pending >>= CAUSEB_IP;
	while (pending) {
		irq = fls(pending) - 1;
		if (IS_ENABLED(CONFIG_GENERIC_IRQ_IPI) && irq < 2)
			virq = irq_linear_revmap(ipi_domain, irq);
		else
			virq = irq_linear_revmap(irq_domain, irq);
		do_IRQ(virq);
		pending &= ~BIT(irq);
	}
}

static int mips_cpu_intc_map(struct irq_domain *d, unsigned int irq,
			     irq_hw_number_t hw)
{
	struct irq_chip *chip;

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

#ifdef CONFIG_GENERIC_IRQ_IPI

struct cpu_ipi_domain_state {
	DECLARE_BITMAP(allocated, 2);
};

static int mips_cpu_ipi_alloc(struct irq_domain *domain, unsigned int virq,
			      unsigned int nr_irqs, void *arg)
{
	struct cpu_ipi_domain_state *state = domain->host_data;
	unsigned int i, hwirq;
	int ret;

	for (i = 0; i < nr_irqs; i++) {
		hwirq = find_first_zero_bit(state->allocated, 2);
		if (hwirq == 2)
			return -EBUSY;
		bitmap_set(state->allocated, hwirq, 1);

		ret = irq_domain_set_hwirq_and_chip(domain, virq + i, hwirq,
						    &mips_mt_cpu_irq_controller,
						    NULL);
		if (ret)
			return ret;

		ret = irq_set_irq_type(virq + i, IRQ_TYPE_LEVEL_HIGH);
		if (ret)
			return ret;
	}

	return 0;
}

static int mips_cpu_ipi_match(struct irq_domain *d, struct device_node *node,
			      enum irq_domain_bus_token bus_token)
{
	bool is_ipi;

	switch (bus_token) {
	case DOMAIN_BUS_IPI:
		is_ipi = d->bus_token == bus_token;
		return (!node || (to_of_node(d->fwnode) == node)) && is_ipi;
	default:
		return 0;
	}
}

static const struct irq_domain_ops mips_cpu_ipi_chip_ops = {
	.alloc	= mips_cpu_ipi_alloc,
	.match	= mips_cpu_ipi_match,
};

static void mips_cpu_register_ipi_domain(struct device_node *of_node)
{
	struct cpu_ipi_domain_state *ipi_domain_state;

	ipi_domain_state = kzalloc(sizeof(*ipi_domain_state), GFP_KERNEL);
	ipi_domain = irq_domain_add_hierarchy(irq_domain,
					      IRQ_DOMAIN_FLAG_IPI_SINGLE,
					      2, of_node,
					      &mips_cpu_ipi_chip_ops,
					      ipi_domain_state);
	if (!ipi_domain)
		panic("Failed to add MIPS CPU IPI domain");
	irq_domain_update_bus_token(ipi_domain, DOMAIN_BUS_IPI);
}

#else /* !CONFIG_GENERIC_IRQ_IPI */

static inline void mips_cpu_register_ipi_domain(struct device_node *of_node) {}

#endif /* !CONFIG_GENERIC_IRQ_IPI */

static void __init __mips_cpu_irq_init(struct device_node *of_node)
{
	/* Mask interrupts. */
	clear_c0_status(ST0_IM);
	clear_c0_cause(CAUSEF_IP);

	irq_domain = irq_domain_add_legacy(of_node, 8, MIPS_CPU_IRQ_BASE, 0,
					   &mips_cpu_intc_irq_domain_ops,
					   NULL);
	if (!irq_domain)
		panic("Failed to add irqdomain for MIPS CPU");

	/*
	 * Only proceed to register the software interrupt IPI implementation
	 * for CPUs which implement the MIPS MT (multi-threading) ASE.
	 */
	if (cpu_has_mipsmt)
		mips_cpu_register_ipi_domain(of_node);
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
IRQCHIP_DECLARE(cpu_intc, "mti,cpu-interrupt-controller", mips_cpu_irq_of_init);
