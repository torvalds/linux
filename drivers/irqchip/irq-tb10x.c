// SPDX-License-Identifier: GPL-2.0-only
/*
 * Abilis Systems interrupt controller driver
 *
 * Copyright (C) Abilis Systems 2012
 *
 * Author: Christian Ruppert <christian.ruppert@abilis.com>
 */

#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/bitops.h>

#define AB_IRQCTL_INT_ENABLE   0x00
#define AB_IRQCTL_INT_STATUS   0x04
#define AB_IRQCTL_SRC_MODE     0x08
#define AB_IRQCTL_SRC_POLARITY 0x0C
#define AB_IRQCTL_INT_MODE     0x10
#define AB_IRQCTL_INT_POLARITY 0x14
#define AB_IRQCTL_INT_FORCE    0x18

#define AB_IRQCTL_MAXIRQ       32

static inline void ab_irqctl_writereg(struct irq_chip_generic *gc, u32 reg,
	u32 val)
{
	irq_reg_writel(gc, val, reg);
}

static inline u32 ab_irqctl_readreg(struct irq_chip_generic *gc, u32 reg)
{
	return irq_reg_readl(gc, reg);
}

static int tb10x_irq_set_type(struct irq_data *data, unsigned int flow_type)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(data);
	uint32_t im, mod, pol;

	im = data->mask;

	irq_gc_lock(gc);

	mod = ab_irqctl_readreg(gc, AB_IRQCTL_SRC_MODE) | im;
	pol = ab_irqctl_readreg(gc, AB_IRQCTL_SRC_POLARITY) | im;

	switch (flow_type & IRQF_TRIGGER_MASK) {
	case IRQ_TYPE_EDGE_FALLING:
		pol ^= im;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		mod ^= im;
		break;
	case IRQ_TYPE_NONE:
		flow_type = IRQ_TYPE_LEVEL_LOW;
		fallthrough;
	case IRQ_TYPE_LEVEL_LOW:
		mod ^= im;
		pol ^= im;
		break;
	case IRQ_TYPE_EDGE_RISING:
		break;
	default:
		irq_gc_unlock(gc);
		pr_err("%s: Cannot assign multiple trigger modes to IRQ %d.\n",
			__func__, data->irq);
		return -EBADR;
	}

	irqd_set_trigger_type(data, flow_type);
	irq_setup_alt_chip(data, flow_type);

	ab_irqctl_writereg(gc, AB_IRQCTL_SRC_MODE, mod);
	ab_irqctl_writereg(gc, AB_IRQCTL_SRC_POLARITY, pol);
	ab_irqctl_writereg(gc, AB_IRQCTL_INT_STATUS, im);

	irq_gc_unlock(gc);

	return IRQ_SET_MASK_OK;
}

static void tb10x_irq_cascade(struct irq_desc *desc)
{
	struct irq_domain *domain = irq_desc_get_handler_data(desc);
	unsigned int irq = irq_desc_get_irq(desc);

	generic_handle_domain_irq(domain, irq);
}

static int __init of_tb10x_init_irq(struct device_node *ictl,
					struct device_node *parent)
{
	int i, ret, nrirqs = of_irq_count(ictl);
	struct resource mem;
	struct irq_chip_generic *gc;
	struct irq_domain *domain;
	void __iomem *reg_base;

	if (of_address_to_resource(ictl, 0, &mem)) {
		pr_err("%pOFn: No registers declared in DeviceTree.\n",
			ictl);
		return -EINVAL;
	}

	if (!request_mem_region(mem.start, resource_size(&mem),
		ictl->full_name)) {
		pr_err("%pOFn: Request mem region failed.\n", ictl);
		return -EBUSY;
	}

	reg_base = ioremap(mem.start, resource_size(&mem));
	if (!reg_base) {
		ret = -EBUSY;
		pr_err("%pOFn: ioremap failed.\n", ictl);
		goto ioremap_fail;
	}

	domain = irq_domain_add_linear(ictl, AB_IRQCTL_MAXIRQ,
					&irq_generic_chip_ops, NULL);
	if (!domain) {
		ret = -ENOMEM;
		pr_err("%pOFn: Could not register interrupt domain.\n",
			ictl);
		goto irq_domain_add_fail;
	}

	ret = irq_alloc_domain_generic_chips(domain, AB_IRQCTL_MAXIRQ,
				2, ictl->name, handle_level_irq,
				IRQ_NOREQUEST, IRQ_NOPROBE,
				IRQ_GC_INIT_MASK_CACHE);
	if (ret) {
		pr_err("%pOFn: Could not allocate generic interrupt chip.\n",
			ictl);
		goto gc_alloc_fail;
	}

	gc = domain->gc->gc[0];
	gc->reg_base                         = reg_base;

	gc->chip_types[0].type               = IRQ_TYPE_LEVEL_MASK;
	gc->chip_types[0].chip.irq_mask      = irq_gc_mask_clr_bit;
	gc->chip_types[0].chip.irq_unmask    = irq_gc_mask_set_bit;
	gc->chip_types[0].chip.irq_set_type  = tb10x_irq_set_type;
	gc->chip_types[0].regs.mask          = AB_IRQCTL_INT_ENABLE;

	gc->chip_types[1].type               = IRQ_TYPE_EDGE_BOTH;
	gc->chip_types[1].chip.name          = gc->chip_types[0].chip.name;
	gc->chip_types[1].chip.irq_ack       = irq_gc_ack_set_bit;
	gc->chip_types[1].chip.irq_mask      = irq_gc_mask_clr_bit;
	gc->chip_types[1].chip.irq_unmask    = irq_gc_mask_set_bit;
	gc->chip_types[1].chip.irq_set_type  = tb10x_irq_set_type;
	gc->chip_types[1].regs.ack           = AB_IRQCTL_INT_STATUS;
	gc->chip_types[1].regs.mask          = AB_IRQCTL_INT_ENABLE;
	gc->chip_types[1].handler            = handle_edge_irq;

	for (i = 0; i < nrirqs; i++) {
		unsigned int irq = irq_of_parse_and_map(ictl, i);

		irq_set_chained_handler_and_data(irq, tb10x_irq_cascade,
						 domain);
	}

	ab_irqctl_writereg(gc, AB_IRQCTL_INT_ENABLE, 0);
	ab_irqctl_writereg(gc, AB_IRQCTL_INT_MODE, 0);
	ab_irqctl_writereg(gc, AB_IRQCTL_INT_POLARITY, 0);
	ab_irqctl_writereg(gc, AB_IRQCTL_INT_STATUS, ~0UL);

	return 0;

gc_alloc_fail:
	irq_domain_remove(domain);
irq_domain_add_fail:
	iounmap(reg_base);
ioremap_fail:
	release_mem_region(mem.start, resource_size(&mem));
	return ret;
}
IRQCHIP_DECLARE(tb10x_intc, "abilis,tb10x-ictl", of_tb10x_init_irq);
