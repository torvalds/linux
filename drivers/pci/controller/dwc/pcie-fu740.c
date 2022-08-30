// SPDX-License-Identifier: GPL-2.0
/*
 * FU740 DesignWare PCIe Controller integration
 * Copyright (C) 2019-2021 SiFive, Inc.
 * Paul Walmsley
 * Greentime Hu
 *
 * Based in part on the i.MX6 PCIe host controller shim which is:
 *
 * Copyright (C) 2013 Kosagi
 *		https://www.kosagi.com
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/reset.h>

#include "pcie-designware.h"

#define to_fu740_pcie(x)	dev_get_drvdata((x)->dev)

struct fu740_pcie {
	struct dw_pcie pci;
	void __iomem *mgmt_base;
	struct gpio_desc *reset;
	struct gpio_desc *pwren;
	struct clk *pcie_aux;
	struct reset_control *rst;
};

#define SIFIVE_DEVICESRESETREG		0x28

#define PCIEX8MGMT_PERST_N		0x0
#define PCIEX8MGMT_APP_LTSSM_ENABLE	0x10
#define PCIEX8MGMT_APP_HOLD_PHY_RST	0x18
#define PCIEX8MGMT_DEVICE_TYPE		0x708
#define PCIEX8MGMT_PHY0_CR_PARA_ADDR	0x860
#define PCIEX8MGMT_PHY0_CR_PARA_RD_EN	0x870
#define PCIEX8MGMT_PHY0_CR_PARA_RD_DATA	0x878
#define PCIEX8MGMT_PHY0_CR_PARA_SEL	0x880
#define PCIEX8MGMT_PHY0_CR_PARA_WR_DATA	0x888
#define PCIEX8MGMT_PHY0_CR_PARA_WR_EN	0x890
#define PCIEX8MGMT_PHY0_CR_PARA_ACK	0x898
#define PCIEX8MGMT_PHY1_CR_PARA_ADDR	0x8a0
#define PCIEX8MGMT_PHY1_CR_PARA_RD_EN	0x8b0
#define PCIEX8MGMT_PHY1_CR_PARA_RD_DATA	0x8b8
#define PCIEX8MGMT_PHY1_CR_PARA_SEL	0x8c0
#define PCIEX8MGMT_PHY1_CR_PARA_WR_DATA	0x8c8
#define PCIEX8MGMT_PHY1_CR_PARA_WR_EN	0x8d0
#define PCIEX8MGMT_PHY1_CR_PARA_ACK	0x8d8

#define PCIEX8MGMT_PHY_CDR_TRACK_EN	BIT(0)
#define PCIEX8MGMT_PHY_LOS_THRSHLD	BIT(5)
#define PCIEX8MGMT_PHY_TERM_EN		BIT(9)
#define PCIEX8MGMT_PHY_TERM_ACDC	BIT(10)
#define PCIEX8MGMT_PHY_EN		BIT(11)
#define PCIEX8MGMT_PHY_INIT_VAL		(PCIEX8MGMT_PHY_CDR_TRACK_EN|\
					 PCIEX8MGMT_PHY_LOS_THRSHLD|\
					 PCIEX8MGMT_PHY_TERM_EN|\
					 PCIEX8MGMT_PHY_TERM_ACDC|\
					 PCIEX8MGMT_PHY_EN)

#define PCIEX8MGMT_PHY_LANEN_DIG_ASIC_RX_OVRD_IN_3	0x1008
#define PCIEX8MGMT_PHY_LANE_OFF		0x100
#define PCIEX8MGMT_PHY_LANE0_BASE	(PCIEX8MGMT_PHY_LANEN_DIG_ASIC_RX_OVRD_IN_3 + 0x100 * 0)
#define PCIEX8MGMT_PHY_LANE1_BASE	(PCIEX8MGMT_PHY_LANEN_DIG_ASIC_RX_OVRD_IN_3 + 0x100 * 1)
#define PCIEX8MGMT_PHY_LANE2_BASE	(PCIEX8MGMT_PHY_LANEN_DIG_ASIC_RX_OVRD_IN_3 + 0x100 * 2)
#define PCIEX8MGMT_PHY_LANE3_BASE	(PCIEX8MGMT_PHY_LANEN_DIG_ASIC_RX_OVRD_IN_3 + 0x100 * 3)

static void fu740_pcie_assert_reset(struct fu740_pcie *afp)
{
	/* Assert PERST_N GPIO */
	gpiod_set_value_cansleep(afp->reset, 0);
	/* Assert controller PERST_N */
	writel_relaxed(0x0, afp->mgmt_base + PCIEX8MGMT_PERST_N);
}

