// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for Freescale i.MX6 SoCs
 *
 * Copyright (C) 2013 Kosagi
 *		https://www.kosagi.com
 *
 * Author: Sean Cross <xobs@kosagi.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/imx6q-iomuxc-gpr.h>
#include <linux/mfd/syscon/imx7-iomuxc-gpr.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_address.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/resource.h>
#include <linux/signal.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/reset.h>
#include <linux/phy/phy.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>

#include "pcie-designware.h"

#define IMX8MQ_GPR_PCIE_REF_USE_PAD		BIT(9)
#define IMX8MQ_GPR_PCIE_CLK_REQ_OVERRIDE_EN	BIT(10)
#define IMX8MQ_GPR_PCIE_CLK_REQ_OVERRIDE	BIT(11)
#define IMX8MQ_GPR_PCIE_VREG_BYPASS		BIT(12)
#define IMX8MQ_GPR12_PCIE2_CTRL_DEVICE_TYPE	GENMASK(11, 8)
#define IMX8MQ_PCIE2_BASE_ADDR			0x33c00000

#define to_imx6_pcie(x)	dev_get_drvdata((x)->dev)

enum imx6_pcie_variants {
	IMX6Q,
	IMX6SX,
	IMX6QP,
	IMX7D,
	IMX8MQ,
	IMX8MM,
	IMX8MP,
	IMX8MQ_EP,
	IMX8MM_EP,
	IMX8MP_EP,
};

#define IMX6_PCIE_FLAG_IMX6_PHY			BIT(0)
#define IMX6_PCIE_FLAG_IMX6_SPEED_CHANGE	BIT(1)
#define IMX6_PCIE_FLAG_SUPPORTS_SUSPEND		BIT(2)

struct imx6_pcie_drvdata {
	enum imx6_pcie_variants variant;
	enum dw_pcie_device_mode mode;
	u32 flags;
	int dbi_length;
	const char *gpr;
};

struct imx6_pcie {
	struct dw_pcie		*pci;
	int			reset_gpio;
	bool			gpio_active_high;
	bool			link_is_up;
	struct clk		*pcie_bus;
	struct clk		*pcie_phy;
	struct clk		*pcie_inbound_axi;
	struct clk		*pcie;
	struct clk		*pcie_aux;
	struct regmap		*iomuxc_gpr;
	u16			msi_ctrl;
	u32			controller_id;
	struct reset_control	*pciephy_reset;
	struct reset_control	*apps_reset;
	struct reset_control	*turnoff_reset;
	u32			tx_deemph_gen1;
	u32			tx_deemph_gen2_3p5db;
	u32			tx_deemph_gen2_6db;
	u32			tx_swing_full;
	u32			tx_swing_low;
	struct regulator	*vpcie;
	struct regulator	*vph;
	void __iomem		*phy_base;

	/* power domain for pcie */
	struct device		*pd_pcie;
	/* power domain for pcie phy */
	struct device		*pd_pcie_phy;
	struct phy		*phy;
	const struct imx6_pcie_drvdata *drvdata;
};

/* Parameters for the waiting for PCIe PHY PLL to lock on i.MX7 */
#define PHY_PLL_LOCK_WAIT_USLEEP_MAX	200
#define PHY_PLL_LOCK_WAIT_TIMEOUT	(2000 * PHY_PLL_LOCK_WAIT_USLEEP_MAX)

/* PCIe Port Logic registers (memory-mapped) */
#define PL_OFFSET 0x700

#define PCIE_PHY_CTRL (PL_OFFSET + 0x114)
#define PCIE_PHY_CTRL_DATA(x)		FIELD_PREP(GENMASK(15, 0), (x))
#define PCIE_PHY_CTRL_CAP_ADR		BIT(16)
#define PCIE_PHY_CTRL_CAP_DAT		BIT(17)
#define PCIE_PHY_CTRL_WR		BIT(18)
#define PCIE_PHY_CTRL_RD		BIT(19)

#define PCIE_PHY_STAT (PL_OFFSET + 0x110)
#define PCIE_PHY_STAT_ACK		BIT(16)

/* PHY registers (not memory-mapped) */
#define PCIE_PHY_ATEOVRD			0x10
#define  PCIE_PHY_ATEOVRD_EN			BIT(2)
#define  PCIE_PHY_ATEOVRD_REF_CLKDIV_SHIFT	0
#define  PCIE_PHY_ATEOVRD_REF_CLKDIV_MASK	0x1

#define PCIE_PHY_MPLL_OVRD_IN_LO		0x11
#define  PCIE_PHY_MPLL_MULTIPLIER_SHIFT		2
#define  PCIE_PHY_MPLL_MULTIPLIER_MASK		0x7f
#define  PCIE_PHY_MPLL_MULTIPLIER_OVRD		BIT(9)

#define PCIE_PHY_RX_ASIC_OUT 0x100D
#define PCIE_PHY_RX_ASIC_OUT_VALID	(1 << 0)

/* iMX7 PCIe PHY registers */
#define PCIE_PHY_CMN_REG4		0x14
/* These are probably the bits that *aren't* DCC_FB_EN */
#define PCIE_PHY_CMN_REG4_DCC_FB_EN	0x29

#define PCIE_PHY_CMN_REG15	        0x54
#define PCIE_PHY_CMN_REG15_DLY_4	BIT(2)
#define PCIE_PHY_CMN_REG15_PLL_PD	BIT(5)
#define PCIE_PHY_CMN_REG15_OVRD_PLL_PD	BIT(7)

#define PCIE_PHY_CMN_REG24		0x90
#define PCIE_PHY_CMN_REG24_RX_EQ	BIT(6)
#define PCIE_PHY_CMN_REG24_RX_EQ_SEL	BIT(3)

#define PCIE_PHY_CMN_REG26		0x98
#define PCIE_PHY_CMN_REG26_ATT_MODE	0xBC

#define PHY_RX_OVRD_IN_LO 0x1005
#define PHY_RX_OVRD_IN_LO_RX_DATA_EN		BIT(5)
#define PHY_RX_OVRD_IN_LO_RX_PLL_EN		BIT(3)

static unsigned int imx6_pcie_grp_offset(const struct imx6_pcie *imx6_pcie)
{
	WARN_ON(imx6_pcie->drvdata->variant != IMX8MQ &&
		imx6_pcie->drvdata->variant != IMX8MQ_EP &&
		imx6_pcie->drvdata->variant != IMX8MM &&
		imx6_pcie->drvdata->variant != IMX8MM_EP &&
		imx6_pcie->drvdata->variant != IMX8MP &&
		imx6_pcie->drvdata->variant != IMX8MP_EP);
	return imx6_pcie->controller_id == 1 ? IOMUXC_GPR16 : IOMUXC_GPR14;
}

static void imx6_pcie_configure_type(struct imx6_pcie *imx6_pcie)
{
	unsigned int mask, val, mode;

	if (imx6_pcie->drvdata->mode == DW_PCIE_EP_TYPE)
		mode = PCI_EXP_TYPE_ENDPOINT;
	else
		mode = PCI_EXP_TYPE_ROOT_PORT;

	switch (imx6_pcie->drvdata->variant) {
	case IMX8MQ:
	case IMX8MQ_EP:
		if (imx6_pcie->controller_id == 1) {
			mask = IMX8MQ_GPR12_PCIE2_CTRL_DEVICE_TYPE;
			val  = FIELD_PREP(IMX8MQ_GPR12_PCIE2_CTRL_DEVICE_TYPE,
					  mode);
		} else {
			mask = IMX6Q_GPR12_DEVICE_TYPE;
			val  = FIELD_PREP(IMX6Q_GPR12_DEVICE_TYPE, mode);
		}
		break;
	default:
		mask = IMX6Q_GPR12_DEVICE_TYPE;
		val  = FIELD_PREP(IMX6Q_GPR12_DEVICE_TYPE, mode);
		break;
	}

	regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12, mask, val);
}

