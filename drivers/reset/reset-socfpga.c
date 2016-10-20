/*
 * Copyright 2014 Steffen Trumtrar <s.trumtrar@pengutronix.de>
 *
 * based on
 * Allwinner SoCs Reset Controller driver
 *
 * Copyright 2013 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#define NR_BANKS		4

struct socfpga_reset_data {
	spinlock_t			lock;
	void __iomem			*membase;
	struct reset_controller_dev	rcdev;
};

static int socfpga_reset_assert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	struct socfpga_reset_data *data = container_of(rcdev,
						     struct socfpga_reset_data,
						     rcdev);
	int bank = id / BITS_PER_LONG;
	int offset = id % BITS_PER_LONG;
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&data->lock, flags);

	reg = readl(data->membase + (bank * NR_BANKS));
	writel(reg | BIT(offset), data->membase + (bank * NR_BANKS));
	spin_unlock_irqrestore(&data->lock, flags);

	return 0;
}

static int socfpga_reset_deassert(struct reset_controller_dev *rcdev,
				  unsigned long id)
{
	struct socfpga_reset_data *data = container_of(rcdev,
						     struct socfpga_reset_data,
						     rcdev);

	int bank = id / BITS_PER_LONG;
	int offset = id % BITS_PER_LONG;
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&data->lock, flags);

	reg = readl(data->membase + (bank * NR_BANKS));
	writel(reg & ~BIT(offset), data->membase + (bank * NR_BANKS));

	spin_unlock_irqrestore(&data->lock, flags);

	return 0;
}

static int socfpga_reset_status(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	struct socfpga_reset_data *data = container_of(rcdev,
						struct socfpga_reset_data, rcdev);
	int bank = id / BITS_PER_LONG;
	int offset = id % BITS_PER_LONG;
	u32 reg;

	reg = readl(data->membase + (bank * NR_BANKS));

	return !(reg & BIT(offset));
}

static const struct reset_control_ops socfpga_reset_ops = {
	.assert		= socfpga_reset_assert,
	.deassert	= socfpga_reset_deassert,
	.status		= socfpga_reset_status,
};

static int socfpga_reset_probe(struct platform_device *pdev)
{
	struct socfpga_reset_data *data;
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	u32 modrst_offset;

	/*
	 * The binding was mainlined without the required property.
	 * Do not continue, when we encounter an old DT.
	 */
	if (!of_find_property(pdev->dev.of_node, "#reset-cells", NULL)) {
		dev_err(&pdev->dev, "%s missing #reset-cells property\n",
			pdev->dev.of_node->full_name);
		return -EINVAL;
	}

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->membase))
		return PTR_ERR(data->membase);

	if (of_property_read_u32(np, "altr,modrst-offset", &modrst_offset)) {
		dev_warn(dev, "missing altr,modrst-offset property, assuming 0x10!\n");
		modrst_offset = 0x10;
	}
	data->membase += modrst_offset;

	spin_lock_init(&data->lock);

	data->rcdev.owner = THIS_MODULE;
	data->rcdev.nr_resets = NR_BANKS * BITS_PER_LONG;
	data->rcdev.ops = &socfpga_reset_ops;
	data->rcdev.of_node = pdev->dev.of_node;

	return devm_reset_controller_register(dev, &data->rcdev);
}

static const struct of_device_id socfpga_reset_dt_ids[] = {
	{ .compatible = "altr,rst-mgr", },
	{ /* sentinel */ },
};

static struct platform_driver socfpga_reset_driver = {
	.probe	= socfpga_reset_probe,
	.driver = {
		.name		= "socfpga-reset",
		.of_match_table	= socfpga_reset_dt_ids,
	},
};
module_platform_driver(socfpga_reset_driver);

MODULE_AUTHOR("Steffen Trumtrar <s.trumtrar@pengutronix.de");
MODULE_DESCRIPTION("Socfpga Reset Controller Driver");
MODULE_LICENSE("GPL");
