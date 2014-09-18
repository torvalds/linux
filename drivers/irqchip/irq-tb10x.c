/*
 * Abilis Systems interrupt controller driver
 *
 * Copyright (C) Abilis Systems 2012
 *
 * Author: Christian Ruppert <christian.ruppert@abilis.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include "irqchip.h"

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
	irq_reg_writel(val, gc->reg_base + reg);
}

static inline u32 ab_irqctl_readreg(struct irq_chip_generic *gc, u32 reg)
{
	return irq_reg_readl(gc->reg_base + reg);
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

static void tb10x_irq_cascade(unsigned int irq, struct irq_desc *desc)
{
	struct irq_domain *domain = irq_desc_get_handler_data(desc);

	generic_handle_irq(irq_find_mapping(domain, irq));
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
		pr_err("%s: No registers declared in DeviceTree.\n",
			ictl->name);
		return -EINVAL;
	}

	if (!request_mem_region(mem.start, resource_size(&mem),
		ictl->name)) {
		pr_err("%s: Request mem region failed.\n", ictl->name);
		return -EBUSY;
	}

	reg_base = ioremap(mem.start, resource_size(&mem));
	if (!reg_base) {
		ret = -EBUSY;
		pr_err("%s: ioremap failed.\n", ictl->name);
		goto ioremap_fail;
	}

	domain = irq_domain_add_linear(ictl, AB_IRQCTL_MAXIRQ,
					&irq_generic_chip_ops, NULL);
	if (!domain) {
		ret = -ENOMEM;
		pr_err("%s: Could not register interrupt domain.\n",
			ictl->name);
		goto irq_domain_add_fail;
	}

	ret = irq_alloc_domain_generic_chips(domain, AB_IRQCTL_MAXIRQ,
				2, ictl->name, handle_level_irq,
				IRQ_NOREQUEST, IRQ_NOPROBE,
				IRQ_GC_INIT_MASK_CACHE);
	if (ret) {
		pr_err("%s: Could not allocate generic interrupt chip.\n",
			ictl->name);
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

		irq_set_handler_data(irq, domain);
		irq_set_chained_handler(irq, tb10x_irq_cascade);
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
