// SPDX-License-Identifier: GPL-2.0-only
//
// Author: Steve Chen <schen@mvista.com>
// Copyright (C) 2008-2009, MontaVista Software, Inc. <source@mvista.com>
// Author: Bartosz Golaszewski <bgolaszewski@baylibre.com>
// Copyright (C) 2019, Texas Instruments
//
// TI Common Platform Interrupt Controller (cp_intc) driver

#include <linux/export.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/irq-davinci-cp-intc.h>
#include <linux/irqdomain.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <asm/exception.h>

#define DAVINCI_CP_INTC_CTRL			0x04
#define DAVINCI_CP_INTC_HOST_CTRL		0x0c
#define DAVINCI_CP_INTC_GLOBAL_ENABLE		0x10
#define DAVINCI_CP_INTC_SYS_STAT_IDX_CLR	0x24
#define DAVINCI_CP_INTC_SYS_ENABLE_IDX_SET	0x28
#define DAVINCI_CP_INTC_SYS_ENABLE_IDX_CLR	0x2c
#define DAVINCI_CP_INTC_HOST_ENABLE_IDX_SET	0x34
#define DAVINCI_CP_INTC_HOST_ENABLE_IDX_CLR	0x38
#define DAVINCI_CP_INTC_PRIO_IDX		0x80
#define DAVINCI_CP_INTC_SYS_STAT_CLR(n)		(0x0280 + (n << 2))
#define DAVINCI_CP_INTC_SYS_ENABLE_CLR(n)	(0x0380 + (n << 2))
#define DAVINCI_CP_INTC_CHAN_MAP(n)		(0x0400 + (n << 2))
#define DAVINCI_CP_INTC_SYS_POLARITY(n)		(0x0d00 + (n << 2))
#define DAVINCI_CP_INTC_SYS_TYPE(n)		(0x0d80 + (n << 2))
#define DAVINCI_CP_INTC_HOST_ENABLE(n)		(0x1500 + (n << 2))
#define DAVINCI_CP_INTC_PRI_INDX_MASK		GENMASK(9, 0)
#define DAVINCI_CP_INTC_GPIR_NONE		BIT(31)

static void __iomem *davinci_cp_intc_base;
static struct irq_domain *davinci_cp_intc_irq_domain;

static inline unsigned int davinci_cp_intc_read(unsigned int offset)
{
	return readl_relaxed(davinci_cp_intc_base + offset);
}

static inline void davinci_cp_intc_write(unsigned long value,
					 unsigned int offset)
{
	writel_relaxed(value, davinci_cp_intc_base + offset);
}

static void davinci_cp_intc_ack_irq(struct irq_data *d)
{
	davinci_cp_intc_write(d->hwirq, DAVINCI_CP_INTC_SYS_STAT_IDX_CLR);
}

static void davinci_cp_intc_mask_irq(struct irq_data *d)
{
	/* XXX don't know why we need to disable nIRQ here... */
	davinci_cp_intc_write(1, DAVINCI_CP_INTC_HOST_ENABLE_IDX_CLR);
	davinci_cp_intc_write(d->hwirq, DAVINCI_CP_INTC_SYS_ENABLE_IDX_CLR);
	davinci_cp_intc_write(1, DAVINCI_CP_INTC_HOST_ENABLE_IDX_SET);
}

static void davinci_cp_intc_unmask_irq(struct irq_data *d)
{
	davinci_cp_intc_write(d->hwirq, DAVINCI_CP_INTC_SYS_ENABLE_IDX_SET);
}

static int davinci_cp_intc_set_irq_type(struct irq_data *d,
					unsigned int flow_type)
{
	unsigned int reg, mask, polarity, type;

	reg = BIT_WORD(d->hwirq);
	mask = BIT_MASK(d->hwirq);
	polarity = davinci_cp_intc_read(DAVINCI_CP_INTC_SYS_POLARITY(reg));
	type = davinci_cp_intc_read(DAVINCI_CP_INTC_SYS_TYPE(reg));

	switch (flow_type) {
	case IRQ_TYPE_EDGE_RISING:
		polarity |= mask;
		type |= mask;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		polarity &= ~mask;
		type |= mask;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		polarity |= mask;
		type &= ~mask;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		polarity &= ~mask;
		type &= ~mask;
		break;
	default:
		return -EINVAL;
	}

	davinci_cp_intc_write(polarity, DAVINCI_CP_INTC_SYS_POLARITY(reg));
	davinci_cp_intc_write(type, DAVINCI_CP_INTC_SYS_TYPE(reg));

	return 0;
}

static struct irq_chip davinci_cp_intc_irq_chip = {
	.name		= "cp_intc",
	.irq_ack	= davinci_cp_intc_ack_irq,
	.irq_mask	= davinci_cp_intc_mask_irq,
	.irq_unmask	= davinci_cp_intc_unmask_irq,
	.irq_set_type	= davinci_cp_intc_set_irq_type,
	.flags		= IRQCHIP_SKIP_SET_WAKE,
};

