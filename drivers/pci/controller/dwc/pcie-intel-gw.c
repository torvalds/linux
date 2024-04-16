// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for Intel Gateway SoCs
 *
 * Copyright (c) 2019 Intel Corporation.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/iopoll.h>
#include <linux/pci_regs.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include "../../pci.h"
#include "pcie-designware.h"

#define PORT_AFR_N_FTS_GEN12_DFT	(SZ_128 - 1)
#define PORT_AFR_N_FTS_GEN3		180
#define PORT_AFR_N_FTS_GEN4		196

/* PCIe Application logic Registers */
#define PCIE_APP_CCR			0x10
#define PCIE_APP_CCR_LTSSM_ENABLE	BIT(0)

#define PCIE_APP_MSG_CR			0x30
#define PCIE_APP_MSG_XMT_PM_TURNOFF	BIT(0)

#define PCIE_APP_PMC			0x44
#define PCIE_APP_PMC_IN_L2		BIT(20)

#define PCIE_APP_IRNEN			0xF4
#define PCIE_APP_IRNCR			0xF8
#define PCIE_APP_IRN_AER_REPORT		BIT(0)
#define PCIE_APP_IRN_PME		BIT(2)
#define PCIE_APP_IRN_RX_VDM_MSG		BIT(4)
#define PCIE_APP_IRN_PM_TO_ACK		BIT(9)
#define PCIE_APP_IRN_LINK_AUTO_BW_STAT	BIT(11)
#define PCIE_APP_IRN_BW_MGT		BIT(12)
#define PCIE_APP_IRN_INTA		BIT(13)
#define PCIE_APP_IRN_INTB		BIT(14)
#define PCIE_APP_IRN_INTC		BIT(15)
#define PCIE_APP_IRN_INTD		BIT(16)
#define PCIE_APP_IRN_MSG_LTR		BIT(18)
#define PCIE_APP_IRN_SYS_ERR_RC		BIT(29)
#define PCIE_APP_INTX_OFST		12

#define PCIE_APP_IRN_INT \
	(PCIE_APP_IRN_AER_REPORT | PCIE_APP_IRN_PME | \
	PCIE_APP_IRN_RX_VDM_MSG | PCIE_APP_IRN_SYS_ERR_RC | \
	PCIE_APP_IRN_PM_TO_ACK | PCIE_APP_IRN_MSG_LTR | \
	PCIE_APP_IRN_BW_MGT | PCIE_APP_IRN_LINK_AUTO_BW_STAT | \
	PCIE_APP_IRN_INTA | PCIE_APP_IRN_INTB | \
	PCIE_APP_IRN_INTC | PCIE_APP_IRN_INTD)

#define BUS_IATU_OFFSET			SZ_256M
#define RESET_INTERVAL_MS		100

struct intel_pcie {
	struct dw_pcie		pci;
	void __iomem		*app_base;
	struct gpio_desc	*reset_gpio;
	u32			rst_intrvl;
	struct clk		*core_clk;
	struct reset_control	*core_rst;
	struct phy		*phy;
};

static void pcie_update_bits(void __iomem *base, u32 ofs, u32 mask, u32 val)
{
	u32 old;

	old = readl(base + ofs);
	val = (old & ~mask) | (val & mask);

	if (val != old)
		writel(val, base + ofs);
}

static inline void pcie_app_wr(struct intel_pcie *pcie, u32 ofs, u32 val)
{
	writel(val, pcie->app_base + ofs);
}

static void pcie_app_wr_mask(struct intel_pcie *pcie, u32 ofs,
			     u32 mask, u32 val)
{
	pcie_update_bits(pcie->app_base, ofs, mask, val);
}

static inline u32 pcie_rc_cfg_rd(struct intel_pcie *pcie, u32 ofs)
{
	return dw_pcie_readl_dbi(&pcie->pci, ofs);
}

static inline void pcie_rc_cfg_wr(struct intel_pcie *pcie, u32 ofs, u32 val)
{
	dw_pcie_writel_dbi(&pcie->pci, ofs, val);
}

static void pcie_rc_cfg_wr_mask(struct intel_pcie *pcie, u32 ofs,
				u32 mask, u32 val)
{
	pcie_update_bits(pcie->pci.dbi_base, ofs, mask, val);
}

static void intel_pcie_ltssm_enable(struct intel_pcie *pcie)
{
	pcie_app_wr_mask(pcie, PCIE_APP_CCR, PCIE_APP_CCR_LTSSM_ENABLE,
			 PCIE_APP_CCR_LTSSM_ENABLE);
}

static void intel_pcie_ltssm_disable(struct intel_pcie *pcie)
{
	pcie_app_wr_mask(pcie, PCIE_APP_CCR, PCIE_APP_CCR_LTSSM_ENABLE, 0);
}

