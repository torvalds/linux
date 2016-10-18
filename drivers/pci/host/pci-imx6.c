/*
 * PCIe host controller driver for Freescale i.MX6 SoCs
 *
 * Copyright (C) 2013 Kosagi
 *		http://www.kosagi.com
 *
 * Author: Sean Cross <xobs@kosagi.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/imx6q-iomuxc-gpr.h>
#include <linux/mfd/syscon/imx7-iomuxc-gpr.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/of_address.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/resource.h>
#include <linux/signal.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/busfreq-imx.h>
#include <linux/regulator/consumer.h>

#include "pcie-designware.h"

#define to_imx6_pcie(x)	container_of(x, struct imx6_pcie, pp)

/*
 * The default value of the reserved ddr memory
 * used to verify EP/RC memory space access operations.
 * The layout of the 1G ddr on SD boards
 * [imx6qdl-sd-ard boards]0x1000_0000 ~ 0x4FFF_FFFF
 * [imx6sx,imx7d platforms]0x8000_0000 ~ 0xBFFF_FFFF
 *
 */
static u32 ddr_test_region = 0, test_region_size = SZ_2M;

struct imx6_pcie {
	u32 			ext_osc;
	int			dis_gpio;
	int			power_on_gpio;
	int			reset_gpio;
	struct clk		*pcie_bus;
	struct clk		*pcie_inbound_axi;
	struct clk		*pcie_phy;
	struct clk		*pcie;
	struct clk		*pcie_ext;
	struct clk		*pcie_ext_src;
	struct pcie_port	pp;
	struct regmap		*iomuxc_gpr;
	struct regmap		*reg_src;
	void __iomem		*mem_base;
	struct regulator	*pcie_phy_regulator;
	struct regulator	*pcie_bus_regulator;
};

/* PCIe Root Complex registers (memory-mapped) */
#define PCIE_RC_LCR				0x7c
#define PCIE_RC_LCR_MAX_LINK_SPEEDS_GEN1	0x1
#define PCIE_RC_LCR_MAX_LINK_SPEEDS_GEN2	0x2
#define PCIE_RC_LCR_MAX_LINK_SPEEDS_MASK	0xf

/* PCIe Port Logic registers (memory-mapped) */
#define PL_OFFSET 0x700
#define PCIE_PL_PFLR (PL_OFFSET + 0x08)
#define PCIE_PL_PFLR_LINK_STATE_MASK		(0x3f << 16)
#define PCIE_PL_PFLR_FORCE_LINK			(1 << 15)
#define PCIE_PHY_DEBUG_R0 (PL_OFFSET + 0x28)
#define PCIE_PHY_DEBUG_R1 (PL_OFFSET + 0x2c)
#define PCIE_PHY_DEBUG_R1_XMLH_LINK_IN_TRAINING	(1 << 29)
#define PCIE_PHY_DEBUG_R1_XMLH_LINK_UP		(1 << 4)

#define PCIE_PHY_CTRL (PL_OFFSET + 0x114)
#define PCIE_PHY_CTRL_DATA_LOC 0
#define PCIE_PHY_CTRL_CAP_ADR_LOC 16
#define PCIE_PHY_CTRL_CAP_DAT_LOC 17
#define PCIE_PHY_CTRL_WR_LOC 18
#define PCIE_PHY_CTRL_RD_LOC 19

#define PCIE_PHY_STAT (PL_OFFSET + 0x110)
#define PCIE_PHY_STAT_ACK_LOC 16

#define PCIE_LINK_WIDTH_SPEED_CONTROL	0x80C
#define PORT_LOGIC_SPEED_CHANGE		(0x1 << 17)

/* PHY registers (not memory-mapped) */
#define PCIE_PHY_RX_ASIC_OUT 0x100D

#define PHY_RX_OVRD_IN_LO 0x1005
#define PHY_RX_OVRD_IN_LO_RX_DATA_EN (1 << 5)
#define PHY_RX_OVRD_IN_LO_RX_PLL_EN (1 << 3)

#define SSP_CR_SUP_DIG_MPLL_OVRD_IN_LO 0x0011
/* FIELD: RES_ACK_IN_OVRD [15:15]
 * FIELD: RES_ACK_IN [14:14]
 * FIELD: RES_REQ_IN_OVRD [13:13]
 * FIELD: RES_REQ_IN [12:12]
 * FIELD: RTUNE_REQ_OVRD [11:11]
 * FIELD: RTUNE_REQ [10:10]
 * FIELD: MPLL_MULTIPLIER_OVRD [9:9]
 * FIELD: MPLL_MULTIPLIER [8:2]
 * FIELD: MPLL_EN_OVRD [1:1]
 * FIELD: MPLL_EN [0:0]
 */

#define SSP_CR_SUP_DIG_ATEOVRD 0x0010
/* FIELD: ateovrd_en [2:2]
 * FIELD: ref_usb2_en [1:1]
 * FIELD: ref_clkdiv2 [0:0]
 */

static inline bool is_imx7d_pcie(struct imx6_pcie *imx6_pcie)
{
	struct pcie_port *pp = &imx6_pcie->pp;
	struct device_node *np = pp->dev->of_node;

	return of_device_is_compatible(np, "fsl,imx7d-pcie");
}

static inline bool is_imx6sx_pcie(struct imx6_pcie *imx6_pcie)
{
	struct pcie_port *pp = &imx6_pcie->pp;
	struct device_node *np = pp->dev->of_node;

	return of_device_is_compatible(np, "fsl,imx6sx-pcie");
}

static inline bool is_imx6qp_pcie(struct imx6_pcie *imx6_pcie)
{
	struct pcie_port *pp = &imx6_pcie->pp;
	struct device_node *np = pp->dev->of_node;

	return of_device_is_compatible(np, "fsl,imx6qp-pcie");
}

static int pcie_phy_poll_ack(void __iomem *dbi_base, int exp_val)
{
	u32 val;
	u32 max_iterations = 10;
	u32 wait_counter = 0;

	do {
		val = readl(dbi_base + PCIE_PHY_STAT);
		val = (val >> PCIE_PHY_STAT_ACK_LOC) & 0x1;
		wait_counter++;

		if (val == exp_val)
			return 0;

		udelay(1);
	} while (wait_counter < max_iterations);

	return -ETIMEDOUT;
}

static int pcie_phy_wait_ack(void __iomem *dbi_base, int addr)
{
	u32 val;
	int ret;

	val = addr << PCIE_PHY_CTRL_DATA_LOC;
	writel(val, dbi_base + PCIE_PHY_CTRL);

	val |= (0x1 << PCIE_PHY_CTRL_CAP_ADR_LOC);
	writel(val, dbi_base + PCIE_PHY_CTRL);

	ret = pcie_phy_poll_ack(dbi_base, 1);
	if (ret)
		return ret;

	val = addr << PCIE_PHY_CTRL_DATA_LOC;
	writel(val, dbi_base + PCIE_PHY_CTRL);

	ret = pcie_phy_poll_ack(dbi_base, 0);
	if (ret)
		return ret;

	return 0;
}

