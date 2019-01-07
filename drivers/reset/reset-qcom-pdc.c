// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

#include <dt-bindings/reset/qcom,sdm845-pdc.h>

#define RPMH_PDC_SYNC_RESET	0x100

struct qcom_pdc_reset_map {
	u8 bit;
};

struct qcom_pdc_reset_data {
	struct reset_controller_dev rcdev;
	struct regmap *regmap;
};

static const struct regmap_config sdm845_pdc_regmap_config = {
	.name		= "pdc-reset",
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x20000,
	.fast_io	= true,
};

static const struct qcom_pdc_reset_map sdm845_pdc_resets[] = {
	[PDC_APPS_SYNC_RESET] = {0},
	[PDC_SP_SYNC_RESET] = {1},
	[PDC_AUDIO_SYNC_RESET] = {2},
	[PDC_SENSORS_SYNC_RESET] = {3},
	[PDC_AOP_SYNC_RESET] = {4},
	[PDC_DEBUG_SYNC_RESET] = {5},
	[PDC_GPU_SYNC_RESET] = {6},
	[PDC_DISPLAY_SYNC_RESET] = {7},
	[PDC_COMPUTE_SYNC_RESET] = {8},
	[PDC_MODEM_SYNC_RESET] = {9},
};

static inline struct qcom_pdc_reset_data *to_qcom_pdc_reset_data(
				struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct qcom_pdc_reset_data, rcdev);
}

static int qcom_pdc_control_assert(struct reset_controller_dev *rcdev,
					unsigned long idx)
{
	struct qcom_pdc_reset_data *data = to_qcom_pdc_reset_data(rcdev);

	return regmap_update_bits(data->regmap, RPMH_PDC_SYNC_RESET,
				  BIT(sdm845_pdc_resets[idx].bit),
				  BIT(sdm845_pdc_resets[idx].bit));
}

static int qcom_pdc_control_deassert(struct reset_controller_dev *rcdev,
					unsigned long idx)
{
	struct qcom_pdc_reset_data *data = to_qcom_pdc_reset_data(rcdev);

	return regmap_update_bits(data->regmap, RPMH_PDC_SYNC_RESET,
				  BIT(sdm845_pdc_resets[idx].bit), 0);
}

static const struct reset_control_ops qcom_pdc_reset_ops = {
	.assert = qcom_pdc_control_assert,
	.deassert = qcom_pdc_control_deassert,
};

static int qcom_pdc_reset_probe(struct platform_device *pdev)
{
	struct qcom_pdc_reset_data *data;
	struct device *dev = &pdev->dev;
	void __iomem *base;
	struct resource *res;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	data->regmap = devm_regmap_init_mmio(dev, base,
					     &sdm845_pdc_regmap_config);
	if (IS_ERR(data->regmap)) {
		dev_err(dev, "Unable to initialize regmap\n");
		return PTR_ERR(data->regmap);
	}

	data->rcdev.owner = THIS_MODULE;
	data->rcdev.ops = &qcom_pdc_reset_ops;
	data->rcdev.nr_resets = ARRAY_SIZE(sdm845_pdc_resets);
	data->rcdev.of_node = dev->of_node;

	return devm_reset_controller_register(dev, &data->rcdev);
}

static const struct of_device_id qcom_pdc_reset_of_match[] = {
	{ .compatible = "qcom,sdm845-pdc-global" },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_pdc_reset_of_match);

static struct platform_driver qcom_pdc_reset_driver = {
	.probe = qcom_pdc_reset_probe,
	.driver = {
		.name = "qcom_pdc_reset",
		.of_match_table = qcom_pdc_reset_of_match,
	},
};
module_platform_driver(qcom_pdc_reset_driver);

MODULE_DESCRIPTION("Qualcomm PDC Reset Driver");
MODULE_LICENSE("GPL v2");
