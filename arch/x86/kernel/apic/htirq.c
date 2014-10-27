/*
 * Support Hypertransport IRQ
 *
 * Copyright (C) 1997, 1998, 1999, 2000, 2009 Ingo Molnar, Hajnalka Szabo
 *	Moved from arch/x86/kernel/apic/io_apic.c.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/htirq.h>
#include <asm/hw_irq.h>
#include <asm/apic.h>
#include <asm/hypertransport.h>

/*
 * Hypertransport interrupt support
 */
static void target_ht_irq(unsigned int irq, unsigned int dest, u8 vector)
{
	struct ht_irq_msg msg;

	fetch_ht_irq_msg(irq, &msg);

	msg.address_lo &= ~(HT_IRQ_LOW_VECTOR_MASK | HT_IRQ_LOW_DEST_ID_MASK);
	msg.address_hi &= ~(HT_IRQ_HIGH_DEST_ID_MASK);

	msg.address_lo |= HT_IRQ_LOW_VECTOR(vector) | HT_IRQ_LOW_DEST_ID(dest);
	msg.address_hi |= HT_IRQ_HIGH_DEST_ID(dest);

	write_ht_irq_msg(irq, &msg);
}

static int
ht_set_affinity(struct irq_data *data, const struct cpumask *mask, bool force)
{
	struct irq_cfg *cfg = data->chip_data;
	unsigned int dest;
	int ret;

	ret = apic_set_affinity(data, mask, &dest);
	if (ret)
		return ret;

	target_ht_irq(data->irq, dest, cfg->vector);
	return IRQ_SET_MASK_OK_NOCOPY;
}

static struct irq_chip ht_irq_chip = {
	.name			= "PCI-HT",
	.irq_mask		= mask_ht_irq,
	.irq_unmask		= unmask_ht_irq,
	.irq_ack		= apic_ack_edge,
	.irq_set_affinity	= ht_set_affinity,
	.irq_retrigger		= apic_retrigger_irq,
	.flags			= IRQCHIP_SKIP_SET_WAKE,
};

int arch_setup_ht_irq(unsigned int irq, struct pci_dev *dev)
{
	struct irq_cfg *cfg;
	struct ht_irq_msg msg;
	unsigned dest;
	int err;

	if (disable_apic)
		return -ENXIO;

	cfg = irq_cfg(irq);
	err = assign_irq_vector(irq, cfg, apic->target_cpus());
	if (err)
		return err;

	err = apic->cpu_mask_to_apicid_and(cfg->domain,
					   apic->target_cpus(), &dest);
	if (err)
		return err;

	msg.address_hi = HT_IRQ_HIGH_DEST_ID(dest);

	msg.address_lo =
		HT_IRQ_LOW_BASE |
		HT_IRQ_LOW_DEST_ID(dest) |
		HT_IRQ_LOW_VECTOR(cfg->vector) |
		((apic->irq_dest_mode == 0) ?
			HT_IRQ_LOW_DM_PHYSICAL :
			HT_IRQ_LOW_DM_LOGICAL) |
		HT_IRQ_LOW_RQEOI_EDGE |
		((apic->irq_delivery_mode != dest_LowestPrio) ?
			HT_IRQ_LOW_MT_FIXED :
			HT_IRQ_LOW_MT_ARBITRATED) |
		HT_IRQ_LOW_IRQ_MASKED;

	write_ht_irq_msg(irq, &msg);

	irq_set_chip_and_handler_name(irq, &ht_irq_chip,
				      handle_edge_irq, "edge");

	dev_dbg(&dev->dev, "irq %d for HT\n", irq);

	return 0;
}
