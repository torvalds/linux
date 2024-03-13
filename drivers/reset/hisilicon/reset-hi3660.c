// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016-2017 Linaro Ltd.
 * Copyright (c) 2016-2017 HiSilicon Technologies Co., Ltd.
 */
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

struct hi3660_reset_controller {
	struct reset_controller_dev rst;
	struct regmap *map;
};

#define to_hi3660_reset_controller(_rst) \
	container_of(_rst, struct hi3660_reset_controller, rst)

static int hi3660_reset_program_hw(struct reset_controller_dev *rcdev,
				   unsigned long idx, bool assert)
{
	struct hi3660_reset_controller *rc = to_hi3660_reset_controller(rcdev);
	unsigned int offset = idx >> 8;
	unsigned int mask = BIT(idx & 0x1f);

	if (assert)
		return regmap_write(rc->map, offset, mask);
	else
		return regmap_write(rc->map, offset + 4, mask);
}

static int hi3660_reset_assert(struct reset_controller_dev *rcdev,
			       unsigned long idx)
{
	return hi3660_reset_program_hw(rcdev, idx, true);
}

static int hi3660_reset_deassert(struct reset_controller_dev *rcdev,
				 unsigned long idx)
{
	return hi3660_reset_program_hw(rcdev, idx, false);
}

static int hi3660_reset_dev(struct reset_controller_dev *rcdev,
			    unsigned long idx)
{
	int err;

	err = hi3660_reset_assert(rcdev, idx);
	if (err)
		return err;

	return hi3660_reset_deassert(rcdev, idx);
}

static const struct reset_control_ops hi3660_reset_ops = {
	.reset    = hi3660_reset_dev,
	.assert   = hi3660_reset_assert,
	.deassert = hi3660_reset_deassert,
};

static int hi3660_reset_xlate(struct reset_controller_dev *rcdev,
			      const struct of_phandle_args *reset_spec)
{
	unsigned int offset, bit;

	offset = reset_spec->args[0];
	bit = reset_spec->args[1];

	return (offset << 8) | bit;
}

static int hi3660_reset_probe(struct platform_device *pdev)
{
	struct hi3660_reset_controller *rc;
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;

	rc = devm_kzalloc(dev, sizeof(*rc), GFP_KERNEL);
	if (!rc)
		return -ENOMEM;

	rc->map = syscon_regmap_lookup_by_phandle(np, "hisilicon,rst-syscon");
	if (rc->map == ERR_PTR(-ENODEV)) {
		/* fall back to the deprecated compatible */
		rc->map = syscon_regmap_lookup_by_phandle(np,
							  "hisi,rst-syscon");
	}
	if (IS_ERR(rc->map)) {
		dev_err(dev, "failed to get hisilicon,rst-syscon\n");
		return PTR_ERR(rc->map);
	}

	rc->rst.ops = &hi3660_reset_ops,
	rc->rst.of_node = np;
	rc->rst.of_reset_n_cells = 2;
	rc->rst.of_xlate = hi3660_reset_xlate;

	return reset_controller_register(&rc->rst);
}

static const struct of_device_id hi3660_reset_match[] = {
	{ .compatible = "hisilicon,hi3660-reset", },
	{},
};
MODULE_DEVICE_TABLE(of, hi3660_reset_match);

static struct platform_driver hi3660_reset_driver = {
	.probe = hi3660_reset_probe,
	.driver = {
		.name = "hi3660-reset",
		.of_match_table = hi3660_reset_match,
	},
};

static int __init hi3660_reset_init(void)
{
	return platform_driver_register(&hi3660_reset_driver);
}
arch_initcall(hi3660_reset_init);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:hi3660-reset");
MODULE_DESCRIPTION("HiSilicon Hi3660 Reset Driver");
