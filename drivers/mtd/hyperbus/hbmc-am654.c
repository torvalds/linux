// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2019 Texas Instruments Incorporated - https://www.ti.com/
// Author: Vignesh Raghavendra <vigneshr@ti.com>

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mtd/cfi.h>
#include <linux/mtd/hyperbus.h>
#include <linux/mtd/mtd.h>
#include <linux/mux/consumer.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#define AM654_HBMC_CALIB_COUNT 25

struct am654_hbmc_priv {
	struct hyperbus_ctlr ctlr;
	struct hyperbus_device hbdev;
	struct mux_control *mux_ctrl;
};

static int am654_hbmc_calibrate(struct hyperbus_device *hbdev)
{
	struct map_info *map = &hbdev->map;
	struct cfi_private cfi;
	int count = AM654_HBMC_CALIB_COUNT;
	int pass_count = 0;
	int ret;

	cfi.interleave = 1;
	cfi.device_type = CFI_DEVICETYPE_X16;
	cfi_send_gen_cmd(0xF0, 0, 0, map, &cfi, cfi.device_type, NULL);
	cfi_send_gen_cmd(0x98, 0x55, 0, map, &cfi, cfi.device_type, NULL);

	while (count--) {
		ret = cfi_qry_present(map, 0, &cfi);
		if (ret)
			pass_count++;
		else
			pass_count = 0;
		if (pass_count == 5)
			break;
	}

	cfi_qry_mode_off(0, map, &cfi);

	return ret;
}

static const struct hyperbus_ops am654_hbmc_ops = {
	.calibrate = am654_hbmc_calibrate,
};

static int am654_hbmc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct am654_hbmc_priv *priv;
	struct resource res;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	priv->hbdev.np = of_get_next_child(np, NULL);
	ret = of_address_to_resource(priv->hbdev.np, 0, &res);
	if (ret)
		return ret;

	if (of_property_read_bool(dev->of_node, "mux-controls")) {
		struct mux_control *control = devm_mux_control_get(dev, NULL);

		if (IS_ERR(control))
			return PTR_ERR(control);

		ret = mux_control_select(control, 1);
		if (ret) {
			dev_err(dev, "Failed to select HBMC mux\n");
			return ret;
		}
		priv->mux_ctrl = control;
	}

	priv->hbdev.map.size = resource_size(&res);
	priv->hbdev.map.virt = devm_ioremap_resource(dev, &res);
	if (IS_ERR(priv->hbdev.map.virt))
		return PTR_ERR(priv->hbdev.map.virt);

	priv->ctlr.dev = dev;
	priv->ctlr.ops = &am654_hbmc_ops;
	priv->hbdev.ctlr = &priv->ctlr;
	ret = hyperbus_register_device(&priv->hbdev);
	if (ret) {
		dev_err(dev, "failed to register controller\n");
		goto disable_mux;
	}

	return 0;
disable_mux:
	if (priv->mux_ctrl)
		mux_control_deselect(priv->mux_ctrl);
	return ret;
}

static int am654_hbmc_remove(struct platform_device *pdev)
{
	struct am654_hbmc_priv *priv = platform_get_drvdata(pdev);
	int ret;

	ret = hyperbus_unregister_device(&priv->hbdev);
	if (priv->mux_ctrl)
		mux_control_deselect(priv->mux_ctrl);

	return ret;
}

static const struct of_device_id am654_hbmc_dt_ids[] = {
	{
		.compatible = "ti,am654-hbmc",
	},
	{ /* end of table */ }
};

MODULE_DEVICE_TABLE(of, am654_hbmc_dt_ids);

static struct platform_driver am654_hbmc_platform_driver = {
	.probe = am654_hbmc_probe,
	.remove = am654_hbmc_remove,
	.driver = {
		.name = "hbmc-am654",
		.of_match_table = am654_hbmc_dt_ids,
	},
};

module_platform_driver(am654_hbmc_platform_driver);

MODULE_DESCRIPTION("HBMC driver for AM654 SoC");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:hbmc-am654");
MODULE_AUTHOR("Vignesh Raghavendra <vigneshr@ti.com>");
