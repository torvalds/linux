// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for Freescale Layerscape SoCs
 *
 * Copyright (C) 2014 Freescale Semiconductor.
 * Copyright 2021 NXP
 *
 * Author: Minghuan Lian <Minghuan.Lian@freescale.com>
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "pcie-designware.h"

/* PEX Internal Configuration Registers */
#define PCIE_STRFMR1		0x71c /* Symbol Timer & Filter Mask Register1 */
#define PCIE_ABSERR		0x8d0 /* Bridge Slave Error Response Register */
#define PCIE_ABSERR_SETTING	0x9401 /* Forward error of non-posted request */

#define PCIE_IATU_NUM		6

struct ls_pcie {
	struct dw_pcie *pci;
};

#define to_ls_pcie(x)	dev_get_drvdata((x)->dev)

static bool ls_pcie_is_bridge(struct ls_pcie *pcie)
{
	struct dw_pcie *pci = pcie->pci;
	u32 header_type;

	header_type = ioread8(pci->dbi_base + PCI_HEADER_TYPE);
	header_type &= 0x7f;

	return header_type == PCI_HEADER_TYPE_BRIDGE;
}

/* Clear multi-function bit */
static void ls_pcie_clear_multifunction(struct ls_pcie *pcie)
{
	struct dw_pcie *pci = pcie->pci;

	iowrite8(PCI_HEADER_TYPE_BRIDGE, pci->dbi_base + PCI_HEADER_TYPE);
}

/* Drop MSG TLP except for Vendor MSG */
static void ls_pcie_drop_msg_tlp(struct ls_pcie *pcie)
{
	u32 val;
	struct dw_pcie *pci = pcie->pci;

	val = ioread32(pci->dbi_base + PCIE_STRFMR1);
	val &= 0xDFFFFFFF;
	iowrite32(val, pci->dbi_base + PCIE_STRFMR1);
}

/* Forward error response of outbound non-posted requests */
static void ls_pcie_fix_error_response(struct ls_pcie *pcie)
{
	struct dw_pcie *pci = pcie->pci;

	iowrite32(PCIE_ABSERR_SETTING, pci->dbi_base + PCIE_ABSERR);
}

static int ls_pcie_host_init(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct ls_pcie *pcie = to_ls_pcie(pci);

	ls_pcie_fix_error_response(pcie);

	dw_pcie_dbi_ro_wr_en(pci);
	ls_pcie_clear_multifunction(pcie);
	dw_pcie_dbi_ro_wr_dis(pci);

	ls_pcie_drop_msg_tlp(pcie);

	return 0;
}

static const struct dw_pcie_host_ops ls_pcie_host_ops = {
	.host_init = ls_pcie_host_init,
};

static const struct of_device_id ls_pcie_of_match[] = {
	{ .compatible = "fsl,ls1012a-pcie", },
	{ .compatible = "fsl,ls1021a-pcie", },
	{ .compatible = "fsl,ls1028a-pcie", },
	{ .compatible = "fsl,ls1043a-pcie", },
	{ .compatible = "fsl,ls1046a-pcie", },
	{ .compatible = "fsl,ls2080a-pcie", },
	{ .compatible = "fsl,ls2085a-pcie", },
	{ .compatible = "fsl,ls2088a-pcie", },
	{ .compatible = "fsl,ls1088a-pcie", },
	{ },
};

static int ls_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_pcie *pci;
	struct ls_pcie *pcie;
	struct resource *dbi_base;

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = dev;
	pci->pp.ops = &ls_pcie_host_ops;

	pcie->pci = pci;

	dbi_base = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	pci->dbi_base = devm_pci_remap_cfg_resource(dev, dbi_base);
	if (IS_ERR(pci->dbi_base))
		return PTR_ERR(pci->dbi_base);

	if (!ls_pcie_is_bridge(pcie))
		return -ENODEV;

	platform_set_drvdata(pdev, pcie);

	return dw_pcie_host_init(&pci->pp);
}

static struct platform_driver ls_pcie_driver = {
	.probe = ls_pcie_probe,
	.driver = {
		.name = "layerscape-pcie",
		.of_match_table = ls_pcie_of_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(ls_pcie_driver);
