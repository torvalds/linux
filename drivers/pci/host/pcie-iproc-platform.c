/*
 * Copyright (C) 2015 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>

#include "pcie-iproc.h"

static int iproc_pcie_pltfm_probe(struct platform_device *pdev)
{
	struct iproc_pcie *pcie;
	struct device_node *np = pdev->dev.of_node;
	struct resource reg;
	resource_size_t iobase = 0;
	LIST_HEAD(res);
	int ret;

	pcie = devm_kzalloc(&pdev->dev, sizeof(struct iproc_pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pcie->dev = &pdev->dev;
	platform_set_drvdata(pdev, pcie);

	ret = of_address_to_resource(np, 0, &reg);
	if (ret < 0) {
		dev_err(pcie->dev, "unable to obtain controller resources\n");
		return ret;
	}

	pcie->base = devm_ioremap(pcie->dev, reg.start, resource_size(&reg));
	if (!pcie->base) {
		dev_err(pcie->dev, "unable to map controller registers\n");
		return -ENOMEM;
	}

	/* PHY use is optional */
	pcie->phy = devm_phy_get(&pdev->dev, "pcie-phy");
	if (IS_ERR(pcie->phy)) {
		if (PTR_ERR(pcie->phy) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		pcie->phy = NULL;
	}

	ret = of_pci_get_host_bridge_resources(np, 0, 0xff, &res, &iobase);
	if (ret) {
		dev_err(pcie->dev,
			"unable to get PCI host bridge resources\n");
		return ret;
	}

	pcie->resources = &res;

	ret = iproc_pcie_setup(pcie);
	if (ret) {
		dev_err(pcie->dev, "PCIe controller setup failed\n");
		return ret;
	}

	return 0;
}

static int iproc_pcie_pltfm_remove(struct platform_device *pdev)
{
	struct iproc_pcie *pcie = platform_get_drvdata(pdev);

	return iproc_pcie_remove(pcie);
}

static const struct of_device_id iproc_pcie_of_match_table[] = {
	{ .compatible = "brcm,iproc-pcie", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, iproc_pcie_of_match_table);

static struct platform_driver iproc_pcie_pltfm_driver = {
	.driver = {
		.name = "iproc-pcie",
		.of_match_table = of_match_ptr(iproc_pcie_of_match_table),
	},
	.probe = iproc_pcie_pltfm_probe,
	.remove = iproc_pcie_pltfm_remove,
};
module_platform_driver(iproc_pcie_pltfm_driver);

MODULE_AUTHOR("Ray Jui <rjui@broadcom.com>");
MODULE_DESCRIPTION("Broadcom iPROC PCIe platform driver");
MODULE_LICENSE("GPL v2");
