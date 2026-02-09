// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe controller driver for CIX's sky1 SoCs
 *
 * Copyright 2025 Cix Technology Group Co., Ltd.
 * Author: Hans Zhang <hans.zhang@cixtech.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pci.h>
#include <linux/pci-ecam.h>
#include <linux/pci_ids.h>

#include "pcie-cadence.h"
#include "pcie-cadence-host-common.h"

#define PCI_VENDOR_ID_CIX		0x1f6c
#define PCI_DEVICE_ID_CIX_SKY1		0x0001

#define STRAP_REG(n)			((n) * 0x04)
#define STATUS_REG(n)			((n) * 0x04)
#define LINK_TRAINING_ENABLE		BIT(0)
#define LINK_COMPLETE			BIT(0)

#define SKY1_IP_REG_BANK		0x1000
#define SKY1_IP_CFG_CTRL_REG_BANK	0x4c00
#define SKY1_IP_AXI_MASTER_COMMON	0xf000
#define SKY1_AXI_SLAVE			0x9000
#define SKY1_AXI_MASTER			0xb000
#define SKY1_AXI_HLS_REGISTERS		0xc000
#define SKY1_AXI_RAS_REGISTERS		0xe000
#define SKY1_DTI_REGISTERS		0xd000

#define IP_REG_I_DBG_STS_0		0x420

struct sky1_pcie {
	struct cdns_pcie *cdns_pcie;
	struct cdns_pcie_rc *cdns_pcie_rc;

	struct resource *cfg_res;
	struct resource *msg_res;
	struct pci_config_window *cfg;
	void __iomem *strap_base;
	void __iomem *status_base;
	void __iomem *reg_base;
	void __iomem *cfg_base;
	void __iomem *msg_base;
};

static int sky1_pcie_resource_get(struct platform_device *pdev,
				  struct sky1_pcie *pcie)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	void __iomem *base;

	base = devm_platform_ioremap_resource_byname(pdev, "reg");
	if (IS_ERR(base))
		return dev_err_probe(dev, PTR_ERR(base),
				     "unable to find \"reg\" registers\n");
	pcie->reg_base = base;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cfg");
	if (!res)
		return dev_err_probe(dev, -ENODEV, "unable to get \"cfg\" resource\n");
	pcie->cfg_res = res;

	base = devm_platform_ioremap_resource_byname(pdev, "rcsu_strap");
	if (IS_ERR(base))
		return dev_err_probe(dev, PTR_ERR(base),
				     "unable to find \"rcsu_strap\" registers\n");
	pcie->strap_base = base;

	base = devm_platform_ioremap_resource_byname(pdev, "rcsu_status");
	if (IS_ERR(base))
		return dev_err_probe(dev, PTR_ERR(base),
				     "unable to find \"rcsu_status\" registers\n");
	pcie->status_base = base;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "msg");
	if (!res)
		return dev_err_probe(dev, -ENODEV, "unable to get \"msg\" resource\n");
	pcie->msg_res = res;
	pcie->msg_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(pcie->msg_base)) {
		return dev_err_probe(dev, PTR_ERR(pcie->msg_base),
				     "unable to ioremap msg resource\n");
	}

	return 0;
}

static int sky1_pcie_start_link(struct cdns_pcie *cdns_pcie)
{
	struct sky1_pcie *pcie = dev_get_drvdata(cdns_pcie->dev);
	u32 val;

	val = readl(pcie->strap_base + STRAP_REG(1));
	val |= LINK_TRAINING_ENABLE;
	writel(val, pcie->strap_base + STRAP_REG(1));

	return 0;
}

static void sky1_pcie_stop_link(struct cdns_pcie *cdns_pcie)
{
	struct sky1_pcie *pcie = dev_get_drvdata(cdns_pcie->dev);
	u32 val;

	val = readl(pcie->strap_base + STRAP_REG(1));
	val &= ~LINK_TRAINING_ENABLE;
	writel(val, pcie->strap_base + STRAP_REG(1));
}

static bool sky1_pcie_link_up(struct cdns_pcie *cdns_pcie)
{
	u32 val;

	val = cdns_pcie_hpa_readl(cdns_pcie, REG_BANK_IP_REG,
				  IP_REG_I_DBG_STS_0);
	return val & LINK_COMPLETE;
}