static int pcie_phy_poll_ack(struct imx6_pcie *imx6_pcie, bool exp_val)
{
	struct dw_pcie *pci = imx6_pcie->pci;
	bool val;
	u32 max_iterations = 10;
	u32 wait_counter = 0;

	do {
		val = dw_pcie_readl_dbi(pci, PCIE_PHY_STAT) &
			PCIE_PHY_STAT_ACK;
		wait_counter++;

		if (val == exp_val)
			return 0;

		udelay(1);
	} while (wait_counter < max_iterations);

	return -ETIMEDOUT;
}

static int pcie_phy_wait_ack(struct imx6_pcie *imx6_pcie, int addr)
{
	struct dw_pcie *pci = imx6_pcie->pci;
	u32 val;
	int ret;

	val = PCIE_PHY_CTRL_DATA(addr);
	dw_pcie_writel_dbi(pci, PCIE_PHY_CTRL, val);

	val |= PCIE_PHY_CTRL_CAP_ADR;
	dw_pcie_writel_dbi(pci, PCIE_PHY_CTRL, val);

	ret = pcie_phy_poll_ack(imx6_pcie, true);
	if (ret)
		return ret;

	val = PCIE_PHY_CTRL_DATA(addr);
	dw_pcie_writel_dbi(pci, PCIE_PHY_CTRL, val);

	return pcie_phy_poll_ack(imx6_pcie, false);
}

/* Read from the 16-bit PCIe PHY control registers (not memory-mapped) */
static int pcie_phy_read(struct imx6_pcie *imx6_pcie, int addr, u16 *data)
{
	struct dw_pcie *pci = imx6_pcie->pci;
	u32 phy_ctl;
	int ret;

	ret = pcie_phy_wait_ack(imx6_pcie, addr);
	if (ret)
		return ret;

	/* assert Read signal */
	phy_ctl = PCIE_PHY_CTRL_RD;
	dw_pcie_writel_dbi(pci, PCIE_PHY_CTRL, phy_ctl);

	ret = pcie_phy_poll_ack(imx6_pcie, true);
	if (ret)
		return ret;

	*data = dw_pcie_readl_dbi(pci, PCIE_PHY_STAT);

	/* deassert Read signal */
	dw_pcie_writel_dbi(pci, PCIE_PHY_CTRL, 0x00);

	return pcie_phy_poll_ack(imx6_pcie, false);
}

static int pcie_phy_write(struct imx6_pcie *imx6_pcie, int addr, u16 data)
{
	struct dw_pcie *pci = imx6_pcie->pci;
	u32 var;
	int ret;

	/* write addr */
	/* cap addr */
	ret = pcie_phy_wait_ack(imx6_pcie, addr);
	if (ret)
		return ret;

	var = PCIE_PHY_CTRL_DATA(data);
	dw_pcie_writel_dbi(pci, PCIE_PHY_CTRL, var);

	/* capture data */
	var |= PCIE_PHY_CTRL_CAP_DAT;
	dw_pcie_writel_dbi(pci, PCIE_PHY_CTRL, var);

	ret = pcie_phy_poll_ack(imx6_pcie, true);
	if (ret)
		return ret;

	/* deassert cap data */
	var = PCIE_PHY_CTRL_DATA(data);
	dw_pcie_writel_dbi(pci, PCIE_PHY_CTRL, var);

	/* wait for ack de-assertion */
	ret = pcie_phy_poll_ack(imx6_pcie, false);
	if (ret)
		return ret;

	/* assert wr signal */
	var = PCIE_PHY_CTRL_WR;
	dw_pcie_writel_dbi(pci, PCIE_PHY_CTRL, var);

	/* wait for ack */
	ret = pcie_phy_poll_ack(imx6_pcie, true);
	if (ret)
		return ret;

	/* deassert wr signal */
	var = PCIE_PHY_CTRL_DATA(data);
	dw_pcie_writel_dbi(pci, PCIE_PHY_CTRL, var);

	/* wait for ack de-assertion */
	ret = pcie_phy_poll_ack(imx6_pcie, false);
	if (ret)
		return ret;

	dw_pcie_writel_dbi(pci, PCIE_PHY_CTRL, 0x0);

	return 0;
}

static void imx6_pcie_init_phy(struct imx6_pcie *imx6_pcie)
{
	switch (imx6_pcie->drvdata->variant) {
	case IMX8MM:
	case IMX8MM_EP:
	case IMX8MP:
	case IMX8MP_EP:
		/*
		 * The PHY initialization had been done in the PHY
		 * driver, break here directly.
		 */
		break;
	case IMX8MQ:
	case IMX8MQ_EP:
		/*
		 * TODO: Currently this code assumes external
		 * oscillator is being used
		 */
		regmap_update_bits(imx6_pcie->iomuxc_gpr,
				   imx6_pcie_grp_offset(imx6_pcie),
				   IMX8MQ_GPR_PCIE_REF_USE_PAD,
				   IMX8MQ_GPR_PCIE_REF_USE_PAD);
		/*
		 * Regarding the datasheet, the PCIE_VPH is suggested
		 * to be 1.8V. If the PCIE_VPH is supplied by 3.3V, the
		 * VREG_BYPASS should be cleared to zero.
		 */
		if (imx6_pcie->vph &&
		    regulator_get_voltage(imx6_pcie->vph) > 3000000)
			regmap_update_bits(imx6_pcie->iomuxc_gpr,
					   imx6_pcie_grp_offset(imx6_pcie),
					   IMX8MQ_GPR_PCIE_VREG_BYPASS,
					   0);
		break;
	case IMX7D:
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
				   IMX7D_GPR12_PCIE_PHY_REFCLK_SEL, 0);
		break;
	case IMX6SX:
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
				   IMX6SX_GPR12_PCIE_RX_EQ_MASK,
				   IMX6SX_GPR12_PCIE_RX_EQ_2);
		fallthrough;
	default:
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
				   IMX6Q_GPR12_PCIE_CTL_2, 0 << 10);

		/* configure constant input signal to the pcie ctrl and phy */
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
				   IMX6Q_GPR12_LOS_LEVEL, 9 << 4);

		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR8,
				   IMX6Q_GPR8_TX_DEEMPH_GEN1,
				   imx6_pcie->tx_deemph_gen1 << 0);
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR8,
				   IMX6Q_GPR8_TX_DEEMPH_GEN2_3P5DB,
				   imx6_pcie->tx_deemph_gen2_3p5db << 6);
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR8,
				   IMX6Q_GPR8_TX_DEEMPH_GEN2_6DB,
				   imx6_pcie->tx_deemph_gen2_6db << 12);
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR8,
				   IMX6Q_GPR8_TX_SWING_FULL,
				   imx6_pcie->tx_swing_full << 18);
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR8,
				   IMX6Q_GPR8_TX_SWING_LOW,
				   imx6_pcie->tx_swing_low << 25);
		break;
	}

	imx6_pcie_configure_type(imx6_pcie);
}

static void imx7d_pcie_wait_for_phy_pll_lock(struct imx6_pcie *imx6_pcie)
{
	u32 val;
	struct device *dev = imx6_pcie->pci->dev;

	if (regmap_read_poll_timeout(imx6_pcie->iomuxc_gpr,
				     IOMUXC_GPR22, val,
				     val & IMX7D_GPR22_PCIE_PHY_PLL_LOCKED,
				     PHY_PLL_LOCK_WAIT_USLEEP_MAX,
				     PHY_PLL_LOCK_WAIT_TIMEOUT))
		dev_err(dev, "PCIe PLL lock timeout\n");
}

