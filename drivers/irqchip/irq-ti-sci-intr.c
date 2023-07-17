// SPDX-License-Identifier: GPL-2.0
/*
 * Texas Instruments' K3 Interrupt Router irqchip driver
 *
 * Copyright (C) 2018-2019 Texas Instruments Incorporated - https://www.ti.com/
 *	Lokesh Vutla <lokeshvutla@ti.com>
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/soc/ti/ti_sci_protocol.h>

/**
 * struct ti_sci_intr_irq_domain - Structure representing a TISCI based
 *				   Interrupt Router IRQ domain.
 * @sci:	Pointer to TISCI handle
 * @out_irqs:	TISCI resource pointer representing INTR irqs.
 * @dev:	Struct device pointer.
 * @ti_sci_id:	TI-SCI device identifier
 * @type:	Specifies the trigger type supported by this Interrupt Router
 */
struct ti_sci_intr_irq_domain {
	const struct ti_sci_handle *sci;
	struct ti_sci_resource *out_irqs;
	struct device *dev;
	u32 ti_sci_id;
	u32 type;
};

static struct irq_chip ti_sci_intr_irq_chip = {
	.name			= "INTR",
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_set_type		= irq_chip_set_type_parent,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
};

/**
 * ti_sci_intr_irq_domain_translate() - Retrieve hwirq and type from
 *					IRQ firmware specific handler.
 * @domain:	Pointer to IRQ domain
 * @fwspec:	Pointer to IRQ specific firmware structure
 * @hwirq:	IRQ number identified by hardware
 * @type:	IRQ type
 *
 * Return 0 if all went ok else appropriate error.
 */
static int ti_sci_intr_irq_domain_translate(struct irq_domain *domain,
					    struct irq_fwspec *fwspec,
					    unsigned long *hwirq,
					    unsigned int *type)
{
	struct ti_sci_intr_irq_domain *intr = domain->host_data;

	if (fwspec->param_count != 1)
		return -EINVAL;

	*hwirq = fwspec->param[0];
	*type = intr->type;

	return 0;
}

/**
 * ti_sci_intr_xlate_irq() - Translate hwirq to parent's hwirq.
 * @intr:	IRQ domain corresponding to Interrupt Router
 * @irq:	Hardware irq corresponding to the above irq domain
 *
 * Return parent irq number if translation is available else -ENOENT.
 */
static int ti_sci_intr_xlate_irq(struct ti_sci_intr_irq_domain *intr, u32 irq)
{
	struct device_node *np = dev_of_node(intr->dev);
	u32 base, pbase, size, len;
	const __be32 *range;

	range = of_get_property(np, "ti,interrupt-ranges", &len);
	if (!range)
		return irq;

	for (len /= sizeof(*range); len >= 3; len -= 3) {
		base = be32_to_cpu(*range++);
		pbase = be32_to_cpu(*range++);
		size = be32_to_cpu(*range++);

		if (base <= irq && irq < base + size)
			return irq - base + pbase;
	}

	return -ENOENT;
}

/**
 * ti_sci_intr_irq_domain_free() - Free the specified IRQs from the domain.
 * @domain:	Domain to which the irqs belong
 * @virq:	Linux virtual IRQ to be freed.
 * @nr_irqs:	Number of continuous irqs to be freed
 */
static void ti_sci_intr_irq_domain_free(struct irq_domain *domain,
					unsigned int virq, unsigned int nr_irqs)
{
	struct ti_sci_intr_irq_domain *intr = domain->host_data;
	struct irq_data *data;
	int out_irq;

	data = irq_domain_get_irq_data(domain, virq);
	out_irq = (uintptr_t)data->chip_data;

	intr->sci->ops.rm_irq_ops.free_irq(intr->sci,
					   intr->ti_sci_id, data->hwirq,
					   intr->ti_sci_id, out_irq);
	ti_sci_release_resource(intr->out_irqs, out_irq);
	irq_domain_free_irqs_parent(domain, virq, 1);
	irq_domain_reset_irq_data(data);
}

/**
 * ti_sci_intr_alloc_parent_irq() - Allocate parent IRQ
 * @domain:	Pointer to the interrupt router IRQ domain
 * @virq:	Corresponding Linux virtual IRQ number
 * @hwirq:	Corresponding hwirq for the IRQ within this IRQ domain
 *
 * Returns intr output irq if all went well else appropriate error pointer.
 */
static int ti_sci_intr_alloc_parent_irq(struct irq_domain *domain,
					unsigned int virq, u32 hwirq)
{
	struct ti_sci_intr_irq_domain *intr = domain->host_data;
	struct device_node *parent_node;
	struct irq_fwspec fwspec;
	int p_hwirq, err = 0;
	u16 out_irq;

	out_irq = ti_sci_get_free_resource(intr->out_irqs);
	if (out_irq == TI_SCI_RESOURCE_NULL)
		return -EINVAL;

	p_hwirq = ti_sci_intr_xlate_irq(intr, out_irq);
	if (p_hwirq < 0)
		goto err_irqs;

	parent_node = of_irq_find_parent(dev_of_node(intr->dev));
	fwspec.fwnode = of_node_to_fwnode(parent_node);

	if (of_device_is_compatible(parent_node, "arm,gic-v3")) {
		/* Parent is GIC */
		fwspec.param_count = 3;
		fwspec.param[0] = 0;	/* SPI */
		fwspec.param[1] = p_hwirq - 32; /* SPI offset */
		fwspec.param[2] = intr->type;
	} else {
		/* Parent is Interrupt Router */
		fwspec.param_count = 1;
		fwspec.param[0] = p_hwirq;
	}

	err = irq_domain_alloc_irqs_parent(domain, virq, 1, &fwspec);
	if (err)
		goto err_irqs;

	err = intr->sci->ops.rm_irq_ops.set_irq(intr->sci,
						intr->ti_sci_id, hwirq,
						intr->ti_sci_id, out_irq);
	if (err)
		goto err_msg;

	return out_irq;

err_msg:
	irq_domain_free_irqs_parent(domain, virq, 1);
err_irqs:
	ti_sci_release_resource(intr->out_irqs, out_irq);
	return err;
}

