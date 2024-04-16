// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe phy driver for Kirin 970
 *
 * Copyright (C) 2017 HiSilicon Electronics Co., Ltd.
 *		https://www.huawei.com
 * Copyright (C) 2021 Huawei Technologies Co., Ltd.
 *		https://www.huawei.com
 *
 * Authors:
 *	Mauro Carvalho Chehab <mchehab+huawei@kernel.org>
 *	Manivannan Sadhasivam <mani@kernel.org>
 *
 * Based on:
 *	https://lore.kernel.org/lkml/4c9d6581478aa966698758c0420933f5defab4dd.1612335031.git.mchehab+huawei@kernel.org/
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define AXI_CLK_FREQ				207500000
#define REF_CLK_FREQ				100000000

/* PCIe CTRL registers */
#define SOC_PCIECTRL_CTRL7_ADDR			0x01c
#define SOC_PCIECTRL_CTRL12_ADDR		0x030
#define SOC_PCIECTRL_CTRL20_ADDR		0x050
#define SOC_PCIECTRL_CTRL21_ADDR		0x054

#define PCIE_OUTPUT_PULL_BITS			GENMASK(3, 0)
#define SOC_PCIECTRL_CTRL20_2P_MEM_CTRL		0x02605550
#define SOC_PCIECTRL_CTRL21_DEFAULT		0x20000070
#define PCIE_PULL_UP_SYS_AUX_PWR_DET		BIT(10)
#define PCIE_OUTPUT_PULL_DOWN			BIT(1)

/* PCIe PHY registers */
#define SOC_PCIEPHY_CTRL0_ADDR			0x000
#define SOC_PCIEPHY_CTRL1_ADDR			0x004
#define SOC_PCIEPHY_CTRL38_ADDR			0x0098
#define SOC_PCIEPHY_STATE0_ADDR			0x400

#define RAWLANEN_DIG_PCS_XF_TX_OVRD_IN_1	0xc004
#define SUP_DIG_LVL_OVRD_IN			0x003c
#define LANEN_DIG_ASIC_TX_OVRD_IN_1		0x4008
#define LANEN_DIG_ASIC_TX_OVRD_IN_2		0x400c

#define PCIEPHY_RESET_BIT			BIT(17)
#define PCIEPHY_PIPE_LINE0_RESET_BIT		BIT(19)
#define PCIE_TXDETECT_RX_FAIL			BIT(2)
#define PCIE_CLK_SOURCE				BIT(8)
#define PCIE_IS_CLOCK_STABLE			BIT(19)
#define PCIE_PULL_DOWN_PHY_TEST_POWERDOWN	BIT(22)
#define PCIE_DEASSERT_CONTROLLER_PERST		BIT(2)

#define EYEPARAM_NOCFG				0xffffffff
#define EYE_PARM0_MASK				GENMASK(8, 6)
#define EYE_PARM1_MASK				GENMASK(11, 8)
#define EYE_PARM2_MASK				GENMASK(5, 0)
#define EYE_PARM3_MASK				GENMASK(12, 7)
#define EYE_PARM4_MASK				GENMASK(14, 9)
#define EYE_PARM0_EN				BIT(9)
#define EYE_PARM1_EN				BIT(12)
#define EYE_PARM2_EN				BIT(6)
#define EYE_PARM3_EN				BIT(13)
#define EYE_PARM4_EN				BIT(15)

/* hi3670 pciephy register */
#define APB_PHY_START_ADDR			0x40000
#define SOC_PCIEPHY_MMC1PLL_CTRL1		0xc04
#define SOC_PCIEPHY_MMC1PLL_CTRL16		0xC40
#define SOC_PCIEPHY_MMC1PLL_CTRL17		0xC44
#define SOC_PCIEPHY_MMC1PLL_CTRL20		0xC50
#define SOC_PCIEPHY_MMC1PLL_CTRL21		0xC54
#define SOC_PCIEPHY_MMC1PLL_STAT0		0xE00

#define CRGPERIPH_PEREN12			0x470
#define CRGPERIPH_PERDIS12			0x474
#define CRGPERIPH_PCIECTRL0			0x800

