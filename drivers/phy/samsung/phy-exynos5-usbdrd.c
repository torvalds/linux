// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung Exynos5 SoC series USB DRD PHY driver
 *
 * Phy provider for USB 3.0 DRD controller on Exynos5 SoC series
 *
 * Copyright (C) 2014 Samsung Electronics Co., Ltd.
 * Author: Vivek Gautam <gautam.vivek@samsung.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/iopoll.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/soc/samsung/exynos-regs-pmu.h>

/* Exynos USB PHY registers */
#define EXYNOS5_FSEL_9MHZ6		0x0
#define EXYNOS5_FSEL_10MHZ		0x1
#define EXYNOS5_FSEL_12MHZ		0x2
#define EXYNOS5_FSEL_19MHZ2		0x3
#define EXYNOS5_FSEL_20MHZ		0x4
#define EXYNOS5_FSEL_24MHZ		0x5
#define EXYNOS5_FSEL_50MHZ		0x7

/* Exynos5: USB 3.0 DRD PHY registers */
#define EXYNOS5_DRD_LINKSYSTEM			0x04

#define LINKSYSTEM_FLADJ_MASK			(0x3f << 1)
#define LINKSYSTEM_FLADJ(_x)			((_x) << 1)
#define LINKSYSTEM_XHCI_VERSION_CONTROL		BIT(27)

#define EXYNOS5_DRD_PHYUTMI			0x08

#define PHYUTMI_OTGDISABLE			BIT(6)
#define PHYUTMI_FORCESUSPEND			BIT(1)
#define PHYUTMI_FORCESLEEP			BIT(0)

#define EXYNOS5_DRD_PHYPIPE			0x0c

#define EXYNOS5_DRD_PHYCLKRST			0x10

#define PHYCLKRST_EN_UTMISUSPEND		BIT(31)

#define PHYCLKRST_SSC_REFCLKSEL_MASK		(0xff << 23)
#define PHYCLKRST_SSC_REFCLKSEL(_x)		((_x) << 23)

#define PHYCLKRST_SSC_RANGE_MASK		(0x03 << 21)
#define PHYCLKRST_SSC_RANGE(_x)			((_x) << 21)

#define PHYCLKRST_SSC_EN			BIT(20)
#define PHYCLKRST_REF_SSP_EN			BIT(19)
#define PHYCLKRST_REF_CLKDIV2			BIT(18)

#define PHYCLKRST_MPLL_MULTIPLIER_MASK		(0x7f << 11)
#define PHYCLKRST_MPLL_MULTIPLIER_100MHZ_REF	(0x19 << 11)
#define PHYCLKRST_MPLL_MULTIPLIER_50M_REF	(0x32 << 11)
#define PHYCLKRST_MPLL_MULTIPLIER_24MHZ_REF	(0x68 << 11)
#define PHYCLKRST_MPLL_MULTIPLIER_20MHZ_REF	(0x7d << 11)
#define PHYCLKRST_MPLL_MULTIPLIER_19200KHZ_REF	(0x02 << 11)

#define PHYCLKRST_FSEL_UTMI_MASK		(0x7 << 5)
#define PHYCLKRST_FSEL_PIPE_MASK		(0x7 << 8)
#define PHYCLKRST_FSEL(_x)			((_x) << 5)
#define PHYCLKRST_FSEL_PAD_100MHZ		(0x27 << 5)
#define PHYCLKRST_FSEL_PAD_24MHZ		(0x2a << 5)
#define PHYCLKRST_FSEL_PAD_20MHZ		(0x31 << 5)
#define PHYCLKRST_FSEL_PAD_19_2MHZ		(0x38 << 5)

#define PHYCLKRST_RETENABLEN			BIT(4)

#define PHYCLKRST_REFCLKSEL_MASK		(0x03 << 2)
#define PHYCLKRST_REFCLKSEL_PAD_REFCLK		(0x2 << 2)
#define PHYCLKRST_REFCLKSEL_EXT_REFCLK		(0x3 << 2)

#define PHYCLKRST_PORTRESET			BIT(1)
#define PHYCLKRST_COMMONONN			BIT(0)

#define EXYNOS5_DRD_PHYREG0			0x14
#define PHYREG0_SSC_REF_CLK_SEL			BIT(21)
#define PHYREG0_SSC_RANGE			BIT(20)
#define PHYREG0_CR_WRITE			BIT(19)
#define PHYREG0_CR_READ				BIT(18)
#define PHYREG0_CR_DATA_IN(_x)			((_x) << 2)
#define PHYREG0_CR_CAP_DATA			BIT(1)
#define PHYREG0_CR_CAP_ADDR			BIT(0)

#define EXYNOS5_DRD_PHYREG1			0x18
#define PHYREG1_CR_DATA_OUT(_x)			((_x) << 1)
#define PHYREG1_CR_ACK				BIT(0)

#define EXYNOS5_DRD_PHYPARAM0			0x1c

