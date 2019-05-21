// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for Freescale Layerscape SoCs
 *
 * Copyright (C) 2014 Freescale Semiconductor.
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

/* PEX1/2 Misc Ports Status Register */
#define SCFG_PEXMSCPORTSR(pex_idx)	(0x94 + (pex_idx) * 4)
#define LTSSM_STATE_SHIFT	20
#define LTSSM_STATE_MASK	0x3f
#define LTSSM_PCIE_L0		0x11 /* L0 state */

/* PEX Internal Configuration Registers */
#define PCIE_STRFMR1		0x71c /* Symbol Timer & Filter Mask Register1 */
#define PCIE_ABSERR		0x8d0 /* Bridge Slave Error Response Register */
#define PCIE_ABSERR_SETTING	0x9401 /* Forward error of non-posted request */

#define PCIE_IATU_NUM		6

struct ls_pcie_drvdata {
	u32 lut_offset;
	u32 ltssm_shift;
	u32 lut_dbg;
	const struct dw_pcie_host_ops *ops;
	const struct dw_pcie_ops *dw_pcie_ops;
};

struct ls_pcie {
	struct dw_pcie *pci;
	void __iomem *lut;
	struct regmap *scfg;
	const struct ls_pcie_drvdata *drvdata;
	int index;
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

static void ls_pcie_disable_outbound_atus(struct ls_pcie *pcie)
{
	int i;

	for (i = 0; i < PCIE_IATU_NUM; i++)
		dw_pcie_disable_atu(pcie->pci, i, DW_PCIE_REGION_OUTBOUND);
}

static int ls1021_pcie_link_up(struct dw_pcie *pci)
{
	u32 state;
	struct ls_pcie *pcie = to_ls_pcie(pci);

	if (!pcie->scfg)
		return 0;

	regmap_read(pcie->scfg, SCFG_PEXMSCPORTSR(pcie->index), &state);
	state = (state >> LTSSM_STATE_SHIFT) & LTSSM_STATE_MASK;

	if (state < LTSSM_PCIE_L0)
		return 0;

	return 1;
}

static int ls_pcie_link_up(struct dw_pcie *pci)
{
	struct ls_pcie *pcie = to_ls_pcie(pci);
	u32 state;

	state = (ioread32(pcie->lut + pcie->drvdata->lut_dbg) >>
		 pcie->drvdata->ltssm_shift) &
		 LTSSM_STATE_MASK;

	if (state < LTSSM_PCIE_L0)
		return 0;

	return 1;
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

	/*
	 * Disable outbound windows configured by the bootloader to avoid
	 * one transaction hitting multiple outbound windows.
	 * dw_pcie_setup_rc() will reconfigure the outbound windows.
	 */
	ls_pcie_disable_outbound_atus(pcie);
	ls_pcie_fix_error_response(pcie);

	dw_pcie_dbi_ro_wr_en(pci);
	ls_pcie_clear_multifunction(pcie);
	dw_pcie_dbi_ro_wr_dis(pci);

	ls_pcie_drop_msg_tlp(pcie);

	dw_pcie_setup_rc(pp);

	return 0;
}

static int ls1021_pcie_host_init(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct ls_pcie *pcie = to_ls_pcie(pci);
	struct device *dev = pci->dev;
	u32 index[2];
	int ret;

	pcie->scfg = syscon_regmap_lookup_by_phandle(dev->of_node,
						     "fsl,pcie-scfg");
	if (IS_ERR(pcie->scfg)) {
		ret = PTR_ERR(pcie->scfg);
		dev_err(dev, "No syscfg phandle specified\n");
		pcie->scfg = NULL;
		return ret;
	}

	if (of_property_read_u32_array(dev->of_node,
				       "fsl,pcie-scfg", index, 2)) {
		pcie->scfg = NULL;
		return -EINVAL;
	}
	pcie->index = index[1];

	return ls_pcie_host_init(pp);
}

static int ls_pcie_msi_host_init(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct device *dev = pci->dev;
	struct device_node *np = dev->of_node;
	struct device_node *msi_node;

	/*
	 * The MSI domain is set by the generic of_msi_configure().  This
	 * .msi_host_init() function keeps us from doing the default MSI
	 * domain setup in dw_pcie_host_init() and also enforces the
	 * requirement that "msi-parent" exists.
	 */
	msi_node = of_parse_phandle(np, "msi-parent", 0);
	if (!msi_node) {
		dev_err(dev, "failed to find msi-parent\n");
		return -EINVAL;
	}

	return 0;
}

static const struct dw_pcie_host_ops ls1021_pcie_host_ops = {
	.host_init = ls1021_pcie_host_init,
	.msi_host_init = ls_pcie_msi_host_init,
};

static const struct dw_pcie_host_ops ls_pcie_host_ops = {
	.host_init = ls_pcie_host_init,
	.msi_host_init = ls_pcie_msi_host_init,
};

static const struct dw_pcie_ops dw_ls1021_pcie_ops = {
	.link_up = ls1021_pcie_link_up,
};

static const struct dw_pcie_ops dw_ls_pcie_ops = {
	.link_up = ls_pcie_link_up,
};

static const struct ls_pcie_drvdata ls1021_drvdata = {
	.ops = &ls1021_pcie_host_ops,
	.dw_pcie_ops = &dw_ls1021_pcie_ops,
};

static const struct ls_pcie_drvdata ls1043_drvdata = {
	.lut_offset = 0x10000,
	.ltssm_shift = 24,
	.lut_dbg = 0x7fc,
	.ops = &ls_pcie_host_ops,
	.dw_pcie_ops = &dw_ls_pcie_ops,
};

static const struct ls_pcie_drvdata ls1046_drvdata = {
	.lut_offset = 0x80000,
	.ltssm_shift = 24,
	.lut_dbg = 0x407fc,
	.ops = &ls_pcie_host_ops,
	.dw_pcie_ops = &dw_ls_pcie_ops,
};

static const struct ls_pcie_drvdata ls2080_drvdata = {
	.lut_offset = 0x80000,
	.ltssm_shift = 0,
	.lut_dbg = 0x7fc,
	.ops = &ls_pcie_host_ops,
	.dw_pcie_ops = &dw_ls_pcie_ops,
};

static const struct ls_pcie_drvdata ls2088_drvdata = {
	.lut_offset = 0x80000,
	.ltssm_shift = 0,
	.lut_dbg = 0x407fc,
	.ops = &ls_pcie_host_ops,
	.dw_pcie_ops = &dw_ls_pcie_ops,
};

static const struct of_device_id ls_pcie_of_match[] = {
	{ .compatible = "fsl,ls1012a-pcie", .data = &ls1046_drvdata },
	{ .compatible = "fsl,ls1021a-pcie", .data = &ls1021_drvdata },
	{ .compatible = "fsl,ls1043a-pcie", .data = &ls1043_drvdata },
	{ .compatible = "fsl,ls1046a-pcie", .data = &ls1046_drvdata },
	{ .compatible = "fsl,ls2080a-pcie", .data = &ls2080_drvdata },
	{ .compatible = "fsl,ls2085a-pcie", .data = &ls2080_drvdata },
	{ .compatible = "fsl,ls2088a-pcie", .data = &ls2088_drvdata },
	{ .compatible = "fsl,ls1088a-pcie", .data = &ls2088_drvdata },
	{ },
};

static int __init ls_add_pcie_port(struct ls_pcie *pcie)
{
	struct dw_pcie *pci = pcie->pci;
	struct pcie_port *pp = &pci->pp;
	struct device *dev = pci->dev;
	int ret;

	pp->ops = pcie->drvdata->ops;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "failed to initialize host\n");
		return ret;
	}

	return 0;
}

static int __init ls_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_pcie *pci;
	struct ls_pcie *pcie;
	struct resource *dbi_base;
	int ret;

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pcie->drvdata = of_device_get_match_data(dev);

	pci->dev = dev;
	pci->ops = pcie->drvdata->dw_pcie_ops;

	pcie->pci = pci;

	dbi_base = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	pci->dbi_base = devm_pci_remap_cfg_resource(dev, dbi_base);
	if (IS_ERR(pci->dbi_base))
		return PTR_ERR(pci->dbi_base);

	pcie->lut = pci->dbi_base + pcie->drvdata->lut_offset;

	if (!ls_pcie_is_bridge(pcie))
		return -ENODEV;

	platform_set_drvdata(pdev, pcie);

	ret = ls_add_pcie_port(pcie);
	if (ret < 0)
		return ret;

	return 0;
}

static struct platform_driver ls_pcie_driver = {
	.driver = {
		.name = "layerscape-pcie",
		.of_match_table = ls_pcie_of_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver_probe(ls_pcie_driver, ls_pcie_probe);
