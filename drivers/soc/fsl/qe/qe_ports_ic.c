// SPDX-License-Identifier: GPL-2.0
/*
 * QUICC ENGINE I/O Ports Interrupt Controller
 *
 * Copyright (c) 2025 Christophe Leroy CS GROUP France (christophe.leroy@csgroup.eu)
 */

#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/platform_device.h>

/* QE IC registers offset */
#define CEPIER		0x0c
#define CEPIMR		0x10
#define CEPICR		0x14

struct qepic_data {
	void __iomem *reg;
	struct irq_domain *host;
};

static void qepic_mask(struct irq_data *d)
{
	struct qepic_data *data = irq_data_get_irq_chip_data(d);

	clrbits32(data->reg + CEPIMR, 1 << (31 - irqd_to_hwirq(d)));
}

static void qepic_unmask(struct irq_data *d)
{
	struct qepic_data *data = irq_data_get_irq_chip_data(d);

	setbits32(data->reg + CEPIMR, 1 << (31 - irqd_to_hwirq(d)));
}

static void qepic_end(struct irq_data *d)
{
	struct qepic_data *data = irq_data_get_irq_chip_data(d);

	out_be32(data->reg + CEPIER, 1 << (31 - irqd_to_hwirq(d)));
}

static int qepic_set_type(struct irq_data *d, unsigned int flow_type)
{
	struct qepic_data *data = irq_data_get_irq_chip_data(d);
	unsigned int vec = (unsigned int)irqd_to_hwirq(d);

	switch (flow_type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_FALLING:
		setbits32(data->reg + CEPICR, 1 << (31 - vec));
		return 0;
	case IRQ_TYPE_EDGE_BOTH:
	case IRQ_TYPE_NONE:
		clrbits32(data->reg + CEPICR, 1 << (31 - vec));
		return 0;
	}
	return -EINVAL;
}

static struct irq_chip qepic = {
	.name = "QEPIC",
	.irq_mask = qepic_mask,
	.irq_unmask = qepic_unmask,
	.irq_eoi = qepic_end,
	.irq_set_type = qepic_set_type,
};

static int qepic_get_irq(struct irq_desc *desc)
{
	struct qepic_data *data = irq_desc_get_handler_data(desc);
	u32 event = in_be32(data->reg + CEPIER);

	if (!event)
		return -1;

	return irq_find_mapping(data->host, 32 - ffs(event));
}

static void qepic_cascade(struct irq_desc *desc)
{
	generic_handle_irq(qepic_get_irq(desc));
}

static int qepic_host_map(struct irq_domain *h, unsigned int virq, irq_hw_number_t hw)
{
	irq_set_chip_data(virq, h->host_data);
	irq_set_chip_and_handler(virq, &qepic, handle_fasteoi_irq);
	return 0;
}

static const struct irq_domain_ops qepic_host_ops = {
	.map = qepic_host_map,
};

static int qepic_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct qepic_data *data;
	int irq;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(data->reg))
		return PTR_ERR(data->reg);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	data->host = irq_domain_add_linear(dev->of_node, 32, &qepic_host_ops, data);
	if (!data->host)
		return -ENODEV;

	irq_set_chained_handler_and_data(irq, qepic_cascade, data);

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
