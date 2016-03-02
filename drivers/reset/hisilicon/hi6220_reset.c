/*
 * Hisilicon Hi6220 reset controller driver
 *
 * Copyright (c) 2015 Hisilicon Limited.
 *
 * Author: Feng Chen <puck.chen@hisilicon.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/of.h>
#include <linux/reset-controller.h>
#include <linux/reset.h>
#include <linux/platform_device.h>

#define ASSERT_OFFSET            0x300
#define DEASSERT_OFFSET          0x304
#define MAX_INDEX                0x509

#define to_reset_data(x) container_of(x, struct hi6220_reset_data, rc_dev)

struct hi6220_reset_data {
	void __iomem			*assert_base;
	void __iomem			*deassert_base;
	struct reset_controller_dev	rc_dev;
};

static int hi6220_reset_assert(struct reset_controller_dev *rc_dev,
			       unsigned long idx)
{
	struct hi6220_reset_data *data = to_reset_data(rc_dev);

	int bank = idx >> 8;
	int offset = idx & 0xff;

	writel(BIT(offset), data->assert_base + (bank * 0x10));

	return 0;
}

static int hi6220_reset_deassert(struct reset_controller_dev *rc_dev,
				 unsigned long idx)
{
	struct hi6220_reset_data *data = to_reset_data(rc_dev);

	int bank = idx >> 8;
	int offset = idx & 0xff;

	writel(BIT(offset), data->deassert_base + (bank * 0x10));

	return 0;
}

static struct reset_control_ops hi6220_reset_ops = {
	.assert = hi6220_reset_assert,
	.deassert = hi6220_reset_deassert,
};

static int hi6220_reset_probe(struct platform_device *pdev)
{
	struct hi6220_reset_data *data;
	struct resource *res;
	void __iomem *src_base;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	src_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(src_base))
		return PTR_ERR(src_base);

	data->assert_base = src_base + ASSERT_OFFSET;
	data->deassert_base = src_base + DEASSERT_OFFSET;
	data->rc_dev.nr_resets = MAX_INDEX;
	data->rc_dev.ops = &hi6220_reset_ops;
	data->rc_dev.of_node = pdev->dev.of_node;

	reset_controller_register(&data->rc_dev);

	return 0;
}

static const struct of_device_id hi6220_reset_match[] = {
	{ .compatible = "hisilicon,hi6220-sysctrl" },
	{ },
};

static struct platform_driver hi6220_reset_driver = {
	.probe = hi6220_reset_probe,
	.driver = {
		.name = "reset-hi6220",
		.of_match_table = hi6220_reset_match,
	},
};

static int __init hi6220_reset_init(void)
{
	return platform_driver_register(&hi6220_reset_driver);
}

postcore_initcall(hi6220_reset_init);
