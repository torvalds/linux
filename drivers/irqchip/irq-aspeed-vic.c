// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2015 - Ben Herrenschmidt, IBM Corp.
 *
 *  Driver for Aspeed "new" VIC as found in SoC generation 3 and later
 *
 *  Based on irq-vic.c:
 *
 *  Copyright (C) 1999 - 2003 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 */

#include <linux/export.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/syscore_ops.h>
#include <linux/device.h>
#include <linux/slab.h>

#include <asm/exception.h>
#include <asm/irq.h>

/* These definitions correspond to the "new mapping" of the
 * register set that interleaves "high" and "low". The offsets
 * below are for the "low" register, add 4 to get to the high one
 */
#define AVIC_IRQ_STATUS		0x00
#define AVIC_FIQ_STATUS		0x08
#define AVIC_RAW_STATUS		0x10
#define AVIC_INT_SELECT		0x18
#define AVIC_INT_ENABLE		0x20
#define AVIC_INT_ENABLE_CLR	0x28
#define AVIC_INT_TRIGGER	0x30
#define AVIC_INT_TRIGGER_CLR	0x38
#define AVIC_INT_SENSE		0x40
#define AVIC_INT_DUAL_EDGE	0x48
#define AVIC_INT_EVENT		0x50
#define AVIC_EDGE_CLR		0x58
#define AVIC_EDGE_STATUS	0x60

#define NUM_IRQS		64

struct aspeed_vic {
	void __iomem		*base;
	u32			edge_sources[2];
	struct irq_domain	*dom;
};
static struct aspeed_vic *system_avic;

static void vic_init_hw(struct aspeed_vic *vic)
{
	u32 sense;

	/* Disable all interrupts */
	writel(0xffffffff, vic->base + AVIC_INT_ENABLE_CLR);
	writel(0xffffffff, vic->base + AVIC_INT_ENABLE_CLR + 4);

	/* Make sure no soft trigger is on */
	writel(0xffffffff, vic->base + AVIC_INT_TRIGGER_CLR);
	writel(0xffffffff, vic->base + AVIC_INT_TRIGGER_CLR + 4);

	/* Set everything to be IRQ */
	writel(0, vic->base + AVIC_INT_SELECT);
	writel(0, vic->base + AVIC_INT_SELECT + 4);

	/* Some interrupts have a programable high/low level trigger
	 * (4 GPIO direct inputs), for now we assume this was configured
	 * by firmware. We read which ones are edge now.
	 */
	sense = readl(vic->base + AVIC_INT_SENSE);
	vic->edge_sources[0] = ~sense;
	sense = readl(vic->base + AVIC_INT_SENSE + 4);
	vic->edge_sources[1] = ~sense;

	/* Clear edge detection latches */
	writel(0xffffffff, vic->base + AVIC_EDGE_CLR);
	writel(0xffffffff, vic->base + AVIC_EDGE_CLR + 4);
}

static void __exception_irq_entry avic_handle_irq(struct pt_regs *regs)
{
	struct aspeed_vic *vic = system_avic;
	u32 stat, irq;

	for (;;) {
		irq = 0;
		stat = readl_relaxed(vic->base + AVIC_IRQ_STATUS);
		if (!stat) {
			stat = readl_relaxed(vic->base + AVIC_IRQ_STATUS + 4);
			irq = 32;
		}
		if (stat == 0)
			break;
		irq += ffs(stat) - 1;
		handle_domain_irq(vic->dom, irq, regs);
	}
}

static void avic_ack_irq(struct irq_data *d)
{
	struct aspeed_vic *vic = irq_data_get_irq_chip_data(d);
	unsigned int sidx = d->hwirq >> 5;
	unsigned int sbit = 1u << (d->hwirq & 0x1f);

	/* Clear edge latch for edge interrupts, nop for level */
	if (vic->edge_sources[sidx] & sbit)
		writel(sbit, vic->base + AVIC_EDGE_CLR + sidx * 4);
}

static void avic_mask_irq(struct irq_data *d)
{
	struct aspeed_vic *vic = irq_data_get_irq_chip_data(d);
	unsigned int sidx = d->hwirq >> 5;
	unsigned int sbit = 1u << (d->hwirq & 0x1f);

	writel(sbit, vic->base + AVIC_INT_ENABLE_CLR + sidx * 4);
}

static void avic_unmask_irq(struct irq_data *d)
{
	struct aspeed_vic *vic = irq_data_get_irq_chip_data(d);
	unsigned int sidx = d->hwirq >> 5;
	unsigned int sbit = 1u << (d->hwirq & 0x1f);

	writel(sbit, vic->base + AVIC_INT_ENABLE + sidx * 4);
}

/* For level irq, faster than going through a nop "ack" and mask */
static void avic_mask_ack_irq(struct irq_data *d)
{
	struct aspeed_vic *vic = irq_data_get_irq_chip_data(d);
	unsigned int sidx = d->hwirq >> 5;
	unsigned int sbit = 1u << (d->hwirq & 0x1f);

	/* First mask */
	writel(sbit, vic->base + AVIC_INT_ENABLE_CLR + sidx * 4);

	/* Then clear edge latch for edge interrupts */
	if (vic->edge_sources[sidx] & sbit)
		writel(sbit, vic->base + AVIC_EDGE_CLR + sidx * 4);
}

static struct irq_chip avic_chip = {
	.name		= "AVIC",
	.irq_ack	= avic_ack_irq,
	.irq_mask	= avic_mask_irq,
	.irq_unmask	= avic_unmask_irq,
	.irq_mask_ack	= avic_mask_ack_irq,
};

static int avic_map(struct irq_domain *d, unsigned int irq,
		    irq_hw_number_t hwirq)
{
	struct aspeed_vic *vic = d->host_data;
	unsigned int sidx = hwirq >> 5;
	unsigned int sbit = 1u << (hwirq & 0x1f);

	/* Check if interrupt exists */
	if (sidx > 1)
		return -EPERM;

	if (vic->edge_sources[sidx] & sbit)
		irq_set_chip_and_handler(irq, &avic_chip, handle_edge_irq);
	else
		irq_set_chip_and_handler(irq, &avic_chip, handle_level_irq);
	irq_set_chip_data(irq, vic);
	irq_set_probe(irq);
	return 0;
}

static const struct irq_domain_ops avic_dom_ops = {
	.map = avic_map,
	.xlate = irq_domain_xlate_onetwocell,
};

static int __init avic_of_init(struct device_node *node,
			       struct device_node *parent)
{
	void __iomem *regs;
	struct aspeed_vic *vic;

	if (WARN(parent, "non-root Aspeed VIC not supported"))
		return -EINVAL;
	if (WARN(system_avic, "duplicate Aspeed VIC not supported"))
		return -EINVAL;

	regs = of_iomap(node, 0);
	if (WARN_ON(!regs))
		return -EIO;

	vic = kzalloc(sizeof(struct aspeed_vic), GFP_KERNEL);
	if (WARN_ON(!vic)) {
		iounmap(regs);
		return -ENOMEM;
	}
	vic->base = regs;

	/* Initialize soures, all masked */
	vic_init_hw(vic);

	/* Ready to receive interrupts */
	system_avic = vic;
	set_handle_irq(avic_handle_irq);

	/* Register our domain */
	vic->dom = irq_domain_add_simple(node, NUM_IRQS, 0,
					 &avic_dom_ops, vic);

	return 0;
}

IRQCHIP_DECLARE(ast2400_vic, "aspeed,ast2400-vic", avic_of_init);
IRQCHIP_DECLARE(ast2500_vic, "aspeed,ast2500-vic", avic_of_init);