/* Read from the 16-bit PCIe PHY control registers (not memory-mapped) */
static int pcie_phy_read(void __iomem *dbi_base, int addr , int *data)
{
	u32 val, phy_ctl;
	int ret;

	ret = pcie_phy_wait_ack(dbi_base, addr);
	if (ret)
		return ret;

	/* assert Read signal */
	phy_ctl = 0x1 << PCIE_PHY_CTRL_RD_LOC;
	writel(phy_ctl, dbi_base + PCIE_PHY_CTRL);

	ret = pcie_phy_poll_ack(dbi_base, 1);
	if (ret)
		return ret;

	val = readl(dbi_base + PCIE_PHY_STAT);
	*data = val & 0xffff;

	/* deassert Read signal */
	writel(0x00, dbi_base + PCIE_PHY_CTRL);

	ret = pcie_phy_poll_ack(dbi_base, 0);
	if (ret)
		return ret;

	return 0;
}

static int pcie_phy_write(void __iomem *dbi_base, int addr, int data)
{
	u32 var;
	int ret;

	/* write addr */
	/* cap addr */
	ret = pcie_phy_wait_ack(dbi_base, addr);
	if (ret)
		return ret;

	var = data << PCIE_PHY_CTRL_DATA_LOC;
	writel(var, dbi_base + PCIE_PHY_CTRL);

	/* capture data */
	var |= (0x1 << PCIE_PHY_CTRL_CAP_DAT_LOC);
	writel(var, dbi_base + PCIE_PHY_CTRL);

	ret = pcie_phy_poll_ack(dbi_base, 1);
	if (ret)
		return ret;

	/* deassert cap data */
	var = data << PCIE_PHY_CTRL_DATA_LOC;
	writel(var, dbi_base + PCIE_PHY_CTRL);

	/* wait for ack de-assertion */
	ret = pcie_phy_poll_ack(dbi_base, 0);
	if (ret)
		return ret;

	/* assert wr signal */
	var = 0x1 << PCIE_PHY_CTRL_WR_LOC;
	writel(var, dbi_base + PCIE_PHY_CTRL);

	/* wait for ack */
	ret = pcie_phy_poll_ack(dbi_base, 1);
	if (ret)
		return ret;

	/* deassert wr signal */
	var = data << PCIE_PHY_CTRL_DATA_LOC;
	writel(var, dbi_base + PCIE_PHY_CTRL);

	/* wait for ack de-assertion */
	ret = pcie_phy_poll_ack(dbi_base, 0);
	if (ret)
		return ret;

	writel(0x0, dbi_base + PCIE_PHY_CTRL);

	return 0;
}

/*  Added for PCI abort handling */
static int imx6q_pcie_abort_handler(unsigned long addr,
		unsigned int fsr, struct pt_regs *regs)
{
	return 0;
}

static int imx6_pcie_assert_core_reset(struct pcie_port *pp)
{
	struct imx6_pcie *imx6_pcie = to_imx6_pcie(pp);
	u32 val, gpr1, gpr12;

	if (is_imx7d_pcie(imx6_pcie)) {
		/* G_RST */
		regmap_update_bits(imx6_pcie->reg_src, 0x2c, BIT(1), BIT(1));

		/* BTNRST */
		regmap_update_bits(imx6_pcie->reg_src, 0x2c, BIT(2), BIT(2));
	} else if (is_imx6sx_pcie(imx6_pcie)) {
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
				IMX6SX_GPR12_PCIE_TEST_PD,
				IMX6SX_GPR12_PCIE_TEST_PD);
		/* Force PCIe PHY reset */
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR5,
				IMX6SX_GPR5_PCIE_BTNRST,
				IMX6SX_GPR5_PCIE_BTNRST);
	} else if (is_imx6qp_pcie(imx6_pcie)) {
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR1,
				IMX6Q_GPR1_PCIE_SW_RST, IMX6Q_GPR1_PCIE_SW_RST);
	} else {
		/*
		 * If the bootloader already enabled the link we need some
		 * special handling to get the core back into a state where
		 * it is safe to touch it for configuration.  As there is
		 * no dedicated reset signal wired up for MX6QDL, we need
		 * to manually force LTSSM into "detect" state before
		 * completely disabling LTSSM, which is a prerequisite
		 * for core configuration.
		 *
		 * If both LTSSM_ENABLE and REF_SSP_ENABLE are active we
		 * have a strong indication that the bootloader activated
		 * the link.
		 */
		regmap_read(imx6_pcie->iomuxc_gpr, IOMUXC_GPR1, &gpr1);
		regmap_read(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12, &gpr12);

		if ((gpr1 & IMX6Q_GPR1_PCIE_REF_CLK_EN) &&
		    (gpr12 & IMX6Q_GPR12_PCIE_CTL_2)) {
			val = readl(pp->dbi_base + PCIE_PL_PFLR);
			val &= ~PCIE_PL_PFLR_LINK_STATE_MASK;
			val |= PCIE_PL_PFLR_FORCE_LINK;
			writel(val, pp->dbi_base + PCIE_PL_PFLR);

			regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
					IMX6Q_GPR12_PCIE_CTL_2, 0 << 10);
		}

		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR1,
				IMX6Q_GPR1_PCIE_TEST_PD, 1 << 18);
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR1,
				IMX6Q_GPR1_PCIE_REF_CLK_EN, 0 << 16);
	}

	return 0;
}

static void pci_imx_phy_pll_locked(struct imx6_pcie *imx6_pcie)
{
	u32 val;
	int count = 20000;

	while (count--) {
		regmap_read(imx6_pcie->iomuxc_gpr, IOMUXC_GPR22, &val);
		if (val & BIT(31))
			break;
		udelay(10);
		if (count == 0)
			pr_info("pcie phy pll can't be locked.\n");
	}
}

