// SPDX-License-Identifier: GPL-2.0
/*
 * Sophgo SG2044 multi-function system controller driver
 *
 * Copyright (C) 2025 Inochi Amaoto <inochiama@gmail.com>
 */

#include <linux/mfd/core.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/resource.h>

static const struct mfd_cell sg2044_topsys_subdev[] = {
	{
		.name = "sg2044-pll",
	},
};

static int sg2044_topsys_probe(struct platform_device *pdev)
{
	return devm_mfd_add_devices(&pdev->dev, PLATFORM_DEVID_AUTO,
				    sg2044_topsys_subdev,
				    ARRAY_SIZE(sg2044_topsys_subdev),
				    NULL, 0, NULL);
}

static const struct of_device_id sg2044_topsys_of_match[] = {
	{ .compatible = "sophgo,sg2044-top-syscon" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sg2044_topsys_of_match);

static struct platform_driver sg2044_topsys_driver = {
	.probe = sg2044_topsys_probe,
	.driver = {
		.name = "sg2044-topsys",
		.of_match_table = sg2044_topsys_of_match,
	},
};
module_platform_driver(sg2044_topsys_driver);

MODULE_AUTHOR("Inochi Amaoto <inochiama@gmail.com>");
MODULE_DESCRIPTION("Sophgo SG2044 multi-function system controller driver");
MODULE_LICENSE("GPL");