/**
 * ti_sci_intr_irq_domain_alloc() - Allocate Interrupt router IRQs
 * @domain:	Point to the interrupt router IRQ domain
 * @virq:	Corresponding Linux virtual IRQ number
 * @nr_irqs:	Continuous irqs to be allocated
 * @data:	Pointer to firmware specifier
 *
 * Return 0 if all went well else appropriate error value.
 */
static int ti_sci_intr_irq_domain_alloc(struct irq_domain *domain,
					unsigned int virq, unsigned int nr_irqs,
					void *data)
{
	struct irq_fwspec *fwspec = data;
	unsigned long hwirq;
	unsigned int flags;
	int err, out_irq;

	err = ti_sci_intr_irq_domain_translate(domain, fwspec, &hwirq, &flags);
	if (err)
		return err;

	out_irq = ti_sci_intr_alloc_parent_irq(domain, virq, hwirq);
	if (out_irq < 0)
		return out_irq;

	irq_domain_set_hwirq_and_chip(domain, virq, hwirq,
				      &ti_sci_intr_irq_chip,
				      (void *)(uintptr_t)out_irq);

	return 0;
}

static const struct irq_domain_ops ti_sci_intr_irq_domain_ops = {
	.free		= ti_sci_intr_irq_domain_free,
	.alloc		= ti_sci_intr_irq_domain_alloc,
	.translate	= ti_sci_intr_irq_domain_translate,
};

static int ti_sci_intr_irq_domain_probe(struct platform_device *pdev)
{
	struct irq_domain *parent_domain, *domain;
	struct ti_sci_intr_irq_domain *intr;
	struct device_node *parent_node;
	struct device *dev = &pdev->dev;
	int ret;

	parent_node = of_irq_find_parent(dev_of_node(dev));
	if (!parent_node) {
		dev_err(dev, "Failed to get IRQ parent node\n");
		return -ENODEV;
	}

	parent_domain = irq_find_host(parent_node);
	of_node_put(parent_node);
	if (!parent_domain) {
		dev_err(dev, "Failed to find IRQ parent domain\n");
		return -ENODEV;
	}

	intr = devm_kzalloc(dev, sizeof(*intr), GFP_KERNEL);
	if (!intr)
		return -ENOMEM;

	intr->dev = dev;
	ret = of_property_read_u32(dev_of_node(dev), "ti,intr-trigger-type",
				   &intr->type);
	if (ret) {
		dev_err(dev, "missing ti,intr-trigger-type property\n");
		return -EINVAL;
	}

	intr->sci = devm_ti_sci_get_by_phandle(dev, "ti,sci");
	if (IS_ERR(intr->sci))
		return dev_err_probe(dev, PTR_ERR(intr->sci),
				     "ti,sci read fail\n");

	ret = of_property_read_u32(dev_of_node(dev), "ti,sci-dev-id",
				   &intr->ti_sci_id);
	if (ret) {
		dev_err(dev, "missing 'ti,sci-dev-id' property\n");
		return -EINVAL;
	}

	intr->out_irqs = devm_ti_sci_get_resource(intr->sci, dev,
						  intr->ti_sci_id,
						  TI_SCI_RESASG_SUBTYPE_IR_OUTPUT);
	if (IS_ERR(intr->out_irqs)) {
		dev_err(dev, "Destination irq resource allocation failed\n");
		return PTR_ERR(intr->out_irqs);
	}

	domain = irq_domain_add_hierarchy(parent_domain, 0, 0, dev_of_node(dev),
					  &ti_sci_intr_irq_domain_ops, intr);
	if (!domain) {
		dev_err(dev, "Failed to allocate IRQ domain\n");
		return -ENOMEM;
	}

	dev_info(dev, "Interrupt Router %d domain created\n", intr->ti_sci_id);

	return 0;
}

static const struct of_device_id ti_sci_intr_irq_domain_of_match[] = {
	{ .compatible = "ti,sci-intr", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ti_sci_intr_irq_domain_of_match);

static struct platform_driver ti_sci_intr_irq_domain_driver = {
	.probe = ti_sci_intr_irq_domain_probe,
	.driver = {
		.name = "ti-sci-intr",
		.of_match_table = ti_sci_intr_irq_domain_of_match,
	},
};
module_platform_driver(ti_sci_intr_irq_domain_driver);

MODULE_AUTHOR("Lokesh Vutla <lokeshvutla@ticom>");
MODULE_DESCRIPTION("K3 Interrupt Router driver over TI SCI protocol");