static int imx6_setup_phy_mpll(struct imx6_pcie *imx6_pcie)
{
	unsigned long phy_rate = clk_get_rate(imx6_pcie->pcie_phy);
	int mult, div;
	u16 val;

	if (!(imx6_pcie->drvdata->flags & IMX6_PCIE_FLAG_IMX6_PHY))
		return 0;

	switch (phy_rate) {
	case 125000000:
		/*
		 * The default settings of the MPLL are for a 125MHz input
		 * clock, so no need to reconfigure anything in that case.
		 */
		return 0;
	case 100000000:
		mult = 25;
		div = 0;
		break;
	case 200000000:
		mult = 25;
		div = 1;
		break;
	default:
		dev_err(imx6_pcie->pci->dev,
			"Unsupported PHY reference clock rate %lu\n", phy_rate);
		return -EINVAL;
	}

	pcie_phy_read(imx6_pcie, PCIE_PHY_MPLL_OVRD_IN_LO, &val);
	val &= ~(PCIE_PHY_MPLL_MULTIPLIER_MASK <<
		 PCIE_PHY_MPLL_MULTIPLIER_SHIFT);
	val |= mult << PCIE_PHY_MPLL_MULTIPLIER_SHIFT;
	val |= PCIE_PHY_MPLL_MULTIPLIER_OVRD;
	pcie_phy_write(imx6_pcie, PCIE_PHY_MPLL_OVRD_IN_LO, val);

	pcie_phy_read(imx6_pcie, PCIE_PHY_ATEOVRD, &val);
	val &= ~(PCIE_PHY_ATEOVRD_REF_CLKDIV_MASK <<
		 PCIE_PHY_ATEOVRD_REF_CLKDIV_SHIFT);
	val |= div << PCIE_PHY_ATEOVRD_REF_CLKDIV_SHIFT;
	val |= PCIE_PHY_ATEOVRD_EN;
	pcie_phy_write(imx6_pcie, PCIE_PHY_ATEOVRD, val);

	return 0;
}

static void imx6_pcie_reset_phy(struct imx6_pcie *imx6_pcie)
{
	u16 tmp;

	if (!(imx6_pcie->drvdata->flags & IMX6_PCIE_FLAG_IMX6_PHY))
		return;

	pcie_phy_read(imx6_pcie, PHY_RX_OVRD_IN_LO, &tmp);
	tmp |= (PHY_RX_OVRD_IN_LO_RX_DATA_EN |
		PHY_RX_OVRD_IN_LO_RX_PLL_EN);
	pcie_phy_write(imx6_pcie, PHY_RX_OVRD_IN_LO, tmp);

	usleep_range(2000, 3000);

	pcie_phy_read(imx6_pcie, PHY_RX_OVRD_IN_LO, &tmp);
	tmp &= ~(PHY_RX_OVRD_IN_LO_RX_DATA_EN |
		  PHY_RX_OVRD_IN_LO_RX_PLL_EN);
	pcie_phy_write(imx6_pcie, PHY_RX_OVRD_IN_LO, tmp);
}

#ifdef CONFIG_ARM
/*  Added for PCI abort handling */
static int imx6q_pcie_abort_handler(unsigned long addr,
		unsigned int fsr, struct pt_regs *regs)
{
	unsigned long pc = instruction_pointer(regs);
	unsigned long instr = *(unsigned long *)pc;
	int reg = (instr >> 12) & 15;

	/*
	 * If the instruction being executed was a read,
	 * make it look like it read all-ones.
	 */
	if ((instr & 0x0c100000) == 0x04100000) {
		unsigned long val;

		if (instr & 0x00400000)
			val = 255;
		else
			val = -1;

		regs->uregs[reg] = val;
		regs->ARM_pc += 4;
		return 0;
	}

	if ((instr & 0x0e100090) == 0x00100090) {
		regs->uregs[reg] = -1;
		regs->ARM_pc += 4;
		return 0;
	}

	return 1;
}
#endif

static int imx6_pcie_attach_pd(struct device *dev)
{
	struct imx6_pcie *imx6_pcie = dev_get_drvdata(dev);
	struct device_link *link;

	/* Do nothing when in a single power domain */
	if (dev->pm_domain)
		return 0;

	imx6_pcie->pd_pcie = dev_pm_domain_attach_by_name(dev, "pcie");
	if (IS_ERR(imx6_pcie->pd_pcie))
		return PTR_ERR(imx6_pcie->pd_pcie);
	/* Do nothing when power domain missing */
	if (!imx6_pcie->pd_pcie)
		return 0;
	link = device_link_add(dev, imx6_pcie->pd_pcie,
			DL_FLAG_STATELESS |
			DL_FLAG_PM_RUNTIME |
			DL_FLAG_RPM_ACTIVE);
	if (!link) {
		dev_err(dev, "Failed to add device_link to pcie pd.\n");
		return -EINVAL;
	}

	imx6_pcie->pd_pcie_phy = dev_pm_domain_attach_by_name(dev, "pcie_phy");
	if (IS_ERR(imx6_pcie->pd_pcie_phy))
		return PTR_ERR(imx6_pcie->pd_pcie_phy);

	link = device_link_add(dev, imx6_pcie->pd_pcie_phy,
			DL_FLAG_STATELESS |
			DL_FLAG_PM_RUNTIME |
			DL_FLAG_RPM_ACTIVE);
	if (!link) {
		dev_err(dev, "Failed to add device_link to pcie_phy pd.\n");
		return -EINVAL;
	}

	return 0;
}

static int imx6_pcie_enable_ref_clk(struct imx6_pcie *imx6_pcie)
{
	struct dw_pcie *pci = imx6_pcie->pci;
	struct device *dev = pci->dev;
	unsigned int offset;
	int ret = 0;

	switch (imx6_pcie->drvdata->variant) {
	case IMX6SX:
		ret = clk_prepare_enable(imx6_pcie->pcie_inbound_axi);
		if (ret) {
			dev_err(dev, "unable to enable pcie_axi clock\n");
			break;
		}

		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
				   IMX6SX_GPR12_PCIE_TEST_POWERDOWN, 0);
		break;
	case IMX6QP:
	case IMX6Q:
		/* power up core phy and enable ref clock */
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR1,
				   IMX6Q_GPR1_PCIE_TEST_PD, 0 << 18);
		/*
		 * the async reset input need ref clock to sync internally,
		 * when the ref clock comes after reset, internal synced
		 * reset time is too short, cannot meet the requirement.
		 * add one ~10us delay here.
		 */
		usleep_range(10, 100);
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR1,
				   IMX6Q_GPR1_PCIE_REF_CLK_EN, 1 << 16);
		break;
	case IMX7D:
		break;
	case IMX8MM:
	case IMX8MM_EP:
	case IMX8MQ:
	case IMX8MQ_EP:
	case IMX8MP:
	case IMX8MP_EP:
		ret = clk_prepare_enable(imx6_pcie->pcie_aux);
		if (ret) {
			dev_err(dev, "unable to enable pcie_aux clock\n");
			break;
		}

		offset = imx6_pcie_grp_offset(imx6_pcie);
		/*
		 * Set the over ride low and enabled
		 * make sure that REF_CLK is turned on.
		 */
		regmap_update_bits(imx6_pcie->iomuxc_gpr, offset,
				   IMX8MQ_GPR_PCIE_CLK_REQ_OVERRIDE,
				   0);
		regmap_update_bits(imx6_pcie->iomuxc_gpr, offset,
				   IMX8MQ_GPR_PCIE_CLK_REQ_OVERRIDE_EN,
				   IMX8MQ_GPR_PCIE_CLK_REQ_OVERRIDE_EN);
		break;
	}

	return ret;
}