static int imx6_pcie_deassert_core_reset(struct pcie_port *pp)
{
	int ret;
	u32 val;
	struct imx6_pcie *imx6_pcie = to_imx6_pcie(pp);

	if (gpio_is_valid(imx6_pcie->power_on_gpio))
		gpio_set_value_cansleep(imx6_pcie->power_on_gpio, 1);

	request_bus_freq(BUS_FREQ_HIGH);
	ret = clk_prepare_enable(imx6_pcie->pcie);
	if (ret) {
		dev_err(pp->dev, "unable to enable pcie clock\n");
		goto err_pcie;
	}

	if (imx6_pcie->ext_osc) {
		clk_set_parent(imx6_pcie->pcie_ext,
				imx6_pcie->pcie_ext_src);
		ret = clk_prepare_enable(imx6_pcie->pcie_ext);
		if (ret) {
			dev_err(pp->dev, "unable to enable pcie_ext clock\n");
			goto err_pcie_bus;
		}
	} else {
		ret = clk_prepare_enable(imx6_pcie->pcie_bus);
		if (ret) {
			dev_err(pp->dev, "unable to enable pcie_bus clock\n");
			goto err_pcie_bus;
		}
	}

	ret = clk_prepare_enable(imx6_pcie->pcie_phy);
	if (ret) {
		dev_err(pp->dev, "unable to enable pcie_phy clock\n");
		goto err_pcie_phy;
	}

	if (is_imx6sx_pcie(imx6_pcie)) {
		ret = clk_prepare_enable(imx6_pcie->pcie_inbound_axi);
		if (ret) {
			dev_err(pp->dev, "unable to enable pcie clock\n");
			goto err_inbound_axi;
		}

		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
				IMX6SX_GPR12_PCIE_TEST_PD, 0);
	} else if (!is_imx7d_pcie(imx6_pcie)) {
		/* power up core phy and enable ref clock */
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR1,
				IMX6Q_GPR1_PCIE_TEST_PD, 0 << 18);
		/*
		 * the async reset input need ref clock to sync internally,
		 * when the ref clock comes after reset, internal synced
		 * reset time is too short, cannot meet the requirement.
		 * add one ~10us delay here.
		 */
		udelay(10);
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR1,
				IMX6Q_GPR1_PCIE_REF_CLK_EN, 1 << 16);
	}

	/* allow the clocks to stabilize */
	udelay(200);

	/* Some boards don't have PCIe reset GPIO. */
	if (gpio_is_valid(imx6_pcie->reset_gpio)) {
		gpio_set_value_cansleep(imx6_pcie->reset_gpio, 0);
		mdelay(20);
		gpio_set_value_cansleep(imx6_pcie->reset_gpio, 1);
		mdelay(20);
	}

	/*
	 * Release the PCIe PHY reset here
	 */
	if (is_imx7d_pcie(imx6_pcie)) {
		/* wait for more than 10us to release phy g_rst and btnrst */
		udelay(10);
		regmap_update_bits(imx6_pcie->reg_src, 0x2c, BIT(6), 0);
		regmap_update_bits(imx6_pcie->reg_src, 0x2c, BIT(1), 0);
		regmap_update_bits(imx6_pcie->reg_src, 0x2c, BIT(2), 0);

		/* wait for phy pll lock firstly. */
		pci_imx_phy_pll_locked(imx6_pcie);
	} else if (is_imx6sx_pcie(imx6_pcie)) {
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR5,
				IMX6SX_GPR5_PCIE_BTNRST, 0);
	} else if (is_imx6qp_pcie(imx6_pcie)) {
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR1,
				IMX6Q_GPR1_PCIE_SW_RST, 0);
		/*
		 * some delay are required by 6qp, after the SW_RST is
		 * cleared, before access the cfg register.
		 */
		udelay(200);
	}

	/* Configure the PHY when 100Mhz external OSC is used as input clock */
	if (imx6_pcie->ext_osc && is_imx6qp_pcie(imx6_pcie)) {
		mdelay(4);
		pcie_phy_read(pp->dbi_base, SSP_CR_SUP_DIG_MPLL_OVRD_IN_LO, &val);
		/* MPLL_MULTIPLIER [8:2] */
		val &= ~(0x7F << 2);
		val |= (0x19 << 2);
		/* MPLL_MULTIPLIER_OVRD [9:9] */
		val |= (0x1 << 9);
		pcie_phy_write(pp->dbi_base, SSP_CR_SUP_DIG_MPLL_OVRD_IN_LO, val);
		mdelay(4);

		pcie_phy_read(pp->dbi_base, SSP_CR_SUP_DIG_ATEOVRD, &val);
		/* ref_clkdiv2 [0:0] */
		val &= ~0x1;
		/* ateovrd_en [2:2] */
		val |=  0x4;
		pcie_phy_write(pp->dbi_base, SSP_CR_SUP_DIG_ATEOVRD, val);
		mdelay(4);
	}

	return 0;

err_inbound_axi:
	clk_disable_unprepare(imx6_pcie->pcie);
err_pcie_phy:
	if (!imx6_pcie->ext_osc)
		clk_disable_unprepare(imx6_pcie->pcie_bus);
err_pcie_bus:
	clk_disable_unprepare(imx6_pcie->pcie);
err_pcie:
	return ret;

}

static void imx6_pcie_init_phy(struct pcie_port *pp)
{
	int ret;
	struct imx6_pcie *imx6_pcie = to_imx6_pcie(pp);

	if (is_imx7d_pcie(imx6_pcie)) {
		/* Enable PCIe PHY 1P0D */
		regulator_set_voltage(imx6_pcie->pcie_phy_regulator,
				1000000, 1000000);
		ret = regulator_enable(imx6_pcie->pcie_phy_regulator);
		if (ret)
			dev_err(pp->dev, "failed to enable pcie regulator.\n");

		/* pcie phy ref clock select; 1? internal pll : external osc */
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
				BIT(5), 0);
	} else if (is_imx6sx_pcie(imx6_pcie)) {
		/* Force PCIe PHY reset */
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR5,
				IMX6SX_GPR5_PCIE_BTNRST,
				IMX6SX_GPR5_PCIE_BTNRST);

		regulator_set_voltage(imx6_pcie->pcie_phy_regulator,
				1100000, 1100000);
		ret = regulator_enable(imx6_pcie->pcie_phy_regulator);
		if (ret)
			dev_err(pp->dev, "failed to enable pcie regulator.\n");
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
				IMX6SX_GPR12_RX_EQ_MASK, IMX6SX_GPR12_RX_EQ_2);
	}

	if (imx6_pcie->pcie_bus_regulator != NULL) {
		ret = regulator_enable(imx6_pcie->pcie_bus_regulator);
		if (ret)
			dev_err(pp->dev, "failed to enable pcie regulator.\n");
	}

	if (!is_imx7d_pcie(imx6_pcie)) {
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
				IMX6Q_GPR12_PCIE_CTL_2, 0 << 10);

		/* configure constant input signal to the pcie ctrl and phy */
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
				IMX6Q_GPR12_LOS_LEVEL, IMX6Q_GPR12_LOS_LEVEL_9);

		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR8,
				IMX6Q_GPR8_TX_DEEMPH_GEN1, 20 << 0);
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR8,
				IMX6Q_GPR8_TX_DEEMPH_GEN2_3P5DB, 20 << 6);
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR8,
				IMX6Q_GPR8_TX_DEEMPH_GEN2_6DB, 20 << 12);
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR8,
				IMX6Q_GPR8_TX_SWING_FULL, 115 << 18);
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR8,
				IMX6Q_GPR8_TX_SWING_LOW, 115 << 25);
	}

	/* configure the device type */
	if (IS_ENABLED(CONFIG_EP_MODE_IN_EP_RC_SYS))
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
				IMX6Q_GPR12_DEVICE_TYPE,
				PCI_EXP_TYPE_ENDPOINT << 12);
	else
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
				IMX6Q_GPR12_DEVICE_TYPE,
				PCI_EXP_TYPE_ROOT_PORT << 12);
}

static int imx6_pcie_wait_for_link(struct pcie_port *pp)
{
	int count = 20000;
	struct imx6_pcie *imx6_pcie = to_imx6_pcie(pp);

	while (!dw_pcie_link_up(pp)) {
		udelay(10);
		if (--count)
			continue;

		dev_err(pp->dev, "phy link never came up\n");
		dev_dbg(pp->dev, "DEBUG_R0: 0x%08x, DEBUG_R1: 0x%08x\n",
			readl(pp->dbi_base + PCIE_PHY_DEBUG_R0),
			readl(pp->dbi_base + PCIE_PHY_DEBUG_R1));

		if (!IS_ENABLED(CONFIG_PCI_IMX6_COMPLIANCE_TEST)) {
			clk_disable_unprepare(imx6_pcie->pcie);
			if (!imx6_pcie->ext_osc)
				clk_disable_unprepare(imx6_pcie->pcie_bus);
			clk_disable_unprepare(imx6_pcie->pcie_phy);
			if (is_imx6sx_pcie(imx6_pcie))
				clk_disable_unprepare(imx6_pcie->pcie_inbound_axi);
			release_bus_freq(BUS_FREQ_HIGH);
			if (imx6_pcie->pcie_phy_regulator != NULL)
				regulator_disable(imx6_pcie->pcie_phy_regulator);
			if (imx6_pcie->pcie_bus_regulator != NULL)
				regulator_disable(imx6_pcie->pcie_bus_regulator);
		}
		return -EINVAL;
	}

	return 0;
}

