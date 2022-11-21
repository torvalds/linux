// SPDX-License-Identifier: GPL-2.0-only
/*
 * Delta TN48M CPLD reset driver
 *
 * Copyright (C) 2021 Sartura Ltd.
 *
 * Author: Robert Marko <robert.marko@sartura.hr>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

#include <dt-bindings/reset/delta,tn48m-reset.h>

#define TN48M_RESET_REG		0x10

#define TN48M_RESET_TIMEOUT_US	125000
#define TN48M_RESET_SLEEP_US	10

struct tn48_reset_map {
	u8 bit;
};

struct tn48_reset_data {
	struct reset_controller_dev rcdev;
	struct regmap *regmap;
};

static const struct tn48_reset_map tn48m_resets[] = {
	[CPU_88F7040_RESET] = {0},
	[CPU_88F6820_RESET] = {1},
	[MAC_98DX3265_RESET] = {2},
	[PHY_88E1680_RESET] = {4},
	[PHY_88E1512_RESET] = {6},
	[POE_RESET] = {7},
};

static inline struct tn48_reset_data *to_tn48_reset_data(
			struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct tn48_reset_data, rcdev);
}

static int tn48m_control_reset(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	struct tn48_reset_data *data = to_tn48_reset_data(rcdev);
	unsigned int val;

	regmap_update_bits(data->regmap, TN48M_RESET_REG,
			   BIT(tn48m_resets[id].bit), 0);

	return regmap_read_poll_timeout(data->regmap,
					TN48M_RESET_REG,
					val,
					val & BIT(tn48m_resets[id].bit),
					TN48M_RESET_SLEEP_US,
					TN48M_RESET_TIMEOUT_US);
}

static int tn48m_control_status(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	struct tn48_reset_data *data = to_tn48_reset_data(rcdev);
	unsigned int regval;
	int ret;

	ret = regmap_read(data->regmap, TN48M_RESET_REG, &regval);
	if (ret < 0)
		return ret;

	if (BIT(tn48m_resets[id].bit) & regval)
		return 0;
	else
		return 1;
}

static const struct reset_control_ops tn48_reset_ops = {
	.reset		= tn48m_control_reset,
	.status		= tn48m_control_status,
};

static int tn48m_reset_probe(struct platform_device *pdev)
{
	struct tn48_reset_data *data;
	struct regmap *regmap;

	regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!regmap)
		return -ENODEV;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->regmap = regmap;

	data->rcdev.owner = THIS_MODULE;
	data->rcdev.ops = &tn48_reset_ops;
	data->rcdev.nr_resets = ARRAY_SIZE(tn48m_resets);
	data->rcdev.of_node = pdev->dev.of_node;

	return devm_reset_controller_register(&pdev->dev, &data->rcdev);
}

static const struct of_device_id tn48m_reset_of_match[] = {
	{ .compatible = "delta,tn48m-reset" },
	{ }
};
MODULE_DEVICE_TABLE(of, tn48m_reset_of_match);

static struct platform_driver tn48m_reset_driver = {
	.driver = {
		.name = "delta-tn48m-reset",
		.of_match_table = tn48m_reset_of_match,
	},
	.probe = tn48m_reset_probe,
};
module_platform_driver(tn48m_reset_driver);

MODULE_AUTHOR("Robert Marko <robert.marko@sartura.hr>");
MODULE_DESCRIPTION("Delta TN48M CPLD reset driver");
MODULE_LICENSE("GPL");
