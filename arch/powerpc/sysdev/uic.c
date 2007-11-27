/*
 * arch/powerpc/sysdev/uic.c
 *
 * IBM PowerPC 4xx Universal Interrupt Controller
 *
 * Copyright 2007 David Gibson <dwg@au1.ibm.com>, IBM Corporation.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/sysdev.h>
#include <linux/device.h>
#include <linux/bootmem.h>
#include <linux/spinlock.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/dcr.h>

#define NR_UIC_INTS	32

#define UIC_SR		0x0
#define UIC_ER		0x2
#define UIC_CR		0x3
#define UIC_PR		0x4
#define UIC_TR		0x5
#define UIC_MSR		0x6
#define UIC_VR		0x7
#define UIC_VCR		0x8

#define uic_irq_to_hw(virq)	(irq_map[virq].hwirq)

struct uic *primary_uic;

struct uic {
	int index;
	int dcrbase;

	spinlock_t lock;

	/* The remapper for this UIC */
	struct irq_host	*irqhost;

	/* For secondary UICs, the cascade interrupt's irqaction */
	struct irqaction cascade;
};

static void uic_unmask_irq(unsigned int virq)
{
	struct uic *uic = get_irq_chip_data(virq);
	unsigned int src = uic_irq_to_hw(virq);
	unsigned long flags;
	u32 er;

	spin_lock_irqsave(&uic->lock, flags);
	er = mfdcr(uic->dcrbase + UIC_ER);
	er |= 1 << (31 - src);
	mtdcr(uic->dcrbase + UIC_ER, er);
	spin_unlock_irqrestore(&uic->lock, flags);
}

static void uic_mask_irq(unsigned int virq)
{
	struct uic *uic = get_irq_chip_data(virq);
	unsigned int src = uic_irq_to_hw(virq);
	unsigned long flags;
	u32 er;

	spin_lock_irqsave(&uic->lock, flags);
	er = mfdcr(uic->dcrbase + UIC_ER);
	er &= ~(1 << (31 - src));
	mtdcr(uic->dcrbase + UIC_ER, er);
	spin_unlock_irqrestore(&uic->lock, flags);
}

static void uic_ack_irq(unsigned int virq)
{
	struct uic *uic = get_irq_chip_data(virq);
	unsigned int src = uic_irq_to_hw(virq);
	unsigned long flags;

	spin_lock_irqsave(&uic->lock, flags);
	mtdcr(uic->dcrbase + UIC_SR, 1 << (31-src));
	spin_unlock_irqrestore(&uic->lock, flags);
}

static void uic_mask_ack_irq(unsigned int virq)
{
	struct uic *uic = get_irq_chip_data(virq);
	unsigned int src = uic_irq_to_hw(virq);
	unsigned long flags;
	u32 er, sr;

	sr = 1 << (31-src);
	spin_lock_irqsave(&uic->lock, flags);
	er = mfdcr(uic->dcrbase + UIC_ER);
	er &= ~sr;
	mtdcr(uic->dcrbase + UIC_ER, er);
	mtdcr(uic->dcrbase + UIC_SR, sr);
	spin_unlock_irqrestore(&uic->lock, flags);
}

static int uic_set_irq_type(unsigned int virq, unsigned int flow_type)
{
	struct uic *uic = get_irq_chip_data(virq);
	unsigned int src = uic_irq_to_hw(virq);
	struct irq_desc *desc = get_irq_desc(virq);
	unsigned long flags;
	int trigger, polarity;
	u32 tr, pr, mask;

	switch (flow_type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_NONE:
		uic_mask_irq(virq);
		return 0;

	case IRQ_TYPE_EDGE_RISING:
		trigger = 1; polarity = 1;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		trigger = 1; polarity = 0;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		trigger = 0; polarity = 1;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		trigger = 0; polarity = 0;
		break;
	default:
		return -EINVAL;
	}

	mask = ~(1 << (31 - src));

	spin_lock_irqsave(&uic->lock, flags);
	tr = mfdcr(uic->dcrbase + UIC_TR);
	pr = mfdcr(uic->dcrbase + UIC_PR);
	tr = (tr & mask) | (trigger << (31-src));
	pr = (pr & mask) | (polarity << (31-src));

	mtdcr(uic->dcrbase + UIC_PR, pr);
	mtdcr(uic->dcrbase + UIC_TR, tr);

	desc->status &= ~(IRQ_TYPE_SENSE_MASK | IRQ_LEVEL);
	desc->status |= flow_type & IRQ_TYPE_SENSE_MASK;
	if (!trigger)
		desc->status |= IRQ_LEVEL;

	spin_unlock_irqrestore(&uic->lock, flags);

	return 0;
}