static void fu740_pcie_deassert_reset(struct fu740_pcie *afp)
{
	/* Deassert controller PERST_N */
	writel_relaxed(0x1, afp->mgmt_base + PCIEX8MGMT_PERST_N);
	/* Deassert PERST_N GPIO */
	gpiod_set_value_cansleep(afp->reset, 1);
}

static void fu740_pcie_power_on(struct fu740_pcie *afp)
{
	gpiod_set_value_cansleep(afp->pwren, 1);
	/*
	 * Ensure that PERST has been asserted for at least 100 ms.
	 * Section 2.2 of PCI Express Card Electromechanical Specification
	 * Revision 3.0
	 */
	msleep(100);
}

static void fu740_pcie_drive_reset(struct fu740_pcie *afp)
{
	fu740_pcie_assert_reset(afp);
	fu740_pcie_power_on(afp);
	fu740_pcie_deassert_reset(afp);
}

static void fu740_phyregwrite(const uint8_t phy, const uint16_t addr,
			      const uint16_t wrdata, struct fu740_pcie *afp)
{
	struct device *dev = afp->pci.dev;
	void __iomem *phy_cr_para_addr;
	void __iomem *phy_cr_para_wr_data;
	void __iomem *phy_cr_para_wr_en;
	void __iomem *phy_cr_para_ack;
	int ret, val;

	/* Setup */
	if (phy) {
		phy_cr_para_addr = afp->mgmt_base + PCIEX8MGMT_PHY1_CR_PARA_ADDR;
		phy_cr_para_wr_data = afp->mgmt_base + PCIEX8MGMT_PHY1_CR_PARA_WR_DATA;
		phy_cr_para_wr_en = afp->mgmt_base + PCIEX8MGMT_PHY1_CR_PARA_WR_EN;
		phy_cr_para_ack = afp->mgmt_base + PCIEX8MGMT_PHY1_CR_PARA_ACK;
	} else {
		phy_cr_para_addr = afp->mgmt_base + PCIEX8MGMT_PHY0_CR_PARA_ADDR;
		phy_cr_para_wr_data = afp->mgmt_base + PCIEX8MGMT_PHY0_CR_PARA_WR_DATA;
		phy_cr_para_wr_en = afp->mgmt_base + PCIEX8MGMT_PHY0_CR_PARA_WR_EN;
		phy_cr_para_ack = afp->mgmt_base + PCIEX8MGMT_PHY0_CR_PARA_ACK;
	}

	writel_relaxed(addr, phy_cr_para_addr);
	writel_relaxed(wrdata, phy_cr_para_wr_data);
	writel_relaxed(1, phy_cr_para_wr_en);

	/* Wait for wait_idle */
	ret = readl_poll_timeout(phy_cr_para_ack, val, val, 10, 5000);
	if (ret)
		dev_warn(dev, "Wait for wait_idle state failed!\n");

	/* Clear */
	writel_relaxed(0, phy_cr_para_wr_en);

	/* Wait for ~wait_idle */
	ret = readl_poll_timeout(phy_cr_para_ack, val, !val, 10, 5000);
	if (ret)
		dev_warn(dev, "Wait for !wait_idle state failed!\n");
}

