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
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/resource.h>
#include <linux/signal.h>
#include <linux/types.h>
#include <linux/interrupt.h>

#include "pcie-designware.h"

#define to_imx6_pcie(x)	container_of(x, struct imx6_pcie, pp)

struct imx6_pcie {
	int			reset_gpio;
	struct clk		*pcie_bus;
	struct clk		*pcie_phy;
	struct clk		*pcie;
	struct pcie_port	pp;
	struct regmap		*iomuxc_gpr;
	void __iomem		*mem_base;
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

	/*
	 * If the bootloader already enabled the link we need some special
	 * handling to get the core back into a state where it is safe to
	 * touch it for configuration.  As there is no dedicated reset signal
	 * wired up for MX6QDL, we need to manually force LTSSM into "detect"
	 * state before completely disabling LTSSM, which is a prerequisite
	 * for core configuration.
	 *
	 * If both LTSSM_ENABLE and REF_SSP_ENABLE are active we have a strong
	 * indication that the bootloader activated the link.
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

	return 0;
}

static int imx6_pcie_deassert_core_reset(struct pcie_port *pp)
{
	struct imx6_pcie *imx6_pcie = to_imx6_pcie(pp);
	int ret;

	ret = clk_prepare_enable(imx6_pcie->pcie_phy);
	if (ret) {
		dev_err(pp->dev, "unable to enable pcie_phy clock\n");
		goto err_pcie_phy;
	}

	ret = clk_prepare_enable(imx6_pcie->pcie_bus);
	if (ret) {
		dev_err(pp->dev, "unable to enable pcie_bus clock\n");
		goto err_pcie_bus;
	}

	ret = clk_prepare_enable(imx6_pcie->pcie);
	if (ret) {
		dev_err(pp->dev, "unable to enable pcie clock\n");
		goto err_pcie;
	}

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

	/* allow the clocks to stabilize */
	usleep_range(200, 500);

	/* Some boards don't have PCIe reset GPIO. */
	if (gpio_is_valid(imx6_pcie->reset_gpio)) {
		gpio_set_value(imx6_pcie->reset_gpio, 0);
		msleep(100);
		gpio_set_value(imx6_pcie->reset_gpio, 1);
	}
	return 0;

err_pcie:
	clk_disable_unprepare(imx6_pcie->pcie_bus);
err_pcie_bus:
	clk_disable_unprepare(imx6_pcie->pcie_phy);
err_pcie_phy:
	return ret;

}

static void imx6_pcie_init_phy(struct pcie_port *pp)
{
	struct imx6_pcie *imx6_pcie = to_imx6_pcie(pp);

	regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
			IMX6Q_GPR12_PCIE_CTL_2, 0 << 10);

	/* configure constant input signal to the pcie ctrl and phy */
	regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
			IMX6Q_GPR12_DEVICE_TYPE, PCI_EXP_TYPE_ROOT_PORT << 12);
	regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
			IMX6Q_GPR12_LOS_LEVEL, 9 << 4);

	regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR8,
			IMX6Q_GPR8_TX_DEEMPH_GEN1, 0 << 0);
	regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR8,
			IMX6Q_GPR8_TX_DEEMPH_GEN2_3P5DB, 0 << 6);
	regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR8,
			IMX6Q_GPR8_TX_DEEMPH_GEN2_6DB, 20 << 12);
	regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR8,
			IMX6Q_GPR8_TX_SWING_FULL, 127 << 18);
	regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR8,
			IMX6Q_GPR8_TX_SWING_LOW, 127 << 25);
}

static int imx6_pcie_wait_for_link(struct pcie_port *pp)
{
	int count = 200;

	while (!dw_pcie_link_up(pp)) {
		usleep_range(100, 1000);
		if (--count)
			continue;

		dev_err(pp->dev, "phy link never came up\n");
		dev_dbg(pp->dev, "DEBUG_R0: 0x%08x, DEBUG_R1: 0x%08x\n",
			readl(pp->dbi_base + PCIE_PHY_DEBUG_R0),
			readl(pp->dbi_base + PCIE_PHY_DEBUG_R1));
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
	tmp = readl(pp->dbi_base + PCIE_RC_LCR);
	tmp &= ~PCIE_RC_LCR_MAX_LINK_SPEEDS_MASK;
	tmp |= PCIE_RC_LCR_MAX_LINK_SPEEDS_GEN1;
	writel(tmp, pp->dbi_base + PCIE_RC_LCR);

	/* Start LTSSM. */
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

	count = 200;
	while (count--) {
		tmp = readl(pp->dbi_base + PCIE_LINK_WIDTH_SPEED_CONTROL);
		/* Test if the speed change finished. */
		if (!(tmp & PORT_LOGIC_SPEED_CHANGE))
			break;
		usleep_range(100, 1000);
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

static void imx6_pcie_host_init(struct pcie_port *pp)
{
	imx6_pcie_assert_core_reset(pp);

	imx6_pcie_init_phy(pp);

	imx6_pcie_deassert_core_reset(pp);

	dw_pcie_setup_rc(pp);

	imx6_pcie_start_link(pp);

	if (IS_ENABLED(CONFIG_PCI_MSI))
		dw_pcie_msi_init(pp);
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
	int count = 5;

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
		usleep_range(1000, 2000);
	}
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

	/* Added for PCI abort handling */
	hook_fault_code(16 + 6, imx6q_pcie_abort_handler, SIGBUS, 0,
		"imprecise external abort");

	dbi_base = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pp->dbi_base = devm_ioremap_resource(&pdev->dev, dbi_base);
	if (IS_ERR(pp->dbi_base))
		return PTR_ERR(pp->dbi_base);

	/* Fetch GPIOs */
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

	imx6_pcie->pcie = devm_clk_get(&pdev->dev, "pcie");
	if (IS_ERR(imx6_pcie->pcie)) {
		dev_err(&pdev->dev,
			"pcie clock source missing or invalid\n");
		return PTR_ERR(imx6_pcie->pcie);
	}

	/* Grab GPR config register range */
	imx6_pcie->iomuxc_gpr =
		 syscon_regmap_lookup_by_compatible("fsl,imx6q-iomuxc-gpr");
	if (IS_ERR(imx6_pcie->iomuxc_gpr)) {
		dev_err(&pdev->dev, "unable to find iomuxc registers\n");
		return PTR_ERR(imx6_pcie->iomuxc_gpr);
	}

	ret = imx6_add_pcie_port(pp, pdev);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, imx6_pcie);
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
	{},
};
MODULE_DEVICE_TABLE(of, imx6_pcie_of_match);

static struct platform_driver imx6_pcie_driver = {
	.driver = {
		.name	= "imx6q-pcie",
		.owner	= THIS_MODULE,
		.of_match_table = imx6_pcie_of_match,
	},
	.shutdown = imx6_pcie_shutdown,
};

/* Freescale PCIe driver does not allow module unload */

static int __init imx6_pcie_init(void)
{
	return platform_driver_probe(&imx6_pcie_driver, imx6_pcie_probe);
}
module_init(imx6_pcie_init);

MODULE_AUTHOR("Sean Cross <xobs@kosagi.com>");
MODULE_DESCRIPTION("Freescale i.MX6 PCIe host controller driver");
MODULE_LICENSE("GPL v2");
