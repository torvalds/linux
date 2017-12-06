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
#include <linux/init.h>
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
	int reg_width = sizeof(u32);
	int bank = id / (reg_width * BITS_PER_BYTE);
	int offset = id % (reg_width * BITS_PER_BYTE);
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&data->lock, flags);

	reg = readl(data->membase + (bank * reg_width));
	writel(reg & ~BIT(offset), data->membase + (bank * reg_width));

	spin_unlock_irqrestore(&data->lock, flags);

	return 0;
}

static int sunxi_reset_deassert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	struct sunxi_reset_data *data = container_of(rcdev,
						     struct sunxi_reset_data,
						     rcdev);
	int reg_width = sizeof(u32);
	int bank = id / (reg_width * BITS_PER_BYTE);
	int offset = id % (reg_width * BITS_PER_BYTE);
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&data->lock, flags);

	reg = readl(data->membase + (bank * reg_width));
	writel(reg | BIT(offset), data->membase + (bank * reg_width));

	spin_unlock_irqrestore(&data->lock, flags);

	return 0;
}

static const struct reset_control_ops sunxi_reset_ops = {
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

	spin_lock_init(&data->lock);

	data->rcdev.owner = THIS_MODULE;
	data->rcdev.nr_resets = size * 8;
	data->rcdev.ops = &sunxi_reset_ops;
	data->rcdev.of_node = np;

	return reset_controller_register(&data->rcdev);

err_alloc:
	kfree(data);
	return ret;
};

/*
 * These are the reset controller we need to initialize early on in
 * our system, before we can even think of using a regular device
 * driver for it.
 */
static const struct of_device_id sunxi_early_reset_dt_ids[] __initconst = {
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

	spin_lock_init(&data->lock);

	data->rcdev.owner = THIS_MODULE;
	data->rcdev.nr_resets = resource_size(res) * 8;
	data->rcdev.ops = &sunxi_reset_ops;
	data->rcdev.of_node = pdev->dev.of_node;

	return devm_reset_controller_register(&pdev->dev, &data->rcdev);
}

static struct platform_driver sunxi_reset_driver = {
	.probe	= sunxi_reset_probe,
	.driver = {
		.name		= "sunxi-reset",
		.of_match_table	= sunxi_reset_dt_ids,
	},
};
builtin_platform_driver(sunxi_reset_driver);