static void imx6_pcie_disable_ref_clk(struct imx6_pcie *imx6_pcie)
{
	switch (imx6_pcie->drvdata->variant) {
	case IMX6SX:
		clk_disable_unprepare(imx6_pcie->pcie_inbound_axi);
		break;
	case IMX6QP:
	case IMX6Q:
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR1,
				IMX6Q_GPR1_PCIE_REF_CLK_EN, 0);
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR1,
				IMX6Q_GPR1_PCIE_TEST_PD,
				IMX6Q_GPR1_PCIE_TEST_PD);
		break;
	case IMX7D:
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
				   IMX7D_GPR12_PCIE_PHY_REFCLK_SEL,
				   IMX7D_GPR12_PCIE_PHY_REFCLK_SEL);
		break;
	case IMX8MM:
	case IMX8MM_EP:
	case IMX8MQ:
	case IMX8MQ_EP:
	case IMX8MP:
	case IMX8MP_EP:
		clk_disable_unprepare(imx6_pcie->pcie_aux);
		break;
	default:
		break;
	}
}

static int imx6_pcie_clk_enable(struct imx6_pcie *imx6_pcie)
{
	struct dw_pcie *pci = imx6_pcie->pci;
	struct device *dev = pci->dev;
	int ret;

	ret = clk_prepare_enable(imx6_pcie->pcie_phy);
	if (ret) {
		dev_err(dev, "unable to enable pcie_phy clock\n");
		return ret;
	}

	ret = clk_prepare_enable(imx6_pcie->pcie_bus);
	if (ret) {
		dev_err(dev, "unable to enable pcie_bus clock\n");
		goto err_pcie_bus;
	}

	ret = clk_prepare_enable(imx6_pcie->pcie);
	if (ret) {
		dev_err(dev, "unable to enable pcie clock\n");
		goto err_pcie;
	}

	ret = imx6_pcie_enable_ref_clk(imx6_pcie);
	if (ret) {
		dev_err(dev, "unable to enable pcie ref clock\n");
		goto err_ref_clk;
	}

	/* allow the clocks to stabilize */
	usleep_range(200, 500);
	return 0;

err_ref_clk:
	clk_disable_unprepare(imx6_pcie->pcie);
err_pcie:
	clk_disable_unprepare(imx6_pcie->pcie_bus);
err_pcie_bus:
	clk_disable_unprepare(imx6_pcie->pcie_phy);

	return ret;
}

static void imx6_pcie_clk_disable(struct imx6_pcie *imx6_pcie)
{
	imx6_pcie_disable_ref_clk(imx6_pcie);
	clk_disable_unprepare(imx6_pcie->pcie);
	clk_disable_unprepare(imx6_pcie->pcie_bus);
	clk_disable_unprepare(imx6_pcie->pcie_phy);
}

static void imx6_pcie_assert_core_reset(struct imx6_pcie *imx6_pcie)
{
	switch (imx6_pcie->drvdata->variant) {
	case IMX7D:
	case IMX8MQ:
	case IMX8MQ_EP:
		reset_control_assert(imx6_pcie->pciephy_reset);
		fallthrough;
	case IMX8MM:
	case IMX8MM_EP:
	case IMX8MP:
	case IMX8MP_EP:
		reset_control_assert(imx6_pcie->apps_reset);
		break;
	case IMX6SX:
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
				   IMX6SX_GPR12_PCIE_TEST_POWERDOWN,
				   IMX6SX_GPR12_PCIE_TEST_POWERDOWN);
		/* Force PCIe PHY reset */
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR5,
				   IMX6SX_GPR5_PCIE_BTNRST_RESET,
				   IMX6SX_GPR5_PCIE_BTNRST_RESET);
		break;
	case IMX6QP:
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR1,
				   IMX6Q_GPR1_PCIE_SW_RST,
				   IMX6Q_GPR1_PCIE_SW_RST);
		break;
	case IMX6Q:
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR1,
				   IMX6Q_GPR1_PCIE_TEST_PD, 1 << 18);
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR1,
				   IMX6Q_GPR1_PCIE_REF_CLK_EN, 0 << 16);
		break;
	}

	/* Some boards don't have PCIe reset GPIO. */
	if (gpio_is_valid(imx6_pcie->reset_gpio))
		gpio_set_value_cansleep(imx6_pcie->reset_gpio,
					imx6_pcie->gpio_active_high);
}

static int imx6_pcie_deassert_core_reset(struct imx6_pcie *imx6_pcie)
{
	struct dw_pcie *pci = imx6_pcie->pci;
	struct device *dev = pci->dev;

	switch (imx6_pcie->drvdata->variant) {
	case IMX8MQ:
	case IMX8MQ_EP:
		reset_control_deassert(imx6_pcie->pciephy_reset);
		break;
	case IMX7D:
		reset_control_deassert(imx6_pcie->pciephy_reset);

		/* Workaround for ERR010728, failure of PCI-e PLL VCO to
		 * oscillate, especially when cold.  This turns off "Duty-cycle
		 * Corrector" and other mysterious undocumented things.
		 */
		if (likely(imx6_pcie->phy_base)) {
			/* De-assert DCC_FB_EN */
			writel(PCIE_PHY_CMN_REG4_DCC_FB_EN,
			       imx6_pcie->phy_base + PCIE_PHY_CMN_REG4);
			/* Assert RX_EQS and RX_EQS_SEL */
			writel(PCIE_PHY_CMN_REG24_RX_EQ_SEL
				| PCIE_PHY_CMN_REG24_RX_EQ,
			       imx6_pcie->phy_base + PCIE_PHY_CMN_REG24);
			/* Assert ATT_MODE */
			writel(PCIE_PHY_CMN_REG26_ATT_MODE,
			       imx6_pcie->phy_base + PCIE_PHY_CMN_REG26);
		} else {
			dev_warn(dev, "Unable to apply ERR010728 workaround. DT missing fsl,imx7d-pcie-phy phandle ?\n");
		}

		imx7d_pcie_wait_for_phy_pll_lock(imx6_pcie);
		break;
	case IMX6SX:
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR5,
				   IMX6SX_GPR5_PCIE_BTNRST_RESET, 0);
		break;
	case IMX6QP:
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR1,
				   IMX6Q_GPR1_PCIE_SW_RST, 0);

		usleep_range(200, 500);
		break;
	case IMX6Q:		/* Nothing to do */
	case IMX8MM:
	case IMX8MM_EP:
	case IMX8MP:
	case IMX8MP_EP:
		break;
	}

	/* Some boards don't have PCIe reset GPIO. */
	if (gpio_is_valid(imx6_pcie->reset_gpio)) {
		msleep(100);
		gpio_set_value_cansleep(imx6_pcie->reset_gpio,
					!imx6_pcie->gpio_active_high);
		/* Wait for 100ms after PERST# deassertion (PCIe r5.0, 6.6.1) */
		msleep(100);
	}

	return 0;
}

static int imx6_pcie_wait_for_speed_change(struct imx6_pcie *imx6_pcie)
{
	struct dw_pcie *pci = imx6_pcie->pci;
	struct device *dev = pci->dev;
	u32 tmp;
	unsigned int retries;

	for (retries = 0; retries < 200; retries++) {
		tmp = dw_pcie_readl_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL);
		/* Test if the speed change finished. */
		if (!(tmp & PORT_LOGIC_SPEED_CHANGE))
			return 0;
		usleep_range(100, 1000);
	}

	dev_err(dev, "Speed change timeout\n");
	return -ETIMEDOUT;
}

