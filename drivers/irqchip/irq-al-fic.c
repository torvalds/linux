// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 */

#include <linux/bitfield.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

/* FIC Registers */
#define AL_FIC_CAUSE		0x00
#define AL_FIC_SET_CAUSE	0x08
#define AL_FIC_INFO_MASK	0x10
#define AL_FIC_CONTROL		0x28
#define AL_FIC_ERROR_MASK	0x2c
#define AL_FIC_FATAL_MASK	0x34

#define CONTROL_TRIGGER_RISING	BIT(3)
#define CONTROL_MASK_MSI_X	BIT(5)
#define CONTROL_VERSION_ID	GENMASK(29, 28)

#define NR_FIC_IRQS 32

MODULE_AUTHOR("Talel Shenhar");
MODULE_DESCRIPTION("Amazon's Annapurna Labs Interrupt Controller Driver");

enum al_fic_state {
	AL_FIC_UNCONFIGURED = 0,
	AL_FIC_CONFIGURED_LEVEL,
	AL_FIC_CONFIGURED_RISING_EDGE,
};

/*
 * FIC hardware revision, as reported by the CONTROL register version field
 * (CONTROL_VERSION_ID, bits 29-28). These are this driver's names for that
 * field's encoding.
 */
enum al_fic_version {
	AL_FIC_VERSION_V1,
	AL_FIC_VERSION_V2,
	AL_FIC_VERSION_V3,
};

enum al_fic_id {
	AL_FIC_ID_INFO,
	AL_FIC_ID_ERROR,
	AL_FIC_ID_FATAL,
	AL_FIC_ID_MAX,
};

/* Mask register offset for each interrupt group */
static const unsigned int al_fic_mask_offset[AL_FIC_ID_MAX] = {
	[AL_FIC_ID_INFO]  = AL_FIC_INFO_MASK,
	[AL_FIC_ID_ERROR] = AL_FIC_ERROR_MASK,
	[AL_FIC_ID_FATAL] = AL_FIC_FATAL_MASK,
};

/* amazon,al-fic-mask property value for each interrupt group */
static const char * const al_fic_mask_name[AL_FIC_ID_MAX] = {
	[AL_FIC_ID_INFO]  = "info",
	[AL_FIC_ID_ERROR] = "error",
	[AL_FIC_ID_FATAL] = "fatal",
};

struct al_fic {
	void __iomem		*base;
	struct irq_domain	*domain;
	struct device_node	*node;
	unsigned int		parent_irq;
	enum al_fic_state	state;
};

static void al_fic_set_trigger(struct al_fic *fic, struct irq_chip_generic *gc,
			       enum al_fic_state new_state)
{
	u32 control = readl_relaxed(fic->base + AL_FIC_CONTROL);
	irq_flow_handler_t handler;

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
		pr_debug("fic %pOF state already configured to %d\n", fic->node, fic->state);
		return -EINVAL;
	}
	return 0;
}

static irqreturn_t al_fic_irq_handler(int irq, void *data)
{
	struct al_fic *fic = data;
	struct irq_domain *domain = fic->domain;
	struct irq_chip_generic *gc = irq_get_domain_generic_chip(domain, 0);
	unsigned long pending;
	u32 hwirq;

	pending = readl_relaxed(fic->base + AL_FIC_CAUSE);
	pending &= ~gc->mask_cache;

	for_each_set_bit(hwirq, &pending, NR_FIC_IRQS) {
		/*
		 * Redirect a child whose affinity excludes this CPU to its
		 * target CPU via irq_work. The ack moves to
		 * al_fic_irq_pre_redirect(), because the whole flow handler is
		 * deferred when that happens.
		 */
		generic_handle_demux_domain_irq(domain, hwirq);
	}

	return pending ? IRQ_HANDLED : IRQ_NONE;
}

static int al_fic_irq_retrigger(struct irq_data *data)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(data);
	struct al_fic *fic = gc->private;

	writel_relaxed(BIT(data->hwirq), fic->base + AL_FIC_SET_CAUSE);

	return 1;
}

#ifdef CONFIG_SMP
/* The ack happens in al_fic_irq_pre_redirect() on both paths instead. */
static void al_fic_ack_noop(struct irq_data *data) { }

