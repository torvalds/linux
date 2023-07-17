// SPDX-License-Identifier: GPL-2.0-only
/*
 * sl28cpld interrupt controller driver
 *
 * Copyright 2020 Kontron Europe GmbH
 */

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>

#define INTC_IE 0x00
#define INTC_IP 0x01

static const struct regmap_irq sl28cpld_irqs[] = {
	REGMAP_IRQ_REG_LINE(0, 8),
	REGMAP_IRQ_REG_LINE(1, 8),
	REGMAP_IRQ_REG_LINE(2, 8),
	REGMAP_IRQ_REG_LINE(3, 8),
	REGMAP_IRQ_REG_LINE(4, 8),
	REGMAP_IRQ_REG_LINE(5, 8),
	REGMAP_IRQ_REG_LINE(6, 8),
	REGMAP_IRQ_REG_LINE(7, 8),
};

struct sl28cpld_intc {
	struct regmap *regmap;
	struct regmap_irq_chip chip;
	struct regmap_irq_chip_data *irq_data;
};

static int sl28cpld_intc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sl28cpld_intc *irqchip;
	int irq;
	u32 base;
	int ret;

	if (!dev->parent)
		return -ENODEV;

	irqchip = devm_kzalloc(dev, sizeof(*irqchip), GFP_KERNEL);
	if (!irqchip)
		return -ENOMEM;

	irqchip->regmap = dev_get_regmap(dev->parent, NULL);
	if (!irqchip->regmap)
		return -ENODEV;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = device_property_read_u32(&pdev->dev, "reg", &base);
	if (ret)
		return -EINVAL;

	irqchip->chip.name = "sl28cpld-intc";
	irqchip->chip.irqs = sl28cpld_irqs;
	irqchip->chip.num_irqs = ARRAY_SIZE(sl28cpld_irqs);
	irqchip->chip.num_regs = 1;
	irqchip->chip.status_base = base + INTC_IP;
	irqchip->chip.unmask_base = base + INTC_IE;
	irqchip->chip.ack_base = base + INTC_IP;

	return devm_regmap_add_irq_chip_fwnode(dev, dev_fwnode(dev),
					       irqchip->regmap, irq,
					       IRQF_SHARED | IRQF_ONESHOT, 0,
					       &irqchip->chip,
					       &irqchip->irq_data);
}

static const struct of_device_id sl28cpld_intc_of_match[] = {
	{ .compatible = "kontron,sl28cpld-intc" },
	{}
};
MODULE_DEVICE_TABLE(of, sl28cpld_intc_of_match);

static struct platform_driver sl28cpld_intc_driver = {
	.probe = sl28cpld_intc_probe,
	.driver = {
		.name = "sl28cpld-intc",
		.of_match_table = sl28cpld_intc_of_match,
	}
};
module_platform_driver(sl28cpld_intc_driver);

MODULE_DESCRIPTION("sl28cpld Interrupt Controller Driver");
MODULE_AUTHOR("Michael Walle <michael@walle.cc>");