static irqreturn_t imx6_pcie_msi_handler(int irq, void *arg)
{
	struct pcie_port *pp = arg;

	return dw_handle_msi_irq(pp);
}

static int imx6_pcie_start_link(struct pcie_port *pp)
{
	struct imx6_pcie *imx6_pcie = to_imx6_pcie(pp);
	uint32_t tmp;
	int ret, count;
	/*
	 * Force Gen1 operation when starting the link.  In case the link is
	 * started in Gen2 mode, there is a possibility the devices on the
	 * bus will not be detected at all.  This happens with PCIe switches.
	 */

	if (!IS_ENABLED(CONFIG_PCI_IMX6_COMPLIANCE_TEST)) {
		tmp = readl(pp->dbi_base + PCIE_RC_LCR);
		tmp &= ~PCIE_RC_LCR_MAX_LINK_SPEEDS_MASK;
		tmp |= PCIE_RC_LCR_MAX_LINK_SPEEDS_GEN1;
		writel(tmp, pp->dbi_base + PCIE_RC_LCR);
	}

	/* Start LTSSM. */
	if (is_imx7d_pcie(imx6_pcie))
		regmap_update_bits(imx6_pcie->reg_src, 0x2c, BIT(6), BIT(6));
	else
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
				IMX6Q_GPR12_PCIE_CTL_2, 1 << 10);

	ret = imx6_pcie_wait_for_link(pp);
	if (ret)
		return ret;

	/* Allow Gen2 mode after the link is up. */
	tmp = readl(pp->dbi_base + PCIE_RC_LCR);
	tmp &= ~PCIE_RC_LCR_MAX_LINK_SPEEDS_MASK;
	tmp |= PCIE_RC_LCR_MAX_LINK_SPEEDS_GEN2;
	writel(tmp, pp->dbi_base + PCIE_RC_LCR);

	/*
	 * Start Directed Speed Change so the best possible speed both link
	 * partners support can be negotiated.
	 */
	tmp = readl(pp->dbi_base + PCIE_LINK_WIDTH_SPEED_CONTROL);
	tmp |= PORT_LOGIC_SPEED_CHANGE;
	writel(tmp, pp->dbi_base + PCIE_LINK_WIDTH_SPEED_CONTROL);

	count = 2000;
	while (count--) {
		tmp = readl(pp->dbi_base + PCIE_LINK_WIDTH_SPEED_CONTROL);
		/* Test if the speed change finished. */
		if (!(tmp & PORT_LOGIC_SPEED_CHANGE))
			break;
		udelay(10);
	}

	/* Make sure link training is finished as well! */
	if (count)
		ret = imx6_pcie_wait_for_link(pp);
	else
		ret = -EINVAL;

	if (ret) {
		dev_err(pp->dev, "Failed to bring link up!\n");
	} else {
		tmp = readl(pp->dbi_base + 0x80);
		dev_dbg(pp->dev, "Link up, Gen=%i\n", (tmp >> 16) & 0xf);
	}

	return ret;
}

static int imx6_pcie_host_init(struct pcie_port *pp)
{
	int ret;
	struct imx6_pcie *imx6_pcie = to_imx6_pcie(pp);

	/* enable disp_mix power domain */
	if (is_imx7d_pcie(imx6_pcie))
		pm_runtime_get_sync(pp->dev);

	imx6_pcie_assert_core_reset(pp);

	imx6_pcie_init_phy(pp);

	imx6_pcie_deassert_core_reset(pp);

	dw_pcie_setup_rc(pp);

	ret = imx6_pcie_start_link(pp);
	if (ret < 0)
		return ret;

	if (IS_ENABLED(CONFIG_PCI_MSI))
		dw_pcie_msi_init(pp);

	return 0;
}

static void imx6_pcie_reset_phy(struct pcie_port *pp)
{
	uint32_t temp;

	pcie_phy_read(pp->dbi_base, PHY_RX_OVRD_IN_LO, &temp);
	temp |= (PHY_RX_OVRD_IN_LO_RX_DATA_EN |
		 PHY_RX_OVRD_IN_LO_RX_PLL_EN);
	pcie_phy_write(pp->dbi_base, PHY_RX_OVRD_IN_LO, temp);

	usleep_range(2000, 3000);

	pcie_phy_read(pp->dbi_base, PHY_RX_OVRD_IN_LO, &temp);
	temp &= ~(PHY_RX_OVRD_IN_LO_RX_DATA_EN |
		  PHY_RX_OVRD_IN_LO_RX_PLL_EN);
	pcie_phy_write(pp->dbi_base, PHY_RX_OVRD_IN_LO, temp);
}

static int imx6_pcie_link_up(struct pcie_port *pp)
{
	u32 rc, debug_r0, rx_valid;
	int count = 5000;
	struct imx6_pcie *imx6_pcie = to_imx6_pcie(pp);

	/*
	 * Test if the PHY reports that the link is up and also that the LTSSM
	 * training finished. There are three possible states of the link when
	 * this code is called:
	 * 1) The link is DOWN (unlikely)
	 *     The link didn't come up yet for some reason. This usually means
	 *     we have a real problem somewhere. Reset the PHY and exit. This
	 *     state calls for inspection of the DEBUG registers.
	 * 2) The link is UP, but still in LTSSM training
	 *     Wait for the training to finish, which should take a very short
	 *     time. If the training does not finish, we have a problem and we
	 *     need to inspect the DEBUG registers. If the training does finish,
	 *     the link is up and operating correctly.
	 * 3) The link is UP and no longer in LTSSM training
	 *     The link is up and operating correctly.
	 */
	while (1) {
		rc = readl(pp->dbi_base + PCIE_PHY_DEBUG_R1);
		if (!(rc & PCIE_PHY_DEBUG_R1_XMLH_LINK_UP))
			break;
		if (!(rc & PCIE_PHY_DEBUG_R1_XMLH_LINK_IN_TRAINING))
			return 1;
		if (!count--)
			break;
		dev_dbg(pp->dev, "Link is up, but still in training\n");
		/*
		 * Wait a little bit, then re-check if the link finished
		 * the training.
		 */
		udelay(10);
	}

	if (!is_imx7d_pcie(imx6_pcie)) {
		/*
		 * From L0, initiate MAC entry to gen2 if EP/RC supports gen2.
		 * Wait 2ms (LTSSM timeout is 24ms, PHY lock is ~5us in gen2).
		 * If (MAC/LTSSM.state == Recovery.RcvrLock)
		 * && (PHY/rx_valid==0) then pulse PHY/rx_reset. Transition
		 * to gen2 is stuck
		 */
		pcie_phy_read(pp->dbi_base, PCIE_PHY_RX_ASIC_OUT, &rx_valid);
		debug_r0 = readl(pp->dbi_base + PCIE_PHY_DEBUG_R0);

		if (rx_valid & 0x01)
			return 0;

		if ((debug_r0 & 0x3f) != 0x0d)
			return 0;

		dev_err(pp->dev, "transition to gen2 is stuck, reset PHY!\n");
		dev_dbg(pp->dev, "debug_r0=%08x debug_r1=%08x\n", debug_r0, rc);

		imx6_pcie_reset_phy(pp);
	}

	return 0;
}

