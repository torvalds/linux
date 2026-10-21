// SPDX-License-Identifier: GPL-2.0
/*
 * DWC PCIe RC driver for UltraRISC SoCs
 *
 * Copyright (C) 2026 UltraRISC Technology (Shanghai) Co., Ltd.
 */

#include <linux/clk.h>
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

struct ultrarisc_pcie {
	struct dw_pcie pci;
	struct clk_bulk_data *clks;
	int num_clks;
};

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

static int ultrarisc_pcie_enable_clks(struct ultrarisc_pcie *ultra)
{
	return clk_bulk_prepare_enable(ultra->num_clks, ultra->clks);
}

static void ultrarisc_pcie_disable_clks(void *data)
{
	struct ultrarisc_pcie *ultra = data;

	clk_bulk_disable_unprepare(ultra->num_clks, ultra->clks);
}

static int ultrarisc_pcie_init_clks(struct ultrarisc_pcie *ultra)
{
	struct device *dev = ultra->pci.dev;
	int ret;

	ultra->num_clks = devm_clk_bulk_get_all(dev, &ultra->clks);
	if (ultra->num_clks < 0)
		return dev_err_probe(dev, ultra->num_clks, "Failed to get clocks\n");

	ret = ultrarisc_pcie_enable_clks(ultra);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable clocks\n");

	return devm_add_action_or_reset(dev, ultrarisc_pcie_disable_clks, ultra);
}

static int ultrarisc_pcie_probe(struct platform_device *pdev)
{
	struct ultrarisc_pcie *ultra;
	struct device *dev = &pdev->dev;
	struct dw_pcie_rp *pp;
	struct dw_pcie *pci;
	int ret;

	ultra = devm_kzalloc(dev, sizeof(*ultra), GFP_KERNEL);
	if (!ultra)
		return -ENOMEM;

	pci = &ultra->pci;
	pci->dev = dev;
	pci->ops = &dw_pcie_ops;

	/* Set a default value suitable for at most 16 in and 16 out windows */
	pci->atu_size = SZ_8K;

	pp = &pci->pp;

	platform_set_drvdata(pdev, ultra);

	ret = ultrarisc_pcie_init_clks(ultra);
	if (ret)
		return ret;

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
	struct ultrarisc_pcie *ultra = dev_get_drvdata(dev);
	struct dw_pcie *pci = &ultra->pci;
	int ret;

	/*
	 * A failed resume leaves the DWC suspended and the clocks disabled.
	 * A later suspend must not access the controller or disable them again.
	 */
	if (pci->suspended)
		return 0;

	ret = dw_pcie_suspend_noirq(pci);
	if (ret)
		return ret;

	if (pci->suspended)
		ultrarisc_pcie_disable_clks(ultra);

	return 0;
}

static int ultrarisc_pcie_resume_noirq(struct device *dev)
{
	struct ultrarisc_pcie *ultra = dev_get_drvdata(dev);
	struct dw_pcie *pci = &ultra->pci;
	int ret;

	if (pci->suspended) {
		ret = ultrarisc_pcie_enable_clks(ultra);
		if (ret)
			return ret;

		ret = dw_pcie_resume_noirq(pci);
		if (ret) {
			ultrarisc_pcie_disable_clks(ultra);
			return ret;
		}
	}

	return 0;
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
module_platform_driver(ultrarisc_pcie_driver);

MODULE_DESCRIPTION("UltraRISC DP1000 DWC PCIe host controller");
MODULE_LICENSE("GPL");
