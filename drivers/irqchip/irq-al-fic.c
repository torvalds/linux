// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 */

#include <linux/bitfield.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

/* FIC Registers */
#define AL_FIC_CAUSE		0x00
#define AL_FIC_SET_CAUSE	0x08
#define AL_FIC_MASK		0x10
#define AL_FIC_CONTROL		0x28

#define CONTROL_TRIGGER_RISING	BIT(3)
#define CONTROL_MASK_MSI_X	BIT(5)

#define NR_FIC_IRQS 32

MODULE_AUTHOR("Talel Shenhar");
MODULE_DESCRIPTION("Amazon's Annapurna Labs Interrupt Controller Driver");

enum al_fic_state {
	AL_FIC_UNCONFIGURED = 0,
	AL_FIC_CONFIGURED_LEVEL,
	AL_FIC_CONFIGURED_RISING_EDGE,
};

struct al_fic {
	void __iomem *base;
	struct irq_domain *domain;
	const char *name;
	unsigned int parent_irq;
	enum al_fic_state state;
};

static void al_fic_set_trigger(struct al_fic *fic,
			       struct irq_chip_generic *gc,
			       enum al_fic_state new_state)
{
	irq_flow_handler_t handler;
	u32 control = readl_relaxed(fic->base + AL_FIC_CONTROL);

	if (new_state == AL_FIC_CONFIGURED_LEVEL) {
		handler = handle_level_irq;
		control &= ~CONTROL_TRIGGER_RISING;
	} else {
		handler = handle_edge_irq;
		control |= CONTROL_TRIGGER_RISING;
	}
	gc->chip_types->handler = handler;
	fic->state = new_state;
	writel_relaxed(control, fic->base + AL_FIC_CONTROL);
}

static int al_fic_irq_set_type(struct irq_data *data, unsigned int flow_type)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(data);
	struct al_fic *fic = gc->private;
	enum al_fic_state new_state;

	guard(raw_spinlock)(&gc->lock);

	if (((flow_type & IRQ_TYPE_SENSE_MASK) != IRQ_TYPE_LEVEL_HIGH) &&
	    ((flow_type & IRQ_TYPE_SENSE_MASK) != IRQ_TYPE_EDGE_RISING)) {
		pr_debug("fic doesn't support flow type %d\n", flow_type);
		return -EINVAL;
	}

	new_state = (flow_type & IRQ_TYPE_LEVEL_HIGH) ?
		AL_FIC_CONFIGURED_LEVEL : AL_FIC_CONFIGURED_RISING_EDGE;

	/*
	 * A given FIC instance can be either all level or all edge triggered.
	 * This is generally fixed depending on what pieces of HW it's wired up
	 * to.
	 *
	 * We configure it based on the sensitivity of the first source
	 * being setup, and reject any subsequent attempt at configuring it in a
	 * different way.
	 */
	if (fic->state == AL_FIC_UNCONFIGURED) {
		al_fic_set_trigger(fic, gc, new_state);
	} else if (fic->state != new_state) {
		pr_debug("fic %s state already configured to %d\n", fic->name, fic->state);
		return -EINVAL;
	}
	return 0;
}

static void al_fic_irq_handler(struct irq_desc *desc)
{
	struct al_fic *fic = irq_desc_get_handler_data(desc);
	struct irq_domain *domain = fic->domain;
	struct irq_chip *irqchip = irq_desc_get_chip(desc);
	struct irq_chip_generic *gc = irq_get_domain_generic_chip(domain, 0);
	unsigned long pending;
	u32 hwirq;

	chained_irq_enter(irqchip, desc);

	pending = readl_relaxed(fic->base + AL_FIC_CAUSE);
	pending &= ~gc->mask_cache;

	for_each_set_bit(hwirq, &pending, NR_FIC_IRQS)
		generic_handle_domain_irq(domain, hwirq);

	chained_irq_exit(irqchip, desc);
}

static int al_fic_irq_retrigger(struct irq_data *data)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(data);
	struct al_fic *fic = gc->private;

	writel_relaxed(BIT(data->hwirq), fic->base + AL_FIC_SET_CAUSE);

	return 1;
}

static int al_fic_register(struct device_node *node,
			   struct al_fic *fic)
{
	struct irq_chip_generic *gc;
	int ret;

	fic->domain = irq_domain_create_linear(of_fwnode_handle(node),
					    NR_FIC_IRQS,
					    &irq_generic_chip_ops,
					    fic);
	if (!fic->domain) {
		pr_err("fail to add irq domain\n");
		return -ENOMEM;
	}