static void fu740_pcie_init_phy(struct fu740_pcie *afp)
{
	/* Enable phy cr_para_sel interfaces */
	writel_relaxed(0x1, afp->mgmt_base + PCIEX8MGMT_PHY0_CR_PARA_SEL);
	writel_relaxed(0x1, afp->mgmt_base + PCIEX8MGMT_PHY1_CR_PARA_SEL);

	/*
	 * Wait 10 cr_para cycles to guarantee that the registers are ready
	 * to be edited.
	 */
	ndelay(10);

	/* Set PHY AC termination mode */
	fu740_phyregwrite(0, PCIEX8MGMT_PHY_LANE0_BASE, PCIEX8MGMT_PHY_INIT_VAL, afp);
	fu740_phyregwrite(0, PCIEX8MGMT_PHY_LANE1_BASE, PCIEX8MGMT_PHY_INIT_VAL, afp);
	fu740_phyregwrite(0, PCIEX8MGMT_PHY_LANE2_BASE, PCIEX8MGMT_PHY_INIT_VAL, afp);
	fu740_phyregwrite(0, PCIEX8MGMT_PHY_LANE3_BASE, PCIEX8MGMT_PHY_INIT_VAL, afp);
	fu740_phyregwrite(1, PCIEX8MGMT_PHY_LANE0_BASE, PCIEX8MGMT_PHY_INIT_VAL, afp);
	fu740_phyregwrite(1, PCIEX8MGMT_PHY_LANE1_BASE, PCIEX8MGMT_PHY_INIT_VAL, afp);
	fu740_phyregwrite(1, PCIEX8MGMT_PHY_LANE2_BASE, PCIEX8MGMT_PHY_INIT_VAL, afp);
	fu740_phyregwrite(1, PCIEX8MGMT_PHY_LANE3_BASE, PCIEX8MGMT_PHY_INIT_VAL, afp);
}

static int fu740_pcie_start_link(struct dw_pcie *pci)
{
	struct device *dev = pci->dev;
	struct fu740_pcie *afp = dev_get_drvdata(dev);
	u8 cap_exp = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	int ret;
	u32 orig, tmp;

	/*
	 * Force 2.5GT/s when starting the link, due to some devices not
	 * probing at higher speeds. This happens with the PCIe switch
	 * on the Unmatched board when U-Boot has not initialised the PCIe.
	 * The fix in U-Boot is to force 2.5GT/s, which then gets cleared
	 * by the soft reset done by this driver.
	 */
	dev_dbg(dev, "cap_exp at %x\n", cap_exp);
	dw_pcie_dbi_ro_wr_en(pci);

	tmp = dw_pcie_readl_dbi(pci, cap_exp + PCI_EXP_LNKCAP);
	orig = tmp & PCI_EXP_LNKCAP_SLS;
	tmp &= ~PCI_EXP_LNKCAP_SLS;
	tmp |= PCI_EXP_LNKCAP_SLS_2_5GB;
	dw_pcie_writel_dbi(pci, cap_exp + PCI_EXP_LNKCAP, tmp);

	/* Enable LTSSM */
	writel_relaxed(0x1, afp->mgmt_base + PCIEX8MGMT_APP_LTSSM_ENABLE);

	ret = dw_pcie_wait_for_link(pci);
	if (ret) {
		dev_err(dev, "error: link did not start\n");
		goto err;
	}

	tmp = dw_pcie_readl_dbi(pci, cap_exp + PCI_EXP_LNKCAP);
	if ((tmp & PCI_EXP_LNKCAP_SLS) != orig) {
		dev_dbg(dev, "changing speed back to original\n");

		tmp &= ~PCI_EXP_LNKCAP_SLS;
		tmp |= orig;
		dw_pcie_writel_dbi(pci, cap_exp + PCI_EXP_LNKCAP, tmp);

		tmp = dw_pcie_readl_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL);
		tmp |= PORT_LOGIC_SPEED_CHANGE;
		dw_pcie_writel_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, tmp);

		ret = dw_pcie_wait_for_link(pci);
		if (ret) {
			dev_err(dev, "error: link did not start at new speed\n");
			goto err;
		}
	}

	ret = 0;
err:
	WARN_ON(ret);	/* we assume that errors will be very rare */
	dw_pcie_dbi_ro_wr_dis(pci);
	return ret;
}

