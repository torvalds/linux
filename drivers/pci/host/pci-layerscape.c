/*
 * PCIe host controller driver for Freescale Layerscape SoCs
 *
 * Copyright (C) 2014 Freescale Semiconductor.
 *
  * Author: Minghuan Lian <Minghuan.Lian@freescale.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
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

/* PEX1/2 Misc Ports Status Register */
#define SCFG_PEXMSCPORTSR(pex_idx)	(0x94 + (pex_idx) * 4)
#define LTSSM_STATE_SHIFT	20
#define LTSSM_STATE_MASK	0x3f
#define LTSSM_PCIE_L0		0x11 /* L0 state */

/* Symbol Timer Register and Filter Mask Register 1 */
#define PCIE_STRFMR1 0x71c

struct ls_pcie {
	struct list_head node;
	struct device *dev;
	struct pci_bus *bus;
	void __iomem *dbi;
	struct regmap *scfg;
	struct pcie_port pp;
	int index;
	int msi_irq;
};

#define to_ls_pcie(x)	container_of(x, struct ls_pcie, pp)

static int ls_pcie_link_up(struct pcie_port *pp)
{
	u32 state;
	struct ls_pcie *pcie = to_ls_pcie(pp);

	regmap_read(pcie->scfg, SCFG_PEXMSCPORTSR(pcie->index), &state);
	state = (state >> LTSSM_STATE_SHIFT) & LTSSM_STATE_MASK;

	if (state < LTSSM_PCIE_L0)
		return 0;

	return 1;
}

static void ls_pcie_host_init(struct pcie_port *pp)
{
	struct ls_pcie *pcie = to_ls_pcie(pp);
	int count = 0;
	u32 val;

	dw_pcie_setup_rc(pp);

	while (!ls_pcie_link_up(pp)) {
		usleep_range(100, 1000);
		count++;
		if (count >= 200) {
			dev_err(pp->dev, "phy link never came up\n");
			return;
		}
	}

	/*
	 * LS1021A Workaround for internal TKT228622
	 * to fix the INTx hang issue
	 */
	val = ioread32(pcie->dbi + PCIE_STRFMR1);
	val &= 0xffff;
	iowrite32(val, pcie->dbi + PCIE_STRFMR1);
}

static struct pcie_host_ops ls_pcie_host_ops = {
	.link_up = ls_pcie_link_up,
	.host_init = ls_pcie_host_init,
};

static int ls_add_pcie_port(struct ls_pcie *pcie)
{
	struct pcie_port *pp;
	int ret;

	pp = &pcie->pp;
	pp->dev = pcie->dev;
	pp->dbi_base = pcie->dbi;
	pp->root_bus_nr = -1;
	pp->ops = &ls_pcie_host_ops;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(pp->dev, "failed to initialize host\n");
		return ret;
	}

	return 0;
}

static int __init ls_pcie_probe(struct platform_device *pdev)
{
	struct ls_pcie *pcie;
	struct resource *dbi_base;
	u32 index[2];
	int ret;

	pcie = devm_kzalloc(&pdev->dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pcie->dev = &pdev->dev;

	dbi_base = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	if (!dbi_base) {
		dev_err(&pdev->dev, "missing *regs* space\n");
		return -ENODEV;
	}

	pcie->dbi = devm_ioremap_resource(&pdev->dev, dbi_base);
	if (IS_ERR(pcie->dbi))
		return PTR_ERR(pcie->dbi);

	pcie->scfg = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
						     "fsl,pcie-scfg");
	if (IS_ERR(pcie->scfg)) {
		dev_err(&pdev->dev, "No syscfg phandle specified\n");
		return PTR_ERR(pcie->scfg);
	}

	ret = of_property_read_u32_array(pdev->dev.of_node,
					 "fsl,pcie-scfg", index, 2);
	if (ret)
		return ret;
	pcie->index = index[1];

	ret = ls_add_pcie_port(pcie);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, pcie);

	return 0;
}

static const struct of_device_id ls_pcie_of_match[] = {
	{ .compatible = "fsl,ls1021a-pcie" },
	{ },
};
MODULE_DEVICE_TABLE(of, ls_pcie_of_match);

static struct platform_driver ls_pcie_driver = {
	.driver = {
		.name = "layerscape-pcie",
		.owner = THIS_MODULE,
		.of_match_table = ls_pcie_of_match,
	},
};

module_platform_driver_probe(ls_pcie_driver, ls_pcie_probe);

MODULE_AUTHOR("Minghuan Lian <Minghuan.Lian@freescale.com>");
MODULE_DESCRIPTION("Freescale Layerscape PCIe host controller driver");
MODULE_LICENSE("GPL v2");