#define PHYPARAM0_REF_USE_PAD			BIT(31)
#define PHYPARAM0_REF_LOSLEVEL_MASK		(0x1f << 26)
#define PHYPARAM0_REF_LOSLEVEL			(0x9 << 26)

#define EXYNOS5_DRD_PHYPARAM1			0x20

#define PHYPARAM1_PCS_TXDEEMPH_MASK		(0x1f << 0)
#define PHYPARAM1_PCS_TXDEEMPH			(0x1c)

#define EXYNOS5_DRD_PHYTERM			0x24

#define EXYNOS5_DRD_PHYTEST			0x28

#define PHYTEST_POWERDOWN_SSP			BIT(3)
#define PHYTEST_POWERDOWN_HSP			BIT(2)

#define EXYNOS5_DRD_PHYADP			0x2c

#define EXYNOS5_DRD_PHYUTMICLKSEL		0x30

#define PHYUTMICLKSEL_UTMI_CLKSEL		BIT(2)

#define EXYNOS5_DRD_PHYRESUME			0x34
#define EXYNOS5_DRD_LINKPORT			0x44

/* USB 3.0 DRD PHY SS Function Control Reg; accessed by CR_PORT */
#define EXYNOS5_DRD_PHYSS_LOSLEVEL_OVRD_IN		(0x15)
#define LOSLEVEL_OVRD_IN_LOS_BIAS_5420			(0x5 << 13)
#define LOSLEVEL_OVRD_IN_LOS_BIAS_DEFAULT		(0x0 << 13)
#define LOSLEVEL_OVRD_IN_EN				(0x1 << 10)
#define LOSLEVEL_OVRD_IN_LOS_LEVEL_DEFAULT		(0x9 << 0)

#define EXYNOS5_DRD_PHYSS_TX_VBOOSTLEVEL_OVRD_IN	(0x12)
#define TX_VBOOSTLEVEL_OVRD_IN_VBOOST_5420		(0x5 << 13)
#define TX_VBOOSTLEVEL_OVRD_IN_VBOOST_DEFAULT		(0x4 << 13)

#define EXYNOS5_DRD_PHYSS_LANE0_TX_DEBUG		(0x1010)
#define LANE0_TX_DEBUG_RXDET_MEAS_TIME_19M2_20M		(0x4 << 4)
#define LANE0_TX_DEBUG_RXDET_MEAS_TIME_24M		(0x8 << 4)
#define LANE0_TX_DEBUG_RXDET_MEAS_TIME_25M_26M		(0x8 << 4)
#define LANE0_TX_DEBUG_RXDET_MEAS_TIME_48M_50M_52M	(0x20 << 4)
#define LANE0_TX_DEBUG_RXDET_MEAS_TIME_62M5		(0x20 << 4)
#define LANE0_TX_DEBUG_RXDET_MEAS_TIME_96M_100M		(0x40 << 4)

#define KHZ	1000
#define MHZ	(KHZ * KHZ)

enum exynos5_usbdrd_phy_id {
	EXYNOS5_DRDPHY_UTMI,
	EXYNOS5_DRDPHY_PIPE3,
	EXYNOS5_DRDPHYS_NUM,
};

struct phy_usb_instance;
struct exynos5_usbdrd_phy;

struct exynos5_usbdrd_phy_config {
	u32 id;
	void (*phy_isol)(struct phy_usb_instance *inst, u32 on);
	void (*phy_init)(struct exynos5_usbdrd_phy *phy_drd);
	unsigned int (*set_refclk)(struct phy_usb_instance *inst);
};

struct exynos5_usbdrd_phy_drvdata {
	const struct exynos5_usbdrd_phy_config *phy_cfg;
	u32 pmu_offset_usbdrd0_phy;
	u32 pmu_offset_usbdrd1_phy;
	bool has_common_clk_gate;
};

/**
 * struct exynos5_usbdrd_phy - driver data for USB 3.0 PHY
 * @dev: pointer to device instance of this platform device
 * @reg_phy: usb phy controller register memory base
 * @clk: phy clock for register access
 * @pipeclk: clock for pipe3 phy
 * @utmiclk: clock for utmi+ phy
 * @itpclk: clock for ITP generation
 * @drv_data: pointer to SoC level driver data structure
 * @phys: array for 'EXYNOS5_DRDPHYS_NUM' number of PHY
 *	    instances each with its 'phy' and 'phy_cfg'.
 * @extrefclk: frequency select settings when using 'separate
 *	       reference clocks' for SS and HS operations
 * @ref_clk: reference clock to PHY block from which PHY's
 *	     operational clocks are derived
 * @vbus: VBUS regulator for phy
 * @vbus_boost: Boost regulator for VBUS present on few Exynos boards
 */
