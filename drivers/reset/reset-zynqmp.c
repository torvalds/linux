// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Xilinx, Inc.
 *
 */

#include <linux/err.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/of_device.h>

#define ZYNQMP_NR_RESETS (ZYNQMP_PM_RESET_END - ZYNQMP_PM_RESET_START)
#define ZYNQMP_RESET_ID ZYNQMP_PM_RESET_START
#define VERSAL_NR_RESETS	95

struct zynqmp_reset_soc_data {
	u32 reset_id;
	u32 num_resets;
};

struct zynqmp_reset_data {
	struct reset_controller_dev rcdev;
	const struct zynqmp_reset_soc_data *data;
};

static inline struct zynqmp_reset_data *
to_zynqmp_reset_data(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct zynqmp_reset_data, rcdev);
}

static int zynqmp_reset_assert(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	struct zynqmp_reset_data *priv = to_zynqmp_reset_data(rcdev);

	return zynqmp_pm_reset_assert(priv->data->reset_id + id,
				      PM_RESET_ACTION_ASSERT);
}

static int zynqmp_reset_deassert(struct reset_controller_dev *rcdev,
				 unsigned long id)
{
	struct zynqmp_reset_data *priv = to_zynqmp_reset_data(rcdev);

	return zynqmp_pm_reset_assert(priv->data->reset_id + id,
				      PM_RESET_ACTION_RELEASE);
}

static int zynqmp_reset_status(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	struct zynqmp_reset_data *priv = to_zynqmp_reset_data(rcdev);
	int val, err;

	err = zynqmp_pm_reset_get_status(priv->data->reset_id + id, &val);
	if (err)
		return err;

	return val;
}

static int zynqmp_reset_reset(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	struct zynqmp_reset_data *priv = to_zynqmp_reset_data(rcdev);

	return zynqmp_pm_reset_assert(priv->data->reset_id + id,
				      PM_RESET_ACTION_PULSE);
}

static int zynqmp_reset_of_xlate(struct reset_controller_dev *rcdev,
				 const struct of_phandle_args *reset_spec)
{
	return reset_spec->args[0];
}

static const struct zynqmp_reset_soc_data zynqmp_reset_data = {
	.reset_id = ZYNQMP_RESET_ID,
	.num_resets = ZYNQMP_NR_RESETS,
};

static const struct zynqmp_reset_soc_data versal_reset_data = {
	.reset_id = 0,
	.num_resets = VERSAL_NR_RESETS,
};

static const struct reset_control_ops zynqmp_reset_ops = {
	.reset = zynqmp_reset_reset,
	.assert = zynqmp_reset_assert,
	.deassert = zynqmp_reset_deassert,
	.status = zynqmp_reset_status,
};

static int zynqmp_reset_probe(struct platform_device *pdev)
{
	struct zynqmp_reset_data *priv;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->data = of_device_get_match_data(&pdev->dev);
	if (!priv->data)
		return -EINVAL;

	platform_set_drvdata(pdev, priv);

	priv->rcdev.ops = &zynqmp_reset_ops;
	priv->rcdev.owner = THIS_MODULE;
	priv->rcdev.of_node = pdev->dev.of_node;
	priv->rcdev.nr_resets = priv->data->num_resets;
	priv->rcdev.of_reset_n_cells = 1;
	priv->rcdev.of_xlate = zynqmp_reset_of_xlate;

	return devm_reset_controller_register(&pdev->dev, &priv->rcdev);
}

static const struct of_device_id zynqmp_reset_dt_ids[] = {
	{ .compatible = "xlnx,zynqmp-reset", .data = &zynqmp_reset_data, },
	{ .compatible = "xlnx,versal-reset", .data = &versal_reset_data, },
	{ /* sentinel */ },
};

static struct platform_driver zynqmp_reset_driver = {
	.probe	= zynqmp_reset_probe,
	.driver = {
		.name		= KBUILD_MODNAME,
		.of_match_table	= zynqmp_reset_dt_ids,
	},
};

static int __init zynqmp_reset_init(void)
{
	return platform_driver_register(&zynqmp_reset_driver);
}

arch_initcall(zynqmp_reset_init);
