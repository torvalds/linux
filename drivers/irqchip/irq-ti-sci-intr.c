// SPDX-License-Identifier: GPL-2.0
/*
 * Texas Instruments' K3 Interrupt Router irqchip driver
 *
 * Copyright (C) 2018-2019 Texas Instruments Incorporated - http://www.ti.com/
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

#define TI_SCI_DEV_ID_MASK	0xffff
#define TI_SCI_DEV_ID_SHIFT	16
#define TI_SCI_IRQ_ID_MASK	0xffff
#define TI_SCI_IRQ_ID_SHIFT	0
#define HWIRQ_TO_DEVID(hwirq)	(((hwirq) >> (TI_SCI_DEV_ID_SHIFT)) & \
				 (TI_SCI_DEV_ID_MASK))
#define HWIRQ_TO_IRQID(hwirq)	((hwirq) & (TI_SCI_IRQ_ID_MASK))
#define TO_HWIRQ(dev, index)	((((dev) & TI_SCI_DEV_ID_MASK) << \
				 TI_SCI_DEV_ID_SHIFT) | \
				((index) & TI_SCI_IRQ_ID_MASK))

/**
 * struct ti_sci_intr_irq_domain - Structure representing a TISCI based
 *				   Interrupt Router IRQ domain.
 * @sci:	Pointer to TISCI handle
 * @dst_irq:	TISCI resource pointer representing GIC irq controller.
 * @dst_id:	TISCI device ID of the GIC irq controller.
 * @type:	Specifies the trigger type supported by this Interrupt Router
 */
struct ti_sci_intr_irq_domain {
	const struct ti_sci_handle *sci;
	struct ti_sci_resource *dst_irq;
	u32 dst_id;
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

	if (fwspec->param_count != 2)
		return -EINVAL;

	*hwirq = TO_HWIRQ(fwspec->param[0], fwspec->param[1]);
	*type = intr->type;

	return 0;
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
	struct irq_data *data, *parent_data;
	u16 dev_id, irq_index;

	parent_data = irq_domain_get_irq_data(domain->parent, virq);
	data = irq_domain_get_irq_data(domain, virq);
	irq_index = HWIRQ_TO_IRQID(data->hwirq);
	dev_id = HWIRQ_TO_DEVID(data->hwirq);

	intr->sci->ops.rm_irq_ops.free_irq(intr->sci, dev_id, irq_index,
					   intr->dst_id, parent_data->hwirq);
	ti_sci_release_resource(intr->dst_irq, parent_data->hwirq);
	irq_domain_free_irqs_parent(domain, virq, 1);
	irq_domain_reset_irq_data(data);
}

/**
 * ti_sci_intr_alloc_gic_irq() - Allocate GIC specific IRQ
 * @domain:	Pointer to the interrupt router IRQ domain
 * @virq:	Corresponding Linux virtual IRQ number
 * @hwirq:	Corresponding hwirq for the IRQ within this IRQ domain
 *
 * Returns 0 if all went well else appropriate error pointer.
 */
static int ti_sci_intr_alloc_gic_irq(struct irq_domain *domain,
				     unsigned int virq, u32 hwirq)
{
	struct ti_sci_intr_irq_domain *intr = domain->host_data;
	struct irq_fwspec fwspec;
	u16 dev_id, irq_index;
	u16 dst_irq;
	int err;

	dev_id = HWIRQ_TO_DEVID(hwirq);
	irq_index = HWIRQ_TO_IRQID(hwirq);

	dst_irq = ti_sci_get_free_resource(intr->dst_irq);
	if (dst_irq == TI_SCI_RESOURCE_NULL)
		return -EINVAL;

	fwspec.fwnode = domain->parent->fwnode;
	fwspec.param_count = 3;
	fwspec.param[0] = 0;	/* SPI */
	fwspec.param[1] = dst_irq - 32; /* SPI offset */
	fwspec.param[2] = intr->type;

	err = irq_domain_alloc_irqs_parent(domain, virq, 1, &fwspec);
	if (err)
		goto err_irqs;

	err = intr->sci->ops.rm_irq_ops.set_irq(intr->sci, dev_id, irq_index,
						intr->dst_id, dst_irq);
	if (err)
		goto err_msg;

	return 0;

err_msg:
	irq_domain_free_irqs_parent(domain, virq, 1);
err_irqs:
	ti_sci_release_resource(intr->dst_irq, dst_irq);
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
	int err;

	err = ti_sci_intr_irq_domain_translate(domain, fwspec, &hwirq, &flags);
	if (err)
		return err;

	err = ti_sci_intr_alloc_gic_irq(domain, virq, hwirq);
	if (err)
		return err;

	irq_domain_set_hwirq_and_chip(domain, virq, hwirq,
				      &ti_sci_intr_irq_chip, NULL);

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
	if (!parent_domain) {
		dev_err(dev, "Failed to find IRQ parent domain\n");
		return -ENODEV;
	}

	intr = devm_kzalloc(dev, sizeof(*intr), GFP_KERNEL);
	if (!intr)
		return -ENOMEM;

	ret = of_property_read_u32(dev_of_node(dev), "ti,intr-trigger-type",
				   &intr->type);
	if (ret) {
		dev_err(dev, "missing ti,intr-trigger-type property\n");
		return -EINVAL;
	}

	intr->sci = devm_ti_sci_get_by_phandle(dev, "ti,sci");
	if (IS_ERR(intr->sci)) {
		ret = PTR_ERR(intr->sci);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "ti,sci read fail %d\n", ret);
		intr->sci = NULL;
		return ret;
	}

	ret = of_property_read_u32(dev_of_node(dev), "ti,sci-dst-id",
				   &intr->dst_id);
	if (ret) {
		dev_err(dev, "missing 'ti,sci-dst-id' property\n");
		return -EINVAL;
	}

	intr->dst_irq = devm_ti_sci_get_of_resource(intr->sci, dev,
						    intr->dst_id,
						    "ti,sci-rm-range-girq");
	if (IS_ERR(intr->dst_irq)) {
		dev_err(dev, "Destination irq resource allocation failed\n");
		return PTR_ERR(intr->dst_irq);
	}

	domain = irq_domain_add_hierarchy(parent_domain, 0, 0, dev_of_node(dev),
					  &ti_sci_intr_irq_domain_ops, intr);
	if (!domain) {
		dev_err(dev, "Failed to allocate IRQ domain\n");
		return -ENOMEM;
	}

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
MODULE_LICENSE("GPL v2");