static void imx6_pcie_ltssm_enable(struct device *dev)
{
	struct imx6_pcie *imx6_pcie = dev_get_drvdata(dev);

	switch (imx6_pcie->drvdata->variant) {
	case IMX6Q:
	case IMX6SX:
	case IMX6QP:
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
				   IMX6Q_GPR12_PCIE_CTL_2,
				   IMX6Q_GPR12_PCIE_CTL_2);
		break;
	case IMX7D:
	case IMX8MQ:
	case IMX8MQ_EP:
	case IMX8MM:
	case IMX8MM_EP:
	case IMX8MP:
	case IMX8MP_EP:
		reset_control_deassert(imx6_pcie->apps_reset);
		break;
	}
}

static void imx6_pcie_ltssm_disable(struct device *dev)
{
	struct imx6_pcie *imx6_pcie = dev_get_drvdata(dev);

	switch (imx6_pcie->drvdata->variant) {
	case IMX6Q:
	case IMX6SX:
	case IMX6QP:
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
				   IMX6Q_GPR12_PCIE_CTL_2, 0);
		break;
	case IMX7D:
	case IMX8MQ:
	case IMX8MQ_EP:
	case IMX8MM:
	case IMX8MM_EP:
	case IMX8MP:
	case IMX8MP_EP:
		reset_control_assert(imx6_pcie->apps_reset);
		break;
	}
}

static int imx6_pcie_start_link(struct dw_pcie *pci)
{
	struct imx6_pcie *imx6_pcie = to_imx6_pcie(pci);
	struct device *dev = pci->dev;
	u8 offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	u32 tmp;
	int ret;

	/*
	 * Force Gen1 operation when starting the link.  In case the link is
	 * started in Gen2 mode, there is a possibility the devices on the
	 * bus will not be detected at all.  This happens with PCIe switches.
	 */
	dw_pcie_dbi_ro_wr_en(pci);
	tmp = dw_pcie_readl_dbi(pci, offset + PCI_EXP_LNKCAP);
	tmp &= ~PCI_EXP_LNKCAP_SLS;
	tmp |= PCI_EXP_LNKCAP_SLS_2_5GB;
	dw_pcie_writel_dbi(pci, offset + PCI_EXP_LNKCAP, tmp);
	dw_pcie_dbi_ro_wr_dis(pci);

	/* Start LTSSM. */
	imx6_pcie_ltssm_enable(dev);

	ret = dw_pcie_wait_for_link(pci);
	if (ret)
		goto err_reset_phy;

	if (pci->link_gen > 1) {
		/* Allow faster modes after the link is up */
		dw_pcie_dbi_ro_wr_en(pci);
		tmp = dw_pcie_readl_dbi(pci, offset + PCI_EXP_LNKCAP);
		tmp &= ~PCI_EXP_LNKCAP_SLS;
		tmp |= pci->link_gen;
		dw_pcie_writel_dbi(pci, offset + PCI_EXP_LNKCAP, tmp);

		/*
		 * Start Directed Speed Change so the best possible
		 * speed both link partners support can be negotiated.
		 */
		tmp = dw_pcie_readl_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL);
		tmp |= PORT_LOGIC_SPEED_CHANGE;
		dw_pcie_writel_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, tmp);
		dw_pcie_dbi_ro_wr_dis(pci);

		if (imx6_pcie->drvdata->flags &
		    IMX6_PCIE_FLAG_IMX6_SPEED_CHANGE) {
			/*
			 * On i.MX7, DIRECT_SPEED_CHANGE behaves differently
			 * from i.MX6 family when no link speed transition
			 * occurs and we go Gen1 -> yep, Gen1. The difference
			 * is that, in such case, it will not be cleared by HW
			 * which will cause the following code to report false
			 * failure.
			 */

			ret = imx6_pcie_wait_for_speed_change(imx6_pcie);
			if (ret) {
				dev_err(dev, "Failed to bring link up!\n");
				goto err_reset_phy;
			}
		}

		/* Make sure link training is finished as well! */
		ret = dw_pcie_wait_for_link(pci);
		if (ret)
			goto err_reset_phy;
	} else {
		dev_info(dev, "Link: Only Gen1 is enabled\n");
	}

	imx6_pcie->link_is_up = true;
	tmp = dw_pcie_readw_dbi(pci, offset + PCI_EXP_LNKSTA);
	dev_info(dev, "Link up, Gen%i\n", tmp & PCI_EXP_LNKSTA_CLS);
	return 0;

err_reset_phy:
	imx6_pcie->link_is_up = false;
	dev_dbg(dev, "PHY DEBUG_R0=0x%08x DEBUG_R1=0x%08x\n",
		dw_pcie_readl_dbi(pci, PCIE_PORT_DEBUG0),
		dw_pcie_readl_dbi(pci, PCIE_PORT_DEBUG1));
	imx6_pcie_reset_phy(imx6_pcie);
	return 0;
}

static void imx6_pcie_stop_link(struct dw_pcie *pci)
{
	struct device *dev = pci->dev;

	/* Turn off PCIe LTSSM */
	imx6_pcie_ltssm_disable(dev);
}

static int imx6_pcie_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct device *dev = pci->dev;
	struct imx6_pcie *imx6_pcie = to_imx6_pcie(pci);
	int ret;

	if (imx6_pcie->vpcie) {
		ret = regulator_enable(imx6_pcie->vpcie);
		if (ret) {
			dev_err(dev, "failed to enable vpcie regulator: %d\n",
				ret);
			return ret;
		}
	}

	imx6_pcie_assert_core_reset(imx6_pcie);
	imx6_pcie_init_phy(imx6_pcie);

	ret = imx6_pcie_clk_enable(imx6_pcie);
	if (ret) {
		dev_err(dev, "unable to enable pcie clocks: %d\n", ret);
		goto err_reg_disable;
	}

	if (imx6_pcie->phy) {
		ret = phy_init(imx6_pcie->phy);
		if (ret) {
			dev_err(dev, "pcie PHY power up failed\n");
			goto err_clk_disable;
		}
	}

	if (imx6_pcie->phy) {
		ret = phy_power_on(imx6_pcie->phy);
		if (ret) {
			dev_err(dev, "waiting for PHY ready timeout!\n");
			goto err_phy_off;
		}
	}

	ret = imx6_pcie_deassert_core_reset(imx6_pcie);
	if (ret < 0) {
		dev_err(dev, "pcie deassert core reset failed: %d\n", ret);
		goto err_phy_off;
	}

	imx6_setup_phy_mpll(imx6_pcie);

	return 0;

err_phy_off:
	if (imx6_pcie->phy)
		phy_exit(imx6_pcie->phy);
err_clk_disable:
	imx6_pcie_clk_disable(imx6_pcie);
err_reg_disable:
	if (imx6_pcie->vpcie)
		regulator_disable(imx6_pcie->vpcie);
	return ret;
}

static void imx6_pcie_host_exit(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct imx6_pcie *imx6_pcie = to_imx6_pcie(pci);

	if (imx6_pcie->phy) {
		if (phy_power_off(imx6_pcie->phy))
			dev_err(pci->dev, "unable to power off PHY\n");
		phy_exit(imx6_pcie->phy);
	}
	imx6_pcie_clk_disable(imx6_pcie);

	if (imx6_pcie->vpcie)
		regulator_disable(imx6_pcie->vpcie);
}

