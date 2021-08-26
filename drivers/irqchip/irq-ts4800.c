/*
 * Multiplexed-IRQs driver for TS-4800's FPGA
 *
 * Copyright (c) 2015 - Savoir-faire Linux
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

#define IRQ_MASK        0x4
#define IRQ_STATUS      0x8

struct ts4800_irq_data {
	void __iomem            *base;
	struct irq_domain       *domain;
	struct irq_chip         irq_chip;
};

static void ts4800_irq_mask(struct irq_data *d)
{
	struct ts4800_irq_data *data = irq_data_get_irq_chip_data(d);
	u16 reg = readw(data->base + IRQ_MASK);
	u16 mask = 1 << d->hwirq;

	writew(reg | mask, data->base + IRQ_MASK);
}

static void ts4800_irq_unmask(struct irq_data *d)
{
	struct ts4800_irq_data *data = irq_data_get_irq_chip_data(d);
	u16 reg = readw(data->base + IRQ_MASK);
	u16 mask = 1 << d->hwirq;

	writew(reg & ~mask, data->base + IRQ_MASK);
}

static int ts4800_irqdomain_map(struct irq_domain *d, unsigned int irq,
				irq_hw_number_t hwirq)
{
	struct ts4800_irq_data *data = d->host_data;

	irq_set_chip_and_handler(irq, &data->irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, data);
	irq_set_noprobe(irq);

	return 0;
}

static const struct irq_domain_ops ts4800_ic_ops = {
	.map = ts4800_irqdomain_map,
	.xlate = irq_domain_xlate_onecell,
};

static void ts4800_ic_chained_handle_irq(struct irq_desc *desc)
{
	struct ts4800_irq_data *data = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	u16 status = readw(data->base + IRQ_STATUS);

	chained_irq_enter(chip, desc);

	if (unlikely(status == 0)) {
		handle_bad_irq(desc);
		goto out;
	}

	do {
		unsigned int bit = __ffs(status);

		generic_handle_domain_irq(data->domain, bit);
		status &= ~(1 << bit);
	} while (status);

out:
	chained_irq_exit(chip, desc);
}

static int ts4800_ic_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct ts4800_irq_data *data;
	struct irq_chip *irq_chip;
	struct resource *res;
	int parent_irq;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->base))
		return PTR_ERR(data->base);

	writew(0xFFFF, data->base + IRQ_MASK);

	parent_irq = irq_of_parse_and_map(node, 0);
	if (!parent_irq) {
		dev_err(&pdev->dev, "failed to get parent IRQ\n");
		return -EINVAL;
	}

	irq_chip = &data->irq_chip;
	irq_chip->name = dev_name(&pdev->dev);
	irq_chip->irq_mask = ts4800_irq_mask;
	irq_chip->irq_unmask = ts4800_irq_unmask;

	data->domain = irq_domain_add_linear(node, 8, &ts4800_ic_ops, data);
	if (!data->domain) {
		dev_err(&pdev->dev, "cannot add IRQ domain\n");
		return -ENOMEM;
	}

	irq_set_chained_handler_and_data(parent_irq,
					 ts4800_ic_chained_handle_irq, data);

	platform_set_drvdata(pdev, data);

	return 0;
}

static int ts4800_ic_remove(struct platform_device *pdev)
{
	struct ts4800_irq_data *data = platform_get_drvdata(pdev);

	irq_domain_remove(data->domain);

	return 0;
}

static const struct of_device_id ts4800_ic_of_match[] = {
	{ .compatible = "technologic,ts4800-irqc", },
	{},
};
MODULE_DEVICE_TABLE(of, ts4800_ic_of_match);

static struct platform_driver ts4800_ic_driver = {
	.probe  = ts4800_ic_probe,
	.remove = ts4800_ic_remove,
	.driver = {
		.name = "ts4800-irqc",
		.of_match_table = ts4800_ic_of_match,
	},
};
module_platform_driver(ts4800_ic_driver);

MODULE_AUTHOR("Damien Riegel <damien.riegel@savoirfairelinux.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:ts4800_irqc");
