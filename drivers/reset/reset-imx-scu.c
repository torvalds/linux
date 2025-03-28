// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2025 NXP
 * Frank Li <Frank.Li@nxp.com>
 */
#include <linux/firmware/imx/svc/misc.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>

#include <dt-bindings/firmware/imx/rsrc.h>

struct imx_scu_reset {
	struct reset_controller_dev rc;
	struct imx_sc_ipc *ipc_handle;
};

static struct imx_scu_reset *to_imx_scu(struct reset_controller_dev *rc)
{
	return container_of(rc, struct imx_scu_reset, rc);
}

struct imx_scu_id_map {
	u32 resource_id;
	u32 command_id;
};

static const struct imx_scu_id_map imx_scu_id_map[] = {
	{ IMX_SC_R_CSI_0, IMX_SC_C_MIPI_RESET },
	{ IMX_SC_R_CSI_1, IMX_SC_C_MIPI_RESET },
};

static int imx_scu_reset_assert(struct reset_controller_dev *rc, unsigned long id)
{
	struct imx_scu_reset *priv = to_imx_scu(rc);

	return imx_sc_misc_set_control(priv->ipc_handle, imx_scu_id_map[id].resource_id,
				       imx_scu_id_map[id].command_id, true);
}

static const struct reset_control_ops imx_scu_reset_ops = {
	.assert = imx_scu_reset_assert,
};

static int imx_scu_xlate(struct reset_controller_dev *rc, const struct of_phandle_args *reset_spec)
{
	int i;

	for (i = 0; i < rc->nr_resets; i++)
		if (reset_spec->args[0] == imx_scu_id_map[i].resource_id)
			return i;

	return -EINVAL;
}

static int imx_scu_reset_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct imx_scu_reset *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, &priv->rc);

	ret = imx_scu_get_handle(&priv->ipc_handle);
	if (ret)
		return dev_err_probe(dev, ret, "sc_misc_MIPI get ipc handle failed!\n");

	priv->rc.ops = &imx_scu_reset_ops;
	priv->rc.owner = THIS_MODULE;
	priv->rc.of_node = dev->of_node;
	priv->rc.of_reset_n_cells = 1;
	priv->rc.of_xlate = imx_scu_xlate;
	priv->rc.nr_resets = ARRAY_SIZE(imx_scu_id_map);

	return devm_reset_controller_register(dev, &priv->rc);
}

static const struct of_device_id imx_scu_reset_ids[] = {
	{ .compatible = "fsl,imx-scu-reset", },
	{}
};
MODULE_DEVICE_TABLE(of, imx_scu_reset_ids);

static struct platform_driver imx_scu_reset_driver = {
	.probe          = imx_scu_reset_probe,
	.driver = {
		.name = "scu-reset",
		.of_match_table = imx_scu_reset_ids,
	},
};
module_platform_driver(imx_scu_reset_driver);

MODULE_AUTHOR("Frank Li <Frank.Li@nxp.com>");
MODULE_DESCRIPTION("i.MX scu reset driver");
MODULE_LICENSE("GPL");