static struct pcie_host_ops imx6_pcie_host_ops = {
	.link_up = imx6_pcie_link_up,
	.host_init = imx6_pcie_host_init,
};

static int __init imx6_add_pcie_port(struct pcie_port *pp,
			struct platform_device *pdev)
{
	int ret;

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		pp->msi_irq = platform_get_irq_byname(pdev, "msi");
		if (pp->msi_irq <= 0) {
			dev_err(&pdev->dev, "failed to get MSI irq\n");
			return -ENODEV;
		}

		ret = devm_request_irq(&pdev->dev, pp->msi_irq,
				       imx6_pcie_msi_handler,
				       IRQF_SHARED, "mx6-pcie-msi", pp);
		if (ret) {
			dev_err(&pdev->dev, "failed to request MSI irq\n");
			return -ENODEV;
		}
	}

	pp->root_bus_nr = -1;
	pp->ops = &imx6_pcie_host_ops;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(&pdev->dev, "failed to initialize host\n");
		return ret;
	}

	return 0;
}

static ssize_t imx_pcie_bar0_addr_info(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct imx6_pcie *imx6_pcie = dev_get_drvdata(dev);
	struct pcie_port *pp = &imx6_pcie->pp;

	return sprintf(buf, "imx-pcie-bar0-addr-info start 0x%08x\n",
			readl(pp->dbi_base + PCI_BASE_ADDRESS_0));
}

static ssize_t imx_pcie_bar0_addr_start(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	u32 bar_start;
	struct imx6_pcie *imx6_pcie = dev_get_drvdata(dev);
	struct pcie_port *pp = &imx6_pcie->pp;

	sscanf(buf, "%x\n", &bar_start);
	writel(bar_start, pp->dbi_base + PCI_BASE_ADDRESS_0);

	return count;
}

static void imx_pcie_regions_setup(struct device *dev)
{
	struct imx6_pcie *imx6_pcie = dev_get_drvdata(dev);
	struct pcie_port *pp = &imx6_pcie->pp;

	if (is_imx7d_pcie(imx6_pcie) && ddr_test_region == 0)
		ddr_test_region = 0xb0000000;
	else if (is_imx6sx_pcie(imx6_pcie) && ddr_test_region == 0)
		ddr_test_region = 0xb0000000;
	else if (ddr_test_region == 0)
		ddr_test_region = 0x40000000;

	/*
	 * region2 outbound used to access rc/ep mem
	 * in imx6 pcie ep/rc validation system
	 */
	writel(2, pp->dbi_base + 0x900);
	writel((u32)pp->mem_base, pp->dbi_base + 0x90c);
	writel(0, pp->dbi_base + 0x910);
	writel((u32)pp->mem_base + test_region_size, pp->dbi_base + 0x914);

	writel(ddr_test_region, pp->dbi_base + 0x918);
	writel(0, pp->dbi_base + 0x91c);
	writel(0, pp->dbi_base + 0x904);
	writel(1 << 31, pp->dbi_base + 0x908);
}

static ssize_t imx_pcie_memw_info(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	return sprintf(buf, "imx-pcie-rc-memw-info start 0x%08x, size 0x%08x\n",
			ddr_test_region, test_region_size);
}

static ssize_t
imx_pcie_memw_start(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	u32 memw_start;
	struct imx6_pcie *imx6_pcie = dev_get_drvdata(dev);

	sscanf(buf, "%x\n", &memw_start);

	if (is_imx7d_pcie(imx6_pcie) || is_imx6sx_pcie(imx6_pcie)) {
		if (memw_start < 0x80000000 || memw_start > 0xb0000000) {
			dev_err(dev, "Invalid memory start addr.\n");
			dev_info(dev, "e.x: echo 0xb0000000 > /sys/...");
			return -1;
		}
	} else {
		if (memw_start < 0x10000000 || memw_start > 0x40000000) {
			dev_err(dev, "Invalid imx6q sd memory start addr.\n");
			dev_info(dev, "e.x: echo 0x30000000 > /sys/...");
			return -1;
		}
	}

	if (ddr_test_region != memw_start) {
		ddr_test_region = memw_start;
		imx_pcie_regions_setup(dev);
	}

	return count;
}

static ssize_t
imx_pcie_memw_size(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	u32 memw_size;

	sscanf(buf, "%x\n", &memw_size);

	if ((memw_size > (SZ_16M - SZ_1M)) || (memw_size < SZ_64K)) {
		dev_err(dev, "Invalid, should be [SZ_64K,SZ_16M - SZ_1MB].\n");
		dev_info(dev, "For example: echo 0x200000 > /sys/...");
		return -1;
	}

	if (test_region_size != memw_size) {
		test_region_size = memw_size;
		imx_pcie_regions_setup(dev);
	}

	return count;
}

static DEVICE_ATTR(memw_info, S_IRUGO, imx_pcie_memw_info, NULL);
static DEVICE_ATTR(memw_start_set, S_IWUSR, NULL, imx_pcie_memw_start);
static DEVICE_ATTR(memw_size_set, S_IWUSR, NULL, imx_pcie_memw_size);
static DEVICE_ATTR(ep_bar0_addr, S_IWUSR | S_IRUGO, imx_pcie_bar0_addr_info,
		imx_pcie_bar0_addr_start);

static struct attribute *imx_pcie_attrs[] = {
	/*
	 * The start address, and the limitation (64KB ~ (16MB - 1MB))
	 * of the ddr mem window reserved by RC, and used for EP to access.
	 * BTW, these attrs are only configured at EP side.
	 */
	&dev_attr_memw_info.attr,
	&dev_attr_memw_start_set.attr,
	&dev_attr_memw_size_set.attr,
	&dev_attr_ep_bar0_addr.attr,
	NULL
};

static struct attribute_group imx_pcie_attrgroup = {
	.attrs	= imx_pcie_attrs,
};

