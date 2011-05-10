/*
 * PQ2 ADS-style PCI interrupt controller
 *
 * Copyright 2007 Freescale Semiconductor, Inc.
 * Author: Scott Wood <scottwood@freescale.com>
 *
 * Loosely based on mpc82xx ADS support by Vitaly Bordug <vbordug@ru.mvista.com>
 * Copyright (c) 2006 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/irq.h>
#include <linux/types.h>
#include <linux/bootmem.h>
#include <linux/slab.h>

#include <asm/io.h>
#include <asm/prom.h>
#include <asm/cpm2.h>

#include "pq2.h"

static DEFINE_RAW_SPINLOCK(pci_pic_lock);

struct pq2ads_pci_pic {
	struct device_node *node;
	struct irq_host *host;

	struct {
		u32 stat;
		u32 mask;
	} __iomem *regs;
};

#define NUM_IRQS 32

static void pq2ads_pci_mask_irq(struct irq_data *d)
{
	struct pq2ads_pci_pic *priv = irq_data_get_irq_chip_data(d);
	int irq = NUM_IRQS - irqd_to_hwirq(d) - 1;

	if (irq != -1) {
		unsigned long flags;
		raw_spin_lock_irqsave(&pci_pic_lock, flags);

		setbits32(&priv->regs->mask, 1 << irq);
		mb();

		raw_spin_unlock_irqrestore(&pci_pic_lock, flags);
	}
}

static void pq2ads_pci_unmask_irq(struct irq_data *d)
{
	struct pq2ads_pci_pic *priv = irq_data_get_irq_chip_data(d);
	int irq = NUM_IRQS - irqd_to_hwirq(d) - 1;

	if (irq != -1) {
		unsigned long flags;

		raw_spin_lock_irqsave(&pci_pic_lock, flags);
		clrbits32(&priv->regs->mask, 1 << irq);
		raw_spin_unlock_irqrestore(&pci_pic_lock, flags);
	}
}

static struct irq_chip pq2ads_pci_ic = {
	.name = "PQ2 ADS PCI",
	.irq_mask = pq2ads_pci_mask_irq,
	.irq_mask_ack = pq2ads_pci_mask_irq,
	.irq_ack = pq2ads_pci_mask_irq,
	.irq_unmask = pq2ads_pci_unmask_irq,
	.irq_enable = pq2ads_pci_unmask_irq,
	.irq_disable = pq2ads_pci_mask_irq
};

static void pq2ads_pci_irq_demux(unsigned int irq, struct irq_desc *desc)
{
	struct pq2ads_pci_pic *priv = irq_desc_get_handler_data(desc);
	u32 stat, mask, pend;
	int bit;

	for (;;) {
		stat = in_be32(&priv->regs->stat);
		mask = in_be32(&priv->regs->mask);

		pend = stat & ~mask;

		if (!pend)
			break;

		for (bit = 0; pend != 0; ++bit, pend <<= 1) {
			if (pend & 0x80000000) {
				int virq = irq_linear_revmap(priv->host, bit);
				generic_handle_irq(virq);
			}
		}
	}
}

static int pci_pic_host_map(struct irq_host *h, unsigned int virq,
			    irq_hw_number_t hw)
{
	irq_set_status_flags(virq, IRQ_LEVEL);
	irq_set_chip_data(virq, h->host_data);
	irq_set_chip_and_handler(virq, &pq2ads_pci_ic, handle_level_irq);
	return 0;
}

static struct irq_host_ops pci_pic_host_ops = {
	.map = pci_pic_host_map,
};

int __init pq2ads_pci_init_irq(void)
{
	struct pq2ads_pci_pic *priv;
	struct irq_host *host;
	struct device_node *np;
	int ret = -ENODEV;
	int irq;

	np = of_find_compatible_node(NULL, NULL, "fsl,pq2ads-pci-pic");
	if (!np) {
		printk(KERN_ERR "No pci pic node in device tree.\n");
		of_node_put(np);
		goto out;
	}

	irq = irq_of_parse_and_map(np, 0);
	if (irq == NO_IRQ) {
		printk(KERN_ERR "No interrupt in pci pic node.\n");
		of_node_put(np);
		goto out;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		of_node_put(np);
		ret = -ENOMEM;
		goto out_unmap_irq;
	}

	/* PCI interrupt controller registers: status and mask */
	priv->regs = of_iomap(np, 0);
	if (!priv->regs) {
		printk(KERN_ERR "Cannot map PCI PIC registers.\n");
		goto out_free_bootmem;
	}

	/* mask all PCI interrupts */
	out_be32(&priv->regs->mask, ~0);
	mb();

	host = irq_alloc_host(np, IRQ_HOST_MAP_LINEAR, NUM_IRQS,
	                      &pci_pic_host_ops, NUM_IRQS);
	if (!host) {
		ret = -ENOMEM;
		goto out_unmap_regs;
	}

	host->host_data = priv;

	priv->host = host;
	host->host_data = priv;
	irq_set_handler_data(irq, priv);
	irq_set_chained_handler(irq, pq2ads_pci_irq_demux);

	of_node_put(np);
	return 0;

out_unmap_regs:
	iounmap(priv->regs);
out_free_bootmem:
	free_bootmem((unsigned long)priv,
	             sizeof(struct pq2ads_pci_pic));
	of_node_put(np);
out_unmap_irq:
	irq_dispose_mapping(irq);
out:
	return ret;
}