static const struct cdns_pcie_ops sky1_pcie_ops = {
	.start_link = sky1_pcie_start_link,
	.stop_link = sky1_pcie_stop_link,
	.link_up = sky1_pcie_link_up,
};

static int sky1_pcie_probe(struct platform_device *pdev)
{
	struct cdns_plat_pcie_of_data *reg_off;
	struct device *dev = &pdev->dev;
	struct pci_host_bridge *bridge;
	struct cdns_pcie *cdns_pcie;
	struct resource_entry *bus;
	struct cdns_pcie_rc *rc;
	struct sky1_pcie *pcie;
	int ret;

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	bridge = devm_pci_alloc_host_bridge(dev, sizeof(*rc));
	if (!bridge)
		return -ENOMEM;

	ret = sky1_pcie_resource_get(pdev, pcie);
	if (ret < 0)
		return ret;

	bus = resource_list_first_type(&bridge->windows, IORESOURCE_BUS);
	if (!bus)
		return -ENODEV;

	pcie->cfg = pci_ecam_create(dev, pcie->cfg_res, bus->res,
				    &pci_generic_ecam_ops);
	if (IS_ERR(pcie->cfg))
		return PTR_ERR(pcie->cfg);

	bridge->ops = (struct pci_ops *)&pci_generic_ecam_ops.pci_ops;
	rc = pci_host_bridge_priv(bridge);
	rc->ecam_supported = 1;
	rc->cfg_base = pcie->cfg->win;
	rc->cfg_res = &pcie->cfg->res;

	cdns_pcie = &rc->pcie;
	cdns_pcie->dev = dev;
	cdns_pcie->ops = &sky1_pcie_ops;
	cdns_pcie->reg_base = pcie->reg_base;
	cdns_pcie->msg_res = pcie->msg_res;
	cdns_pcie->is_rc = 1;

	reg_off = devm_kzalloc(dev, sizeof(*reg_off), GFP_KERNEL);
	if (!reg_off)
		return -ENOMEM;

	reg_off->ip_reg_bank_offset = SKY1_IP_REG_BANK;
	reg_off->ip_cfg_ctrl_reg_offset = SKY1_IP_CFG_CTRL_REG_BANK;
	reg_off->axi_mstr_common_offset = SKY1_IP_AXI_MASTER_COMMON;
	reg_off->axi_slave_offset = SKY1_AXI_SLAVE;
	reg_off->axi_master_offset = SKY1_AXI_MASTER;
	reg_off->axi_hls_offset = SKY1_AXI_HLS_REGISTERS;
	reg_off->axi_ras_offset = SKY1_AXI_RAS_REGISTERS;
	reg_off->axi_dti_offset = SKY1_DTI_REGISTERS;
	cdns_pcie->cdns_pcie_reg_offsets = reg_off;

	pcie->cdns_pcie = cdns_pcie;
	pcie->cdns_pcie_rc = rc;
	pcie->cfg_base = rc->cfg_base;
	bridge->sysdata = pcie->cfg;

	rc->vendor_id = PCI_VENDOR_ID_CIX;
	rc->device_id = PCI_DEVICE_ID_CIX_SKY1;
	rc->no_inbound_map = 1;

	dev_set_drvdata(dev, pcie);

	ret = cdns_pcie_hpa_host_setup(rc);
	if (ret < 0) {
		pci_ecam_free(pcie->cfg);
		return ret;
	}

	return 0;
}

static const struct of_device_id of_sky1_pcie_match[] = {
	{ .compatible = "cix,sky1-pcie-host", },
	{},
};
MODULE_DEVICE_TABLE(of, of_sky1_pcie_match);

static void sky1_pcie_remove(struct platform_device *pdev)
{
	struct sky1_pcie *pcie = platform_get_drvdata(pdev);

	pci_ecam_free(pcie->cfg);
}

static struct platform_driver sky1_pcie_driver = {
	.probe  = sky1_pcie_probe,
	.remove = sky1_pcie_remove,
	.driver = {
		.name = "sky1-pcie",
		.of_match_table = of_sky1_pcie_match,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};
module_platform_driver(sky1_pcie_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PCIe controller driver for CIX's sky1 SoCs");
MODULE_AUTHOR("Hans Zhang <hans.zhang@cixtech.com>");