static const struct dw_pcie_host_ops imx6_pcie_host_ops = {
	.host_init = imx6_pcie_host_init,
	.host_deinit = imx6_pcie_host_exit,
};

static const struct dw_pcie_ops dw_pcie_ops = {
	.start_link = imx6_pcie_start_link,
	.stop_link = imx6_pcie_stop_link,
};

static void imx6_pcie_ep_init(struct dw_pcie_ep *ep)
{
	enum pci_barno bar;
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	for (bar = BAR_0; bar <= BAR_5; bar++)
		dw_pcie_ep_reset_bar(pci, bar);
}

static int imx6_pcie_ep_raise_irq(struct dw_pcie_ep *ep, u8 func_no,
				  enum pci_epc_irq_type type,
				  u16 interrupt_num)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	switch (type) {
	case PCI_EPC_IRQ_LEGACY:
		return dw_pcie_ep_raise_legacy_irq(ep, func_no);
	case PCI_EPC_IRQ_MSI:
		return dw_pcie_ep_raise_msi_irq(ep, func_no, interrupt_num);
	case PCI_EPC_IRQ_MSIX:
		return dw_pcie_ep_raise_msix_irq(ep, func_no, interrupt_num);
	default:
		dev_err(pci->dev, "UNKNOWN IRQ type\n");
		return -EINVAL;
	}

	return 0;
}

static const struct pci_epc_features imx8m_pcie_epc_features = {
	.linkup_notifier = false,
	.msi_capable = true,
	.msix_capable = false,
	.reserved_bar = 1 << BAR_1 | 1 << BAR_3,
	.align = SZ_64K,
};

static const struct pci_epc_features*
imx6_pcie_ep_get_features(struct dw_pcie_ep *ep)
{
	return &imx8m_pcie_epc_features;
}

static const struct dw_pcie_ep_ops pcie_ep_ops = {
	.ep_init = imx6_pcie_ep_init,
	.raise_irq = imx6_pcie_ep_raise_irq,
	.get_features = imx6_pcie_ep_get_features,
};

static int imx6_add_pcie_ep(struct imx6_pcie *imx6_pcie,
			   struct platform_device *pdev)
{
	int ret;
	unsigned int pcie_dbi2_offset;
	struct dw_pcie_ep *ep;
	struct resource *res;
	struct dw_pcie *pci = imx6_pcie->pci;
	struct dw_pcie_rp *pp = &pci->pp;
	struct device *dev = pci->dev;

	imx6_pcie_host_init(pp);
	ep = &pci->ep;
	ep->ops = &pcie_ep_ops;

	switch (imx6_pcie->drvdata->variant) {
	case IMX8MQ_EP:
	case IMX8MM_EP:
	case IMX8MP_EP:
		pcie_dbi2_offset = SZ_1M;
		break;
	default:
		pcie_dbi2_offset = SZ_4K;
		break;
	}
	pci->dbi_base2 = pci->dbi_base + pcie_dbi2_offset;
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "addr_space");
	if (!res)
		return -EINVAL;

	ep->phys_base = res->start;
	ep->addr_size = resource_size(res);
	ep->page_size = SZ_64K;

	ret = dw_pcie_ep_init(ep);
	if (ret) {
		dev_err(dev, "failed to initialize endpoint\n");
		return ret;
	}
	/* Start LTSSM. */
	imx6_pcie_ltssm_enable(dev);

	return 0;
}

static void imx6_pcie_pm_turnoff(struct imx6_pcie *imx6_pcie)
{
	struct device *dev = imx6_pcie->pci->dev;

	/* Some variants have a turnoff reset in DT */
	if (imx6_pcie->turnoff_reset) {
		reset_control_assert(imx6_pcie->turnoff_reset);
		reset_control_deassert(imx6_pcie->turnoff_reset);
		goto pm_turnoff_sleep;
	}

	/* Others poke directly at IOMUXC registers */
	switch (imx6_pcie->drvdata->variant) {
	case IMX6SX:
	case IMX6QP:
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
				IMX6SX_GPR12_PCIE_PM_TURN_OFF,
				IMX6SX_GPR12_PCIE_PM_TURN_OFF);
		regmap_update_bits(imx6_pcie->iomuxc_gpr, IOMUXC_GPR12,
				IMX6SX_GPR12_PCIE_PM_TURN_OFF, 0);
		break;
	default:
		dev_err(dev, "PME_Turn_Off not implemented\n");
		return;
	}

	/*
	 * Components with an upstream port must respond to
	 * PME_Turn_Off with PME_TO_Ack but we can't check.
	 *
	 * The standard recommends a 1-10ms timeout after which to
	 * proceed anyway as if acks were received.
	 */
pm_turnoff_sleep:
	usleep_range(1000, 10000);
}

static void imx6_pcie_msi_save_restore(struct imx6_pcie *imx6_pcie, bool save)
{
	u8 offset;
	u16 val;
	struct dw_pcie *pci = imx6_pcie->pci;

	if (pci_msi_enabled()) {
		offset = dw_pcie_find_capability(pci, PCI_CAP_ID_MSI);
		if (save) {
			val = dw_pcie_readw_dbi(pci, offset + PCI_MSI_FLAGS);
			imx6_pcie->msi_ctrl = val;
		} else {
			dw_pcie_dbi_ro_wr_en(pci);
			val = imx6_pcie->msi_ctrl;
			dw_pcie_writew_dbi(pci, offset + PCI_MSI_FLAGS, val);
			dw_pcie_dbi_ro_wr_dis(pci);
		}
	}
}

static int imx6_pcie_suspend_noirq(struct device *dev)
{
	struct imx6_pcie *imx6_pcie = dev_get_drvdata(dev);
	struct dw_pcie_rp *pp = &imx6_pcie->pci->pp;

	if (!(imx6_pcie->drvdata->flags & IMX6_PCIE_FLAG_SUPPORTS_SUSPEND))
		return 0;

	imx6_pcie_msi_save_restore(imx6_pcie, true);
	imx6_pcie_pm_turnoff(imx6_pcie);
	imx6_pcie_stop_link(imx6_pcie->pci);
	imx6_pcie_host_exit(pp);

	return 0;
}

static int imx6_pcie_resume_noirq(struct device *dev)
{
	int ret;
	struct imx6_pcie *imx6_pcie = dev_get_drvdata(dev);
	struct dw_pcie_rp *pp = &imx6_pcie->pci->pp;

	if (!(imx6_pcie->drvdata->flags & IMX6_PCIE_FLAG_SUPPORTS_SUSPEND))
		return 0;

	ret = imx6_pcie_host_init(pp);
	if (ret)
		return ret;
	imx6_pcie_msi_save_restore(imx6_pcie, false);
	dw_pcie_setup_rc(pp);

	if (imx6_pcie->link_is_up)
		imx6_pcie_start_link(imx6_pcie->pci);

	return 0;
}

static const struct dev_pm_ops imx6_pcie_pm_ops = {
	NOIRQ_SYSTEM_SLEEP_PM_OPS(imx6_pcie_suspend_noirq,
				  imx6_pcie_resume_noirq)
};