static void imx6_pcie_setup_ep(struct pcie_port *pp)
{
		/* CMD reg:I/O space, MEM space, and Bus Master Enable */
		writel(readl(pp->dbi_base + PCI_COMMAND)
				| PCI_COMMAND_IO
				| PCI_COMMAND_MEMORY
				| PCI_COMMAND_MASTER,
				pp->dbi_base + PCI_COMMAND);

		/*
		 * configure the class_rev(emaluate one memory ram ep device),
		 * bar0 and bar1 of ep
		 */
		writel(0xdeadbeaf, pp->dbi_base + PCI_VENDOR_ID);
		writel(readl(pp->dbi_base + PCI_CLASS_REVISION)
				| (PCI_CLASS_MEMORY_RAM	<< 16),
				pp->dbi_base + PCI_CLASS_REVISION);
		writel(0xdeadbeaf, pp->dbi_base
				+ PCI_SUBSYSTEM_VENDOR_ID);

		/* 32bit none-prefetchable 8M bytes memory on bar0 */
		writel(0x0, pp->dbi_base + PCI_BASE_ADDRESS_0);
		writel(SZ_8M - 1, pp->dbi_base + (1 << 12)
				+ PCI_BASE_ADDRESS_0);

		/* None used bar1 */
		writel(0x0, pp->dbi_base + PCI_BASE_ADDRESS_1);
		writel(0, pp->dbi_base + (1 << 12) + PCI_BASE_ADDRESS_1);

		/* 4K bytes IO on bar2 */
		writel(0x1, pp->dbi_base + PCI_BASE_ADDRESS_2);
		writel(SZ_4K - 1, pp->dbi_base + (1 << 12) +
				PCI_BASE_ADDRESS_2);

		/*
		 * 32bit prefetchable 1M bytes memory on bar3
		 * FIXME BAR MASK3 is not changable, the size
		 * is fixed to 256 bytes.
		 */
		writel(0x8, pp->dbi_base + PCI_BASE_ADDRESS_3);
		writel(SZ_1M - 1, pp->dbi_base + (1 << 12)
				+ PCI_BASE_ADDRESS_3);

		/*
		 * 64bit prefetchable 1M bytes memory on bar4-5.
		 * FIXME BAR4,5 are not enabled yet
		 */
		writel(0xc, pp->dbi_base + PCI_BASE_ADDRESS_4);
		writel(SZ_1M - 1, pp->dbi_base + (1 << 12)
				+ PCI_BASE_ADDRESS_4);
		writel(0, pp->dbi_base + (1 << 12) + PCI_BASE_ADDRESS_5);
}

#ifdef CONFIG_PM_SLEEP
/* PM_TURN_OFF */
static void pci_imx_pm_turn_off(struct imx6_pcie *imx6_pcie)
{
	/* PM_TURN_OFF */
	if (is_imx7d_pcie(imx6_pcie)) {
		regmap_update_bits(imx6_pcie->reg_src, 0x2c,
				BIT(11), BIT(11));
		regmap_update_bits(imx6_pcie->reg_src, 0x2c,
				BIT(11), 0);
	} else if (is_imx6sx_pcie(imx6_pcie)) {
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
				IMX6SX_GPR12_PCIE_PM_TURN_OFF,
				IMX6SX_GPR12_PCIE_PM_TURN_OFF);
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
				IMX6SX_GPR12_PCIE_PM_TURN_OFF, 0);
	} else if (is_imx6qp_pcie(imx6_pcie)) {
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
				IMX6Q_GPR12_PCIE_PM_TURN_OFF,
				IMX6Q_GPR12_PCIE_PM_TURN_OFF);
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
				IMX6Q_GPR12_PCIE_PM_TURN_OFF, 0);
	} else {
		pr_info("Info: don't support pm_turn_off yet.\n");
		return;
	}

	udelay(1000);
	if (gpio_is_valid(imx6_pcie->reset_gpio))
		gpio_set_value_cansleep(imx6_pcie->reset_gpio, 0);
}

static int pci_imx_suspend_noirq(struct device *dev)
{
	struct imx6_pcie *imx6_pcie = dev_get_drvdata(dev);
	struct pcie_port *pp = &imx6_pcie->pp;

	if (IS_ENABLED(CONFIG_PCI_MSI))
		dw_pcie_msi_cfg_store(pp);

	pci_imx_pm_turn_off(imx6_pcie);

	if (is_imx6sx_pcie(imx6_pcie) || is_imx7d_pcie(imx6_pcie)
			|| is_imx6qp_pcie(imx6_pcie)) {
		/* Disable clks */
		clk_disable_unprepare(imx6_pcie->pcie);
		clk_disable_unprepare(imx6_pcie->pcie_phy);
		if (!imx6_pcie->ext_osc)
			clk_disable_unprepare(imx6_pcie->pcie_bus);
		if (is_imx6sx_pcie(imx6_pcie))
			clk_disable_unprepare(imx6_pcie->pcie_inbound_axi);
		else if (is_imx7d_pcie(imx6_pcie))
			/* turn off external osc input */
			regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
					BIT(5), BIT(5));
		else if (is_imx6qp_pcie(imx6_pcie)) {
			regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR1,
					IMX6Q_GPR1_PCIE_REF_CLK_EN, 0);
			regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR1,
					IMX6Q_GPR1_PCIE_TEST_PD,
					IMX6Q_GPR1_PCIE_TEST_PD);
		}
		release_bus_freq(BUS_FREQ_HIGH);

		/* Power down PCIe PHY. */
		if (imx6_pcie->pcie_phy_regulator != NULL)
			regulator_disable(imx6_pcie->pcie_phy_regulator);
		if (imx6_pcie->pcie_bus_regulator != NULL)
			regulator_disable(imx6_pcie->pcie_bus_regulator);
		if (gpio_is_valid(imx6_pcie->power_on_gpio))
			gpio_set_value_cansleep(imx6_pcie->power_on_gpio, 0);
	} else {
		/*
		 * L2 can exit by 'reset' or Inband beacon (from remote EP)
		 * toggling phy_powerdown has same effect as 'inband beacon'
		 * So, toggle bit18 of GPR1, used as a workaround of errata
		 * "PCIe PCIe does not support L2 Power Down"
		 */
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR1,
				IMX6Q_GPR1_PCIE_TEST_PD,
				IMX6Q_GPR1_PCIE_TEST_PD);
	}

	return 0;
}

static int pci_imx_resume_noirq(struct device *dev)
{
	int ret = 0;
	struct imx6_pcie *imx6_pcie = dev_get_drvdata(dev);
	struct pcie_port *pp = &imx6_pcie->pp;

	if (is_imx6sx_pcie(imx6_pcie) || is_imx7d_pcie(imx6_pcie)
			|| is_imx6qp_pcie(imx6_pcie)) {
		if (is_imx7d_pcie(imx6_pcie))
			regmap_update_bits(imx6_pcie->reg_src, 0x2c, BIT(6), 0);
		else
			regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
					IMX6Q_GPR12_PCIE_CTL_2, 0);

		imx6_pcie_assert_core_reset(pp);

		imx6_pcie_init_phy(pp);

		ret = imx6_pcie_deassert_core_reset(pp);
		if (ret < 0)
			return ret;

		/*
		 * controller maybe turn off, re-configure again
		 */
		dw_pcie_setup_rc(pp);

		if (IS_ENABLED(CONFIG_PCI_MSI))
			dw_pcie_msi_cfg_restore(pp);

		if (is_imx7d_pcie(imx6_pcie)) {
			/* wait for phy pll lock firstly. */
			pci_imx_phy_pll_locked(imx6_pcie);
			regmap_update_bits(imx6_pcie->reg_src, 0x2c,
					BIT(6), BIT(6));
		} else {
			regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
					IMX6Q_GPR12_PCIE_CTL_2,
					IMX6Q_GPR12_PCIE_CTL_2);
		}

		ret = imx6_pcie_start_link(pp);
		if (ret < 0)
			pr_info("pcie link is down after resume.\n");
	} else {
		/*
		 * L2 can exit by 'reset' or Inband beacon (from remote EP)
		 * toggling phy_powerdown has same effect as 'inband beacon'
		 * So, toggle bit18 of GPR1, used as a workaround of errata
		 * "PCIe PCIe does not support L2 Power Down"
		 */
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR1,
				IMX6Q_GPR1_PCIE_TEST_PD, 0);
	}

	return 0;
}