#define PCIE_FNPLL_FBDIV_MASK			GENMASK(27, 16)
#define PCIE_FNPLL_FRACDIV_MASK			GENMASK(23, 0)
#define PCIE_FNPLL_POSTDIV1_MASK		GENMASK(10, 8)
#define PCIE_FNPLL_POSTDIV2_MASK		GENMASK(14, 12)
#define PCIE_FNPLL_PLL_MODE_MASK		BIT(25)

#define PCIE_FNPLL_DLL_EN			BIT(27)
#define PCIE_FNPLL_FBDIV			0xd0
#define PCIE_FNPLL_FRACDIV			0x555555
#define PCIE_FNPLL_POSTDIV1			0x5
#define PCIE_FNPLL_POSTDIV2			0x4
#define PCIE_FNPLL_PLL_MODE			0x0

#define PCIE_PHY_MMC1PLL			0x20
#define PCIE_PHY_CHOOSE_FNPLL			BIT(27)
#define PCIE_PHY_MMC1PLL_DISABLE		BIT(0)
#define PCIE_PHY_PCIEPL_BP			BIT(16)

/* define ie,oe cfg */
#define IO_OE_HARD_GT_MODE			BIT(1)
#define IO_IE_EN_HARD_BYPASS			BIT(27)
#define IO_OE_EN_HARD_BYPASS			BIT(11)
#define IO_HARD_CTRL_DEBOUNCE_BYPASS		BIT(10)
#define IO_OE_GT_MODE				BIT(8)
#define DEBOUNCE_WAITCFG_IN			GENMASK(23, 20)
#define DEBOUNCE_WAITCFG_OUT			GENMASK(16, 13)

#define IO_HP_DEBOUNCE_GT			(BIT(12) | BIT(15))
#define IO_PHYREF_SOFT_GT_MODE			BIT(14)
#define IO_REF_SOFT_GT_MODE			BIT(13)
#define IO_REF_HARD_GT_MODE			BIT(0)

/* noc power domain */
#define NOC_POWER_IDLEREQ_1			0x38c
#define NOC_POWER_IDLE_1			0x394
#define NOC_PW_MASK				0x10000
#define NOC_PW_SET_BIT				0x1

#define NUM_EYEPARAM				5

/* info located in sysctrl */
#define SCTRL_PCIE_CMOS_OFFSET			0x60
#define SCTRL_PCIE_CMOS_BIT			0x10
#define SCTRL_PCIE_ISO_OFFSET			0x44
#define SCTRL_PCIE_ISO_BIT			0x30
#define SCTRL_PCIE_HPCLK_OFFSET			0x190
#define SCTRL_PCIE_HPCLK_BIT			0x184000
#define SCTRL_PCIE_OE_OFFSET			0x14a
#define PCIE_DEBOUNCE_PARAM			0xf0f400
#define PCIE_OE_BYPASS				GENMASK(29, 28)

/* peri_crg ctrl */
#define CRGCTRL_PCIE_ASSERT_OFFSET		0x88
#define CRGCTRL_PCIE_ASSERT_BIT			0x8c000000

#define FNPLL_HAS_LOCKED			BIT(4)

/* Time for delay */
#define TIME_CMOS_MIN		100
#define TIME_CMOS_MAX		105
#define PIPE_CLK_STABLE_TIME	100
#define PLL_CTRL_WAIT_TIME	200
#define NOC_POWER_TIME		100

struct hi3670_pcie_phy {
	struct device	*dev;
	void __iomem	*base;
	struct regmap	*apb;
	struct regmap	*crgctrl;
	struct regmap	*sysctrl;
	struct regmap	*pmctrl;
	struct clk	*apb_sys_clk;
	struct clk	*apb_phy_clk;
	struct clk	*phy_ref_clk;
	struct clk	*aclk;
	struct clk	*aux_clk;
	u32		eye_param[NUM_EYEPARAM];
};

/* Registers in PCIePHY */
static inline void hi3670_apb_phy_writel(struct hi3670_pcie_phy *phy, u32 val,
					 u32 reg)
{
	writel(val, phy->base + APB_PHY_START_ADDR + reg);
}

static inline u32 hi3670_apb_phy_readl(struct hi3670_pcie_phy *phy, u32 reg)
{
	return readl(phy->base + APB_PHY_START_ADDR + reg);
}

