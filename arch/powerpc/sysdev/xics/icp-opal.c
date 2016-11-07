/*
 * Copyright 2016 IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/cpu.h>
#include <linux/of.h>

#include <asm/smp.h>
#include <asm/irq.h>
#include <asm/errno.h>
#include <asm/xics.h>
#include <asm/io.h>
#include <asm/opal.h>

static void icp_opal_teardown_cpu(void)
{
	int hw_cpu = hard_smp_processor_id();

	/* Clear any pending IPI */
	opal_int_set_mfrr(hw_cpu, 0xff);
}

static void icp_opal_flush_ipi(void)
{
	/*
	 * We take the ipi irq but and never return so we need to EOI the IPI,
	 * but want to leave our priority 0.
	 *
	 * Should we check all the other interrupts too?
	 * Should we be flagging idle loop instead?
	 * Or creating some task to be scheduled?
	 */
	opal_int_eoi((0x00 << 24) | XICS_IPI);
}

static unsigned int icp_opal_get_irq(void)
{
	unsigned int xirr;
	unsigned int vec;
	unsigned int irq;
	int64_t rc;

	rc = opal_int_get_xirr(&xirr, false);
	if (rc < 0)
		return 0;
	xirr = be32_to_cpu(xirr);
	vec = xirr & 0x00ffffff;
	if (vec == XICS_IRQ_SPURIOUS)
		return 0;

	irq = irq_find_mapping(xics_host, vec);
	if (likely(irq)) {
		xics_push_cppr(vec);
		return irq;
	}

	/* We don't have a linux mapping, so have rtas mask it. */
	xics_mask_unknown_vec(vec);

	/* We might learn about it later, so EOI it */
	opal_int_eoi(xirr);

	return 0;
}

static void icp_opal_set_cpu_priority(unsigned char cppr)
{
	xics_set_base_cppr(cppr);
	opal_int_set_cppr(cppr);
	iosync();
}

static void icp_opal_eoi(struct irq_data *d)
{
	unsigned int hw_irq = (unsigned int)irqd_to_hwirq(d);
	int64_t rc;

	iosync();
	rc = opal_int_eoi((xics_pop_cppr() << 24) | hw_irq);

	/*
	 * EOI tells us whether there are more interrupts to fetch.
	 *
	 * Some HW implementations might not be able to send us another
	 * external interrupt in that case, so we force a replay.
	 */
	if (rc > 0)
		force_external_irq_replay();
}

#ifdef CONFIG_SMP

static void icp_opal_cause_ipi(int cpu, unsigned long data)
{
	int hw_cpu = get_hard_smp_processor_id(cpu);

	opal_int_set_mfrr(hw_cpu, IPI_PRIORITY);
}

static irqreturn_t icp_opal_ipi_action(int irq, void *dev_id)
{
	int hw_cpu = hard_smp_processor_id();

	opal_int_set_mfrr(hw_cpu, 0xff);

	return smp_ipi_demux();
}

#endif /* CONFIG_SMP */

static const struct icp_ops icp_opal_ops = {
	.get_irq	= icp_opal_get_irq,
	.eoi		= icp_opal_eoi,
	.set_priority	= icp_opal_set_cpu_priority,
	.teardown_cpu	= icp_opal_teardown_cpu,
	.flush_ipi	= icp_opal_flush_ipi,
#ifdef CONFIG_SMP
	.ipi_action	= icp_opal_ipi_action,
	.cause_ipi	= icp_opal_cause_ipi,
#endif
};

int icp_opal_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "ibm,opal-intc");
	if (!np)
		return -ENODEV;

	icp_ops = &icp_opal_ops;

	printk("XICS: Using OPAL ICP fallbacks\n");

	return 0;
}