static const struct dev_pm_ops pci_imx_pm_ops = {
	.suspend_noirq = pci_imx_suspend_noirq,
	.resume_noirq = pci_imx_resume_noirq,
	.freeze_noirq = pci_imx_suspend_noirq,
	.thaw_noirq = pci_imx_resume_noirq,
	.poweroff_noirq = pci_imx_suspend_noirq,
	.restore_noirq = pci_imx_resume_noirq,
};
#endif

static int __init imx6_pcie_probe(struct platform_device *pdev)
{
	struct imx6_pcie *imx6_pcie;
	struct pcie_port *pp;
	struct device_node *np = pdev->dev.of_node;
	struct resource *dbi_base;
	int ret;

	imx6_pcie = devm_kzalloc(&pdev->dev, sizeof(*imx6_pcie), GFP_KERNEL);
	if (!imx6_pcie)
		return -ENOMEM;

	pp = &imx6_pcie->pp;
	pp->dev = &pdev->dev;

	if (IS_ENABLED(CONFIG_EP_MODE_IN_EP_RC_SYS)) {
		/* add attributes for device */
		ret = sysfs_create_group(&pdev->dev.kobj, &imx_pcie_attrgroup);
		if (ret)
			return -EINVAL;
	}

	/* Added for PCI abort handling */
	hook_fault_code(16 + 6, imx6q_pcie_abort_handler, SIGBUS, 0,
		"imprecise external abort");

	dbi_base = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pp->dbi_base = devm_ioremap_resource(&pdev->dev, dbi_base);
	if (IS_ERR(pp->dbi_base))
		return PTR_ERR(pp->dbi_base);

	/* Fetch GPIOs */
	imx6_pcie->dis_gpio = of_get_named_gpio(np, "disable-gpio", 0);
	if (gpio_is_valid(imx6_pcie->dis_gpio)) {
		ret = devm_gpio_request_one(&pdev->dev, imx6_pcie->dis_gpio,
					    GPIOF_OUT_INIT_HIGH, "PCIe DIS");
		if (ret) {
			dev_err(&pdev->dev, "unable to get disable gpio\n");
			return ret;
		}
	}

	imx6_pcie->power_on_gpio = of_get_named_gpio(np, "power-on-gpio", 0);
	if (gpio_is_valid(imx6_pcie->power_on_gpio)) {
		ret = devm_gpio_request_one(&pdev->dev,
					imx6_pcie->power_on_gpio,
					GPIOF_OUT_INIT_LOW,
					"PCIe power enable");
		if (ret) {
			dev_err(&pdev->dev, "unable to get power-on gpio\n");
			return ret;
		}
	}

	imx6_pcie->reset_gpio = of_get_named_gpio(np, "reset-gpio", 0);
	if (gpio_is_valid(imx6_pcie->reset_gpio)) {
		ret = devm_gpio_request_one(&pdev->dev, imx6_pcie->reset_gpio,
					    GPIOF_OUT_INIT_LOW, "PCIe reset");
		if (ret) {
			dev_err(&pdev->dev, "unable to get reset gpio\n");
			return ret;
		}
	}

	/* Fetch clocks */
	imx6_pcie->pcie_phy = devm_clk_get(&pdev->dev, "pcie_phy");
	if (IS_ERR(imx6_pcie->pcie_phy)) {
		dev_err(&pdev->dev,
			"pcie_phy clock source missing or invalid\n");
		return PTR_ERR(imx6_pcie->pcie_phy);
	}

	imx6_pcie->pcie_bus = devm_clk_get(&pdev->dev, "pcie_bus");
	if (IS_ERR(imx6_pcie->pcie_bus)) {
		dev_err(&pdev->dev,
			"pcie_bus clock source missing or invalid\n");
		return PTR_ERR(imx6_pcie->pcie_bus);
	}

	if (of_property_read_u32(np, "ext_osc", &imx6_pcie->ext_osc) < 0)
		imx6_pcie->ext_osc = 0;

	if (imx6_pcie->ext_osc) {
		imx6_pcie->pcie_ext = devm_clk_get(&pdev->dev, "pcie_ext");
		if (IS_ERR(imx6_pcie->pcie_ext)) {
			dev_err(&pdev->dev,
				"pcie_ext clock source missing or invalid\n");
			return PTR_ERR(imx6_pcie->pcie_ext);
		}

		imx6_pcie->pcie_ext_src = devm_clk_get(&pdev->dev,
				"pcie_ext_src");
		if (IS_ERR(imx6_pcie->pcie_ext_src)) {
			dev_err(&pdev->dev,
				"pcie_ext_src clk src missing or invalid\n");
			return PTR_ERR(imx6_pcie->pcie_ext_src);
		}
	}

	imx6_pcie->pcie = devm_clk_get(&pdev->dev, "pcie");
	if (IS_ERR(imx6_pcie->pcie)) {
		dev_err(&pdev->dev,
			"pcie clock source missing or invalid\n");
		return PTR_ERR(imx6_pcie->pcie);
	}

	imx6_pcie->pcie_bus_regulator = devm_regulator_get(pp->dev,
			"pcie-bus");
	if (IS_ERR(imx6_pcie->pcie_bus_regulator))
		imx6_pcie->pcie_bus_regulator = NULL;

	/* Grab GPR config register range */
	if (is_imx7d_pcie(imx6_pcie)) {
		imx6_pcie->iomuxc_gpr =
			 syscon_regmap_lookup_by_compatible
			 ("fsl,imx7d-iomuxc-gpr");
		imx6_pcie->reg_src =
			 syscon_regmap_lookup_by_compatible("fsl,imx7d-src");
		if (IS_ERR(imx6_pcie->reg_src)) {
			dev_err(&pdev->dev,
				"imx7d pcie phy src missing or invalid\n");
			return PTR_ERR(imx6_pcie->reg_src);
		}
		imx6_pcie->pcie_phy_regulator = devm_regulator_get(pp->dev,
				"pcie-phy");
	} else if (is_imx6sx_pcie(imx6_pcie)) {
		imx6_pcie->pcie_inbound_axi = devm_clk_get(&pdev->dev,
				"pcie_inbound_axi");
		if (IS_ERR(imx6_pcie->pcie_inbound_axi)) {
			dev_err(&pdev->dev,
				"pcie clock source missing or invalid\n");
			return PTR_ERR(imx6_pcie->pcie_inbound_axi);
		}

		imx6_pcie->pcie_phy_regulator = devm_regulator_get(pp->dev,
				"pcie-phy");

		imx6_pcie->iomuxc_gpr =
			 syscon_regmap_lookup_by_compatible
			 ("fsl,imx6sx-iomuxc-gpr");
	} else {
		imx6_pcie->iomuxc_gpr =
		 syscon_regmap_lookup_by_compatible("fsl,imx6q-iomuxc-gpr");
	}
	if (IS_ERR(imx6_pcie->iomuxc_gpr)) {
		dev_err(&pdev->dev, "unable to find iomuxc registers\n");
		return PTR_ERR(imx6_pcie->iomuxc_gpr);
	}

	if (IS_ENABLED(CONFIG_EP_MODE_IN_EP_RC_SYS)) {
		int i;
		void *test_reg1, *test_reg2;
		void __iomem *pcie_arb_base_addr;
		struct timeval tv1s, tv1e, tv2s, tv2e;
		u32 tv_count1, tv_count2;
		struct device_node *np = pp->dev->of_node;
		struct of_pci_range range;
		struct of_pci_range_parser parser;
		unsigned long restype;

		if (of_pci_range_parser_init(&parser, np)) {
			dev_err(pp->dev, "missing ranges property\n");
			return -EINVAL;
		}

		/* Get the memory ranges from DT */
		for_each_of_pci_range(&parser, &range) {
			restype = range.flags & IORESOURCE_TYPE_BITS;
			if (restype == IORESOURCE_MEM) {
				of_pci_range_to_resource(&range,
						np, &pp->mem);
				pp->mem.name = "MEM";
			}
		}

		pp->mem_base = pp->mem.start;

		/* enable disp_mix power domain */
		if (is_imx7d_pcie(imx6_pcie))
			pm_runtime_get_sync(pp->dev);

		imx6_pcie_assert_core_reset(pp);
		imx6_pcie_init_phy(pp);
		ret = imx6_pcie_deassert_core_reset(pp);
		if (ret < 0) {
			dev_err(&pdev->dev, "unable to init pcie ep.\n");
			return ret;
		}

		/*
		 * iMX6SX PCIe has the stand-alone power domain.
		 * refer to the initialization for iMX6SX PCIe,
		 * release the PCIe PHY reset here,
		 * before LTSSM enable is set
		 * .
		 */
		if (is_imx6sx_pcie(imx6_pcie))
			regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR5,
					BIT(19), 0 << 19);

		/* assert LTSSM enable */
		if (is_imx7d_pcie(imx6_pcie)) {
			regmap_update_bits(imx6_pcie->reg_src, 0x2c,
					BIT(6), BIT(6));
		} else {
			regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
					IMX6Q_GPR12_PCIE_CTL_2, 1 << 10);
		}

		dev_info(&pdev->dev, "PCIe EP: waiting for link up...\n");

		platform_set_drvdata(pdev, imx6_pcie);
		/* link is indicated by the bit4 of DB_R1 register */
		do {
			usleep_range(10, 20);
		} while ((readl(pp->dbi_base + PCIE_PHY_DEBUG_R1) & 0x10) == 0);

		imx6_pcie_setup_ep(pp);

		imx_pcie_regions_setup(&pdev->dev);

		/* self io test */
		test_reg1 = devm_kzalloc(&pdev->dev,
				test_region_size, GFP_KERNEL);
		if (!test_reg1) {
			pr_err("pcie ep: can't alloc the test reg1.\n");
			ret = PTR_ERR(test_reg1);
			return ret;
		}

		test_reg2 = devm_kzalloc(&pdev->dev,
				test_region_size, GFP_KERNEL);
		if (!test_reg2) {
			pr_err("pcie ep: can't alloc the test reg2.\n");
			ret = PTR_ERR(test_reg1);
			return ret;
		}

		/*
		 * FIXME when the ddr_test_region is mapped as cache-able,
		 * system hang when read the ddr memory content back from rc
		 * reserved ddr memory after write the ddr_test_region
		 * content to rc.
		 */
		pcie_arb_base_addr = ioremap_nocache(pp->mem_base,
					test_region_size);

		if (!pcie_arb_base_addr) {
			pr_err("error with ioremap in ep selftest\n");
			ret = PTR_ERR(pcie_arb_base_addr);
			return ret;
		}

		for (i = 0; i < test_region_size; i = i + 4) {
			writel(0xE6600D00 + i, test_reg1 + i);
			writel(0xDEADBEAF, test_reg2 + i);
		}

		/* PCIe EP start the data transfer after link up */
		pr_info("pcie ep: Starting data transfer...\n");
		do_gettimeofday(&tv1s);

		memcpy((unsigned int *)pcie_arb_base_addr,
				(unsigned int *)test_reg1,
				test_region_size);

		do_gettimeofday(&tv1e);

		do_gettimeofday(&tv2s);

		memcpy((unsigned int *)test_reg2,
				(unsigned int *)pcie_arb_base_addr,
				test_region_size);

		do_gettimeofday(&tv2e);
		if (memcmp(test_reg2, test_reg1, test_region_size) == 0) {
			tv_count1 = (tv1e.tv_sec - tv1s.tv_sec)
				* USEC_PER_SEC
				+ tv1e.tv_usec - tv1s.tv_usec;
			tv_count2 = (tv2e.tv_sec - tv2s.tv_sec)
				* USEC_PER_SEC
				+ tv2e.tv_usec - tv2s.tv_usec;

			pr_info("pcie ep: Data transfer is successful."
					" tv_count1 %dus,"
					" tv_count2 %dus.\n",
					tv_count1, tv_count2);
			pr_info("pcie ep: Data write speed:%ldMB/s.\n",
					((test_region_size/1024)
					   * MSEC_PER_SEC)
					/(tv_count1));
			pr_info("pcie ep: Data read speed:%ldMB/s.\n",
					((test_region_size/1024)
					   * MSEC_PER_SEC)
					/(tv_count2));
		} else {
			pr_info("pcie ep: Data transfer is failed.\n");
		} /* end of self io test. */
	} else {
		ret = imx6_add_pcie_port(pp, pdev);
		if (ret < 0)
			return ret;

		platform_set_drvdata(pdev, imx6_pcie);

		if (IS_ENABLED(CONFIG_RC_MODE_IN_EP_RC_SYS))
			imx_pcie_regions_setup(&pdev->dev);
	}
	return 0;
}