static inline void hi3670_apb_phy_updatel(struct hi3670_pcie_phy *phy,
					  u32 val, u32 mask, u32 reg)
{
	u32 regval;

	regval = hi3670_apb_phy_readl(phy, reg);
	regval &= ~mask;
	regval |= val;
	hi3670_apb_phy_writel(phy, regval, reg);
}

static inline void kirin_apb_natural_phy_writel(struct hi3670_pcie_phy *phy,
						u32 val, u32 reg)
{
	writel(val, phy->base + reg);
}

static inline u32 kirin_apb_natural_phy_readl(struct hi3670_pcie_phy *phy,
					      u32 reg)
{
	return readl(phy->base + reg);
}

static void hi3670_pcie_phy_oe_enable(struct hi3670_pcie_phy *phy, bool enable)
{
	u32 val;

	regmap_read(phy->sysctrl, SCTRL_PCIE_OE_OFFSET, &val);
	val |= PCIE_DEBOUNCE_PARAM;
	if (enable)
		val &= ~PCIE_OE_BYPASS;
	else
		val |= PCIE_OE_BYPASS;
	regmap_write(phy->sysctrl, SCTRL_PCIE_OE_OFFSET, val);
}

static void hi3670_pcie_get_eyeparam(struct hi3670_pcie_phy *phy)
{
	struct device *dev = phy->dev;
	struct device_node *np;
	int ret, i;

	np = dev->of_node;

	ret = of_property_read_u32_array(np, "hisilicon,eye-diagram-param",
					 phy->eye_param, NUM_EYEPARAM);
	if (!ret)
		return;

	/* There's no optional eye_param property. Set array to default */
	for (i = 0; i < NUM_EYEPARAM; i++)
		phy->eye_param[i] = EYEPARAM_NOCFG;
}

static void hi3670_pcie_set_eyeparam(struct hi3670_pcie_phy *phy)
{
	u32 val;

	val = kirin_apb_natural_phy_readl(phy, RAWLANEN_DIG_PCS_XF_TX_OVRD_IN_1);

	if (phy->eye_param[1] != EYEPARAM_NOCFG) {
		val &= ~EYE_PARM1_MASK;
		val |= FIELD_PREP(EYE_PARM1_MASK, phy->eye_param[1]);
		val |= EYE_PARM1_EN;
	}
	kirin_apb_natural_phy_writel(phy, val,
				     RAWLANEN_DIG_PCS_XF_TX_OVRD_IN_1);

	val = kirin_apb_natural_phy_readl(phy, LANEN_DIG_ASIC_TX_OVRD_IN_2);
	val &= ~(EYE_PARM2_MASK | EYE_PARM3_MASK);
	if (phy->eye_param[2] != EYEPARAM_NOCFG) {
		val |= FIELD_PREP(EYE_PARM2_MASK, phy->eye_param[2]);
		val |= EYE_PARM2_EN;
	}

	if (phy->eye_param[3] != EYEPARAM_NOCFG) {
		val |= FIELD_PREP(EYE_PARM3_MASK, phy->eye_param[3]);
		val |= EYE_PARM3_EN;
	}

	kirin_apb_natural_phy_writel(phy, val, LANEN_DIG_ASIC_TX_OVRD_IN_2);

	val = kirin_apb_natural_phy_readl(phy, SUP_DIG_LVL_OVRD_IN);
	if (phy->eye_param[0] != EYEPARAM_NOCFG) {
		val &= ~EYE_PARM0_MASK;
		val |= FIELD_PREP(EYE_PARM0_MASK, phy->eye_param[0]);
		val |= EYE_PARM0_EN;
	}
	kirin_apb_natural_phy_writel(phy, val, SUP_DIG_LVL_OVRD_IN);

	val = kirin_apb_natural_phy_readl(phy, LANEN_DIG_ASIC_TX_OVRD_IN_1);
	if (phy->eye_param[4] != EYEPARAM_NOCFG) {
		val &= ~EYE_PARM4_MASK;
		val |= FIELD_PREP(EYE_PARM4_MASK, phy->eye_param[4]);
		val |= EYE_PARM4_EN;
	}
	kirin_apb_natural_phy_writel(phy, val, LANEN_DIG_ASIC_TX_OVRD_IN_1);
}

