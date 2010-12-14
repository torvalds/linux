/*
 * Interrupt management for most GSC and related devices.
 *
 * (c) Copyright 1999 Alex deVries for The Puffin Group
 * (c) Copyright 1999 Grant Grundler for Hewlett-Packard
 * (c) Copyright 1999 Matthew Wilcox
 * (c) Copyright 2000 Helge Deller
 * (c) Copyright 2001 Matthew Wilcox for Hewlett-Packard
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 */

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/types.h>

#include <asm/hardware.h>
#include <asm/io.h>

#include "gsc.h"

#undef DEBUG

#ifdef DEBUG
#define DEBPRINTK printk
#else
#define DEBPRINTK(x,...)
#endif

int gsc_alloc_irq(struct gsc_irq *i)
{
	int irq = txn_alloc_irq(GSC_EIM_WIDTH);
	if (irq < 0) {
		printk("cannot get irq\n");
		return irq;
	}

	i->txn_addr = txn_alloc_addr(irq);
	i->txn_data = txn_alloc_data(irq);
	i->irq = irq;

	return irq;
}

int gsc_claim_irq(struct gsc_irq *i, int irq)
{
	int c = irq;

	irq += CPU_IRQ_BASE; /* virtualize the IRQ first */

	irq = txn_claim_irq(irq);
	if (irq < 0) {
		printk("cannot claim irq %d\n", c);
		return irq;
	}

	i->txn_addr = txn_alloc_addr(irq);
	i->txn_data = txn_alloc_data(irq);
	i->irq = irq;

	return irq;
}

EXPORT_SYMBOL(gsc_alloc_irq);
EXPORT_SYMBOL(gsc_claim_irq);

/* Common interrupt demultiplexer used by Asp, Lasi & Wax.  */
irqreturn_t gsc_asic_intr(int gsc_asic_irq, void *dev)
{
	unsigned long irr;
	struct gsc_asic *gsc_asic = dev;

	irr = gsc_readl(gsc_asic->hpa + OFFSET_IRR);
	if (irr == 0)
		return IRQ_NONE;

	DEBPRINTK("%s intr, mask=0x%x\n", gsc_asic->name, irr);

	do {
		int local_irq = __ffs(irr);
		unsigned int irq = gsc_asic->global_irq[local_irq];
		generic_handle_irq(irq);
		irr &= ~(1 << local_irq);
	} while (irr);

	return IRQ_HANDLED;
}

int gsc_find_local_irq(unsigned int irq, int *global_irqs, int limit)
{
	int local_irq;

	for (local_irq = 0; local_irq < limit; local_irq++) {
		if (global_irqs[local_irq] == irq)
			return local_irq;
	}

	return NO_IRQ;
}

static void gsc_asic_mask_irq(unsigned int irq)
{
	struct gsc_asic *irq_dev = get_irq_chip_data(irq);
	int local_irq = gsc_find_local_irq(irq, irq_dev->global_irq, 32);
	u32 imr;

	DEBPRINTK(KERN_DEBUG "%s(%d) %s: IMR 0x%x\n", __func__, irq,
			irq_dev->name, imr);

	/* Disable the IRQ line by clearing the bit in the IMR */
	imr = gsc_readl(irq_dev->hpa + OFFSET_IMR);
	imr &= ~(1 << local_irq);
	gsc_writel(imr, irq_dev->hpa + OFFSET_IMR);
}

static void gsc_asic_unmask_irq(unsigned int irq)
{
	struct gsc_asic *irq_dev = get_irq_chip_data(irq);
	int local_irq = gsc_find_local_irq(irq, irq_dev->global_irq, 32);
	u32 imr;

	DEBPRINTK(KERN_DEBUG "%s(%d) %s: IMR 0x%x\n", __func__, irq,
			irq_dev->name, imr);

	/* Enable the IRQ line by setting the bit in the IMR */
	imr = gsc_readl(irq_dev->hpa + OFFSET_IMR);
	imr |= 1 << local_irq;
	gsc_writel(imr, irq_dev->hpa + OFFSET_IMR);
	/*
	 * FIXME: read IPR to make sure the IRQ isn't already pending.
	 *   If so, we need to read IRR and manually call do_irq().
	 */
}

static struct irq_chip gsc_asic_interrupt_type = {
	.name	=	"GSC-ASIC",
	.unmask	=	gsc_asic_unmask_irq,
	.mask	=	gsc_asic_mask_irq,
	.ack	=	no_ack_irq,
};

int gsc_assign_irq(struct irq_chip *type, void *data)
{
	static int irq = GSC_IRQ_BASE;

	if (irq > GSC_IRQ_MAX)
		return NO_IRQ;

	set_irq_chip_and_handler(irq, type, handle_level_irq);
	set_irq_chip_data(irq, data);

	return irq++;
}

void gsc_asic_assign_irq(struct gsc_asic *asic, int local_irq, int *irqp)
{
	int irq = asic->global_irq[local_irq];
	
	if (irq <= 0) {
		irq = gsc_assign_irq(&gsc_asic_interrupt_type, asic);
		if (irq == NO_IRQ)
			return;

		asic->global_irq[local_irq] = irq;
	}
	*irqp = irq;
}

struct gsc_fixup_struct {
	void (*choose_irq)(struct parisc_device *, void *);
	void *ctrl;
};

static int gsc_fixup_irqs_callback(struct device *dev, void *data)
{
	struct parisc_device *padev = to_parisc_device(dev);
	struct gsc_fixup_struct *gf = data;

	/* work-around for 715/64 and others which have parent
	   at path [5] and children at path [5/0/x] */
	if (padev->id.hw_type == HPHW_FAULTY)
		gsc_fixup_irqs(padev, gf->ctrl, gf->choose_irq);
	gf->choose_irq(padev, gf->ctrl);

	return 0;
}

void gsc_fixup_irqs(struct parisc_device *parent, void *ctrl,
			void (*choose_irq)(struct parisc_device *, void *))
{
	struct gsc_fixup_struct data = {
		.choose_irq	= choose_irq,
		.ctrl		= ctrl,
	};

	device_for_each_child(&parent->dev, &data, gsc_fixup_irqs_callback);
}

int gsc_common_setup(struct parisc_device *parent, struct gsc_asic *gsc_asic)
{
	struct resource *res;
	int i;

	gsc_asic->gsc = parent;

	/* Initialise local irq -> global irq mapping */
	for (i = 0; i < 32; i++) {
		gsc_asic->global_irq[i] = NO_IRQ;
	}

	/* allocate resource region */
	res = request_mem_region(gsc_asic->hpa, 0x100000, gsc_asic->name);
	if (res) {
		res->flags = IORESOURCE_MEM; 	/* do not mark it busy ! */
	}

#if 0
	printk(KERN_WARNING "%s IRQ %d EIM 0x%x", gsc_asic->name,
			parent->irq, gsc_asic->eim);
	if (gsc_readl(gsc_asic->hpa + OFFSET_IMR))
		printk("  IMR is non-zero! (0x%x)",
				gsc_readl(gsc_asic->hpa + OFFSET_IMR));
	printk("\n");
#endif

	return 0;
}

extern struct parisc_driver lasi_driver;
extern struct parisc_driver asp_driver;
extern struct parisc_driver wax_driver;

void __init gsc_init(void)
{
#ifdef CONFIG_GSC_LASI
	register_parisc_driver(&lasi_driver);
	register_parisc_driver(&asp_driver);
#endif
#ifdef CONFIG_GSC_WAX
	register_parisc_driver(&wax_driver);
#endif
}