static int imx6_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_pcie *pci;
	struct imx6_pcie *imx6_pcie;
	struct device_node *np;
	struct resource *dbi_base;
	struct device_node *node = dev->of_node;
	int ret;
	u16 val;

	imx6_pcie = devm_kzalloc(dev, sizeof(*imx6_pcie), GFP_KERNEL);
	if (!imx6_pcie)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = dev;
	pci->ops = &dw_pcie_ops;
	pci->pp.ops = &imx6_pcie_host_ops;

	imx6_pcie->pci = pci;
	imx6_pcie->drvdata = of_device_get_match_data(dev);

	/* Find the PHY if one is defined, only imx7d uses it */
	np = of_parse_phandle(node, "fsl,imx7d-pcie-phy", 0);
	if (np) {
		struct resource res;

		ret = of_address_to_resource(np, 0, &res);
		if (ret) {
			dev_err(dev, "Unable to map PCIe PHY\n");
			return ret;
		}
		imx6_pcie->phy_base = devm_ioremap_resource(dev, &res);
		if (IS_ERR(imx6_pcie->phy_base))
			return PTR_ERR(imx6_pcie->phy_base);
	}

	pci->dbi_base = devm_platform_get_and_ioremap_resource(pdev, 0, &dbi_base);
	if (IS_ERR(pci->dbi_base))
		return PTR_ERR(pci->dbi_base);

	/* Fetch GPIOs */
	imx6_pcie->reset_gpio = of_get_named_gpio(node, "reset-gpio", 0);
	imx6_pcie->gpio_active_high = of_property_read_bool(node,
						"reset-gpio-active-high");
	if (gpio_is_valid(imx6_pcie->reset_gpio)) {
		ret = devm_gpio_request_one(dev, imx6_pcie->reset_gpio,
				imx6_pcie->gpio_active_high ?
					GPIOF_OUT_INIT_HIGH :
					GPIOF_OUT_INIT_LOW,
				"PCIe reset");
		if (ret) {
			dev_err(dev, "unable to get reset gpio\n");
			return ret;
		}
	} else if (imx6_pcie->reset_gpio == -EPROBE_DEFER) {
		return imx6_pcie->reset_gpio;
	}

	/* Fetch clocks */
	imx6_pcie->pcie_bus = devm_clk_get(dev, "pcie_bus");
	if (IS_ERR(imx6_pcie->pcie_bus))
		return dev_err_probe(dev, PTR_ERR(imx6_pcie->pcie_bus),
				     "pcie_bus clock source missing or invalid\n");

	imx6_pcie->pcie = devm_clk_get(dev, "pcie");
	if (IS_ERR(imx6_pcie->pcie))
		return dev_err_probe(dev, PTR_ERR(imx6_pcie->pcie),
				     "pcie clock source missing or invalid\n");

	switch (imx6_pcie->drvdata->variant) {
	case IMX6SX:
		imx6_pcie->pcie_inbound_axi = devm_clk_get(dev,
							   "pcie_inbound_axi");
		if (IS_ERR(imx6_pcie->pcie_inbound_axi))
			return dev_err_probe(dev, PTR_ERR(imx6_pcie->pcie_inbound_axi),
					     "pcie_inbound_axi clock missing or invalid\n");
		break;
	case IMX8MQ:
	case IMX8MQ_EP:
		imx6_pcie->pcie_aux = devm_clk_get(dev, "pcie_aux");
		if (IS_ERR(imx6_pcie->pcie_aux))
			return dev_err_probe(dev, PTR_ERR(imx6_pcie->pcie_aux),
					     "pcie_aux clock source missing or invalid\n");
		fallthrough;
	case IMX7D:
		if (dbi_base->start == IMX8MQ_PCIE2_BASE_ADDR)
			imx6_pcie->controller_id = 1;

		imx6_pcie->pciephy_reset = devm_reset_control_get_exclusive(dev,
									    "pciephy");
		if (IS_ERR(imx6_pcie->pciephy_reset)) {
			dev_err(dev, "Failed to get PCIEPHY reset control\n");
			return PTR_ERR(imx6_pcie->pciephy_reset);
		}

		imx6_pcie->apps_reset = devm_reset_control_get_exclusive(dev,
									 "apps");
		if (IS_ERR(imx6_pcie->apps_reset)) {
			dev_err(dev, "Failed to get PCIE APPS reset control\n");
			return PTR_ERR(imx6_pcie->apps_reset);
		}
		break;
	case IMX8MM:
	case IMX8MM_EP:
	case IMX8MP:
	case IMX8MP_EP:
		imx6_pcie->pcie_aux = devm_clk_get(dev, "pcie_aux");
		if (IS_ERR(imx6_pcie->pcie_aux))
			return dev_err_probe(dev, PTR_ERR(imx6_pcie->pcie_aux),
					     "pcie_aux clock source missing or invalid\n");
		imx6_pcie->apps_reset = devm_reset_control_get_exclusive(dev,
									 "apps");
		if (IS_ERR(imx6_pcie->apps_reset))
			return dev_err_probe(dev, PTR_ERR(imx6_pcie->apps_reset),
					     "failed to get pcie apps reset control\n");

		imx6_pcie->phy = devm_phy_get(dev, "pcie-phy");
		if (IS_ERR(imx6_pcie->phy))
			return dev_err_probe(dev, PTR_ERR(imx6_pcie->phy),
					     "failed to get pcie phy\n");

		break;
	default:
		break;
	}
	/* Don't fetch the pcie_phy clock, if it has abstract PHY driver */
	if (imx6_pcie->phy == NULL) {
		imx6_pcie->pcie_phy = devm_clk_get(dev, "pcie_phy");
		if (IS_ERR(imx6_pcie->pcie_phy))
			return dev_err_probe(dev, PTR_ERR(imx6_pcie->pcie_phy),
					     "pcie_phy clock source missing or invalid\n");
	}


	/* Grab turnoff reset */
	imx6_pcie->turnoff_reset = devm_reset_control_get_optional_exclusive(dev, "turnoff");
	if (IS_ERR(imx6_pcie->turnoff_reset)) {
		dev_err(dev, "Failed to get TURNOFF reset control\n");
		return PTR_ERR(imx6_pcie->turnoff_reset);
	}

	/* Grab GPR config register range */
	imx6_pcie->iomuxc_gpr =
		 syscon_regmap_lookup_by_compatible(imx6_pcie->drvdata->gpr);
	if (IS_ERR(imx6_pcie->iomuxc_gpr)) {
		dev_err(dev, "unable to find iomuxc registers\n");
		return PTR_ERR(imx6_pcie->iomuxc_gpr);
	}

	/* Grab PCIe PHY Tx Settings */
	if (of_property_read_u32(node, "fsl,tx-deemph-gen1",
				 &imx6_pcie->tx_deemph_gen1))
		imx6_pcie->tx_deemph_gen1 = 0;

	if (of_property_read_u32(node, "fsl,tx-deemph-gen2-3p5db",
				 &imx6_pcie->tx_deemph_gen2_3p5db))
		imx6_pcie->tx_deemph_gen2_3p5db = 0;

	if (of_property_read_u32(node, "fsl,tx-deemph-gen2-6db",
				 &imx6_pcie->tx_deemph_gen2_6db))
		imx6_pcie->tx_deemph_gen2_6db = 20;

	if (of_property_read_u32(node, "fsl,tx-swing-full",
				 &imx6_pcie->tx_swing_full))
		imx6_pcie->tx_swing_full = 127;

	if (of_property_read_u32(node, "fsl,tx-swing-low",
				 &imx6_pcie->tx_swing_low))
		imx6_pcie->tx_swing_low = 127;

	/* Limit link speed */
	pci->link_gen = 1;
	of_property_read_u32(node, "fsl,max-link-speed", &pci->link_gen);

	imx6_pcie->vpcie = devm_regulator_get_optional(&pdev->dev, "vpcie");
	if (IS_ERR(imx6_pcie->vpcie)) {
		if (PTR_ERR(imx6_pcie->vpcie) != -ENODEV)
			return PTR_ERR(imx6_pcie->vpcie);
		imx6_pcie->vpcie = NULL;
	}

	imx6_pcie->vph = devm_regulator_get_optional(&pdev->dev, "vph");
	if (IS_ERR(imx6_pcie->vph)) {
		if (PTR_ERR(imx6_pcie->vph) != -ENODEV)
			return PTR_ERR(imx6_pcie->vph);
		imx6_pcie->vph = NULL;
	}

	platform_set_drvdata(pdev, imx6_pcie);

	ret = imx6_pcie_attach_pd(dev);
	if (ret)
		return ret;

	if (imx6_pcie->drvdata->mode == DW_PCIE_EP_TYPE) {
		ret = imx6_add_pcie_ep(imx6_pcie, pdev);
		if (ret < 0)
			return ret;
	} else {
		ret = dw_pcie_host_init(&pci->pp);
		if (ret < 0)
			return ret;

		if (pci_msi_enabled()) {
			u8 offset = dw_pcie_find_capability(pci, PCI_CAP_ID_MSI);

			val = dw_pcie_readw_dbi(pci, offset + PCI_MSI_FLAGS);
			val |= PCI_MSI_FLAGS_ENABLE;
			dw_pcie_writew_dbi(pci, offset + PCI_MSI_FLAGS, val);
		}
	}

	return 0;
}