static void hi3670_pcie_natural_cfg(struct hi3670_pcie_phy *phy)
{
	u32 val;

	/* change 2p mem_ctrl */
	regmap_write(phy->apb, SOC_PCIECTRL_CTRL20_ADDR,
		     SOC_PCIECTRL_CTRL20_2P_MEM_CTRL);

	regmap_read(phy->apb, SOC_PCIECTRL_CTRL7_ADDR, &val);
	val |= PCIE_PULL_UP_SYS_AUX_PWR_DET;
	regmap_write(phy->apb, SOC_PCIECTRL_CTRL7_ADDR, val);

	/* output, pull down */
	regmap_read(phy->apb, SOC_PCIECTRL_CTRL12_ADDR, &val);
	val &= ~PCIE_OUTPUT_PULL_BITS;
	val |= PCIE_OUTPUT_PULL_DOWN;
	regmap_write(phy->apb, SOC_PCIECTRL_CTRL12_ADDR, val);

	/* Handle phy_reset and lane0_reset to HW */
	hi3670_apb_phy_updatel(phy, PCIEPHY_RESET_BIT,
			       PCIEPHY_PIPE_LINE0_RESET_BIT | PCIEPHY_RESET_BIT,
			       SOC_PCIEPHY_CTRL1_ADDR);

	/* fix chip bug: TxDetectRx fail */
	hi3670_apb_phy_updatel(phy, PCIE_TXDETECT_RX_FAIL, PCIE_TXDETECT_RX_FAIL,
			       SOC_PCIEPHY_CTRL38_ADDR);
}

static void hi3670_pcie_pll_init(struct hi3670_pcie_phy *phy)
{
	hi3670_apb_phy_updatel(phy, PCIE_PHY_CHOOSE_FNPLL, PCIE_PHY_CHOOSE_FNPLL,
			       SOC_PCIEPHY_MMC1PLL_CTRL1);

	hi3670_apb_phy_updatel(phy,
			       FIELD_PREP(PCIE_FNPLL_FBDIV_MASK, PCIE_FNPLL_FBDIV),
			       PCIE_FNPLL_FBDIV_MASK,
			       SOC_PCIEPHY_MMC1PLL_CTRL16);

	hi3670_apb_phy_updatel(phy,
			       FIELD_PREP(PCIE_FNPLL_FRACDIV_MASK, PCIE_FNPLL_FRACDIV),
			       PCIE_FNPLL_FRACDIV_MASK, SOC_PCIEPHY_MMC1PLL_CTRL17);

	hi3670_apb_phy_updatel(phy,
			       PCIE_FNPLL_DLL_EN |
			       FIELD_PREP(PCIE_FNPLL_POSTDIV1_MASK, PCIE_FNPLL_POSTDIV1) |
			       FIELD_PREP(PCIE_FNPLL_POSTDIV2_MASK, PCIE_FNPLL_POSTDIV2) |
			       FIELD_PREP(PCIE_FNPLL_PLL_MODE_MASK, PCIE_FNPLL_PLL_MODE),
			       PCIE_FNPLL_POSTDIV1_MASK |
			       PCIE_FNPLL_POSTDIV2_MASK |
			       PCIE_FNPLL_PLL_MODE_MASK | PCIE_FNPLL_DLL_EN,
			       SOC_PCIEPHY_MMC1PLL_CTRL20);

	hi3670_apb_phy_writel(phy, PCIE_PHY_MMC1PLL,
			      SOC_PCIEPHY_MMC1PLL_CTRL21);
}

static int hi3670_pcie_pll_ctrl(struct hi3670_pcie_phy *phy, bool enable)
{
	struct device *dev = phy->dev;
	u32 val;
	int time = PLL_CTRL_WAIT_TIME;

	if (enable) {
		/* pd = 0 */
		hi3670_apb_phy_updatel(phy, 0, PCIE_PHY_MMC1PLL_DISABLE,
				       SOC_PCIEPHY_MMC1PLL_CTRL16);

		/* choose FNPLL */
		val = hi3670_apb_phy_readl(phy, SOC_PCIEPHY_MMC1PLL_STAT0);
		while (!(val & FNPLL_HAS_LOCKED)) {
			if (!time) {
				dev_err(dev, "wait for pll_lock timeout\n");
				return -EINVAL;
			}
			time--;
			udelay(1);
			val = hi3670_apb_phy_readl(phy, SOC_PCIEPHY_MMC1PLL_STAT0);
		}

		hi3670_apb_phy_updatel(phy, 0, PCIE_PHY_PCIEPL_BP,
				       SOC_PCIEPHY_MMC1PLL_CTRL20);

	} else {
		hi3670_apb_phy_updatel(phy,
				       PCIE_PHY_MMC1PLL_DISABLE,
				       PCIE_PHY_MMC1PLL_DISABLE,
				       SOC_PCIEPHY_MMC1PLL_CTRL16);

		hi3670_apb_phy_updatel(phy, PCIE_PHY_PCIEPL_BP,
				       PCIE_PHY_PCIEPL_BP,
				       SOC_PCIEPHY_MMC1PLL_CTRL20);
	}

	return 0;
}

