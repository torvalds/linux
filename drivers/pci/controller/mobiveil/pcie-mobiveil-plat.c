// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for Mobiveil PCIe Host controller
 *
 * Copyright (c) 2018 Mobiveil Inc.
 * Copyright 2019 NXP
 *
 * Author: Subrahmanya Lingappa <l.subrahmanya@mobiveil.co.in>
 *	   Hou Zhiqiang <Zhiqiang.Hou@nxp.com>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "pcie-mobiveil.h"

static int mobiveil_pcie_probe(struct platform_device *pdev)
{
	struct mobiveil_pcie *pcie;
	struct pci_host_bridge *bridge;
	struct device *dev = &pdev->dev;

	/* allocate the PCIe port */
	bridge = devm_pci_alloc_host_bridge(dev, sizeof(*pcie));
	if (!bridge)
		return -ENOMEM;

	pcie = pci_host_bridge_priv(bridge);
	pcie->rp.bridge = bridge;

	pcie->pdev = pdev;

	return mobiveil_pcie_host_probe(pcie);
}

static const struct of_device_id mobiveil_pcie_of_match[] = {
	{.compatible = "mbvl,gpex40-pcie",},
	{},
};

MODULE_DEVICE_TABLE(of, mobiveil_pcie_of_match);

static struct platform_driver mobiveil_pcie_driver = {
	.probe = mobiveil_pcie_probe,
	.driver = {
		.name = "mobiveil-pcie",
		.of_match_table = mobiveil_pcie_of_match,
		.suppress_bind_attrs = true,
	},
};

builtin_platform_driver(mobiveil_pcie_driver);

MODULE_DESCRIPTION("Mobiveil PCIe host controller driver");
MODULE_AUTHOR("Subrahmanya Lingappa <l.subrahmanya@mobiveil.co.in>");
