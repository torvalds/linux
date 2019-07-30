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

#define ZYNQMP_NR_RESETS (ZYNQMP_PM_RESET_END - ZYNQMP_PM_RESET_START)
#define ZYNQMP_RESET_ID ZYNQMP_PM_RESET_START

struct zynqmp_reset_data {
	struct reset_controller_dev rcdev;
	const struct zynqmp_eemi_ops *eemi_ops;
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

	return priv->eemi_ops->reset_assert(ZYNQMP_RESET_ID + id,
					    PM_RESET_ACTION_ASSERT);
}

static int zynqmp_reset_deassert(struct reset_controller_dev *rcdev,
				 unsigned long id)
{
	struct zynqmp_reset_data *priv = to_zynqmp_reset_data(rcdev);

	return priv->eemi_ops->reset_assert(ZYNQMP_RESET_ID + id,
					    PM_RESET_ACTION_RELEASE);
}

static int zynqmp_reset_status(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	struct zynqmp_reset_data *priv = to_zynqmp_reset_data(rcdev);
	int val, err;

	err = priv->eemi_ops->reset_get_status(ZYNQMP_RESET_ID + id, &val);
	if (err)
		return err;

	return val;
}

static int zynqmp_reset_reset(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	struct zynqmp_reset_data *priv = to_zynqmp_reset_data(rcdev);

	return priv->eemi_ops->reset_assert(ZYNQMP_RESET_ID + id,
					    PM_RESET_ACTION_PULSE);
}

static struct reset_control_ops zynqmp_reset_ops = {
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

	priv->eemi_ops = zynqmp_pm_get_eemi_ops();
	if (IS_ERR(priv->eemi_ops))
		return PTR_ERR(priv->eemi_ops);

	platform_set_drvdata(pdev, priv);

	priv->rcdev.ops = &zynqmp_reset_ops;
	priv->rcdev.owner = THIS_MODULE;
	priv->rcdev.of_node = pdev->dev.of_node;
	priv->rcdev.nr_resets = ZYNQMP_NR_RESETS;

	return devm_reset_controller_register(&pdev->dev, &priv->rcdev);
}

static const struct of_device_id zynqmp_reset_dt_ids[] = {
	{ .compatible = "xlnx,zynqmp-reset", },
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