static void hi3670_pcie_hp_debounce_gt(struct hi3670_pcie_phy *phy, bool open)
{
	if (open)
		/* gt_clk_pcie_hp/gt_clk_pcie_debounce open */
		regmap_write(phy->crgctrl, CRGPERIPH_PEREN12,
			     IO_HP_DEBOUNCE_GT);
	else
		/* gt_clk_pcie_hp/gt_clk_pcie_debounce close */
		regmap_write(phy->crgctrl, CRGPERIPH_PERDIS12,
			     IO_HP_DEBOUNCE_GT);
}

static void hi3670_pcie_phyref_gt(struct hi3670_pcie_phy *phy, bool open)
{
	unsigned int val;

	regmap_read(phy->crgctrl, CRGPERIPH_PCIECTRL0, &val);

	if (open)
		val &= ~IO_OE_HARD_GT_MODE; /* enable hard gt mode */
	else
		val |= IO_OE_HARD_GT_MODE; /* disable hard gt mode */

	regmap_write(phy->crgctrl, CRGPERIPH_PCIECTRL0, val);

	/* disable soft gt mode */
	regmap_write(phy->crgctrl, CRGPERIPH_PERDIS12, IO_PHYREF_SOFT_GT_MODE);
}

static void hi3670_pcie_oe_ctrl(struct hi3670_pcie_phy *phy, bool en_flag)
{
	unsigned int val;

	regmap_read(phy->crgctrl, CRGPERIPH_PCIECTRL0, &val);

	/* set ie cfg */
	val |= IO_IE_EN_HARD_BYPASS;

	/* set oe cfg */
	val &= ~IO_HARD_CTRL_DEBOUNCE_BYPASS;

	/* set phy_debounce in&out time */
	val |= (DEBOUNCE_WAITCFG_IN | DEBOUNCE_WAITCFG_OUT);

	/* select oe_gt_mode */
	val |= IO_OE_GT_MODE;

	if (en_flag)
		val &= ~IO_OE_EN_HARD_BYPASS;
	else
		val |= IO_OE_EN_HARD_BYPASS;

	regmap_write(phy->crgctrl, CRGPERIPH_PCIECTRL0, val);
}

static void hi3670_pcie_ioref_gt(struct hi3670_pcie_phy *phy, bool open)
{
	unsigned int val;

	if (open) {
		regmap_write(phy->apb, SOC_PCIECTRL_CTRL21_ADDR,
			     SOC_PCIECTRL_CTRL21_DEFAULT);

		hi3670_pcie_oe_ctrl(phy, true);

		/* en hard gt mode */
		regmap_read(phy->crgctrl, CRGPERIPH_PCIECTRL0, &val);
		val &= ~IO_REF_HARD_GT_MODE;
		regmap_write(phy->crgctrl, CRGPERIPH_PCIECTRL0, val);

		/* disable soft gt mode */
		regmap_write(phy->crgctrl, CRGPERIPH_PERDIS12,
			     IO_REF_SOFT_GT_MODE);

	} else {
		/* disable hard gt mode */
		regmap_read(phy->crgctrl, CRGPERIPH_PCIECTRL0, &val);
		val |= IO_REF_HARD_GT_MODE;
		regmap_write(phy->crgctrl, CRGPERIPH_PCIECTRL0, val);

		/* disable soft gt mode */
		regmap_write(phy->crgctrl, CRGPERIPH_PERDIS12,
			     IO_REF_SOFT_GT_MODE);

		hi3670_pcie_oe_ctrl(phy, false);
	}
}