static void imx6_pcie_shutdown(struct platform_device *pdev)
{
	struct imx6_pcie *imx6_pcie = platform_get_drvdata(pdev);

	/* bring down link, so bootloader gets clean state in case of reboot */
	imx6_pcie_assert_core_reset(imx6_pcie);
}

static const struct imx6_pcie_drvdata drvdata[] = {
	[IMX6Q] = {
		.variant = IMX6Q,
		.flags = IMX6_PCIE_FLAG_IMX6_PHY |
			 IMX6_PCIE_FLAG_IMX6_SPEED_CHANGE,
		.dbi_length = 0x200,
		.gpr = "fsl,imx6q-iomuxc-gpr",
	},
	[IMX6SX] = {
		.variant = IMX6SX,
		.flags = IMX6_PCIE_FLAG_IMX6_PHY |
			 IMX6_PCIE_FLAG_IMX6_SPEED_CHANGE |
			 IMX6_PCIE_FLAG_SUPPORTS_SUSPEND,
		.gpr = "fsl,imx6q-iomuxc-gpr",
	},
	[IMX6QP] = {
		.variant = IMX6QP,
		.flags = IMX6_PCIE_FLAG_IMX6_PHY |
			 IMX6_PCIE_FLAG_IMX6_SPEED_CHANGE |
			 IMX6_PCIE_FLAG_SUPPORTS_SUSPEND,
		.dbi_length = 0x200,
		.gpr = "fsl,imx6q-iomuxc-gpr",
	},
	[IMX7D] = {
		.variant = IMX7D,
		.flags = IMX6_PCIE_FLAG_SUPPORTS_SUSPEND,
		.gpr = "fsl,imx7d-iomuxc-gpr",
	},
	[IMX8MQ] = {
		.variant = IMX8MQ,
		.gpr = "fsl,imx8mq-iomuxc-gpr",
	},
	[IMX8MM] = {
		.variant = IMX8MM,
		.flags = IMX6_PCIE_FLAG_SUPPORTS_SUSPEND,
		.gpr = "fsl,imx8mm-iomuxc-gpr",
	},
	[IMX8MP] = {
		.variant = IMX8MP,
		.flags = IMX6_PCIE_FLAG_SUPPORTS_SUSPEND,
		.gpr = "fsl,imx8mp-iomuxc-gpr",
	},
	[IMX8MQ_EP] = {
		.variant = IMX8MQ_EP,
		.mode = DW_PCIE_EP_TYPE,
		.gpr = "fsl,imx8mq-iomuxc-gpr",
	},
	[IMX8MM_EP] = {
		.variant = IMX8MM_EP,
		.mode = DW_PCIE_EP_TYPE,
		.gpr = "fsl,imx8mm-iomuxc-gpr",
	},
	[IMX8MP_EP] = {
		.variant = IMX8MP_EP,
		.mode = DW_PCIE_EP_TYPE,
		.gpr = "fsl,imx8mp-iomuxc-gpr",
	},
};

static const struct of_device_id imx6_pcie_of_match[] = {
	{ .compatible = "fsl,imx6q-pcie",  .data = &drvdata[IMX6Q],  },
	{ .compatible = "fsl,imx6sx-pcie", .data = &drvdata[IMX6SX], },
	{ .compatible = "fsl,imx6qp-pcie", .data = &drvdata[IMX6QP], },
	{ .compatible = "fsl,imx7d-pcie",  .data = &drvdata[IMX7D],  },
	{ .compatible = "fsl,imx8mq-pcie", .data = &drvdata[IMX8MQ], },
	{ .compatible = "fsl,imx8mm-pcie", .data = &drvdata[IMX8MM], },
	{ .compatible = "fsl,imx8mp-pcie", .data = &drvdata[IMX8MP], },
	{ .compatible = "fsl,imx8mq-pcie-ep", .data = &drvdata[IMX8MQ_EP], },
	{ .compatible = "fsl,imx8mm-pcie-ep", .data = &drvdata[IMX8MM_EP], },
	{ .compatible = "fsl,imx8mp-pcie-ep", .data = &drvdata[IMX8MP_EP], },
	{},
};

static struct platform_driver imx6_pcie_driver = {
	.driver = {
		.name	= "imx6q-pcie",
		.of_match_table = imx6_pcie_of_match,
		.suppress_bind_attrs = true,
		.pm = &imx6_pcie_pm_ops,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe    = imx6_pcie_probe,
	.shutdown = imx6_pcie_shutdown,
};

static void imx6_pcie_quirk(struct pci_dev *dev)
{
	struct pci_bus *bus = dev->bus;
	struct dw_pcie_rp *pp = bus->sysdata;

	/* Bus parent is the PCI bridge, its parent is this platform driver */
	if (!bus->dev.parent || !bus->dev.parent->parent)
		return;

	/* Make sure we only quirk devices associated with this driver */
	if (bus->dev.parent->parent->driver != &imx6_pcie_driver.driver)
		return;

	if (pci_is_root_bus(bus)) {
		struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
		struct imx6_pcie *imx6_pcie = to_imx6_pcie(pci);

		/*
		 * Limit config length to avoid the kernel reading beyond
		 * the register set and causing an abort on i.MX 6Quad
		 */
		if (imx6_pcie->drvdata->dbi_length) {
			dev->cfg_size = imx6_pcie->drvdata->dbi_length;
			dev_info(&dev->dev, "Limiting cfg_size to %d\n",
					dev->cfg_size);
		}
	}
}
DECLARE_PCI_FIXUP_CLASS_HEADER(PCI_VENDOR_ID_SYNOPSYS, 0xabcd,
			PCI_CLASS_BRIDGE_PCI, 8, imx6_pcie_quirk);

static int __init imx6_pcie_init(void)
{
#ifdef CONFIG_ARM
	struct device_node *np;

	np = of_find_matching_node(NULL, imx6_pcie_of_match);
	if (!np)
		return -ENODEV;
	of_node_put(np);

	/*
	 * Since probe() can be deferred we need to make sure that
	 * hook_fault_code is not called after __init memory is freed
	 * by kernel and since imx6q_pcie_abort_handler() is a no-op,
	 * we can install the handler here without risking it
	 * accessing some uninitialized driver state.
	 */
	hook_fault_code(8, imx6q_pcie_abort_handler, SIGBUS, 0,
			"external abort on non-linefetch");
#endif

	return platform_driver_register(&imx6_pcie_driver);
}
device_initcall(imx6_pcie_init);
