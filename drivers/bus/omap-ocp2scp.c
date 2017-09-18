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

#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_platform.h>

#define OCP2SCP_TIMING 0x18
#define SYNC2_MASK 0xf

static int ocp2scp_remove_devices(struct device *dev, void *c)
{
	struct platform_device *pdev = to_platform_device(dev);

	platform_device_unregister(pdev);

	return 0;
}

static int omap_ocp2scp_probe(struct platform_device *pdev)
{
	int ret;
	u32 reg;
	void __iomem *regs;
	struct resource *res;
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
	/*
	 * As per AM572x TRM: http://www.ti.com/lit/ug/spruhz6/spruhz6.pdf
	 * under section 26.3.2.2, table 26-26 OCP2SCP TIMING Caution;
	 * As per OMAP4430 TRM: http://www.ti.com/lit/ug/swpu231ap/swpu231ap.pdf
	 * under section 23.12.6.2.2 , Table 23-1213 OCP2SCP TIMING Caution;
	 * As per OMAP4460 TRM: http://www.ti.com/lit/ug/swpu235ab/swpu235ab.pdf
	 * under section 23.12.6.2.2, Table 23-1213 OCP2SCP TIMING Caution;
	 * As per OMAP543x TRM http://www.ti.com/lit/pdf/swpu249
	 * under section 27.3.2.2, Table 27-27 OCP2SCP TIMING Caution;
	 *
	 * Read path of OCP2SCP is not working properly due to low reset value
	 * of SYNC2 parameter in OCP2SCP. Suggested reset value is 0x6 or more.
	 */
	if (!of_device_is_compatible(np, "ti,am437x-ocp2scp")) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		regs = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(regs)) {
			ret = PTR_ERR(regs);
			goto err1;
		}

		pm_runtime_get_sync(&pdev->dev);
		reg = readl_relaxed(regs + OCP2SCP_TIMING);
		reg &= ~(SYNC2_MASK);
		reg |= 0x6;
		writel_relaxed(reg, regs + OCP2SCP_TIMING);
		pm_runtime_put_sync(&pdev->dev);
	}

	return 0;

err1:
	pm_runtime_disable(&pdev->dev);

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
	{ .compatible = "ti,am437x-ocp2scp" },
	{}
};
MODULE_DEVICE_TABLE(of, omap_ocp2scp_id_table);
#endif

static struct platform_driver omap_ocp2scp_driver = {
	.probe		= omap_ocp2scp_probe,
	.remove		= omap_ocp2scp_remove,
	.driver		= {
		.name	= "omap-ocp2scp",
		.of_match_table = of_match_ptr(omap_ocp2scp_id_table),
	},
};

module_platform_driver(omap_ocp2scp_driver);

MODULE_ALIAS("platform:omap-ocp2scp");
MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("OMAP OCP2SCP driver");
MODULE_LICENSE("GPL v2");
