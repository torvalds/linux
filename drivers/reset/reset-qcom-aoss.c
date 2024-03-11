// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <dt-bindings/reset/qcom,sdm845-aoss.h>

struct qcom_aoss_reset_map {
	unsigned int reg;
};

struct qcom_aoss_desc {
	const struct qcom_aoss_reset_map *resets;
	size_t num_resets;
};

struct qcom_aoss_reset_data {
	struct reset_controller_dev rcdev;
	void __iomem *base;
	const struct qcom_aoss_desc *desc;
};

static const struct qcom_aoss_reset_map sdm845_aoss_resets[] = {
	[AOSS_CC_MSS_RESTART] = {0x10000},
	[AOSS_CC_CAMSS_RESTART] = {0x11000},
	[AOSS_CC_VENUS_RESTART] = {0x12000},
	[AOSS_CC_GPU_RESTART] = {0x13000},
	[AOSS_CC_DISPSS_RESTART] = {0x14000},
	[AOSS_CC_WCSS_RESTART] = {0x20000},
	[AOSS_CC_LPASS_RESTART] = {0x30000},
};

static const struct qcom_aoss_desc sdm845_aoss_desc = {
	.resets = sdm845_aoss_resets,
	.num_resets = ARRAY_SIZE(sdm845_aoss_resets),
};

static inline struct qcom_aoss_reset_data *to_qcom_aoss_reset_data(
				struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct qcom_aoss_reset_data, rcdev);
}

static int qcom_aoss_control_assert(struct reset_controller_dev *rcdev,
				    unsigned long idx)
{
	struct qcom_aoss_reset_data *data = to_qcom_aoss_reset_data(rcdev);
	const struct qcom_aoss_reset_map *map = &data->desc->resets[idx];

	writel(1, data->base + map->reg);
	/* Wait 6 32kHz sleep cycles for reset */
	usleep_range(200, 300);
	return 0;
}

static int qcom_aoss_control_deassert(struct reset_controller_dev *rcdev,
				      unsigned long idx)
{
	struct qcom_aoss_reset_data *data = to_qcom_aoss_reset_data(rcdev);
	const struct qcom_aoss_reset_map *map = &data->desc->resets[idx];

	writel(0, data->base + map->reg);
	/* Wait 6 32kHz sleep cycles for reset */
	usleep_range(200, 300);
	return 0;
}

static int qcom_aoss_control_reset(struct reset_controller_dev *rcdev,
					unsigned long idx)
{
	qcom_aoss_control_assert(rcdev, idx);

	return qcom_aoss_control_deassert(rcdev, idx);
}

static const struct reset_control_ops qcom_aoss_reset_ops = {
	.reset = qcom_aoss_control_reset,
	.assert = qcom_aoss_control_assert,
	.deassert = qcom_aoss_control_deassert,
};

static int qcom_aoss_reset_probe(struct platform_device *pdev)
{
	struct qcom_aoss_reset_data *data;
	struct device *dev = &pdev->dev;
	const struct qcom_aoss_desc *desc;

	desc = of_device_get_match_data(dev);
	if (!desc)
		return -EINVAL;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->desc = desc;
	data->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(data->base))
		return PTR_ERR(data->base);

	data->rcdev.owner = THIS_MODULE;
	data->rcdev.ops = &qcom_aoss_reset_ops;
	data->rcdev.nr_resets = desc->num_resets;
	data->rcdev.of_node = dev->of_node;

	return devm_reset_controller_register(dev, &data->rcdev);
}

static const struct of_device_id qcom_aoss_reset_of_match[] = {
	{ .compatible = "qcom,sdm845-aoss-cc", .data = &sdm845_aoss_desc },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_aoss_reset_of_match);

static struct platform_driver qcom_aoss_reset_driver = {
	.probe = qcom_aoss_reset_probe,
	.driver  = {
		.name = "qcom_aoss_reset",
		.of_match_table = qcom_aoss_reset_of_match,
	},
};

module_platform_driver(qcom_aoss_reset_driver);

MODULE_DESCRIPTION("Qualcomm AOSS Reset Driver");
MODULE_LICENSE("GPL v2");