static void imx6_pcie_shutdown(struct platform_device *pdev)
{
	struct imx6_pcie *imx6_pcie = platform_get_drvdata(pdev);

	/* bring down link, so bootloader gets clean state in case of reboot */
	imx6_pcie_assert_core_reset(&imx6_pcie->pp);
}

static const struct of_device_id imx6_pcie_of_match[] = {
	{ .compatible = "fsl,imx6q-pcie", },
	{ .compatible = "fsl,imx6sx-pcie", },
	{ .compatible = "fsl,imx7d-pcie", },
	{ .compatible = "fsl,imx6qp-pcie", },
	{},
};
MODULE_DEVICE_TABLE(of, imx6_pcie_of_match);

static struct platform_driver imx6_pcie_driver = {
	.driver = {
		.name	= "imx6q-pcie",
		.of_match_table = imx6_pcie_of_match,
		.pm = &pci_imx_pm_ops,
	},
	.shutdown = imx6_pcie_shutdown,
};

/* Freescale PCIe driver does not allow module unload */

static int __init imx6_pcie_init(void)
{
	return platform_driver_probe(&imx6_pcie_driver, imx6_pcie_probe);
}
late_initcall(imx6_pcie_init);

MODULE_AUTHOR("Sean Cross <xobs@kosagi.com>");
MODULE_DESCRIPTION("Freescale i.MX6 PCIe host controller driver");
MODULE_LICENSE("GPL v2");
