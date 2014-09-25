/*
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
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

struct sunxi_reset_data {
	spinlock_t			lock;
	void __iomem			*membase;
	struct reset_controller_dev	rcdev;
};

static int sunxi_reset_assert(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	struct sunxi_reset_data *data = container_of(rcdev,
						     struct sunxi_reset_data,
						     rcdev);
	int bank = id / BITS_PER_LONG;
	int offset = id % BITS_PER_LONG;
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&data->lock, flags);

	reg = readl(data->membase + (bank * 4));
	writel(reg & ~BIT(offset), data->membase + (bank * 4));

	spin_unlock_irqrestore(&data->lock, flags);

	return 0;
}

static int sunxi_reset_deassert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	struct sunxi_reset_data *data = container_of(rcdev,
						     struct sunxi_reset_data,
						     rcdev);
	int bank = id / BITS_PER_LONG;
	int offset = id % BITS_PER_LONG;
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&data->lock, flags);

	reg = readl(data->membase + (bank * 4));
	writel(reg | BIT(offset), data->membase + (bank * 4));

	spin_unlock_irqrestore(&data->lock, flags);

	return 0;
}

static struct reset_control_ops sunxi_reset_ops = {
	.assert		= sunxi_reset_assert,
	.deassert	= sunxi_reset_deassert,
};

static int sunxi_reset_init(struct device_node *np)
{
	struct sunxi_reset_data *data;
	struct resource res;
	resource_size_t size;
	int ret;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = of_address_to_resource(np, 0, &res);
	if (ret)
		goto err_alloc;

	size = resource_size(&res);
	if (!request_mem_region(res.start, size, np->name)) {
		ret = -EBUSY;
		goto err_alloc;
	}

	data->membase = ioremap(res.start, size);
	if (!data->membase) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	data->rcdev.owner = THIS_MODULE;
	data->rcdev.nr_resets = size * 32;
	data->rcdev.ops = &sunxi_reset_ops;
	data->rcdev.of_node = np;
	reset_controller_register(&data->rcdev);

	return 0;

err_alloc:
	kfree(data);
	return ret;
};

/*
 * These are the reset controller we need to initialize early on in
 * our system, before we can even think of using a regular device
 * driver for it.
 */
static const struct of_device_id sunxi_early_reset_dt_ids[] __initdata = {
	{ .compatible = "allwinner,sun6i-a31-ahb1-reset", },
	{ /* sentinel */ },
};

void __init sun6i_reset_init(void)
{
	struct device_node *np;

	for_each_matching_node(np, sunxi_early_reset_dt_ids)
		sunxi_reset_init(np);
}

/*
 * And these are the controllers we can register through the regular
 * device model.
 */
static const struct of_device_id sunxi_reset_dt_ids[] = {
	 { .compatible = "allwinner,sun6i-a31-clock-reset", },
	 { /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, sunxi_reset_dt_ids);

static int sunxi_reset_probe(struct platform_device *pdev)
{
	struct sunxi_reset_data *data;
	struct resource *res;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->membase))
		return PTR_ERR(data->membase);

	data->rcdev.owner = THIS_MODULE;
	data->rcdev.nr_resets = resource_size(res) * 32;
	data->rcdev.ops = &sunxi_reset_ops;
	data->rcdev.of_node = pdev->dev.of_node;

	return reset_controller_register(&data->rcdev);
}

static int sunxi_reset_remove(struct platform_device *pdev)
{
	struct sunxi_reset_data *data = platform_get_drvdata(pdev);

	reset_controller_unregister(&data->rcdev);

	return 0;
}

static struct platform_driver sunxi_reset_driver = {
	.probe	= sunxi_reset_probe,
	.remove	= sunxi_reset_remove,
	.driver = {
		.name		= "sunxi-reset",
		.owner		= THIS_MODULE,
		.of_match_table	= sunxi_reset_dt_ids,
	},
};
module_platform_driver(sunxi_reset_driver);

MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com");
MODULE_DESCRIPTION("Allwinner SoCs Reset Controller Driver");
MODULE_LICENSE("GPL");