static int hi3670_pcie_allclk_ctrl(struct hi3670_pcie_phy *phy, bool clk_on)
{
	struct device *dev = phy->dev;
	int ret = 0;

	if (!clk_on)
		goto close_clocks;

	/* choose 100MHz clk src: Bit[8]==1 pad, Bit[8]==0 pll */
	hi3670_apb_phy_updatel(phy, 0, PCIE_CLK_SOURCE,
			       SOC_PCIEPHY_CTRL1_ADDR);

	hi3670_pcie_pll_init(phy);

	ret = hi3670_pcie_pll_ctrl(phy, true);
	if (ret) {
		dev_err(dev, "Failed to enable pll\n");
		return -EINVAL;
	}
	hi3670_pcie_hp_debounce_gt(phy, true);
	hi3670_pcie_phyref_gt(phy, true);
	hi3670_pcie_ioref_gt(phy, true);

	ret = clk_set_rate(phy->aclk, AXI_CLK_FREQ);
	if (ret) {
		dev_err(dev, "Failed to set rate\n");
		goto close_clocks;
	}

	return 0;

close_clocks:
	hi3670_pcie_ioref_gt(phy, false);
	hi3670_pcie_phyref_gt(phy, false);
	hi3670_pcie_hp_debounce_gt(phy, false);

	hi3670_pcie_pll_ctrl(phy, false);

	return ret;
}

static bool is_pipe_clk_stable(struct hi3670_pcie_phy *phy)
{
	struct device *dev = phy->dev;
	u32 val;
	u32 time = PIPE_CLK_STABLE_TIME;
	u32 pipe_clk_stable = PCIE_IS_CLOCK_STABLE;

	val = hi3670_apb_phy_readl(phy, SOC_PCIEPHY_STATE0_ADDR);
	while (val & pipe_clk_stable) {
		mdelay(1);
		if (!time) {
			dev_err(dev, "PIPE clk is not stable\n");
			return false;
		}
		time--;
		val = hi3670_apb_phy_readl(phy, SOC_PCIEPHY_STATE0_ADDR);
	}

	return true;
}

static int hi3670_pcie_noc_power(struct hi3670_pcie_phy *phy, bool enable)
{
	struct device *dev = phy->dev;
	u32 time = NOC_POWER_TIME;
	unsigned int val = NOC_PW_MASK;
	int rst;

	if (enable)
		val = NOC_PW_MASK | NOC_PW_SET_BIT;
	else
		val = NOC_PW_MASK;
	rst = enable ? 1 : 0;

	regmap_write(phy->pmctrl, NOC_POWER_IDLEREQ_1, val);

	time = NOC_POWER_TIME;
	regmap_read(phy->pmctrl, NOC_POWER_IDLE_1, &val);
	while ((val & NOC_PW_SET_BIT) != rst) {
		udelay(10);
		if (!time) {
			dev_err(dev, "Failed to reverse noc power-status\n");
			return -EINVAL;
		}
		time--;
		regmap_read(phy->pmctrl, NOC_POWER_IDLE_1, &val);
	}

	return 0;
}

static int hi3670_pcie_get_resources_from_pcie(struct hi3670_pcie_phy *phy)
{
	struct device_node *pcie_port;
	struct device *dev = phy->dev;
	struct device *pcie_dev;

	pcie_port = of_get_child_by_name(dev->parent->of_node, "pcie");
	if (!pcie_port) {
		dev_err(dev, "no pcie node found in %s\n",
			dev->parent->of_node->full_name);
		return -ENODEV;
	}

	pcie_dev = bus_find_device_by_of_node(&platform_bus_type, pcie_port);
	if (!pcie_dev) {
		dev_err(dev, "Didn't find pcie device\n");
		return -ENODEV;
	}

	/*
	 * We might just use NULL instead of the APB name, as the
	 * pcie-kirin currently registers directly just one regmap (although
	 * the DWC driver register other regmaps).
	 *
	 * Yet, it sounds safer to warrant that it will be accessing the
	 * right regmap. So, let's use the named version.
	 */
	phy->apb = dev_get_regmap(pcie_dev, "kirin_pcie_apb");
	if (!phy->apb) {
		dev_err(dev, "Failed to get APB regmap\n");
		return -ENODEV;
	}

	return 0;
}

