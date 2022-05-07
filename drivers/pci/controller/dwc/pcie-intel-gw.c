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
#define PCIE_APP_IRN_MSG_LTR		BIT(18)
#define PCIE_APP_IRN_SYS_ERR_RC		BIT(29)
#define PCIE_APP_INTX_OFST		12

#define PCIE_APP_IRN_INT \
	(PCIE_APP_IRN_AER_REPORT | PCIE_APP_IRN_PME | \
	PCIE_APP_IRN_RX_VDM_MSG | PCIE_APP_IRN_SYS_ERR_RC | \
	PCIE_APP_IRN_PM_TO_ACK | PCIE_APP_IRN_MSG_LTR | \
	PCIE_APP_IRN_BW_MGT | PCIE_APP_IRN_LINK_AUTO_BW_STAT | \
	(PCIE_APP_INTX_OFST + PCI_INTERRUPT_INTA) | \
	(PCIE_APP_INTX_OFST + PCI_INTERRUPT_INTB) | \
	(PCIE_APP_INTX_OFST + PCI_INTERRUPT_INTC) | \
	(PCIE_APP_INTX_OFST + PCI_INTERRUPT_INTD))

#define BUS_IATU_OFFSET			SZ_256M
#define RESET_INTERVAL_MS		100

struct intel_pcie_soc {
	unsigned int	pcie_ver;
	unsigned int	pcie_atu_offset;
	u32		num_viewport;
};