	ret = irq_alloc_domain_generic_chips(fic->domain,
					     NR_FIC_IRQS,
					     1, fic->name,
					     handle_level_irq,
					     0, 0, IRQ_GC_INIT_MASK_CACHE);
	if (ret) {
		pr_err("fail to allocate generic chip (%d)\n", ret);
		goto err_domain_remove;
	}

	gc = irq_get_domain_generic_chip(fic->domain, 0);
	gc->reg_base = fic->base;
	gc->chip_types->regs.mask = AL_FIC_MASK;
	gc->chip_types->regs.ack = AL_FIC_CAUSE;
	gc->chip_types->chip.irq_mask = irq_gc_mask_set_bit;
	gc->chip_types->chip.irq_unmask = irq_gc_mask_clr_bit;
	gc->chip_types->chip.irq_ack = irq_gc_ack_clr_bit;
	gc->chip_types->chip.irq_set_type = al_fic_irq_set_type;
	gc->chip_types->chip.irq_retrigger = al_fic_irq_retrigger;
	gc->chip_types->chip.flags = IRQCHIP_SKIP_SET_WAKE;
	gc->private = fic;

	irq_set_chained_handler_and_data(fic->parent_irq,
					 al_fic_irq_handler,
					 fic);
	return 0;

err_domain_remove:
	irq_domain_remove(fic->domain);

	return ret;
}

/*
 * al_fic_wire_init() - initialize and configure fic in wire mode
 * @of_node: optional pointer to interrupt controller's device tree node.
 * @base: mmio to fic register
 * @name: name of the fic
 * @parent_irq: interrupt of parent
 *
 * This API will configure the fic hardware to to work in wire mode.
 * In wire mode, fic hardware is generating a wire ("wired") interrupt.
 * Interrupt can be generated based on positive edge or level - configuration is
 * to be determined based on connected hardware to this fic.
 */
static struct al_fic *al_fic_wire_init(struct device_node *node,
				       void __iomem *base,
				       const char *name,
				       unsigned int parent_irq)
{
	struct al_fic *fic;
	int ret;
	u32 control = CONTROL_MASK_MSI_X;

	fic = kzalloc(sizeof(*fic), GFP_KERNEL);
	if (!fic)
		return ERR_PTR(-ENOMEM);

	fic->base = base;
	fic->parent_irq = parent_irq;
	fic->name = name;

	/* mask out all interrupts */
	writel_relaxed(0xFFFFFFFF, fic->base + AL_FIC_MASK);

	/* clear any pending interrupt */
	writel_relaxed(0, fic->base + AL_FIC_CAUSE);

	writel_relaxed(control, fic->base + AL_FIC_CONTROL);

	ret = al_fic_register(node, fic);
	if (ret) {
		pr_err("fail to register irqchip\n");
		goto err_free;
	}

	pr_debug("%s initialized successfully in Legacy mode (parent-irq=%u)\n",
		 fic->name, parent_irq);

	return fic;

err_free:
	kfree(fic);
	return ERR_PTR(ret);
}

static int __init al_fic_init_dt(struct device_node *node,
				 struct device_node *parent)
{
	int ret;
	void __iomem *base;
	unsigned int parent_irq;
	struct al_fic *fic;

	if (!parent) {
		pr_err("%s: unsupported - device require a parent\n",
		       node->name);
		return -EINVAL;
	}

	base = of_iomap(node, 0);
	if (!base) {
		pr_err("%s: fail to map memory\n", node->name);
		return -ENOMEM;
	}

	parent_irq = irq_of_parse_and_map(node, 0);
	if (!parent_irq) {
		pr_err("%s: fail to map irq\n", node->name);
		ret = -EINVAL;
		goto err_unmap;
	}

	fic = al_fic_wire_init(node,
			       base,
			       node->name,
			       parent_irq);
	if (IS_ERR(fic)) {
		pr_err("%s: fail to initialize irqchip (%lu)\n",
		       node->name,
		       PTR_ERR(fic));
		ret = PTR_ERR(fic);
		goto err_irq_dispose;
	}

	return 0;

err_irq_dispose:
	irq_dispose_mapping(parent_irq);
err_unmap:
	iounmap(base);

	return ret;
}

IRQCHIP_DECLARE(al_fic, "amazon,al-fic", al_fic_init_dt);