static int kirin_pcie_clk_ctrl(struct hi3670_pcie_phy *phy, bool enable)
{
	int ret = 0;

	if (!enable)
		goto close_clk;

	ret = clk_set_rate(phy->phy_ref_clk, REF_CLK_FREQ);
	if (ret)
		return ret;

	ret = clk_prepare_enable(phy->phy_ref_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(phy->apb_sys_clk);
	if (ret)
		goto apb_sys_fail;

	ret = clk_prepare_enable(phy->apb_phy_clk);
	if (ret)
		goto apb_phy_fail;

	ret = clk_prepare_enable(phy->aclk);
	if (ret)
		goto aclk_fail;

	ret = clk_prepare_enable(phy->aux_clk);
	if (ret)
		goto aux_clk_fail;

	return 0;

close_clk:
	clk_disable_unprepare(phy->aux_clk);
aux_clk_fail:
	clk_disable_unprepare(phy->aclk);
aclk_fail:
	clk_disable_unprepare(phy->apb_phy_clk);
apb_phy_fail:
	clk_disable_unprepare(phy->apb_sys_clk);
apb_sys_fail:
	clk_disable_unprepare(phy->phy_ref_clk);

	return ret;
}

static int hi3670_pcie_phy_init(struct phy *generic_phy)
{
	struct hi3670_pcie_phy *phy = phy_get_drvdata(generic_phy);
	int ret;

	/*
	 * The code under hi3670_pcie_get_resources_from_pcie() need to
	 * access the reset-gpios and the APB registers, both from the
	 * pcie-kirin driver.
	 *
	 * The APB is obtained via the pcie driver's regmap
	 * Such kind of resource can only be obtained during the PCIe
	 * power_on sequence, as the code inside pcie-kirin needs to
	 * be already probed, as it needs to register the APB regmap.
	 */

	ret = hi3670_pcie_get_resources_from_pcie(phy);
	if (ret)
		return ret;

	return 0;
}

static int hi3670_pcie_phy_power_on(struct phy *generic_phy)
{
	struct hi3670_pcie_phy *phy = phy_get_drvdata(generic_phy);
	int val, ret;

	/* Power supply for Host */
	regmap_write(phy->sysctrl, SCTRL_PCIE_CMOS_OFFSET, SCTRL_PCIE_CMOS_BIT);
	usleep_range(TIME_CMOS_MIN, TIME_CMOS_MAX);

	hi3670_pcie_phy_oe_enable(phy, true);

	ret = kirin_pcie_clk_ctrl(phy, true);
	if (ret)
		return ret;

	/* ISO disable, PCIeCtrl, PHY assert and clk gate clear */
	regmap_write(phy->sysctrl, SCTRL_PCIE_ISO_OFFSET, SCTRL_PCIE_ISO_BIT);
	regmap_write(phy->crgctrl, CRGCTRL_PCIE_ASSERT_OFFSET,
		     CRGCTRL_PCIE_ASSERT_BIT);
	regmap_write(phy->sysctrl, SCTRL_PCIE_HPCLK_OFFSET,
		     SCTRL_PCIE_HPCLK_BIT);

	hi3670_pcie_natural_cfg(phy);

	ret = hi3670_pcie_allclk_ctrl(phy, true);
	if (ret)
		goto disable_clks;

	/* pull down phy_test_powerdown signal */
	hi3670_apb_phy_updatel(phy, 0, PCIE_PULL_DOWN_PHY_TEST_POWERDOWN,
			       SOC_PCIEPHY_CTRL0_ADDR);

	/* deassert controller perst_n */
	regmap_read(phy->apb, SOC_PCIECTRL_CTRL12_ADDR, &val);
	val |= PCIE_DEASSERT_CONTROLLER_PERST;
	regmap_write(phy->apb, SOC_PCIECTRL_CTRL12_ADDR, val);
	udelay(10);

	ret = is_pipe_clk_stable(phy);
	if (!ret)
		goto disable_clks;

	hi3670_pcie_set_eyeparam(phy);

	ret = hi3670_pcie_noc_power(phy, false);
	if (ret)
		goto disable_clks;

	return 0;

disable_clks:
	kirin_pcie_clk_ctrl(phy, false);
	return ret;
}

static int hi3670_pcie_phy_power_off(struct phy *generic_phy)
{
	struct hi3670_pcie_phy *phy = phy_get_drvdata(generic_phy);

	hi3670_pcie_phy_oe_enable(phy, false);

	hi3670_pcie_allclk_ctrl(phy, false);

	/* Drop power supply for Host */
	regmap_write(phy->sysctrl, SCTRL_PCIE_CMOS_OFFSET, 0);

	/*
	 * FIXME: The enabled clocks should be disabled here by calling
	 * kirin_pcie_clk_ctrl(phy, false);
	 * However, some clocks used at Kirin 970 should be marked as
	 * CLK_IS_CRITICAL at clk-hi3670 driver, as powering such clocks off
	 * cause an Asynchronous SError interrupt, which produces panic().
	 * While clk-hi3670 is not fixed, we cannot risk disabling clocks here.
	 */

	return 0;
}

static const struct phy_ops hi3670_phy_ops = {
	.init		= hi3670_pcie_phy_init,
	.power_on	= hi3670_pcie_phy_power_on,
	.power_off	= hi3670_pcie_phy_power_off,
	.owner		= THIS_MODULE,
};

static int hi3670_pcie_phy_get_resources(struct hi3670_pcie_phy *phy,
					 struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	/* syscon */
	phy->crgctrl = syscon_regmap_lookup_by_compatible("hisilicon,hi3670-crgctrl");
	if (IS_ERR(phy->crgctrl))
		return PTR_ERR(phy->crgctrl);

	phy->sysctrl = syscon_regmap_lookup_by_compatible("hisilicon,hi3670-sctrl");
	if (IS_ERR(phy->sysctrl))
		return PTR_ERR(phy->sysctrl);

	phy->pmctrl = syscon_regmap_lookup_by_compatible("hisilicon,hi3670-pmctrl");
	if (IS_ERR(phy->pmctrl))
		return PTR_ERR(phy->pmctrl);

	/* clocks */
	phy->phy_ref_clk = devm_clk_get(dev, "phy_ref");
	if (IS_ERR(phy->phy_ref_clk))
		return PTR_ERR(phy->phy_ref_clk);

	phy->aux_clk = devm_clk_get(dev, "aux");
	if (IS_ERR(phy->aux_clk))
		return PTR_ERR(phy->aux_clk);

	phy->apb_phy_clk = devm_clk_get(dev, "apb_phy");
	if (IS_ERR(phy->apb_phy_clk))
		return PTR_ERR(phy->apb_phy_clk);

	phy->apb_sys_clk = devm_clk_get(dev, "apb_sys");
	if (IS_ERR(phy->apb_sys_clk))
		return PTR_ERR(phy->apb_sys_clk);

	phy->aclk = devm_clk_get(dev, "aclk");
	if (IS_ERR(phy->aclk))
		return PTR_ERR(phy->aclk);

	/* registers */
	phy->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(phy->base))
		return PTR_ERR(phy->base);

	hi3670_pcie_get_eyeparam(phy);

	return 0;
}