static struct irq_chip uic_irq_chip = {
	.typename	= " UIC  ",
	.unmask		= uic_unmask_irq,
	.mask		= uic_mask_irq,
 	.mask_ack	= uic_mask_ack_irq,
	.ack		= uic_ack_irq,
	.set_type	= uic_set_irq_type,
};

/**
 *	handle_uic_irq - irq flow handler for UIC
 *	@irq:	the interrupt number
 *	@desc:	the interrupt description structure for this irq
 *
 * This is modified version of the generic handle_level_irq() suitable
 * for the UIC.  On the UIC, acking (i.e. clearing the SR bit) a level
 * irq will have no effect if the interrupt is still asserted by the
 * device, even if the interrupt is already masked.  Therefore, unlike
 * the standard handle_level_irq(), we must ack the interrupt *after*
 * invoking the ISR (which should have de-asserted the interrupt in
 * the external source).  For edge interrupts we ack at the beginning
 * instead of the end, to keep the window in which we can miss an
 * interrupt as small as possible.
 */
void fastcall handle_uic_irq(unsigned int irq, struct irq_desc *desc)
{
	unsigned int cpu = smp_processor_id();
	struct irqaction *action;
	irqreturn_t action_ret;

	spin_lock(&desc->lock);
	if (desc->status & IRQ_LEVEL)
		desc->chip->mask(irq);
	else
		desc->chip->mask_ack(irq);

	if (unlikely(desc->status & IRQ_INPROGRESS))
		goto out_unlock;
	desc->status &= ~(IRQ_REPLAY | IRQ_WAITING);
	kstat_cpu(cpu).irqs[irq]++;

	/*
	 * If its disabled or no action available
	 * keep it masked and get out of here
	 */
	action = desc->action;
	if (unlikely(!action || (desc->status & IRQ_DISABLED))) {
		desc->status |= IRQ_PENDING;
		goto out_unlock;
	}

	desc->status |= IRQ_INPROGRESS;
	desc->status &= ~IRQ_PENDING;
	spin_unlock(&desc->lock);

	action_ret = handle_IRQ_event(irq, action);

	spin_lock(&desc->lock);
	desc->status &= ~IRQ_INPROGRESS;
	if (desc->status & IRQ_LEVEL)
		desc->chip->ack(irq);
	if (!(desc->status & IRQ_DISABLED) && desc->chip->unmask)
		desc->chip->unmask(irq);
out_unlock:
	spin_unlock(&desc->lock);
}

static int uic_host_map(struct irq_host *h, unsigned int virq,
			irq_hw_number_t hw)
{
	struct uic *uic = h->host_data;

	set_irq_chip_data(virq, uic);
	/* Despite the name, handle_level_irq() works for both level
	 * and edge irqs on UIC.  FIXME: check this is correct */
	set_irq_chip_and_handler(virq, &uic_irq_chip, handle_uic_irq);

	/* Set default irq type */
	set_irq_type(virq, IRQ_TYPE_NONE);

	return 0;
}

static int uic_host_xlate(struct irq_host *h, struct device_node *ct,
			  u32 *intspec, unsigned int intsize,
			  irq_hw_number_t *out_hwirq, unsigned int *out_type)

{
	/* UIC intspecs must have 2 cells */
	BUG_ON(intsize != 2);
	*out_hwirq = intspec[0];
	*out_type = intspec[1];
	return 0;
}

static struct irq_host_ops uic_host_ops = {
	.map	= uic_host_map,
	.xlate	= uic_host_xlate,
};

irqreturn_t uic_cascade(int virq, void *data)
{
	struct uic *uic = data;
	u32 msr;
	int src;
	int subvirq;

	msr = mfdcr(uic->dcrbase + UIC_MSR);
	if (!msr) /* spurious interrupt */
		return IRQ_HANDLED;

	src = 32 - ffs(msr);

	subvirq = irq_linear_revmap(uic->irqhost, src);
	generic_handle_irq(subvirq);

	return IRQ_HANDLED;
}

