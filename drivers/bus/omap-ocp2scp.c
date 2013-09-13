/*
 * omap-ocp2scp.c - transform ocp interface protocol to scp protocol
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_platform.h>

static int ocp2scp_remove_devices(struct device *dev, void *c)
{
	struct platform_device *pdev = to_platform_device(dev);

	platform_device_unregister(pdev);

	return 0;
}

static int omap_ocp2scp_probe(struct platform_device *pdev)
{
	int ret;
	struct device_node *np = pdev->dev.of_node;

	if (np) {
		ret = of_platform_populate(np, NULL, NULL, &pdev->dev);
		if (ret) {
			dev_err(&pdev->dev,
			    "failed to add resources for ocp2scp child\n");
			goto err0;
		}
	}

	pm_runtime_enable(&pdev->dev);

	return 0;

err0:
	device_for_each_child(&pdev->dev, NULL, ocp2scp_remove_devices);

	return ret;
}

static int omap_ocp2scp_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	device_for_each_child(&pdev->dev, NULL, ocp2scp_remove_devices);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id omap_ocp2scp_id_table[] = {
	{ .compatible = "ti,omap-ocp2scp" },
	{}
};
MODULE_DEVICE_TABLE(of, omap_ocp2scp_id_table);
#endif

static struct platform_driver omap_ocp2scp_driver = {
	.probe		= omap_ocp2scp_probe,
	.remove		= omap_ocp2scp_remove,
	.driver		= {
		.name	= "omap-ocp2scp",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(omap_ocp2scp_id_table),
	},
};

module_platform_driver(omap_ocp2scp_driver);

MODULE_ALIAS("platform: omap-ocp2scp");
MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("OMAP OCP2SCP driver");
MODULE_LICENSE("GPL v2");
