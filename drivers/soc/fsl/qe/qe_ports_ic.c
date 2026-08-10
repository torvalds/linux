// SPDX-License-Identifier: GPL-2.0
/*
 * QUICC ENGINE I/O Ports Interrupt Controller
 *
 * Copyright (c) 2025 Christophe Leroy CS GROUP France (christophe.leroy@csgroup.eu)
 */

#include <linux/bitops.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/platform_device.h>

/* QE IC registers offset */
#define CEPIER		0x0c
#define CEPIMR		0x10
#define CEPICR		0x14

struct qepic_data {
	void __iomem *reg;
	int parent_irq;
};

static void qepic_mask(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	u32 val;

	guard(raw_spinlock)(&gc->lock);

	val = ioread32be(gc->reg_base + CEPIMR);
	iowrite32be(val & ~d->mask, gc->reg_base + CEPIMR);
}

static void qepic_unmask(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	u32 val;

	guard(raw_spinlock)(&gc->lock);

	val = ioread32be(gc->reg_base + CEPIMR);
	iowrite32be(val | d->mask, gc->reg_base + CEPIMR);
}

static void qepic_end(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);

	iowrite32be(d->mask, gc->reg_base + CEPIER);
}

static void qepic_calc_mask(struct irq_data *d)
{
	d->mask = 1 << (31 - irqd_to_hwirq(d));
}

static int qepic_set_type(struct irq_data *d, unsigned int flow_type)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	u32 val;

	guard(raw_spinlock)(&gc->lock);

	val = ioread32be(gc->reg_base + CEPICR);
	switch (flow_type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_FALLING:
		iowrite32be(val | d->mask, gc->reg_base + CEPICR);
		return 0;
	case IRQ_TYPE_EDGE_BOTH:
	case IRQ_TYPE_NONE:
		iowrite32be(val & ~d->mask, gc->reg_base + CEPICR);
		return 0;
	}
	return -EINVAL;
}

static void qepic_cascade(struct irq_desc *desc)
{
	struct irq_domain *domain = irq_desc_get_handler_data(desc);
	struct irq_chip_generic *gc = irq_get_domain_generic_chip(domain, 0);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned long event, bit;

	chained_irq_enter(chip, desc);

	event = ioread32be(gc->reg_base + CEPIER);
	if (!event) {
		handle_bad_irq(desc);
		goto out;
	}

	for_each_set_bit(bit, &event, 32)
		generic_handle_domain_irq(domain, 31 - bit);

out:
	chained_irq_exit(chip, desc);
}

static int qepic_chip_init(struct irq_chip_generic *gc)
{
	struct qepic_data *data = gc->domain->host_data;
	struct irq_chip_type *ct = gc->chip_types;

	gc->reg_base = data->reg;

	ct->chip.irq_mask = qepic_mask;
	ct->chip.irq_unmask = qepic_unmask;
	ct->chip.irq_eoi = qepic_end;
	ct->chip.irq_set_type = qepic_set_type;
	ct->chip.irq_calc_mask = qepic_calc_mask;

	return 0;
}

static int qepic_domain_init(struct irq_domain *d)
{
	struct qepic_data *data = d->host_data;

	irq_set_chained_handler_and_data(data->parent_irq, qepic_cascade, d);

	return 0;
}

static void qepic_domain_exit(struct irq_domain *d)
{
	struct qepic_data *data = d->host_data;

	irq_set_chained_handler_and_data(data->parent_irq, NULL, NULL);
}

static int qepic_probe(struct platform_device *pdev)
{
	struct irq_domain_chip_generic_info dgc_info = {
		.name = "QEPIC",
		.handler = handle_fasteoi_irq,
		.irqs_per_chip = 32,
		.num_ct = 1,
		.init = qepic_chip_init,
	};
	struct irq_domain_info d_info = {
		.fwnode = of_fwnode_handle(pdev->dev.of_node),
		.domain_flags = IRQ_DOMAIN_FLAG_DESTROY_GC,
		.size = 32,
		.hwirq_max = 32,
		.ops = &irq_generic_chip_ops,
		.dgc_info = &dgc_info,
		.init = qepic_domain_init,
		.exit = qepic_domain_exit,
	};
	struct device *dev = &pdev->dev;
	struct irq_domain *domain;
	struct qepic_data *data;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	d_info.host_data = data;

	data->reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(data->reg))
		return PTR_ERR(data->reg);

	data->parent_irq = platform_get_irq(pdev, 0);
	if (data->parent_irq < 0)
		return data->parent_irq;

	domain = devm_irq_domain_instantiate(dev, &d_info);
	if (IS_ERR(domain))
		return PTR_ERR(domain);

	return 0;
}

static const struct of_device_id qepic_match[] = {
	{
		.compatible = "fsl,mpc8323-qe-ports-ic",
	},
	{},
};

static struct platform_driver qepic_driver = {
	.driver	= {
		.name		= "qe_ports_ic",
		.of_match_table	= qepic_match,
	},
	.probe	= qepic_probe,
};

static int __init qepic_init(void)
{
	return platform_driver_register(&qepic_driver);
}
arch_initcall(qepic_init);
