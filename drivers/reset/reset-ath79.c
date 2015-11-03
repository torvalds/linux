/*
 * Copyright (C) 2015 Alban Bedel <albeu@free.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>

struct ath79_reset {
	struct reset_controller_dev rcdev;
	void __iomem *base;
	spinlock_t lock;
};

static int ath79_reset_update(struct reset_controller_dev *rcdev,
			unsigned long id, bool assert)
{
	struct ath79_reset *ath79_reset =
		container_of(rcdev, struct ath79_reset, rcdev);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&ath79_reset->lock, flags);
	val = readl(ath79_reset->base);
	if (assert)
		val |= BIT(id);
	else
		val &= ~BIT(id);
	writel(val, ath79_reset->base);
	spin_unlock_irqrestore(&ath79_reset->lock, flags);

	return 0;
}

static int ath79_reset_assert(struct reset_controller_dev *rcdev,
			unsigned long id)
{
	return ath79_reset_update(rcdev, id, true);
}

static int ath79_reset_deassert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	return ath79_reset_update(rcdev, id, false);
}

static int ath79_reset_status(struct reset_controller_dev *rcdev,
			unsigned long id)
{
	struct ath79_reset *ath79_reset =
		container_of(rcdev, struct ath79_reset, rcdev);
	u32 val;

	val = readl(ath79_reset->base);

	return !!(val & BIT(id));
}

static struct reset_control_ops ath79_reset_ops = {
	.assert = ath79_reset_assert,
	.deassert = ath79_reset_deassert,
	.status = ath79_reset_status,
};

static int ath79_reset_probe(struct platform_device *pdev)
{
	struct ath79_reset *ath79_reset;
	struct resource *res;

	ath79_reset = devm_kzalloc(&pdev->dev,
				sizeof(*ath79_reset), GFP_KERNEL);
	if (!ath79_reset)
		return -ENOMEM;

	platform_set_drvdata(pdev, ath79_reset);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ath79_reset->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ath79_reset->base))
		return PTR_ERR(ath79_reset->base);

	spin_lock_init(&ath79_reset->lock);
	ath79_reset->rcdev.ops = &ath79_reset_ops;
	ath79_reset->rcdev.owner = THIS_MODULE;
	ath79_reset->rcdev.of_node = pdev->dev.of_node;
	ath79_reset->rcdev.of_reset_n_cells = 1;
	ath79_reset->rcdev.nr_resets = 32;

	return reset_controller_register(&ath79_reset->rcdev);
}

static int ath79_reset_remove(struct platform_device *pdev)
{
	struct ath79_reset *ath79_reset = platform_get_drvdata(pdev);

	reset_controller_unregister(&ath79_reset->rcdev);

	return 0;
}

static const struct of_device_id ath79_reset_dt_ids[] = {
	{ .compatible = "qca,ar7100-reset", },
	{ },
};
MODULE_DEVICE_TABLE(of, ath79_reset_dt_ids);

static struct platform_driver ath79_reset_driver = {
	.probe	= ath79_reset_probe,
	.remove = ath79_reset_remove,
	.driver = {
		.name		= "ath79-reset",
		.of_match_table	= ath79_reset_dt_ids,
	},
};
module_platform_driver(ath79_reset_driver);

MODULE_AUTHOR("Alban Bedel <albeu@free.fr>");
MODULE_DESCRIPTION("AR71xx Reset Controller Driver");
MODULE_LICENSE("GPL");