struct intel_pcie_port {
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

static inline u32 pcie_app_rd(struct intel_pcie_port *lpp, u32 ofs)
{
	return readl(lpp->app_base + ofs);
}

static inline void pcie_app_wr(struct intel_pcie_port *lpp, u32 ofs, u32 val)
{
	writel(val, lpp->app_base + ofs);
}

static void pcie_app_wr_mask(struct intel_pcie_port *lpp, u32 ofs,
			     u32 mask, u32 val)
{
	pcie_update_bits(lpp->app_base, ofs, mask, val);
}

static inline u32 pcie_rc_cfg_rd(struct intel_pcie_port *lpp, u32 ofs)
{
	return dw_pcie_readl_dbi(&lpp->pci, ofs);
}

static inline void pcie_rc_cfg_wr(struct intel_pcie_port *lpp, u32 ofs, u32 val)
{
	dw_pcie_writel_dbi(&lpp->pci, ofs, val);
}

static void pcie_rc_cfg_wr_mask(struct intel_pcie_port *lpp, u32 ofs,
				u32 mask, u32 val)
{
	pcie_update_bits(lpp->pci.dbi_base, ofs, mask, val);
}

static void intel_pcie_ltssm_enable(struct intel_pcie_port *lpp)
{
	pcie_app_wr_mask(lpp, PCIE_APP_CCR, PCIE_APP_CCR_LTSSM_ENABLE,
			 PCIE_APP_CCR_LTSSM_ENABLE);
}

static void intel_pcie_ltssm_disable(struct intel_pcie_port *lpp)
{
	pcie_app_wr_mask(lpp, PCIE_APP_CCR, PCIE_APP_CCR_LTSSM_ENABLE, 0);
}

static void intel_pcie_link_setup(struct intel_pcie_port *lpp)
{
	u32 val;
	u8 offset = dw_pcie_find_capability(&lpp->pci, PCI_CAP_ID_EXP);

	val = pcie_rc_cfg_rd(lpp, offset + PCI_EXP_LNKCTL);

	val &= ~(PCI_EXP_LNKCTL_LD | PCI_EXP_LNKCTL_ASPMC);
	pcie_rc_cfg_wr(lpp, offset + PCI_EXP_LNKCTL, val);
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

static void intel_pcie_rc_setup(struct intel_pcie_port *lpp)
{
	intel_pcie_ltssm_disable(lpp);
	intel_pcie_link_setup(lpp);
	intel_pcie_init_n_fts(&lpp->pci);
	dw_pcie_setup_rc(&lpp->pci.pp);
	dw_pcie_upconfig_setup(&lpp->pci);
}

static int intel_pcie_ep_rst_init(struct intel_pcie_port *lpp)
{
	struct device *dev = lpp->pci.dev;
	int ret;

	lpp->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(lpp->reset_gpio)) {
		ret = PTR_ERR(lpp->reset_gpio);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to request PCIe GPIO: %d\n", ret);
		return ret;
	}

	/* Make initial reset last for 100us */
	usleep_range(100, 200);

	return 0;
}

static void intel_pcie_core_rst_assert(struct intel_pcie_port *lpp)
{
	reset_control_assert(lpp->core_rst);
}

static void intel_pcie_core_rst_deassert(struct intel_pcie_port *lpp)
{
	/*
	 * One micro-second delay to make sure the reset pulse
	 * wide enough so that core reset is clean.
	 */
	udelay(1);
	reset_control_deassert(lpp->core_rst);

	/*
	 * Some SoC core reset also reset PHY, more delay needed
	 * to make sure the reset process is done.
	 */
	usleep_range(1000, 2000);
}

static void intel_pcie_device_rst_assert(struct intel_pcie_port *lpp)
{
	gpiod_set_value_cansleep(lpp->reset_gpio, 1);
}

static void intel_pcie_device_rst_deassert(struct intel_pcie_port *lpp)
{
	msleep(lpp->rst_intrvl);
	gpiod_set_value_cansleep(lpp->reset_gpio, 0);
}

static int intel_pcie_app_logic_setup(struct intel_pcie_port *lpp)
{
	intel_pcie_device_rst_deassert(lpp);
	intel_pcie_ltssm_enable(lpp);

	return dw_pcie_wait_for_link(&lpp->pci);
}

static void intel_pcie_core_irq_disable(struct intel_pcie_port *lpp)
{
	pcie_app_wr(lpp, PCIE_APP_IRNEN, 0);
	pcie_app_wr(lpp, PCIE_APP_IRNCR, PCIE_APP_IRN_INT);
}

static int intel_pcie_get_resources(struct platform_device *pdev)
{
	struct intel_pcie_port *lpp = platform_get_drvdata(pdev);
	struct dw_pcie *pci = &lpp->pci;
	struct device *dev = pci->dev;
	int ret;

	pci->dbi_base = devm_platform_ioremap_resource_byname(pdev, "dbi");
	if (IS_ERR(pci->dbi_base))
		return PTR_ERR(pci->dbi_base);

	lpp->core_clk = devm_clk_get(dev, NULL);
	if (IS_ERR(lpp->core_clk)) {
		ret = PTR_ERR(lpp->core_clk);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get clks: %d\n", ret);
		return ret;
	}

	lpp->core_rst = devm_reset_control_get(dev, NULL);
	if (IS_ERR(lpp->core_rst)) {
		ret = PTR_ERR(lpp->core_rst);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get resets: %d\n", ret);
		return ret;
	}

	ret = device_property_read_u32(dev, "reset-assert-ms",
				       &lpp->rst_intrvl);
	if (ret)
		lpp->rst_intrvl = RESET_INTERVAL_MS;

	lpp->app_base = devm_platform_ioremap_resource_byname(pdev, "app");
	if (IS_ERR(lpp->app_base))
		return PTR_ERR(lpp->app_base);

	lpp->phy = devm_phy_get(dev, "pcie");
	if (IS_ERR(lpp->phy)) {
		ret = PTR_ERR(lpp->phy);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Couldn't get pcie-phy: %d\n", ret);
		return ret;
	}

	return 0;
}

static void intel_pcie_deinit_phy(struct intel_pcie_port *lpp)
{
	phy_exit(lpp->phy);
}

static int intel_pcie_wait_l2(struct intel_pcie_port *lpp)
{
	u32 value;
	int ret;
	struct dw_pcie *pci = &lpp->pci;

	if (pci->link_gen < 3)
		return 0;

	/* Send PME_TURN_OFF message */
	pcie_app_wr_mask(lpp, PCIE_APP_MSG_CR, PCIE_APP_MSG_XMT_PM_TURNOFF,
			 PCIE_APP_MSG_XMT_PM_TURNOFF);

	/* Read PMC status and wait for falling into L2 link state */
	ret = readl_poll_timeout(lpp->app_base + PCIE_APP_PMC, value,
				 value & PCIE_APP_PMC_IN_L2, 20,
				 jiffies_to_usecs(5 * HZ));
	if (ret)
		dev_err(lpp->pci.dev, "PCIe link enter L2 timeout!\n");

	return ret;
}

static void intel_pcie_turn_off(struct intel_pcie_port *lpp)
{
	if (dw_pcie_link_up(&lpp->pci))
		intel_pcie_wait_l2(lpp);

	/* Put endpoint device in reset state */
	intel_pcie_device_rst_assert(lpp);
	pcie_rc_cfg_wr_mask(lpp, PCI_COMMAND, PCI_COMMAND_MEMORY, 0);
}

static int intel_pcie_host_setup(struct intel_pcie_port *lpp)
{
	int ret;

	intel_pcie_core_rst_assert(lpp);
	intel_pcie_device_rst_assert(lpp);

	ret = phy_init(lpp->phy);
	if (ret)
		return ret;

	intel_pcie_core_rst_deassert(lpp);

	ret = clk_prepare_enable(lpp->core_clk);
	if (ret) {
		dev_err(lpp->pci.dev, "Core clock enable failed: %d\n", ret);
		goto clk_err;
	}

	intel_pcie_rc_setup(lpp);
	ret = intel_pcie_app_logic_setup(lpp);
	if (ret)
		goto app_init_err;

	/* Enable integrated interrupts */
	pcie_app_wr_mask(lpp, PCIE_APP_IRNEN, PCIE_APP_IRN_INT,
			 PCIE_APP_IRN_INT);

	return 0;

app_init_err:
	clk_disable_unprepare(lpp->core_clk);
clk_err:
	intel_pcie_core_rst_assert(lpp);
	intel_pcie_deinit_phy(lpp);

	return ret;
}

static void __intel_pcie_remove(struct intel_pcie_port *lpp)
{
	intel_pcie_core_irq_disable(lpp);
	intel_pcie_turn_off(lpp);
	clk_disable_unprepare(lpp->core_clk);
	intel_pcie_core_rst_assert(lpp);
	intel_pcie_deinit_phy(lpp);
}

static int intel_pcie_remove(struct platform_device *pdev)
{
	struct intel_pcie_port *lpp = platform_get_drvdata(pdev);
	struct pcie_port *pp = &lpp->pci.pp;

	dw_pcie_host_deinit(pp);
	__intel_pcie_remove(lpp);

	return 0;
}

static int __maybe_unused intel_pcie_suspend_noirq(struct device *dev)
{
	struct intel_pcie_port *lpp = dev_get_drvdata(dev);
	int ret;

	intel_pcie_core_irq_disable(lpp);
	ret = intel_pcie_wait_l2(lpp);
	if (ret)
		return ret;

	intel_pcie_deinit_phy(lpp);
	clk_disable_unprepare(lpp->core_clk);
	return ret;
}

static int __maybe_unused intel_pcie_resume_noirq(struct device *dev)
{
	struct intel_pcie_port *lpp = dev_get_drvdata(dev);

	return intel_pcie_host_setup(lpp);
}

static int intel_pcie_rc_init(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct intel_pcie_port *lpp = dev_get_drvdata(pci->dev);

	return intel_pcie_host_setup(lpp);
}

/*
 * Dummy function so that DW core doesn't configure MSI
 */
static int intel_pcie_msi_init(struct pcie_port *pp)
{
	return 0;
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
	.msi_host_init =	intel_pcie_msi_init,
};

static const struct intel_pcie_soc pcie_data = {
	.pcie_ver =		0x520A,
	.pcie_atu_offset =	0xC0000,
	.num_viewport =		3,
};

static int intel_pcie_probe(struct platform_device *pdev)
{
	const struct intel_pcie_soc *data;
	struct device *dev = &pdev->dev;
	struct intel_pcie_port *lpp;
	struct pcie_port *pp;
	struct dw_pcie *pci;
	int ret;

	lpp = devm_kzalloc(dev, sizeof(*lpp), GFP_KERNEL);
	if (!lpp)
		return -ENOMEM;

	platform_set_drvdata(pdev, lpp);
	pci = &lpp->pci;
	pci->dev = dev;
	pp = &pci->pp;

	ret = intel_pcie_get_resources(pdev);
	if (ret)
		return ret;

	ret = intel_pcie_ep_rst_init(lpp);
	if (ret)
		return ret;

	data = device_get_match_data(dev);
	if (!data)
		return -ENODEV;

	pci->ops = &intel_pcie_ops;
	pci->version = data->pcie_ver;
	pci->atu_base = pci->dbi_base + data->pcie_atu_offset;
	pp->ops = &intel_pcie_dw_ops;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "Cannot initialize host\n");
		return ret;
	}

	/*
	 * Intel PCIe doesn't configure IO region, so set viewport
	 * to not perform IO region access.
	 */
	pci->num_viewport = data->num_viewport;

	return 0;
}

static const struct dev_pm_ops intel_pcie_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(intel_pcie_suspend_noirq,
				      intel_pcie_resume_noirq)
};

static const struct of_device_id of_intel_pcie_match[] = {
	{ .compatible = "intel,lgm-pcie", .data = &pcie_data },
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
