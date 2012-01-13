/*
 *  Support for C64x+ Megamodule Interrupt Controller
 *
 *  Copyright (C) 2010, 2011 Texas Instruments Incorporated
 *  Contributed by: Mark Salter <msalter@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <asm/soc.h>
#include <asm/megamod-pic.h>

#define NR_COMBINERS	4
#define NR_MUX_OUTPUTS  12

#define IRQ_UNMAPPED 0xffff

/*
 * Megamodule Interrupt Controller register layout
 */
struct megamod_regs {
	u32	evtflag[8];
	u32	evtset[8];
	u32	evtclr[8];
	u32	reserved0[8];
	u32	evtmask[8];
	u32	mevtflag[8];
	u32	expmask[8];
	u32	mexpflag[8];
	u32	intmux_unused;
	u32	intmux[7];
	u32	reserved1[8];
	u32	aegmux[2];
	u32	reserved2[14];
	u32	intxstat;
	u32	intxclr;
	u32	intdmask;
	u32	reserved3[13];
	u32	evtasrt;
};

struct megamod_pic {
	struct irq_host	*irqhost;
	struct megamod_regs __iomem *regs;
	raw_spinlock_t lock;

	/* hw mux mapping */
	unsigned int output_to_irq[NR_MUX_OUTPUTS];
};

static struct megamod_pic *mm_pic;

struct megamod_cascade_data {
	struct megamod_pic *pic;
	int index;
};

static struct megamod_cascade_data cascade_data[NR_COMBINERS];

static void mask_megamod(struct irq_data *data)
{
	struct megamod_pic *pic = irq_data_get_irq_chip_data(data);
	irq_hw_number_t src = irqd_to_hwirq(data);
	u32 __iomem *evtmask = &pic->regs->evtmask[src / 32];

	raw_spin_lock(&pic->lock);
	soc_writel(soc_readl(evtmask) | (1 << (src & 31)), evtmask);
	raw_spin_unlock(&pic->lock);
}

static void unmask_megamod(struct irq_data *data)
{
	struct megamod_pic *pic = irq_data_get_irq_chip_data(data);
	irq_hw_number_t src = irqd_to_hwirq(data);
	u32 __iomem *evtmask = &pic->regs->evtmask[src / 32];

	raw_spin_lock(&pic->lock);
	soc_writel(soc_readl(evtmask) & ~(1 << (src & 31)), evtmask);
	raw_spin_unlock(&pic->lock);
}

static struct irq_chip megamod_chip = {
	.name		= "megamod",
	.irq_mask	= mask_megamod,
	.irq_unmask	= unmask_megamod,
};

static void megamod_irq_cascade(unsigned int irq, struct irq_desc *desc)
{
	struct megamod_cascade_data *cascade;
	struct megamod_pic *pic;
	u32 events;
	int n, idx;

	cascade = irq_desc_get_handler_data(desc);

	pic = cascade->pic;
	idx = cascade->index;

	while ((events = soc_readl(&pic->regs->mevtflag[idx])) != 0) {
		n = __ffs(events);

		irq = irq_linear_revmap(pic->irqhost, idx * 32 + n);

		soc_writel(1 << n, &pic->regs->evtclr[idx]);

		generic_handle_irq(irq);
	}
}

static int megamod_map(struct irq_host *h, unsigned int virq,
		       irq_hw_number_t hw)
{
	struct megamod_pic *pic = h->host_data;
	int i;

	/* We shouldn't see a hwirq which is muxed to core controller */
	for (i = 0; i < NR_MUX_OUTPUTS; i++)
		if (pic->output_to_irq[i] == hw)
			return -1;

	irq_set_chip_data(virq, pic);
	irq_set_chip_and_handler(virq, &megamod_chip, handle_level_irq);

	/* Set default irq type */
	irq_set_irq_type(virq, IRQ_TYPE_NONE);

	return 0;
}

static int megamod_xlate(struct irq_host *h, struct device_node *ct,
			 const u32 *intspec, unsigned int intsize,
			 irq_hw_number_t *out_hwirq, unsigned int *out_type)

{
	/* megamod intspecs must have 1 cell */
	BUG_ON(intsize != 1);
	*out_hwirq = intspec[0];
	*out_type = IRQ_TYPE_NONE;
	return 0;
}

static struct irq_host_ops megamod_host_ops = {
	.map	= megamod_map,
	.xlate	= megamod_xlate,
};

static void __init set_megamod_mux(struct megamod_pic *pic, int src, int output)
{
	int index, offset;
	u32 val;

	if (src < 0 || src >= (NR_COMBINERS * 32)) {
		pic->output_to_irq[output] = IRQ_UNMAPPED;
		return;
	}

	/* four mappings per mux register */
	index = output / 4;
	offset = (output & 3) * 8;

	val = soc_readl(&pic->regs->intmux[index]);
	val &= ~(0xff << offset);
	val |= src << offset;
	soc_writel(val, &pic->regs->intmux[index]);
}