struct exynos5_usbdrd_phy {
	struct device *dev;
	void __iomem *reg_phy;
	struct clk *clk;
	struct clk *pipeclk;
	struct clk *utmiclk;
	struct clk *itpclk;
	const struct exynos5_usbdrd_phy_drvdata *drv_data;
	struct phy_usb_instance {
		struct phy *phy;
		u32 index;
		struct regmap *reg_pmu;
		u32 pmu_offset;
		const struct exynos5_usbdrd_phy_config *phy_cfg;
	} phys[EXYNOS5_DRDPHYS_NUM];
	u32 extrefclk;
	struct clk *ref_clk;
	struct regulator *vbus;
	struct regulator *vbus_boost;
};

static inline
struct exynos5_usbdrd_phy *to_usbdrd_phy(struct phy_usb_instance *inst)
{
	return container_of((inst), struct exynos5_usbdrd_phy,
			    phys[(inst)->index]);
}

/*
 * exynos5_rate_to_clk() converts the supplied clock rate to the value that
 * can be written to the phy register.
 */
static unsigned int exynos5_rate_to_clk(unsigned long rate, u32 *reg)
{
	/* EXYNOS5_FSEL_MASK */

	switch (rate) {
	case 9600 * KHZ:
		*reg = EXYNOS5_FSEL_9MHZ6;
		break;
	case 10 * MHZ:
		*reg = EXYNOS5_FSEL_10MHZ;
		break;
	case 12 * MHZ:
		*reg = EXYNOS5_FSEL_12MHZ;
		break;
	case 19200 * KHZ:
		*reg = EXYNOS5_FSEL_19MHZ2;
		break;
	case 20 * MHZ:
		*reg = EXYNOS5_FSEL_20MHZ;
		break;
	case 24 * MHZ:
		*reg = EXYNOS5_FSEL_24MHZ;
		break;
	case 50 * MHZ:
		*reg = EXYNOS5_FSEL_50MHZ;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void exynos5_usbdrd_phy_isol(struct phy_usb_instance *inst,
						unsigned int on)
{
	unsigned int val;

	if (!inst->reg_pmu)
		return;

	val = on ? 0 : EXYNOS4_PHY_ENABLE;

	regmap_update_bits(inst->reg_pmu, inst->pmu_offset,
			   EXYNOS4_PHY_ENABLE, val);
}

/*
 * Sets the pipe3 phy's clk as EXTREFCLK (XXTI) which is internal clock
 * from clock core. Further sets multiplier values and spread spectrum
 * clock settings for SuperSpeed operations.
 */
static unsigned int
exynos5_usbdrd_pipe3_set_refclk(struct phy_usb_instance *inst)
{
	u32 reg;
	struct exynos5_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	/* restore any previous reference clock settings */
	reg = readl(phy_drd->reg_phy + EXYNOS5_DRD_PHYCLKRST);

	/* Use EXTREFCLK as ref clock */
	reg &= ~PHYCLKRST_REFCLKSEL_MASK;
	reg |=	PHYCLKRST_REFCLKSEL_EXT_REFCLK;

	/* FSEL settings corresponding to reference clock */
	reg &= ~PHYCLKRST_FSEL_PIPE_MASK |
		PHYCLKRST_MPLL_MULTIPLIER_MASK |
		PHYCLKRST_SSC_REFCLKSEL_MASK;
	switch (phy_drd->extrefclk) {
	case EXYNOS5_FSEL_50MHZ:
		reg |= (PHYCLKRST_MPLL_MULTIPLIER_50M_REF |
			PHYCLKRST_SSC_REFCLKSEL(0x00));
		break;
	case EXYNOS5_FSEL_24MHZ:
		reg |= (PHYCLKRST_MPLL_MULTIPLIER_24MHZ_REF |
			PHYCLKRST_SSC_REFCLKSEL(0x88));
		break;
	case EXYNOS5_FSEL_20MHZ:
		reg |= (PHYCLKRST_MPLL_MULTIPLIER_20MHZ_REF |
			PHYCLKRST_SSC_REFCLKSEL(0x00));
		break;
	case EXYNOS5_FSEL_19MHZ2:
		reg |= (PHYCLKRST_MPLL_MULTIPLIER_19200KHZ_REF |
			PHYCLKRST_SSC_REFCLKSEL(0x88));
		break;
	default:
		dev_dbg(phy_drd->dev, "unsupported ref clk\n");
		break;
	}

	return reg;
}

/*
 * Sets the utmi phy's clk as EXTREFCLK (XXTI) which is internal clock
 * from clock core. Further sets the FSEL values for HighSpeed operations.
 */
static unsigned int
exynos5_usbdrd_utmi_set_refclk(struct phy_usb_instance *inst)
{
	u32 reg;
	struct exynos5_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	/* restore any previous reference clock settings */
	reg = readl(phy_drd->reg_phy + EXYNOS5_DRD_PHYCLKRST);

	reg &= ~PHYCLKRST_REFCLKSEL_MASK;
	reg |=	PHYCLKRST_REFCLKSEL_EXT_REFCLK;

	reg &= ~PHYCLKRST_FSEL_UTMI_MASK |
		PHYCLKRST_MPLL_MULTIPLIER_MASK |
		PHYCLKRST_SSC_REFCLKSEL_MASK;
	reg |= PHYCLKRST_FSEL(phy_drd->extrefclk);

	return reg;
}

static void exynos5_usbdrd_pipe3_init(struct exynos5_usbdrd_phy *phy_drd)
{
	u32 reg;

	reg = readl(phy_drd->reg_phy + EXYNOS5_DRD_PHYPARAM1);
	/* Set Tx De-Emphasis level */
	reg &= ~PHYPARAM1_PCS_TXDEEMPH_MASK;
	reg |=	PHYPARAM1_PCS_TXDEEMPH;
	writel(reg, phy_drd->reg_phy + EXYNOS5_DRD_PHYPARAM1);

	reg = readl(phy_drd->reg_phy + EXYNOS5_DRD_PHYTEST);
	reg &= ~PHYTEST_POWERDOWN_SSP;
	writel(reg, phy_drd->reg_phy + EXYNOS5_DRD_PHYTEST);
}

static void exynos5_usbdrd_utmi_init(struct exynos5_usbdrd_phy *phy_drd)
{
	u32 reg;

	reg = readl(phy_drd->reg_phy + EXYNOS5_DRD_PHYPARAM0);
	/* Set Loss-of-Signal Detector sensitivity */
	reg &= ~PHYPARAM0_REF_LOSLEVEL_MASK;
	reg |=	PHYPARAM0_REF_LOSLEVEL;
	writel(reg, phy_drd->reg_phy + EXYNOS5_DRD_PHYPARAM0);

	reg = readl(phy_drd->reg_phy + EXYNOS5_DRD_PHYPARAM1);
	/* Set Tx De-Emphasis level */
	reg &= ~PHYPARAM1_PCS_TXDEEMPH_MASK;
	reg |=	PHYPARAM1_PCS_TXDEEMPH;
	writel(reg, phy_drd->reg_phy + EXYNOS5_DRD_PHYPARAM1);

	/* UTMI Power Control */
	writel(PHYUTMI_OTGDISABLE, phy_drd->reg_phy + EXYNOS5_DRD_PHYUTMI);

	reg = readl(phy_drd->reg_phy + EXYNOS5_DRD_PHYTEST);
	reg &= ~PHYTEST_POWERDOWN_HSP;
	writel(reg, phy_drd->reg_phy + EXYNOS5_DRD_PHYTEST);
}

static int exynos5_usbdrd_phy_init(struct phy *phy)
{
	int ret;
	u32 reg;
	struct phy_usb_instance *inst = phy_get_drvdata(phy);
	struct exynos5_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	ret = clk_prepare_enable(phy_drd->clk);
	if (ret)
		return ret;

	/* Reset USB 3.0 PHY */
	writel(0x0, phy_drd->reg_phy + EXYNOS5_DRD_PHYREG0);
	writel(0x0, phy_drd->reg_phy + EXYNOS5_DRD_PHYRESUME);

	/*
	 * Setting the Frame length Adj value[6:1] to default 0x20
	 * See xHCI 1.0 spec, 5.2.4
	 */
	reg =	LINKSYSTEM_XHCI_VERSION_CONTROL |
		LINKSYSTEM_FLADJ(0x20);
	writel(reg, phy_drd->reg_phy + EXYNOS5_DRD_LINKSYSTEM);

	reg = readl(phy_drd->reg_phy + EXYNOS5_DRD_PHYPARAM0);
	/* Select PHY CLK source */
	reg &= ~PHYPARAM0_REF_USE_PAD;
	writel(reg, phy_drd->reg_phy + EXYNOS5_DRD_PHYPARAM0);

	/* This bit must be set for both HS and SS operations */
	reg = readl(phy_drd->reg_phy + EXYNOS5_DRD_PHYUTMICLKSEL);
	reg |= PHYUTMICLKSEL_UTMI_CLKSEL;
	writel(reg, phy_drd->reg_phy + EXYNOS5_DRD_PHYUTMICLKSEL);

	/* UTMI or PIPE3 specific init */
	inst->phy_cfg->phy_init(phy_drd);

	/* reference clock settings */
	reg = inst->phy_cfg->set_refclk(inst);

		/* Digital power supply in normal operating mode */
	reg |=	PHYCLKRST_RETENABLEN |
		/* Enable ref clock for SS function */
		PHYCLKRST_REF_SSP_EN |
		/* Enable spread spectrum */
		PHYCLKRST_SSC_EN |
		/* Power down HS Bias and PLL blocks in suspend mode */
		PHYCLKRST_COMMONONN |
		/* Reset the port */
		PHYCLKRST_PORTRESET;

	writel(reg, phy_drd->reg_phy + EXYNOS5_DRD_PHYCLKRST);

	udelay(10);

	reg &= ~PHYCLKRST_PORTRESET;
	writel(reg, phy_drd->reg_phy + EXYNOS5_DRD_PHYCLKRST);

	clk_disable_unprepare(phy_drd->clk);

	return 0;
}

static int exynos5_usbdrd_phy_exit(struct phy *phy)
{
	int ret;
	u32 reg;
	struct phy_usb_instance *inst = phy_get_drvdata(phy);
	struct exynos5_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	ret = clk_prepare_enable(phy_drd->clk);
	if (ret)
		return ret;

	reg =	PHYUTMI_OTGDISABLE |
		PHYUTMI_FORCESUSPEND |
		PHYUTMI_FORCESLEEP;
	writel(reg, phy_drd->reg_phy + EXYNOS5_DRD_PHYUTMI);

	/* Resetting the PHYCLKRST enable bits to reduce leakage current */
	reg = readl(phy_drd->reg_phy + EXYNOS5_DRD_PHYCLKRST);
	reg &= ~(PHYCLKRST_REF_SSP_EN |
		 PHYCLKRST_SSC_EN |
		 PHYCLKRST_COMMONONN);
	writel(reg, phy_drd->reg_phy + EXYNOS5_DRD_PHYCLKRST);

	/* Control PHYTEST to remove leakage current */
	reg = readl(phy_drd->reg_phy + EXYNOS5_DRD_PHYTEST);
	reg |=	PHYTEST_POWERDOWN_SSP |
		PHYTEST_POWERDOWN_HSP;
	writel(reg, phy_drd->reg_phy + EXYNOS5_DRD_PHYTEST);

	clk_disable_unprepare(phy_drd->clk);

	return 0;
}

static int exynos5_usbdrd_phy_power_on(struct phy *phy)
{
	int ret;
	struct phy_usb_instance *inst = phy_get_drvdata(phy);
	struct exynos5_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	dev_dbg(phy_drd->dev, "Request to power_on usbdrd_phy phy\n");

	clk_prepare_enable(phy_drd->ref_clk);
	if (!phy_drd->drv_data->has_common_clk_gate) {
		clk_prepare_enable(phy_drd->pipeclk);
		clk_prepare_enable(phy_drd->utmiclk);
		clk_prepare_enable(phy_drd->itpclk);
	}

	/* Enable VBUS supply */
	if (phy_drd->vbus_boost) {
		ret = regulator_enable(phy_drd->vbus_boost);
		if (ret) {
			dev_err(phy_drd->dev,
				"Failed to enable VBUS boost supply\n");
			goto fail_vbus;
		}
	}

	if (phy_drd->vbus) {
		ret = regulator_enable(phy_drd->vbus);
		if (ret) {
			dev_err(phy_drd->dev, "Failed to enable VBUS supply\n");
			goto fail_vbus_boost;
		}
	}

	/* Power-on PHY*/
	inst->phy_cfg->phy_isol(inst, 0);

	return 0;

fail_vbus_boost:
	if (phy_drd->vbus_boost)
		regulator_disable(phy_drd->vbus_boost);

fail_vbus:
	clk_disable_unprepare(phy_drd->ref_clk);
	if (!phy_drd->drv_data->has_common_clk_gate) {
		clk_disable_unprepare(phy_drd->itpclk);
		clk_disable_unprepare(phy_drd->utmiclk);
		clk_disable_unprepare(phy_drd->pipeclk);
	}

	return ret;
}

static int exynos5_usbdrd_phy_power_off(struct phy *phy)
{
	struct phy_usb_instance *inst = phy_get_drvdata(phy);
	struct exynos5_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	dev_dbg(phy_drd->dev, "Request to power_off usbdrd_phy phy\n");

	/* Power-off the PHY */
	inst->phy_cfg->phy_isol(inst, 1);

	/* Disable VBUS supply */
	if (phy_drd->vbus)
		regulator_disable(phy_drd->vbus);
	if (phy_drd->vbus_boost)
		regulator_disable(phy_drd->vbus_boost);

	clk_disable_unprepare(phy_drd->ref_clk);
	if (!phy_drd->drv_data->has_common_clk_gate) {
		clk_disable_unprepare(phy_drd->itpclk);
		clk_disable_unprepare(phy_drd->pipeclk);
		clk_disable_unprepare(phy_drd->utmiclk);
	}

	return 0;
}

static int crport_handshake(struct exynos5_usbdrd_phy *phy_drd,
			    u32 val, u32 cmd)
{
	unsigned int result;
	int err;

	writel(val | cmd, phy_drd->reg_phy + EXYNOS5_DRD_PHYREG0);

	err = readl_poll_timeout(phy_drd->reg_phy + EXYNOS5_DRD_PHYREG1,
				 result, (result & PHYREG1_CR_ACK), 1, 100);
	if (err == -ETIMEDOUT) {
		dev_err(phy_drd->dev, "CRPORT handshake timeout1 (0x%08x)\n", val);
		return err;
	}

	writel(val, phy_drd->reg_phy + EXYNOS5_DRD_PHYREG0);

	err = readl_poll_timeout(phy_drd->reg_phy + EXYNOS5_DRD_PHYREG1,
				 result, !(result & PHYREG1_CR_ACK), 1, 100);
	if (err == -ETIMEDOUT) {
		dev_err(phy_drd->dev, "CRPORT handshake timeout2 (0x%08x)\n", val);
		return err;
	}

	return 0;
}

static int crport_ctrl_write(struct exynos5_usbdrd_phy *phy_drd,
			     u32 addr, u32 data)
{
	int ret;

	/* Write Address */
	writel(PHYREG0_CR_DATA_IN(addr),
	       phy_drd->reg_phy + EXYNOS5_DRD_PHYREG0);
	ret = crport_handshake(phy_drd, PHYREG0_CR_DATA_IN(addr),
			       PHYREG0_CR_CAP_ADDR);
	if (ret)
		return ret;

	/* Write Data */
	writel(PHYREG0_CR_DATA_IN(data),
	       phy_drd->reg_phy + EXYNOS5_DRD_PHYREG0);
	ret = crport_handshake(phy_drd, PHYREG0_CR_DATA_IN(data),
			       PHYREG0_CR_CAP_DATA);
	if (ret)
		return ret;

	ret = crport_handshake(phy_drd, PHYREG0_CR_DATA_IN(data),
			       PHYREG0_CR_WRITE);

	return ret;
}

/*
 * Calibrate few PHY parameters using CR_PORT register to meet
 * SuperSpeed requirements on Exynos5420 and Exynos5800 systems,
 * which have 28nm USB 3.0 DRD PHY.
 */
static int exynos5420_usbdrd_phy_calibrate(struct exynos5_usbdrd_phy *phy_drd)
{
	unsigned int temp;
	int ret = 0;

	/*
	 * Change los_bias to (0x5) for 28nm PHY from a
	 * default value (0x0); los_level is set as default
	 * (0x9) as also reflected in los_level[30:26] bits
	 * of PHYPARAM0 register.
	 */
	temp = LOSLEVEL_OVRD_IN_LOS_BIAS_5420 |
		LOSLEVEL_OVRD_IN_EN |
		LOSLEVEL_OVRD_IN_LOS_LEVEL_DEFAULT;
	ret = crport_ctrl_write(phy_drd,
				EXYNOS5_DRD_PHYSS_LOSLEVEL_OVRD_IN,
				temp);
	if (ret) {
		dev_err(phy_drd->dev,
			"Failed setting Loss-of-Signal level for SuperSpeed\n");
		return ret;
	}

	/*
	 * Set tx_vboost_lvl to (0x5) for 28nm PHY Tuning,
	 * to raise Tx signal level from its default value of (0x4)
	 */
	temp = TX_VBOOSTLEVEL_OVRD_IN_VBOOST_5420;
	ret = crport_ctrl_write(phy_drd,
				EXYNOS5_DRD_PHYSS_TX_VBOOSTLEVEL_OVRD_IN,
				temp);
	if (ret) {
		dev_err(phy_drd->dev,
			"Failed setting Tx-Vboost-Level for SuperSpeed\n");
		return ret;
	}

	/*
	 * Set proper time to wait for RxDetect measurement, for
	 * desired reference clock of PHY, by tuning the CR_PORT
	 * register LANE0.TX_DEBUG which is internal to PHY.
	 * This fixes issue with few USB 3.0 devices, which are
	 * not detected (not even generate interrupts on the bus
	 * on insertion) without this change.
	 * e.g. Samsung SUM-TSB16S 3.0 USB drive.
	 */
	switch (phy_drd->extrefclk) {
	case EXYNOS5_FSEL_50MHZ:
		temp = LANE0_TX_DEBUG_RXDET_MEAS_TIME_48M_50M_52M;
		break;
	case EXYNOS5_FSEL_20MHZ:
	case EXYNOS5_FSEL_19MHZ2:
		temp = LANE0_TX_DEBUG_RXDET_MEAS_TIME_19M2_20M;
		break;
	case EXYNOS5_FSEL_24MHZ:
	default:
		temp = LANE0_TX_DEBUG_RXDET_MEAS_TIME_24M;
		break;
	}

	ret = crport_ctrl_write(phy_drd,
				EXYNOS5_DRD_PHYSS_LANE0_TX_DEBUG,
				temp);
	if (ret)
		dev_err(phy_drd->dev,
			"Fail to set RxDet measurement time for SuperSpeed\n");

	return ret;
}

static struct phy *exynos5_usbdrd_phy_xlate(struct device *dev,
					struct of_phandle_args *args)
{
	struct exynos5_usbdrd_phy *phy_drd = dev_get_drvdata(dev);

	if (WARN_ON(args->args[0] >= EXYNOS5_DRDPHYS_NUM))
		return ERR_PTR(-ENODEV);

	return phy_drd->phys[args->args[0]].phy;
}

static int exynos5_usbdrd_phy_calibrate(struct phy *phy)
{
	struct phy_usb_instance *inst = phy_get_drvdata(phy);
	struct exynos5_usbdrd_phy *phy_drd = to_usbdrd_phy(inst);

	if (inst->phy_cfg->id == EXYNOS5_DRDPHY_UTMI)
		return exynos5420_usbdrd_phy_calibrate(phy_drd);
	return 0;
}

static const struct phy_ops exynos5_usbdrd_phy_ops = {
	.init		= exynos5_usbdrd_phy_init,
	.exit		= exynos5_usbdrd_phy_exit,
	.power_on	= exynos5_usbdrd_phy_power_on,
	.power_off	= exynos5_usbdrd_phy_power_off,
	.calibrate	= exynos5_usbdrd_phy_calibrate,
	.owner		= THIS_MODULE,
};

static int exynos5_usbdrd_phy_clk_handle(struct exynos5_usbdrd_phy *phy_drd)
{
	unsigned long ref_rate;
	int ret;

	phy_drd->clk = devm_clk_get(phy_drd->dev, "phy");
	if (IS_ERR(phy_drd->clk)) {
		dev_err(phy_drd->dev, "Failed to get phy clock\n");
		return PTR_ERR(phy_drd->clk);
	}

	phy_drd->ref_clk = devm_clk_get(phy_drd->dev, "ref");
	if (IS_ERR(phy_drd->ref_clk)) {
		dev_err(phy_drd->dev, "Failed to get phy reference clock\n");
		return PTR_ERR(phy_drd->ref_clk);
	}
	ref_rate = clk_get_rate(phy_drd->ref_clk);

	ret = exynos5_rate_to_clk(ref_rate, &phy_drd->extrefclk);
	if (ret) {
		dev_err(phy_drd->dev, "Clock rate (%ld) not supported\n",
			ref_rate);
		return ret;
	}

	if (!phy_drd->drv_data->has_common_clk_gate) {
		phy_drd->pipeclk = devm_clk_get(phy_drd->dev, "phy_pipe");
		if (IS_ERR(phy_drd->pipeclk)) {
			dev_info(phy_drd->dev,
				 "PIPE3 phy operational clock not specified\n");
			phy_drd->pipeclk = NULL;
		}

		phy_drd->utmiclk = devm_clk_get(phy_drd->dev, "phy_utmi");
		if (IS_ERR(phy_drd->utmiclk)) {
			dev_info(phy_drd->dev,
				 "UTMI phy operational clock not specified\n");
			phy_drd->utmiclk = NULL;
		}

		phy_drd->itpclk = devm_clk_get(phy_drd->dev, "itp");
		if (IS_ERR(phy_drd->itpclk)) {
			dev_info(phy_drd->dev,
				 "ITP clock from main OSC not specified\n");
			phy_drd->itpclk = NULL;
		}
	}

	return 0;
}

static const struct exynos5_usbdrd_phy_config phy_cfg_exynos5[] = {
	{
		.id		= EXYNOS5_DRDPHY_UTMI,
		.phy_isol	= exynos5_usbdrd_phy_isol,
		.phy_init	= exynos5_usbdrd_utmi_init,
		.set_refclk	= exynos5_usbdrd_utmi_set_refclk,
	},
	{
		.id		= EXYNOS5_DRDPHY_PIPE3,
		.phy_isol	= exynos5_usbdrd_phy_isol,
		.phy_init	= exynos5_usbdrd_pipe3_init,
		.set_refclk	= exynos5_usbdrd_pipe3_set_refclk,
	},
};

static const struct exynos5_usbdrd_phy_drvdata exynos5420_usbdrd_phy = {
	.phy_cfg		= phy_cfg_exynos5,
	.pmu_offset_usbdrd0_phy	= EXYNOS5_USBDRD_PHY_CONTROL,
	.pmu_offset_usbdrd1_phy	= EXYNOS5420_USBDRD1_PHY_CONTROL,
	.has_common_clk_gate	= true,
};

static const struct exynos5_usbdrd_phy_drvdata exynos5250_usbdrd_phy = {
	.phy_cfg		= phy_cfg_exynos5,
	.pmu_offset_usbdrd0_phy	= EXYNOS5_USBDRD_PHY_CONTROL,
	.has_common_clk_gate	= true,
};

static const struct exynos5_usbdrd_phy_drvdata exynos5433_usbdrd_phy = {
	.phy_cfg		= phy_cfg_exynos5,
	.pmu_offset_usbdrd0_phy	= EXYNOS5_USBDRD_PHY_CONTROL,
	.pmu_offset_usbdrd1_phy	= EXYNOS5433_USBHOST30_PHY_CONTROL,
	.has_common_clk_gate	= false,
};

static const struct exynos5_usbdrd_phy_drvdata exynos7_usbdrd_phy = {
	.phy_cfg		= phy_cfg_exynos5,
	.pmu_offset_usbdrd0_phy	= EXYNOS5_USBDRD_PHY_CONTROL,
	.has_common_clk_gate	= false,
};

static const struct of_device_id exynos5_usbdrd_phy_of_match[] = {
	{
		.compatible = "samsung,exynos5250-usbdrd-phy",
		.data = &exynos5250_usbdrd_phy
	}, {
		.compatible = "samsung,exynos5420-usbdrd-phy",
		.data = &exynos5420_usbdrd_phy
	}, {
		.compatible = "samsung,exynos5433-usbdrd-phy",
		.data = &exynos5433_usbdrd_phy
	}, {
		.compatible = "samsung,exynos7-usbdrd-phy",
		.data = &exynos7_usbdrd_phy
	},
	{ },
};
MODULE_DEVICE_TABLE(of, exynos5_usbdrd_phy_of_match);

static int exynos5_usbdrd_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct exynos5_usbdrd_phy *phy_drd;
	struct phy_provider *phy_provider;
	const struct exynos5_usbdrd_phy_drvdata *drv_data;
	struct regmap *reg_pmu;
	u32 pmu_offset;
	int i, ret;
	int channel;

	phy_drd = devm_kzalloc(dev, sizeof(*phy_drd), GFP_KERNEL);
	if (!phy_drd)
		return -ENOMEM;

	dev_set_drvdata(dev, phy_drd);
	phy_drd->dev = dev;

	phy_drd->reg_phy = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(phy_drd->reg_phy))
		return PTR_ERR(phy_drd->reg_phy);

	drv_data = of_device_get_match_data(dev);
	if (!drv_data)
		return -EINVAL;

	phy_drd->drv_data = drv_data;

	ret = exynos5_usbdrd_phy_clk_handle(phy_drd);
	if (ret) {
		dev_err(dev, "Failed to initialize clocks\n");
		return ret;
	}

	reg_pmu = syscon_regmap_lookup_by_phandle(dev->of_node,
						   "samsung,pmu-syscon");
	if (IS_ERR(reg_pmu)) {
		dev_err(dev, "Failed to lookup PMU regmap\n");
		return PTR_ERR(reg_pmu);
	}

	/*
	 * Exynos5420 SoC has multiple channels for USB 3.0 PHY, with
	 * each having separate power control registers.
	 * 'channel' facilitates to set such registers.
	 */
	channel = of_alias_get_id(node, "usbdrdphy");
	if (channel < 0)
		dev_dbg(dev, "Not a multi-controller usbdrd phy\n");

	switch (channel) {
	case 1:
		pmu_offset = phy_drd->drv_data->pmu_offset_usbdrd1_phy;
		break;
	case 0:
	default:
		pmu_offset = phy_drd->drv_data->pmu_offset_usbdrd0_phy;
		break;
	}

	/* Get Vbus regulators */
	phy_drd->vbus = devm_regulator_get(dev, "vbus");
	if (IS_ERR(phy_drd->vbus)) {
		ret = PTR_ERR(phy_drd->vbus);
		if (ret == -EPROBE_DEFER)
			return ret;

		dev_warn(dev, "Failed to get VBUS supply regulator\n");
		phy_drd->vbus = NULL;
	}

	phy_drd->vbus_boost = devm_regulator_get(dev, "vbus-boost");
	if (IS_ERR(phy_drd->vbus_boost)) {
		ret = PTR_ERR(phy_drd->vbus_boost);
		if (ret == -EPROBE_DEFER)
			return ret;

		dev_warn(dev, "Failed to get VBUS boost supply regulator\n");
		phy_drd->vbus_boost = NULL;
	}

	dev_vdbg(dev, "Creating usbdrd_phy phy\n");

	for (i = 0; i < EXYNOS5_DRDPHYS_NUM; i++) {
		struct phy *phy = devm_phy_create(dev, NULL,
						  &exynos5_usbdrd_phy_ops);
		if (IS_ERR(phy)) {
			dev_err(dev, "Failed to create usbdrd_phy phy\n");
			return PTR_ERR(phy);
		}

		phy_drd->phys[i].phy = phy;
		phy_drd->phys[i].index = i;
		phy_drd->phys[i].reg_pmu = reg_pmu;
		phy_drd->phys[i].pmu_offset = pmu_offset;
		phy_drd->phys[i].phy_cfg = &drv_data->phy_cfg[i];
		phy_set_drvdata(phy, &phy_drd->phys[i]);
	}

	phy_provider = devm_of_phy_provider_register(dev,
						     exynos5_usbdrd_phy_xlate);
	if (IS_ERR(phy_provider)) {
		dev_err(phy_drd->dev, "Failed to register phy provider\n");
		return PTR_ERR(phy_provider);
	}

	return 0;
}

static struct platform_driver exynos5_usb3drd_phy = {
	.probe	= exynos5_usbdrd_phy_probe,
	.driver = {
		.of_match_table	= exynos5_usbdrd_phy_of_match,
		.name		= "exynos5_usb3drd_phy",
		.suppress_bind_attrs = true,
	}
};

module_platform_driver(exynos5_usb3drd_phy);
MODULE_DESCRIPTION("Samsung Exynos5 SoCs USB 3.0 DRD controller PHY driver");
MODULE_AUTHOR("Vivek Gautam <gautam.vivek@samsung.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:exynos5_usb3drd_phy");