/*
 * Runs in the parent interrupt context on every dispatch, redirected or not.
 * Clear the cause bit here: once the flow handler is deferred, the FIC keeps
 * driving its output until the irq_work runs, and a level parent SPI re-fires.
 *
 * A level configured group is masked too, because its cause bit re-latches
 * while the input stays asserted; the deferred handle_level_irq() unmasks
 * when the handler is done. An edge configured group is only acked, since
 * handle_edge_irq() never unmasks unconditionally and the mask would stay.
 */
static void al_fic_irq_pre_redirect(struct irq_data *data)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(data);
	struct al_fic *fic = gc->private;

	if (fic->state == AL_FIC_CONFIGURED_LEVEL)
		irq_gc_mask_set_bit(data);

	irq_gc_ack_clr_bit(data);
}
#endif /* CONFIG_SMP */

static int al_fic_register(struct device_node *node, struct al_fic *fic, enum al_fic_id fic_id)
{
	struct irq_chip_generic *gc;
	int ret;

	fic->domain = irq_domain_create_linear(of_fwnode_handle(node), NR_FIC_IRQS,
					       &irq_generic_chip_ops, fic);
	if (!fic->domain) {
		pr_err("fail to add irq domain\n");
		return -ENOMEM;
	}

	/*
	 * Let irq_domain_remove() free the generic chips on either error path
	 * below, instead of calling irq_domain_remove_generic_chips() by hand.
	 */
	fic->domain->flags |= IRQ_DOMAIN_FLAG_DESTROY_GC;

	ret = irq_alloc_domain_generic_chips(fic->domain, NR_FIC_IRQS,
					     1, of_node_full_name(fic->node), handle_level_irq,
					     0, 0, 0);
	if (ret) {
		pr_err("fail to allocate generic chip (%d)\n", ret);
		goto err_domain_remove;
	}

	gc = irq_get_domain_generic_chip(fic->domain, 0);
	gc->reg_base = fic->base;
	gc->chip_types->regs.mask = al_fic_mask_offset[fic_id];
	gc->chip_types->regs.ack = AL_FIC_CAUSE;
	gc->chip_types->chip.irq_mask = irq_gc_mask_set_bit;
	gc->chip_types->chip.irq_unmask = irq_gc_mask_clr_bit;
	gc->chip_types->chip.irq_set_type = al_fic_irq_set_type;
	gc->chip_types->chip.irq_retrigger = al_fic_irq_retrigger;
#ifdef CONFIG_SMP
	/*
	 * The FIC has no hardware affinity control, so give each child real
	 * affinity by redirecting its handler to the target CPU.
	 */
	gc->chip_types->chip.irq_ack = al_fic_ack_noop;
	gc->chip_types->chip.irq_pre_redirect = al_fic_irq_pre_redirect;
	gc->chip_types->chip.irq_set_affinity = irq_chip_redirect_set_affinity;
#else
	gc->chip_types->chip.irq_ack = irq_gc_ack_clr_bit;
#endif
	gc->chip_types->chip.flags = IRQCHIP_SKIP_SET_WAKE;
	gc->private = fic;

	/*
	 * Seed the mask cache with the value al_fic_wire_init() programmed,
	 * rather than having the generic chip read the register back on the
	 * first child mapping: that is later than the parent is requested, and
	 * never happens at all for a group with no consumer in the devicetree.
	 */
	gc->mask_cache = ~0U;

	ret = request_irq(fic->parent_irq, al_fic_irq_handler, IRQF_NO_THREAD | IRQF_SHARED,
			  of_node_full_name(fic->node), fic);
	if (ret) {
		pr_err("fail to request irq (%d)\n", ret);
		goto err_domain_remove;
	}

	return 0;

err_domain_remove:
	irq_domain_remove(fic->domain);
	return ret;
}