static void intel_pcie_link_setup(struct intel_pcie *pcie)
{
	u32 val;
	u8 offset = dw_pcie_find_capability(&pcie->pci, PCI_CAP_ID_EXP);

	val = pcie_rc_cfg_rd(pcie, offset + PCI_EXP_LNKCTL);

	val &= ~(PCI_EXP_LNKCTL_LD | PCI_EXP_LNKCTL_ASPMC);
	pcie_rc_cfg_wr(pcie, offset + PCI_EXP_LNKCTL, val);
}

static void intel_pcie_init_n_fts(struct dw_pcie *pci)
{
	switch (pci->link_gen) {
	case 3:
		pci->n_fts[1] = PORT_AFR_N_FTS_GEN3;
		break;
	case 4:
		pci->n_fts[1] = PORT_AFR_N_FTS_GEN4;
		break;
	default:
		pci->n_fts[1] = PORT_AFR_N_FTS_GEN12_DFT;
		break;
	}
	pci->n_fts[0] = PORT_AFR_N_FTS_GEN12_DFT;
}

static int intel_pcie_ep_rst_init(struct intel_pcie *pcie)
{
	struct device *dev = pcie->pci.dev;
	int ret;

	pcie->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(pcie->reset_gpio)) {
		ret = PTR_ERR(pcie->reset_gpio);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to request PCIe GPIO: %d\n", ret);
		return ret;
	}

	/* Make initial reset last for 100us */
	usleep_range(100, 200);

	return 0;
}

static void intel_pcie_core_rst_assert(struct intel_pcie *pcie)
{
	reset_control_assert(pcie->core_rst);
}

static void intel_pcie_core_rst_deassert(struct intel_pcie *pcie)
{
	/*
	 * One micro-second delay to make sure the reset pulse
	 * wide enough so that core reset is clean.
	 */
	udelay(1);
	reset_control_deassert(pcie->core_rst);

	/*
	 * Some SoC core reset also reset PHY, more delay needed
	 * to make sure the reset process is done.
	 */
	usleep_range(1000, 2000);
}

static void intel_pcie_device_rst_assert(struct intel_pcie *pcie)
{
	gpiod_set_value_cansleep(pcie->reset_gpio, 1);
}

static void intel_pcie_device_rst_deassert(struct intel_pcie *pcie)
{
	msleep(pcie->rst_intrvl);
	gpiod_set_value_cansleep(pcie->reset_gpio, 0);
}

static void intel_pcie_core_irq_disable(struct intel_pcie *pcie)
{
	pcie_app_wr(pcie, PCIE_APP_IRNEN, 0);
	pcie_app_wr(pcie, PCIE_APP_IRNCR, PCIE_APP_IRN_INT);
}

static int intel_pcie_get_resources(struct platform_device *pdev)
{
	struct intel_pcie *pcie = platform_get_drvdata(pdev);
	struct dw_pcie *pci = &pcie->pci;
	struct device *dev = pci->dev;
	int ret;

	pcie->core_clk = devm_clk_get(dev, NULL);
	if (IS_ERR(pcie->core_clk)) {
		ret = PTR_ERR(pcie->core_clk);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get clks: %d\n", ret);
		return ret;
	}

	pcie->core_rst = devm_reset_control_get(dev, NULL);
	if (IS_ERR(pcie->core_rst)) {
		ret = PTR_ERR(pcie->core_rst);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get resets: %d\n", ret);
		return ret;
	}

	ret = device_property_read_u32(dev, "reset-assert-ms",
				       &pcie->rst_intrvl);
	if (ret)
		pcie->rst_intrvl = RESET_INTERVAL_MS;

	pcie->app_base = devm_platform_ioremap_resource_byname(pdev, "app");
	if (IS_ERR(pcie->app_base))
		return PTR_ERR(pcie->app_base);

	pcie->phy = devm_phy_get(dev, "pcie");
	if (IS_ERR(pcie->phy)) {
		ret = PTR_ERR(pcie->phy);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Couldn't get pcie-phy: %d\n", ret);
		return ret;
	}

	return 0;
}

static int intel_pcie_wait_l2(struct intel_pcie *pcie)
{
	u32 value;
	int ret;
	struct dw_pcie *pci = &pcie->pci;

	if (pci->link_gen < 3)
		return 0;

	/* Send PME_TURN_OFF message */
	pcie_app_wr_mask(pcie, PCIE_APP_MSG_CR, PCIE_APP_MSG_XMT_PM_TURNOFF,
			 PCIE_APP_MSG_XMT_PM_TURNOFF);

	/* Read PMC status and wait for falling into L2 link state */
	ret = readl_poll_timeout(pcie->app_base + PCIE_APP_PMC, value,
				 value & PCIE_APP_PMC_IN_L2, 20,
				 jiffies_to_usecs(5 * HZ));
	if (ret)
		dev_err(pcie->pci.dev, "PCIe link enter L2 timeout!\n");

	return ret;
}

static void intel_pcie_turn_off(struct intel_pcie *pcie)
{
	if (dw_pcie_link_up(&pcie->pci))
		intel_pcie_wait_l2(pcie);

	/* Put endpoint device in reset state */
	intel_pcie_device_rst_assert(pcie);
	pcie_rc_cfg_wr_mask(pcie, PCI_COMMAND, PCI_COMMAND_MEMORY, 0);
}