/*
 * Parse the MUX mapping, if one exists.
 *
 * The MUX map is an array of up to 12 cells; one for each usable core priority
 * interrupt. The value of a given cell is the megamodule interrupt source
 * which is to me MUXed to the output corresponding to the cell position
 * withing the array. The first cell in the array corresponds to priority
 * 4 and the last (12th) cell corresponds to priority 15. The allowed
 * values are 4 - ((NR_COMBINERS * 32) - 1). Note that the combined interrupt
 * sources (0 - 3) are not allowed to be mapped through this property. They
 * are handled through the "interrupts" property. This allows us to use a
 * value of zero as a "do not map" placeholder.
 */
static void __init parse_priority_map(struct megamod_pic *pic,
				      int *mapping, int size)
{
	struct device_node *np = pic->irqhost->of_node;
	const __be32 *map;
	int i, maplen;
	u32 val;

	map = of_get_property(np, "ti,c64x+megamod-pic-mux", &maplen);
	if (map) {
		maplen /= 4;
		if (maplen > size)
			maplen = size;

		for (i = 0; i < maplen; i++) {
			val = be32_to_cpup(map);
			if (val && val >= 4)
				mapping[i] = val;
			++map;
		}
	}
}

static struct megamod_pic * __init init_megamod_pic(struct device_node *np)
{
	struct megamod_pic *pic;
	int i, irq;
	int mapping[NR_MUX_OUTPUTS];

	pr_info("Initializing C64x+ Megamodule PIC\n");

	pic = kzalloc(sizeof(struct megamod_pic), GFP_KERNEL);
	if (!pic) {
		pr_err("%s: Could not alloc PIC structure.\n", np->full_name);
		return NULL;
	}

	pic->irqhost = irq_alloc_host(np, IRQ_HOST_MAP_LINEAR,
				      NR_COMBINERS * 32, &megamod_host_ops,
				      IRQ_UNMAPPED);
	if (!pic->irqhost) {
		pr_err("%s: Could not alloc host.\n", np->full_name);
		goto error_free;
	}

	pic->irqhost->host_data = pic;

	raw_spin_lock_init(&pic->lock);

	pic->regs = of_iomap(np, 0);
	if (!pic->regs) {
		pr_err("%s: Could not map registers.\n", np->full_name);
		goto error_free;
	}

	/* Initialize MUX map */
	for (i = 0; i < ARRAY_SIZE(mapping); i++)
		mapping[i] = IRQ_UNMAPPED;

	parse_priority_map(pic, mapping, ARRAY_SIZE(mapping));

	/*
	 * We can have up to 12 interrupts cascading to the core controller.
	 * These cascades can be from the combined interrupt sources or for
	 * individual interrupt sources. The "interrupts" property only
	 * deals with the cascaded combined interrupts. The individual
	 * interrupts muxed to the core controller use the core controller
	 * as their interrupt parent.
	 */
	for (i = 0; i < NR_COMBINERS; i++) {

		irq = irq_of_parse_and_map(np, i);
		if (irq == NO_IRQ)
			continue;

		/*
		 * We count on the core priority interrupts (4 - 15) being
		 * direct mapped. Check that device tree provided something
		 * in that range.
		 */
		if (irq < 4 || irq >= NR_PRIORITY_IRQS) {
			pr_err("%s: combiner-%d virq %d out of range!\n",
				 np->full_name, i, irq);
			continue;
		}

		/* record the mapping */
		mapping[irq - 4] = i;

		pr_debug("%s: combiner-%d cascading to virq %d\n",
			 np->full_name, i, irq);

		cascade_data[i].pic = pic;
		cascade_data[i].index = i;

		/* mask and clear all events in combiner */
		soc_writel(~0, &pic->regs->evtmask[i]);
		soc_writel(~0, &pic->regs->evtclr[i]);

		irq_set_handler_data(irq, &cascade_data[i]);
		irq_set_chained_handler(irq, megamod_irq_cascade);
	}

	/* Finally, set up the MUX registers */
	for (i = 0; i < NR_MUX_OUTPUTS; i++) {
		if (mapping[i] != IRQ_UNMAPPED) {
			pr_debug("%s: setting mux %d to priority %d\n",
				 np->full_name, mapping[i], i + 4);
			set_megamod_mux(pic, mapping[i], i);
		}
	}

	return pic;

error_free:
	kfree(pic);

	return NULL;
}

/*
 * Return next active event after ACK'ing it.
 * Return -1 if no events active.
 */
static int get_exception(void)
{
	int i, bit;
	u32 mask;

	for (i = 0; i < NR_COMBINERS; i++) {
		mask = soc_readl(&mm_pic->regs->mexpflag[i]);
		if (mask) {
			bit = __ffs(mask);
			soc_writel(1 << bit, &mm_pic->regs->evtclr[i]);
			return (i * 32) + bit;
		}
	}
	return -1;
}

static void assert_event(unsigned int val)
{
	soc_writel(val, &mm_pic->regs->evtasrt);
}

void __init megamod_pic_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "ti,c64x+megamod-pic");
	if (!np)
		return;

	mm_pic = init_megamod_pic(np);
	of_node_put(np);

	soc_ops.get_exception = get_exception;
	soc_ops.assert_event = assert_event;

	return;
}