static void __exception_irq_entry davinci_cp_intc_handle_irq(struct pt_regs *regs)
{
	int gpir, irqnr, none;

	/*
	 * The interrupt number is in first ten bits. The NONE field set to 1
	 * indicates a spurious irq.
	 */

	gpir = davinci_cp_intc_read(DAVINCI_CP_INTC_PRIO_IDX);
	irqnr = gpir & DAVINCI_CP_INTC_PRI_INDX_MASK;
	none = gpir & DAVINCI_CP_INTC_GPIR_NONE;

	if (unlikely(none)) {
		pr_err_once("%s: spurious irq!\n", __func__);
		return;
	}

	generic_handle_domain_irq(davinci_cp_intc_irq_domain, irqnr);
}

static int davinci_cp_intc_host_map(struct irq_domain *h, unsigned int virq,
			  irq_hw_number_t hw)
{
	pr_debug("cp_intc_host_map(%d, 0x%lx)\n", virq, hw);

	irq_set_chip(virq, &davinci_cp_intc_irq_chip);
	irq_set_probe(virq);
	irq_set_handler(virq, handle_edge_irq);

	return 0;
}

static const struct irq_domain_ops davinci_cp_intc_irq_domain_ops = {
	.map = davinci_cp_intc_host_map,
	.xlate = irq_domain_xlate_onetwocell,
};

static int __init
davinci_cp_intc_do_init(const struct davinci_cp_intc_config *config,
			struct device_node *node)
{
	unsigned int num_regs = BITS_TO_LONGS(config->num_irqs);
	int offset, irq_base;
	void __iomem *req;

	req = request_mem_region(config->reg.start,
				 resource_size(&config->reg),
				 "davinci-cp-intc");
	if (!req) {
		pr_err("%s: register range busy\n", __func__);
		return -EBUSY;
	}

	davinci_cp_intc_base = ioremap(config->reg.start,
				       resource_size(&config->reg));
	if (!davinci_cp_intc_base) {
		pr_err("%s: unable to ioremap register range\n", __func__);
		return -EINVAL;
	}

	davinci_cp_intc_write(0, DAVINCI_CP_INTC_GLOBAL_ENABLE);

	/* Disable all host interrupts */
	davinci_cp_intc_write(0, DAVINCI_CP_INTC_HOST_ENABLE(0));

	/* Disable system interrupts */
	for (offset = 0; offset < num_regs; offset++)
		davinci_cp_intc_write(~0,
			DAVINCI_CP_INTC_SYS_ENABLE_CLR(offset));

	/* Set to normal mode, no nesting, no priority hold */
	davinci_cp_intc_write(0, DAVINCI_CP_INTC_CTRL);
	davinci_cp_intc_write(0, DAVINCI_CP_INTC_HOST_CTRL);

	/* Clear system interrupt status */
	for (offset = 0; offset < num_regs; offset++)
		davinci_cp_intc_write(~0,
			DAVINCI_CP_INTC_SYS_STAT_CLR(offset));

	/* Enable nIRQ (what about nFIQ?) */
	davinci_cp_intc_write(1, DAVINCI_CP_INTC_HOST_ENABLE_IDX_SET);

	/* Default all priorities to channel 7. */
	num_regs = (config->num_irqs + 3) >> 2;	/* 4 channels per register */
	for (offset = 0; offset < num_regs; offset++)
		davinci_cp_intc_write(0x07070707,
			DAVINCI_CP_INTC_CHAN_MAP(offset));

	irq_base = irq_alloc_descs(-1, 0, config->num_irqs, 0);
	if (irq_base < 0) {
		pr_err("%s: unable to allocate interrupt descriptors: %d\n",
		       __func__, irq_base);
		return irq_base;
	}

	davinci_cp_intc_irq_domain = irq_domain_add_legacy(
					node, config->num_irqs, irq_base, 0,
					&davinci_cp_intc_irq_domain_ops, NULL);

	if (!davinci_cp_intc_irq_domain) {
		pr_err("%s: unable to create an interrupt domain\n", __func__);
		return -EINVAL;
	}

	set_handle_irq(davinci_cp_intc_handle_irq);

	/* Enable global interrupt */
	davinci_cp_intc_write(1, DAVINCI_CP_INTC_GLOBAL_ENABLE);

	return 0;
}

int __init davinci_cp_intc_init(const struct davinci_cp_intc_config *config)
{
	return davinci_cp_intc_do_init(config, NULL);
}

static int __init davinci_cp_intc_of_init(struct device_node *node,
					  struct device_node *parent)
{
	struct davinci_cp_intc_config config = { };
	int ret;

	ret = of_address_to_resource(node, 0, &config.reg);
	if (ret) {
		pr_err("%s: unable to get the register range from device-tree\n",
		       __func__);
		return ret;
	}

	ret = of_property_read_u32(node, "ti,intc-size", &config.num_irqs);
	if (ret) {
		pr_err("%s: unable to read the 'ti,intc-size' property\n",
		       __func__);
		return ret;
	}

	return davinci_cp_intc_do_init(&config, node);
}
IRQCHIP_DECLARE(cp_intc, "ti,cp-intc", davinci_cp_intc_of_init);