static struct uic * __init uic_init_one(struct device_node *node)
{
	struct uic *uic;
	const u32 *indexp, *dcrreg;
	int len;

	BUG_ON(! of_device_is_compatible(node, "ibm,uic"));

	uic = alloc_bootmem(sizeof(*uic));
	if (! uic)
		return NULL; /* FIXME: panic? */

	memset(uic, 0, sizeof(*uic));
	spin_lock_init(&uic->lock);
	indexp = of_get_property(node, "cell-index", &len);
	if (!indexp || (len != sizeof(u32))) {
		printk(KERN_ERR "uic: Device node %s has missing or invalid "
		       "cell-index property\n", node->full_name);
		return NULL;
	}
	uic->index = *indexp;

	dcrreg = of_get_property(node, "dcr-reg", &len);
	if (!dcrreg || (len != 2*sizeof(u32))) {
		printk(KERN_ERR "uic: Device node %s has missing or invalid "
		       "dcr-reg property\n", node->full_name);
		return NULL;
	}
	uic->dcrbase = *dcrreg;

	uic->irqhost = irq_alloc_host(of_node_get(node), IRQ_HOST_MAP_LINEAR,
				      NR_UIC_INTS, &uic_host_ops, -1);
	if (! uic->irqhost) {
		of_node_put(node);
		return NULL; /* FIXME: panic? */
	}

	uic->irqhost->host_data = uic;

	/* Start with all interrupts disabled, level and non-critical */
	mtdcr(uic->dcrbase + UIC_ER, 0);
	mtdcr(uic->dcrbase + UIC_CR, 0);
	mtdcr(uic->dcrbase + UIC_TR, 0);
	/* Clear any pending interrupts, in case the firmware left some */
	mtdcr(uic->dcrbase + UIC_SR, 0xffffffff);

	printk ("UIC%d (%d IRQ sources) at DCR 0x%x\n", uic->index,
		NR_UIC_INTS, uic->dcrbase);

	return uic;
}

void __init uic_init_tree(void)
{
	struct device_node *np;
	struct uic *uic;
	const u32 *interrupts;

	/* First locate and initialize the top-level UIC */

	np = of_find_compatible_node(NULL, NULL, "ibm,uic");
	while (np) {
		interrupts = of_get_property(np, "interrupts", NULL);
		if (! interrupts)
			break;

		np = of_find_compatible_node(np, NULL, "ibm,uic");
	}

	BUG_ON(!np); /* uic_init_tree() assumes there's a UIC as the
		      * top-level interrupt controller */
	primary_uic = uic_init_one(np);
	if (! primary_uic)
		panic("Unable to initialize primary UIC %s\n", np->full_name);

	irq_set_default_host(primary_uic->irqhost);
	of_node_put(np);

	/* The scan again for cascaded UICs */
	np = of_find_compatible_node(NULL, NULL, "ibm,uic");
	while (np) {
		interrupts = of_get_property(np, "interrupts", NULL);
		if (interrupts) {
			/* Secondary UIC */
			int cascade_virq;
			int ret;

			uic = uic_init_one(np);
			if (! uic)
				panic("Unable to initialize a secondary UIC %s\n",
				      np->full_name);

			cascade_virq = irq_of_parse_and_map(np, 0);

			uic->cascade.handler = uic_cascade;
			uic->cascade.name = "UIC cascade";
			uic->cascade.dev_id = uic;

			ret = setup_irq(cascade_virq, &uic->cascade);
			if (ret)
				printk(KERN_ERR "Failed to setup_irq(%d) for "
				       "UIC%d cascade\n", cascade_virq,
				       uic->index);

			/* FIXME: setup critical cascade?? */
		}

		np = of_find_compatible_node(np, NULL, "ibm,uic");
	}
}

/* Return an interrupt vector or NO_IRQ if no interrupt is pending. */
unsigned int uic_get_irq(void)
{
	u32 msr;
	int src;

	BUG_ON(! primary_uic);

	msr = mfdcr(primary_uic->dcrbase + UIC_MSR);
	src = 32 - ffs(msr);

	return irq_linear_revmap(primary_uic->irqhost, src);
}
