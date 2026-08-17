// SPDX-License-Identifier: GPL-2.0
/*
 * DWC PCIe RC driver for UltraRISC SoCs
 *
 * Copyright (C) 2026 UltraRISC Technology (Shanghai) Co., Ltd.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/resource.h>

#include "pcie-designware.h"

#define PCIE_CUS_CORE          0x400000

#define LTSSM_ENABLE           BIT(7)
#define FAST_LINK_MODE         BIT(12)
#define HOLD_PHY_RST           BIT(14)
#define L1SUB_DISABLE          BIT(15)

#define ULTRARISC_PCIE_COMP_TIMEOUT_65_210MS	0x6

static struct pci_ops ultrarisc_pci_ops = {
	.map_bus = dw_pcie_own_conf_map_bus,
	.read = pci_generic_config_read32,
	.write = pci_generic_config_write32,
};

static int ultrarisc_pcie_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct pci_host_bridge *bridge = pp->bridge;
	u8 cap_exp;
	u32 val;

	bridge->ops = &ultrarisc_pci_ops;

	if (dw_pcie_link_up(pci))
		return 0;

	val = dw_pcie_readl_dbi(pci, PCIE_CUS_CORE);
	val &= ~FAST_LINK_MODE;
	dw_pcie_writel_dbi(pci, PCIE_CUS_CORE, val);

	val = dw_pcie_readl_dbi(pci, PCIE_TIMER_CTRL_MAX_FUNC_NUM);
	FIELD_MODIFY(PORT_FLT_SF_MASK, &val, PORT_FLT_SF_VAL_64);
	dw_pcie_writel_dbi(pci, PCIE_TIMER_CTRL_MAX_FUNC_NUM, val);

	cap_exp = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	val = dw_pcie_readl_dbi(pci, cap_exp + PCI_EXP_LNKCTL2);
	FIELD_MODIFY(PCI_EXP_LNKCTL2_TLS, &val, PCI_EXP_LNKCTL2_TLS_16_0GT);
	dw_pcie_writel_dbi(pci, cap_exp + PCI_EXP_LNKCTL2, val);

	val = dw_pcie_readl_dbi(pci, PCIE_PORT_FORCE);
	FIELD_MODIFY(PORT_LINK_NUM_MASK, &val, 0);
	dw_pcie_writel_dbi(pci, PCIE_PORT_FORCE, val);

	val = dw_pcie_readl_dbi(pci, cap_exp + PCI_EXP_DEVCTL2);
	FIELD_MODIFY(PCI_EXP_DEVCTL2_COMP_TIMEOUT, &val,
		     ULTRARISC_PCIE_COMP_TIMEOUT_65_210MS);
	dw_pcie_writel_dbi(pci, cap_exp + PCI_EXP_DEVCTL2, val);

	val = dw_pcie_readl_dbi(pci, PCIE_CUS_CORE);
	val &= ~(HOLD_PHY_RST | L1SUB_DISABLE);
	dw_pcie_writel_dbi(pci, PCIE_CUS_CORE, val);

	return 0;
}

static void ultrarisc_pcie_pme_turn_off(struct dw_pcie_rp *pp)
{
	/*
	 * DP1000 does not support sending PME_Turn_Off from the RC.
	 * Keep this callback empty to skip the generic MSG TLP path.
	 */
}

static const struct dw_pcie_host_ops ultrarisc_pcie_host_ops = {
	.init = ultrarisc_pcie_host_init,
	.pme_turn_off = ultrarisc_pcie_pme_turn_off,
};

static int ultrarisc_pcie_start_link(struct dw_pcie *pci)
{
	u32 val;

	val = dw_pcie_readl_dbi(pci, PCIE_CUS_CORE);
	val |= LTSSM_ENABLE;
	dw_pcie_writel_dbi(pci, PCIE_CUS_CORE, val);

	return 0;
}

static const struct dw_pcie_ops dw_pcie_ops = {
	.start_link = ultrarisc_pcie_start_link,
};

static int ultrarisc_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_pcie_rp *pp;
	struct dw_pcie *pci;
	int ret;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = dev;
	pci->ops = &dw_pcie_ops;

	/* Set a default value suitable for at most 16 in and 16 out windows */
	pci->atu_size = SZ_8K;

	pp = &pci->pp;

	platform_set_drvdata(pdev, pci);

	pp->num_vectors = MAX_MSI_IRQS;
	/* No L2/L3 Ready indication is available on this platform */
	pp->skip_l23_ready = true;
	pp->ops = &ultrarisc_pcie_host_ops;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "Failed to initialize host\n");
		return ret;
	}

	return 0;
}

static int ultrarisc_pcie_suspend_noirq(struct device *dev)
{
	struct dw_pcie *pci = dev_get_drvdata(dev);

	return dw_pcie_suspend_noirq(pci);
}

static int ultrarisc_pcie_resume_noirq(struct device *dev)
{
	struct dw_pcie *pci = dev_get_drvdata(dev);

	return dw_pcie_resume_noirq(pci);
}

static const struct dev_pm_ops ultrarisc_pcie_pm_ops = {
	NOIRQ_SYSTEM_SLEEP_PM_OPS(ultrarisc_pcie_suspend_noirq,
				  ultrarisc_pcie_resume_noirq)
};

static const struct of_device_id ultrarisc_pcie_of_match[] = {
	{
		.compatible = "ultrarisc,dp1000-pcie",
	},
	{},
};
MODULE_DEVICE_TABLE(of, ultrarisc_pcie_of_match);

static struct platform_driver ultrarisc_pcie_driver = {
	.driver = {
		.name	= "ultrarisc-pcie",
		.of_match_table = ultrarisc_pcie_of_match,
		.suppress_bind_attrs = true,
		.pm = &ultrarisc_pcie_pm_ops,
	},
	.probe = ultrarisc_pcie_probe,
};
builtin_platform_driver(ultrarisc_pcie_driver);

MODULE_DESCRIPTION("UltraRISC DP1000 DWC PCIe host controller");
MODULE_LICENSE("GPL");