static int intel_pcie_host_setup(struct intel_pcie *pcie)
{
	int ret;
	struct dw_pcie *pci = &pcie->pci;

	intel_pcie_core_rst_assert(pcie);
	intel_pcie_device_rst_assert(pcie);

	ret = phy_init(pcie->phy);
	if (ret)
		return ret;

	intel_pcie_core_rst_deassert(pcie);

	ret = clk_prepare_enable(pcie->core_clk);
	if (ret) {
		dev_err(pcie->pci.dev, "Core clock enable failed: %d\n", ret);
		goto clk_err;
	}

	pci->atu_base = pci->dbi_base + 0xC0000;

	intel_pcie_ltssm_disable(pcie);
	intel_pcie_link_setup(pcie);
	intel_pcie_init_n_fts(pci);

	ret = dw_pcie_setup_rc(&pci->pp);
	if (ret)
		goto app_init_err;

	dw_pcie_upconfig_setup(pci);

	intel_pcie_device_rst_deassert(pcie);
	intel_pcie_ltssm_enable(pcie);

	ret = dw_pcie_wait_for_link(pci);
	if (ret)
		goto app_init_err;

	/* Enable integrated interrupts */
	pcie_app_wr_mask(pcie, PCIE_APP_IRNEN, PCIE_APP_IRN_INT,
			 PCIE_APP_IRN_INT);

	return 0;

app_init_err:
	clk_disable_unprepare(pcie->core_clk);
clk_err:
	intel_pcie_core_rst_assert(pcie);
	phy_exit(pcie->phy);

	return ret;
}

static void __intel_pcie_remove(struct intel_pcie *pcie)
{
	intel_pcie_core_irq_disable(pcie);
	intel_pcie_turn_off(pcie);
	clk_disable_unprepare(pcie->core_clk);
	intel_pcie_core_rst_assert(pcie);
	phy_exit(pcie->phy);
}

static int intel_pcie_remove(struct platform_device *pdev)
{
	struct intel_pcie *pcie = platform_get_drvdata(pdev);
	struct dw_pcie_rp *pp = &pcie->pci.pp;

	dw_pcie_host_deinit(pp);
	__intel_pcie_remove(pcie);

	return 0;
}

static int intel_pcie_suspend_noirq(struct device *dev)
{
	struct intel_pcie *pcie = dev_get_drvdata(dev);
	int ret;

	intel_pcie_core_irq_disable(pcie);
	ret = intel_pcie_wait_l2(pcie);
	if (ret)
		return ret;

	phy_exit(pcie->phy);
	clk_disable_unprepare(pcie->core_clk);
	return ret;
}

static int intel_pcie_resume_noirq(struct device *dev)
{
	struct intel_pcie *pcie = dev_get_drvdata(dev);

	return intel_pcie_host_setup(pcie);
}

static int intel_pcie_rc_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct intel_pcie *pcie = dev_get_drvdata(pci->dev);

	return intel_pcie_host_setup(pcie);
}

static u64 intel_pcie_cpu_addr(struct dw_pcie *pcie, u64 cpu_addr)
{
	return cpu_addr + BUS_IATU_OFFSET;
}

static const struct dw_pcie_ops intel_pcie_ops = {
	.cpu_addr_fixup = intel_pcie_cpu_addr,
};

static const struct dw_pcie_host_ops intel_pcie_dw_ops = {
	.host_init =		intel_pcie_rc_init,
};

static int intel_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct intel_pcie *pcie;
	struct dw_pcie_rp *pp;
	struct dw_pcie *pci;
	int ret;

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	platform_set_drvdata(pdev, pcie);
	pci = &pcie->pci;
	pci->dev = dev;
	pp = &pci->pp;

	ret = intel_pcie_get_resources(pdev);
	if (ret)
		return ret;

	ret = intel_pcie_ep_rst_init(pcie);
	if (ret)
		return ret;

	pci->ops = &intel_pcie_ops;
	pp->ops = &intel_pcie_dw_ops;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "Cannot initialize host\n");
		return ret;
	}

	return 0;
}

static const struct dev_pm_ops intel_pcie_pm_ops = {
	NOIRQ_SYSTEM_SLEEP_PM_OPS(intel_pcie_suspend_noirq,
				  intel_pcie_resume_noirq)
};

static const struct of_device_id of_intel_pcie_match[] = {
	{ .compatible = "intel,lgm-pcie" },
	{}
};

static struct platform_driver intel_pcie_driver = {
	.probe = intel_pcie_probe,
	.remove = intel_pcie_remove,
	.driver = {
		.name = "intel-gw-pcie",
		.of_match_table = of_intel_pcie_match,
		.pm = &intel_pcie_pm_ops,
	},
};
builtin_platform_driver(intel_pcie_driver);