/*
 * al_fic_wire_init() - initialize and configure fic in wire mode
 * @node: pointer to the interrupt controller's device tree node
 * @base: mmio to fic register
 * @parent_irq: interrupt of parent
 * @fic_id: which of the controller's outputs (info, error or fatal) this
 *          group drives
 *
 * This API will configure the fic hardware to work in wire mode.
 * In wire mode, fic hardware is generating a wire ("wired") interrupt.
 * Interrupt can be generated based on positive edge or level - configuration is
 * to be determined based on connected hardware to this fic.
 */
static struct al_fic *al_fic_wire_init(struct device_node *node, void __iomem *base,
				       unsigned int parent_irq, enum al_fic_id fic_id)
{
	u32 control, version_id;
	struct al_fic *fic;
	int ret;

	fic = kzalloc_obj(*fic);
	if (!fic)
		return ERR_PTR(-ENOMEM);

	fic->base = base;
	fic->parent_irq = parent_irq;
	fic->node = of_node_get(node);

	control = readl_relaxed(fic->base + AL_FIC_CONTROL);
	version_id = FIELD_GET(CONTROL_VERSION_ID, control);
	if (version_id == AL_FIC_VERSION_V1 && fic_id != AL_FIC_ID_INFO) {
		pr_err("%pOF: amazon,al-fic-mask = \"%s\" not available on FIC v1\n",
		       node, al_fic_mask_name[fic_id]);
		ret = -EINVAL;
		goto err_free;
	}

	/* mask out all interrupts */
	writel_relaxed(~0U, fic->base + AL_FIC_INFO_MASK);
	if (version_id > AL_FIC_VERSION_V1) {
		writel_relaxed(~0U, fic->base + AL_FIC_ERROR_MASK);
		writel_relaxed(~0U, fic->base + AL_FIC_FATAL_MASK);
	}

	/* clear any pending interrupt */
	writel_relaxed(0, fic->base + AL_FIC_CAUSE);

	/* make sure the controller works in non msi_x mode */
	control |= CONTROL_MASK_MSI_X;
	writel_relaxed(control, fic->base + AL_FIC_CONTROL);

	ret = al_fic_register(node, fic, fic_id);
	if (ret) {
		pr_err("fail to register irqchip\n");
		goto err_free;
	}

	pr_info("%pOF initialized successfully (mask=%s parent-irq=%u)\n",
		node, al_fic_mask_name[fic_id], parent_irq);

	return fic;

err_free:
	of_node_put(fic->node);
	kfree(fic);
	return ERR_PTR(ret);
}

/*
 * Parse the amazon,al-fic-mask property into an enum al_fic_id, selecting
 * which of the controller's outputs this group drives. The property is
 * optional; an absent property means the info output.
 */
static int al_fic_parse_mask(struct device_node *node, enum al_fic_id *fic_id)
{
	const char *mask;
	int ret;

	ret = of_property_read_string(node, "amazon,al-fic-mask", &mask);
	if (ret == -EINVAL) {
		*fic_id = AL_FIC_ID_INFO;
		return 0;
	}
	if (ret)
		return ret;

	ret = match_string(al_fic_mask_name, AL_FIC_ID_MAX, mask);
	if (ret < 0)
		return ret;

	*fic_id = ret;
	return 0;
}

static int __init al_fic_init_dt(struct device_node *node, struct device_node *parent)
{
	unsigned int parent_irq;
	enum al_fic_id fic_id;
	struct al_fic *fic;
	void __iomem *base;
	int ret;

	if (!parent) {
		pr_err("%pOF: unsupported - device requires a parent\n", node);
		return -EINVAL;
	}

	ret = al_fic_parse_mask(node, &fic_id);
	if (ret) {
		pr_err("%pOF: invalid amazon,al-fic-mask\n", node);
		return ret;
	}

	base = of_iomap(node, 0);
	if (!base) {
		pr_err("%pOF: fail to map memory\n", node);
		return -ENOMEM;
	}

	parent_irq = irq_of_parse_and_map(node, 0);
	if (!parent_irq) {
		pr_err("%pOF: fail to map irq\n", node);
		ret = -EINVAL;
		goto err_unmap;
	}

	fic = al_fic_wire_init(node, base, parent_irq, fic_id);

	if (IS_ERR(fic)) {
		pr_err("%pOF: fail to initialize irqchip (%pe)\n", node, fic);
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