static int fu740_pcie_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct fu740_pcie *afp = to_fu740_pcie(pci);
	struct device *dev = pci->dev;
	int ret;

	/* Power on reset */
	fu740_pcie_drive_reset(afp);

	/* Enable pcieauxclk */
	ret = clk_prepare_enable(afp->pcie_aux);
	if (ret) {
		dev_err(dev, "unable to enable pcie_aux clock\n");
		return ret;
	}

	/*
	 * Assert hold_phy_rst (hold the controller LTSSM in reset after
	 * power_up_rst_n for register programming with cr_para)
	 */
	writel_relaxed(0x1, afp->mgmt_base + PCIEX8MGMT_APP_HOLD_PHY_RST);

	/* Deassert power_up_rst_n */
	ret = reset_control_deassert(afp->rst);
	if (ret) {
		dev_err(dev, "unable to deassert pcie_power_up_rst_n\n");
		return ret;
	}

	fu740_pcie_init_phy(afp);

	/* Disable pcieauxclk */
	clk_disable_unprepare(afp->pcie_aux);
	/* Clear hold_phy_rst */
	writel_relaxed(0x0, afp->mgmt_base + PCIEX8MGMT_APP_HOLD_PHY_RST);
	/* Enable pcieauxclk */
	clk_prepare_enable(afp->pcie_aux);
	/* Set RC mode */
	writel_relaxed(0x4, afp->mgmt_base + PCIEX8MGMT_DEVICE_TYPE);

	return 0;
}

static const struct dw_pcie_host_ops fu740_pcie_host_ops = {
	.host_init = fu740_pcie_host_init,
};

static const struct dw_pcie_ops dw_pcie_ops = {
	.start_link = fu740_pcie_start_link,
};

static int fu740_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_pcie *pci;
	struct fu740_pcie *afp;

	afp = devm_kzalloc(dev, sizeof(*afp), GFP_KERNEL);
	if (!afp)
		return -ENOMEM;
	pci = &afp->pci;
	pci->dev = dev;
	pci->ops = &dw_pcie_ops;
	pci->pp.ops = &fu740_pcie_host_ops;

	/* SiFive specific region: mgmt */
	afp->mgmt_base = devm_platform_ioremap_resource_byname(pdev, "mgmt");
	if (IS_ERR(afp->mgmt_base))
		return PTR_ERR(afp->mgmt_base);

	/* Fetch GPIOs */
	afp->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(afp->reset))
		return dev_err_probe(dev, PTR_ERR(afp->reset), "unable to get reset-gpios\n");

	afp->pwren = devm_gpiod_get_optional(dev, "pwren", GPIOD_OUT_LOW);
	if (IS_ERR(afp->pwren))
		return dev_err_probe(dev, PTR_ERR(afp->pwren), "unable to get pwren-gpios\n");

	/* Fetch clocks */
	afp->pcie_aux = devm_clk_get(dev, "pcie_aux");
	if (IS_ERR(afp->pcie_aux))
		return dev_err_probe(dev, PTR_ERR(afp->pcie_aux),
					     "pcie_aux clock source missing or invalid\n");

	/* Fetch reset */
	afp->rst = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(afp->rst))
		return dev_err_probe(dev, PTR_ERR(afp->rst), "unable to get reset\n");

	platform_set_drvdata(pdev, afp);

	return dw_pcie_host_init(&pci->pp);
}

static void fu740_pcie_shutdown(struct platform_device *pdev)
{
	struct fu740_pcie *afp = platform_get_drvdata(pdev);

	/* Bring down link, so bootloader gets clean state in case of reboot */
	fu740_pcie_assert_reset(afp);
}

static const struct of_device_id fu740_pcie_of_match[] = {
	{ .compatible = "sifive,fu740-pcie", },
	{},
};

static struct platform_driver fu740_pcie_driver = {
	.driver = {
		   .name = "fu740-pcie",
		   .of_match_table = fu740_pcie_of_match,
		   .suppress_bind_attrs = true,
	},
	.probe = fu740_pcie_probe,
	.shutdown = fu740_pcie_shutdown,
};

builtin_platform_driver(fu740_pcie_driver);
