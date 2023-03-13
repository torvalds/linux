// SPDX-License-Identifier: GPL-2.0
/*
 * CDX host controller driver for AMD versal-net platform.
 *
 * Copyright (C) 2022-2023, Advanced Micro Devices, Inc.
 */

#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/cdx/cdx_bus.h>

#include "../cdx.h"
#include "mcdi_functions.h"
#include "mcdi.h"

static unsigned int cdx_mcdi_rpc_timeout(struct cdx_mcdi *cdx, unsigned int cmd)
{
	return MCDI_RPC_TIMEOUT;
}

static void cdx_mcdi_request(struct cdx_mcdi *cdx,
			     const struct cdx_dword *hdr, size_t hdr_len,
			     const struct cdx_dword *sdu, size_t sdu_len)
{
	/*
	 * This will get updated by rpmsg APIs, with RPMSG introduction
	 * in CDX controller as a transport layer.
	 */
}

static const struct cdx_mcdi_ops mcdi_ops = {
	.mcdi_rpc_timeout = cdx_mcdi_rpc_timeout,
	.mcdi_request = cdx_mcdi_request,
};

static int cdx_scan_devices(struct cdx_controller *cdx)
{
	struct cdx_mcdi *cdx_mcdi = cdx->priv;
	u8 bus_num, dev_num, num_cdx_bus;
	int ret;

	/* MCDI FW Read: Fetch the number of CDX buses on this controller */
	ret = cdx_mcdi_get_num_buses(cdx_mcdi);
	if (ret < 0) {
		dev_err(cdx->dev,
			"Get number of CDX buses failed: %d\n", ret);
		return ret;
	}
	num_cdx_bus = (u8)ret;

	for (bus_num = 0; bus_num < num_cdx_bus; bus_num++) {
		u8 num_cdx_dev;

		/* MCDI FW Read: Fetch the number of devices present */
		ret = cdx_mcdi_get_num_devs(cdx_mcdi, bus_num);
		if (ret < 0) {
			dev_err(cdx->dev,
				"Get devices on CDX bus %d failed: %d\n", bus_num, ret);
			continue;
		}
		num_cdx_dev = (u8)ret;

		for (dev_num = 0; dev_num < num_cdx_dev; dev_num++) {
			struct cdx_dev_params dev_params;

			/* MCDI FW: Get the device config */
			ret = cdx_mcdi_get_dev_config(cdx_mcdi, bus_num,
						      dev_num, &dev_params);
			if (ret) {
				dev_err(cdx->dev,
					"CDX device config get failed for %d(bus):%d(dev), %d\n",
					bus_num, dev_num, ret);
				continue;
			}
			dev_params.cdx = cdx;

			/* Add the device to the cdx bus */
			ret = cdx_device_add(&dev_params);
			if (ret) {
				dev_err(cdx->dev, "registering cdx dev: %d failed: %d\n",
					dev_num, ret);
				continue;
			}

			dev_dbg(cdx->dev, "CDX dev: %d on cdx bus: %d created\n",
				dev_num, bus_num);
		}
	}

	return 0;
}

static struct cdx_ops cdx_ops = {
	.scan		= cdx_scan_devices,
};

static int xlnx_cdx_probe(struct platform_device *pdev)
{
	struct cdx_controller *cdx;
	struct cdx_mcdi *cdx_mcdi;
	int ret;

	cdx_mcdi = kzalloc(sizeof(*cdx_mcdi), GFP_KERNEL);
	if (!cdx_mcdi)
		return -ENOMEM;

	/* Store the MCDI ops */
	cdx_mcdi->mcdi_ops = &mcdi_ops;
	/* MCDI FW: Initialize the FW path */
	ret = cdx_mcdi_init(cdx_mcdi);
	if (ret) {
		dev_err_probe(&pdev->dev, ret, "MCDI Initialization failed\n");
		goto mcdi_init_fail;
	}

	cdx = kzalloc(sizeof(*cdx), GFP_KERNEL);
	if (!cdx) {
		ret = -ENOMEM;
		goto cdx_alloc_fail;
	}
	platform_set_drvdata(pdev, cdx);

	cdx->dev = &pdev->dev;
	cdx->priv = cdx_mcdi;
	cdx->ops = &cdx_ops;

	return 0;

cdx_alloc_fail:
	cdx_mcdi_finish(cdx_mcdi);
mcdi_init_fail:
	kfree(cdx_mcdi);

	return ret;
}

static int xlnx_cdx_remove(struct platform_device *pdev)
{
	struct cdx_controller *cdx = platform_get_drvdata(pdev);
	struct cdx_mcdi *cdx_mcdi = cdx->priv;

	kfree(cdx);

	cdx_mcdi_finish(cdx_mcdi);
	kfree(cdx_mcdi);

	return 0;
}

static const struct of_device_id cdx_match_table[] = {
	{.compatible = "xlnx,versal-net-cdx",},
	{ },
};

MODULE_DEVICE_TABLE(of, cdx_match_table);

static struct platform_driver cdx_pdriver = {
	.driver = {
		   .name = "cdx-controller",
		   .pm = NULL,
		   .of_match_table = cdx_match_table,
		   },
	.probe = xlnx_cdx_probe,
	.remove = xlnx_cdx_remove,
};

static int __init cdx_controller_init(void)
{
	int ret;

	ret = platform_driver_register(&cdx_pdriver);
	if (ret)
		pr_err("platform_driver_register() failed: %d\n", ret);

	return ret;
}

static void __exit cdx_controller_exit(void)
{
	platform_driver_unregister(&cdx_pdriver);
}

module_init(cdx_controller_init);
module_exit(cdx_controller_exit);

MODULE_AUTHOR("AMD Inc.");
MODULE_DESCRIPTION("CDX controller for AMD devices");
MODULE_LICENSE("GPL");