static int hi3670_pcie_phy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct hi3670_pcie_phy *phy;
	struct phy *generic_phy;
	int ret;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->dev = dev;

	ret = hi3670_pcie_phy_get_resources(phy, pdev);
	if (ret)
		return ret;

	generic_phy = devm_phy_create(dev, dev->of_node, &hi3670_phy_ops);
	if (IS_ERR(generic_phy)) {
		dev_err(dev, "failed to create PHY\n");
		return PTR_ERR(generic_phy);
	}

	phy_set_drvdata(generic_phy, phy);
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id hi3670_pcie_phy_match[] = {
	{
		.compatible = "hisilicon,hi970-pcie-phy",
	},
	{},
};

static struct platform_driver hi3670_pcie_phy_driver = {
	.probe	= hi3670_pcie_phy_probe,
	.driver = {
		.of_match_table	= hi3670_pcie_phy_match,
		.name		= "hi3670_pcie_phy",
		.suppress_bind_attrs = true,
	}
};
builtin_platform_driver(hi3670_pcie_phy_driver);

MODULE_DEVICE_TABLE(of, hi3670_pcie_phy_match);
MODULE_DESCRIPTION("PCIe phy driver for Kirin 970");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@kernel.org>");
MODULE_AUTHOR("Manivannan Sadhasivam <mani@kernel.org>");
MODULE_LICENSE("GPL v2");
