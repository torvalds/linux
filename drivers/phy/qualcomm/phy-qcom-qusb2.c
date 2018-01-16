// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017, The Linux Foundation. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/slab.h>

#define QUSB2PHY_PLL_TEST		0x04
#define CLK_REF_SEL			BIT(7)

#define QUSB2PHY_PLL_TUNE		0x08
#define QUSB2PHY_PLL_USER_CTL1		0x0c
#define QUSB2PHY_PLL_USER_CTL2		0x10
#define QUSB2PHY_PLL_AUTOPGM_CTL1	0x1c
#define QUSB2PHY_PLL_PWR_CTRL		0x18

/* QUSB2PHY_PLL_STATUS register bits */
#define PLL_LOCKED			BIT(5)

/* QUSB2PHY_PLL_COMMON_STATUS_ONE register bits */
#define CORE_READY_STATUS		BIT(0)

/* QUSB2PHY_PORT_POWERDOWN register bits */
#define CLAMP_N_EN			BIT(5)
#define FREEZIO_N			BIT(1)
#define POWER_DOWN			BIT(0)

/* QUSB2PHY_PWR_CTRL1 register bits */
#define PWR_CTRL1_VREF_SUPPLY_TRIM	BIT(5)
#define PWR_CTRL1_CLAMP_N_EN		BIT(1)

#define QUSB2PHY_REFCLK_ENABLE		BIT(0)

#define PHY_CLK_SCHEME_SEL		BIT(0)

/* QUSB2PHY_INTR_CTRL register bits */
#define DMSE_INTR_HIGH_SEL			BIT(4)
#define DPSE_INTR_HIGH_SEL			BIT(3)
#define CHG_DET_INTR_EN				BIT(2)
#define DMSE_INTR_EN				BIT(1)
#define DPSE_INTR_EN				BIT(0)

/* QUSB2PHY_PLL_CORE_INPUT_OVERRIDE register bits */
#define CORE_PLL_EN_FROM_RESET			BIT(4)
#define CORE_RESET				BIT(5)
#define CORE_RESET_MUX				BIT(6)

#define QUSB2PHY_PLL_ANALOG_CONTROLS_TWO	0x04
#define QUSB2PHY_PLL_CLOCK_INVERTERS		0x18c
#define QUSB2PHY_PLL_CMODE			0x2c
#define QUSB2PHY_PLL_LOCK_DELAY			0x184
#define QUSB2PHY_PLL_DIGITAL_TIMERS_TWO		0xb4
#define QUSB2PHY_PLL_BIAS_CONTROL_1		0x194
#define QUSB2PHY_PLL_BIAS_CONTROL_2		0x198
#define QUSB2PHY_PWR_CTRL2			0x214
#define QUSB2PHY_IMP_CTRL1			0x220
#define QUSB2PHY_IMP_CTRL2			0x224
#define QUSB2PHY_CHG_CTRL2			0x23c

struct qusb2_phy_init_tbl {
	unsigned int offset;
	unsigned int val;
	/*
	 * register part of layout ?
	 * if yes, then offset gives index in the reg-layout
	 */
	int in_layout;
};

#define QUSB2_PHY_INIT_CFG(o, v) \
	{			\
		.offset = o,	\
		.val = v,	\
	}

#define QUSB2_PHY_INIT_CFG_L(o, v) \
	{			\
		.offset = o,	\
		.val = v,	\
		.in_layout = 1,	\
	}

/* set of registers with offsets different per-PHY */
enum qusb2phy_reg_layout {
	QUSB2PHY_PLL_CORE_INPUT_OVERRIDE,
	QUSB2PHY_PLL_STATUS,
	QUSB2PHY_PORT_TUNE1,
	QUSB2PHY_PORT_TUNE2,
	QUSB2PHY_PORT_TUNE3,
	QUSB2PHY_PORT_TUNE4,
	QUSB2PHY_PORT_TUNE5,
	QUSB2PHY_PORT_TEST1,
	QUSB2PHY_PORT_TEST2,
	QUSB2PHY_PORT_POWERDOWN,
	QUSB2PHY_INTR_CTRL,
};

static const unsigned int msm8996_regs_layout[] = {
	[QUSB2PHY_PLL_STATUS]		= 0x38,
	[QUSB2PHY_PORT_TUNE1]		= 0x80,
	[QUSB2PHY_PORT_TUNE2]		= 0x84,
	[QUSB2PHY_PORT_TUNE3]		= 0x88,
	[QUSB2PHY_PORT_TUNE4]		= 0x8c,
	[QUSB2PHY_PORT_TUNE5]		= 0x90,
	[QUSB2PHY_PORT_TEST1]		= 0xb8,
	[QUSB2PHY_PORT_TEST2]		= 0x9c,
	[QUSB2PHY_PORT_POWERDOWN]	= 0xb4,
	[QUSB2PHY_INTR_CTRL]		= 0xbc,
};

static const struct qusb2_phy_init_tbl msm8996_init_tbl[] = {
	QUSB2_PHY_INIT_CFG_L(QUSB2PHY_PORT_TUNE1, 0xf8),
	QUSB2_PHY_INIT_CFG_L(QUSB2PHY_PORT_TUNE2, 0xb3),
	QUSB2_PHY_INIT_CFG_L(QUSB2PHY_PORT_TUNE3, 0x83),
	QUSB2_PHY_INIT_CFG_L(QUSB2PHY_PORT_TUNE4, 0xc0),

	QUSB2_PHY_INIT_CFG(QUSB2PHY_PLL_TUNE, 0x30),
	QUSB2_PHY_INIT_CFG(QUSB2PHY_PLL_USER_CTL1, 0x79),
	QUSB2_PHY_INIT_CFG(QUSB2PHY_PLL_USER_CTL2, 0x21),

	QUSB2_PHY_INIT_CFG_L(QUSB2PHY_PORT_TEST2, 0x14),

	QUSB2_PHY_INIT_CFG(QUSB2PHY_PLL_AUTOPGM_CTL1, 0x9f),
	QUSB2_PHY_INIT_CFG(QUSB2PHY_PLL_PWR_CTRL, 0x00),
};

static const unsigned int qusb2_v2_regs_layout[] = {
	[QUSB2PHY_PLL_CORE_INPUT_OVERRIDE] = 0xa8,
	[QUSB2PHY_PLL_STATUS]		= 0x1a0,
	[QUSB2PHY_PORT_TUNE1]		= 0x240,
	[QUSB2PHY_PORT_TUNE2]		= 0x244,
	[QUSB2PHY_PORT_TUNE3]		= 0x248,
	[QUSB2PHY_PORT_TUNE4]		= 0x24c,
	[QUSB2PHY_PORT_TUNE5]		= 0x250,
	[QUSB2PHY_PORT_TEST1]		= 0x254,
	[QUSB2PHY_PORT_TEST2]		= 0x258,
	[QUSB2PHY_PORT_POWERDOWN]	= 0x210,
	[QUSB2PHY_INTR_CTRL]		= 0x230,
};

static const struct qusb2_phy_init_tbl qusb2_v2_init_tbl[] = {
	QUSB2_PHY_INIT_CFG(QUSB2PHY_PLL_ANALOG_CONTROLS_TWO, 0x03),
	QUSB2_PHY_INIT_CFG(QUSB2PHY_PLL_CLOCK_INVERTERS, 0x7c),
	QUSB2_PHY_INIT_CFG(QUSB2PHY_PLL_CMODE, 0x80),
	QUSB2_PHY_INIT_CFG(QUSB2PHY_PLL_LOCK_DELAY, 0x0a),
	QUSB2_PHY_INIT_CFG(QUSB2PHY_PLL_DIGITAL_TIMERS_TWO, 0x19),
	QUSB2_PHY_INIT_CFG(QUSB2PHY_PLL_BIAS_CONTROL_1, 0x40),
	QUSB2_PHY_INIT_CFG(QUSB2PHY_PLL_BIAS_CONTROL_2, 0x20),
	QUSB2_PHY_INIT_CFG(QUSB2PHY_PWR_CTRL2, 0x21),
	QUSB2_PHY_INIT_CFG(QUSB2PHY_IMP_CTRL1, 0x0),
	QUSB2_PHY_INIT_CFG(QUSB2PHY_IMP_CTRL2, 0x58),

	QUSB2_PHY_INIT_CFG_L(QUSB2PHY_PORT_TUNE1, 0x30),
	QUSB2_PHY_INIT_CFG_L(QUSB2PHY_PORT_TUNE2, 0x29),
	QUSB2_PHY_INIT_CFG_L(QUSB2PHY_PORT_TUNE3, 0xca),
	QUSB2_PHY_INIT_CFG_L(QUSB2PHY_PORT_TUNE4, 0x04),
	QUSB2_PHY_INIT_CFG_L(QUSB2PHY_PORT_TUNE5, 0x03),

	QUSB2_PHY_INIT_CFG(QUSB2PHY_CHG_CTRL2, 0x0),
};

struct qusb2_phy_cfg {
	const struct qusb2_phy_init_tbl *tbl;
	/* number of entries in the table */
	unsigned int tbl_num;
	/* offset to PHY_CLK_SCHEME register in TCSR map */
	unsigned int clk_scheme_offset;

	/* array of registers with different offsets */
	const unsigned int *regs;
	unsigned int mask_core_ready;
	unsigned int disable_ctrl;
	unsigned int autoresume_en;

	/* true if PHY has PLL_TEST register to select clk_scheme */
	bool has_pll_test;

	/* true if TUNE1 register must be updated by fused value, else TUNE2 */
	bool update_tune1_with_efuse;

	/* true if PHY has PLL_CORE_INPUT_OVERRIDE register to reset PLL */
	bool has_pll_override;
};

static const struct qusb2_phy_cfg msm8996_phy_cfg = {
	.tbl		= msm8996_init_tbl,
	.tbl_num	= ARRAY_SIZE(msm8996_init_tbl),
	.regs		= msm8996_regs_layout,

	.has_pll_test	= true,
	.disable_ctrl	= (CLAMP_N_EN | FREEZIO_N | POWER_DOWN),
	.mask_core_ready = PLL_LOCKED,
	.autoresume_en	 = BIT(3),
};

static const struct qusb2_phy_cfg qusb2_v2_phy_cfg = {
	.tbl		= qusb2_v2_init_tbl,
	.tbl_num	= ARRAY_SIZE(qusb2_v2_init_tbl),
	.regs		= qusb2_v2_regs_layout,

	.disable_ctrl	= (PWR_CTRL1_VREF_SUPPLY_TRIM | PWR_CTRL1_CLAMP_N_EN |
			   POWER_DOWN),
	.mask_core_ready = CORE_READY_STATUS,
	.has_pll_override = true,
	.autoresume_en	  = BIT(0),
};

static const char * const qusb2_phy_vreg_names[] = {
	"vdda-pll", "vdda-phy-dpdm",
};

#define QUSB2_NUM_VREGS		ARRAY_SIZE(qusb2_phy_vreg_names)

/**
 * struct qusb2_phy - structure holding qusb2 phy attributes
 *
 * @phy: generic phy
 * @base: iomapped memory space for qubs2 phy
 *
 * @cfg_ahb_clk: AHB2PHY interface clock
 * @ref_clk: phy reference clock
 * @iface_clk: phy interface clock
 * @phy_reset: phy reset control
 * @vregs: regulator supplies bulk data
 *
 * @tcsr: TCSR syscon register map
 * @cell: nvmem cell containing phy tuning value
 *
 * @cfg: phy config data
 * @has_se_clk_scheme: indicate if PHY has single-ended ref clock scheme
 * @phy_initialized: indicate if PHY has been initialized
 * @mode: current PHY mode
 */
struct qusb2_phy {
	struct phy *phy;
	void __iomem *base;

	struct clk *cfg_ahb_clk;
	struct clk *ref_clk;
	struct clk *iface_clk;
	struct reset_control *phy_reset;
	struct regulator_bulk_data vregs[QUSB2_NUM_VREGS];

	struct regmap *tcsr;
	struct nvmem_cell *cell;

	const struct qusb2_phy_cfg *cfg;
	bool has_se_clk_scheme;
	bool phy_initialized;
	enum phy_mode mode;
};

static inline void qusb2_setbits(void __iomem *base, u32 offset, u32 val)
{
	u32 reg;

	reg = readl(base + offset);
	reg |= val;
	writel(reg, base + offset);

	/* Ensure above write is completed */
	readl(base + offset);
}

static inline void qusb2_clrbits(void __iomem *base, u32 offset, u32 val)
{
	u32 reg;

	reg = readl(base + offset);
	reg &= ~val;
	writel(reg, base + offset);

	/* Ensure above write is completed */
	readl(base + offset);
}

static inline
void qcom_qusb2_phy_configure(void __iomem *base,
			      const unsigned int *regs,
			      const struct qusb2_phy_init_tbl tbl[], int num)
{
	int i;

	for (i = 0; i < num; i++) {
		if (tbl[i].in_layout)
			writel(tbl[i].val, base + regs[tbl[i].offset]);
		else
			writel(tbl[i].val, base + tbl[i].offset);
	}
}

/*
 * Fetches HS Tx tuning value from nvmem and sets the
 * QUSB2PHY_PORT_TUNE1/2 register.
 * For error case, skip setting the value and use the default value.
 */
static void qusb2_phy_set_tune2_param(struct qusb2_phy *qphy)
{
	struct device *dev = &qphy->phy->dev;
	const struct qusb2_phy_cfg *cfg = qphy->cfg;
	u8 *val;

	/*
	 * Read efuse register having TUNE2/1 parameter's high nibble.
	 * If efuse register shows value as 0x0, or if we fail to find
	 * a valid efuse register settings, then use default value
	 * as 0xB for high nibble that we have already set while
	 * configuring phy.
	 */
	val = nvmem_cell_read(qphy->cell, NULL);
	if (IS_ERR(val) || !val[0]) {
		dev_dbg(dev, "failed to read a valid hs-tx trim value\n");
		return;
	}

	/* Fused TUNE1/2 value is the higher nibble only */
	if (cfg->update_tune1_with_efuse)
		qusb2_setbits(qphy->base, cfg->regs[QUSB2PHY_PORT_TUNE1],
			      val[0] << 0x4);
	else
		qusb2_setbits(qphy->base, cfg->regs[QUSB2PHY_PORT_TUNE2],
			      val[0] << 0x4);

}

static int qusb2_phy_set_mode(struct phy *phy, enum phy_mode mode)
{
	struct qusb2_phy *qphy = phy_get_drvdata(phy);

	qphy->mode = mode;

	return 0;
}

static int __maybe_unused qusb2_phy_runtime_suspend(struct device *dev)
{
	struct qusb2_phy *qphy = dev_get_drvdata(dev);
	const struct qusb2_phy_cfg *cfg = qphy->cfg;
	u32 intr_mask;

	dev_vdbg(dev, "Suspending QUSB2 Phy, mode:%d\n", qphy->mode);

	if (!qphy->phy_initialized) {
		dev_vdbg(dev, "PHY not initialized, bailing out\n");
		return 0;
	}

	/*
	 * Enable DP/DM interrupts to detect line state changes based on current
	 * speed. In other words, enable the triggers _opposite_ of what the
	 * current D+/D- levels are e.g. if currently D+ high, D- low
	 * (HS 'J'/Suspend), configure the mask to trigger on D+ low OR D- high
	 */
	intr_mask = DPSE_INTR_EN | DMSE_INTR_EN;
	switch (qphy->mode) {
	case PHY_MODE_USB_HOST_HS:
	case PHY_MODE_USB_HOST_FS:
	case PHY_MODE_USB_DEVICE_HS:
	case PHY_MODE_USB_DEVICE_FS:
		intr_mask |= DMSE_INTR_HIGH_SEL;
		break;
	case PHY_MODE_USB_HOST_LS:
	case PHY_MODE_USB_DEVICE_LS:
		intr_mask |= DPSE_INTR_HIGH_SEL;
		break;
	default:
		/* No device connected, enable both DP/DM high interrupt */
		intr_mask |= DMSE_INTR_HIGH_SEL;
		intr_mask |= DPSE_INTR_HIGH_SEL;
		break;
	}

	writel(intr_mask, qphy->base + cfg->regs[QUSB2PHY_INTR_CTRL]);

	/* hold core PLL into reset */
	if (cfg->has_pll_override) {
		qusb2_setbits(qphy->base,
			      cfg->regs[QUSB2PHY_PLL_CORE_INPUT_OVERRIDE],
			      CORE_PLL_EN_FROM_RESET | CORE_RESET |
			      CORE_RESET_MUX);
	}

	/* enable phy auto-resume only if device is connected on bus */
	if (qphy->mode != PHY_MODE_INVALID) {
		qusb2_setbits(qphy->base, cfg->regs[QUSB2PHY_PORT_TEST1],
			      cfg->autoresume_en);
		/* Autoresume bit has to be toggled in order to enable it */
		qusb2_clrbits(qphy->base, cfg->regs[QUSB2PHY_PORT_TEST1],
			      cfg->autoresume_en);
	}

	if (!qphy->has_se_clk_scheme)
		clk_disable_unprepare(qphy->ref_clk);

	clk_disable_unprepare(qphy->cfg_ahb_clk);
	clk_disable_unprepare(qphy->iface_clk);

	return 0;
}

static int __maybe_unused qusb2_phy_runtime_resume(struct device *dev)
{
	struct qusb2_phy *qphy = dev_get_drvdata(dev);
	const struct qusb2_phy_cfg *cfg = qphy->cfg;
	int ret;

	dev_vdbg(dev, "Resuming QUSB2 phy, mode:%d\n", qphy->mode);

	if (!qphy->phy_initialized) {
		dev_vdbg(dev, "PHY not initialized, bailing out\n");
		return 0;
	}

	ret = clk_prepare_enable(qphy->iface_clk);
	if (ret) {
		dev_err(dev, "failed to enable iface_clk, %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(qphy->cfg_ahb_clk);
	if (ret) {
		dev_err(dev, "failed to enable cfg ahb clock, %d\n", ret);
		goto disable_iface_clk;
	}

	if (!qphy->has_se_clk_scheme) {
		clk_prepare_enable(qphy->ref_clk);
		if (ret) {
			dev_err(dev, "failed to enable ref clk, %d\n", ret);
			goto disable_ahb_clk;
		}
	}

	writel(0x0, qphy->base + cfg->regs[QUSB2PHY_INTR_CTRL]);

	/* bring core PLL out of reset */
	if (cfg->has_pll_override) {
		qusb2_clrbits(qphy->base,
			      cfg->regs[QUSB2PHY_PLL_CORE_INPUT_OVERRIDE],
			      CORE_RESET | CORE_RESET_MUX);
	}

	return 0;

disable_ahb_clk:
	clk_disable_unprepare(qphy->cfg_ahb_clk);
disable_iface_clk:
	clk_disable_unprepare(qphy->iface_clk);

	return ret;
}

static int qusb2_phy_init(struct phy *phy)
{
	struct qusb2_phy *qphy = phy_get_drvdata(phy);
	const struct qusb2_phy_cfg *cfg = qphy->cfg;
	unsigned int val = 0;
	unsigned int clk_scheme;
	int ret;

	dev_vdbg(&phy->dev, "%s(): Initializing QUSB2 phy\n", __func__);

	/* turn on regulator supplies */
	ret = regulator_bulk_enable(ARRAY_SIZE(qphy->vregs), qphy->vregs);
	if (ret)
		return ret;

	ret = clk_prepare_enable(qphy->iface_clk);
	if (ret) {
		dev_err(&phy->dev, "failed to enable iface_clk, %d\n", ret);
		goto poweroff_phy;
	}

	/* enable ahb interface clock to program phy */
	ret = clk_prepare_enable(qphy->cfg_ahb_clk);
	if (ret) {
		dev_err(&phy->dev, "failed to enable cfg ahb clock, %d\n", ret);
		goto disable_iface_clk;
	}

	/* Perform phy reset */
	ret = reset_control_assert(qphy->phy_reset);
	if (ret) {
		dev_err(&phy->dev, "failed to assert phy_reset, %d\n", ret);
		goto disable_ahb_clk;
	}

	/* 100 us delay to keep PHY in reset mode */
	usleep_range(100, 150);

	ret = reset_control_deassert(qphy->phy_reset);
	if (ret) {
		dev_err(&phy->dev, "failed to de-assert phy_reset, %d\n", ret);
		goto disable_ahb_clk;
	}

	/* Disable the PHY */
	qusb2_setbits(qphy->base, cfg->regs[QUSB2PHY_PORT_POWERDOWN],
		      qphy->cfg->disable_ctrl);

	if (cfg->has_pll_test) {
		/* save reset value to override reference clock scheme later */
		val = readl(qphy->base + QUSB2PHY_PLL_TEST);
	}

	qcom_qusb2_phy_configure(qphy->base, cfg->regs, cfg->tbl,
				 cfg->tbl_num);

	/* Set efuse value for tuning the PHY */
	qusb2_phy_set_tune2_param(qphy);

	/* Enable the PHY */
	qusb2_clrbits(qphy->base, cfg->regs[QUSB2PHY_PORT_POWERDOWN],
		      POWER_DOWN);

	/* Required to get phy pll lock successfully */
	usleep_range(150, 160);

	/* Default is single-ended clock on msm8996 */
	qphy->has_se_clk_scheme = true;
	/*
	 * read TCSR_PHY_CLK_SCHEME register to check if single-ended
	 * clock scheme is selected. If yes, then disable differential
	 * ref_clk and use single-ended clock, otherwise use differential
	 * ref_clk only.
	 */
	if (qphy->tcsr) {
		ret = regmap_read(qphy->tcsr, qphy->cfg->clk_scheme_offset,
				  &clk_scheme);
		if (ret) {
			dev_err(&phy->dev, "failed to read clk scheme reg\n");
			goto assert_phy_reset;
		}

		/* is it a differential clock scheme ? */
		if (!(clk_scheme & PHY_CLK_SCHEME_SEL)) {
			dev_vdbg(&phy->dev, "%s(): select differential clk\n",
				 __func__);
			qphy->has_se_clk_scheme = false;
		} else {
			dev_vdbg(&phy->dev, "%s(): select single-ended clk\n",
				 __func__);
		}
	}

	if (!qphy->has_se_clk_scheme) {
		ret = clk_prepare_enable(qphy->ref_clk);
		if (ret) {
			dev_err(&phy->dev, "failed to enable ref clk, %d\n",
				ret);
			goto assert_phy_reset;
		}
	}

	if (cfg->has_pll_test) {
		if (!qphy->has_se_clk_scheme)
			val &= ~CLK_REF_SEL;
		else
			val |= CLK_REF_SEL;

		writel(val, qphy->base + QUSB2PHY_PLL_TEST);

		/* ensure above write is through */
		readl(qphy->base + QUSB2PHY_PLL_TEST);
	}

	/* Required to get phy pll lock successfully */
	usleep_range(100, 110);

	val = readb(qphy->base + cfg->regs[QUSB2PHY_PLL_STATUS]);
	if (!(val & cfg->mask_core_ready)) {
		dev_err(&phy->dev,
			"QUSB2PHY pll lock failed: status reg = %x\n", val);
		ret = -EBUSY;
		goto disable_ref_clk;
	}
	qphy->phy_initialized = true;

	return 0;

disable_ref_clk:
	if (!qphy->has_se_clk_scheme)
		clk_disable_unprepare(qphy->ref_clk);
assert_phy_reset:
	reset_control_assert(qphy->phy_reset);
disable_ahb_clk:
	clk_disable_unprepare(qphy->cfg_ahb_clk);
disable_iface_clk:
	clk_disable_unprepare(qphy->iface_clk);
poweroff_phy:
	regulator_bulk_disable(ARRAY_SIZE(qphy->vregs), qphy->vregs);

	return ret;
}

static int qusb2_phy_exit(struct phy *phy)
{
	struct qusb2_phy *qphy = phy_get_drvdata(phy);

	/* Disable the PHY */
	qusb2_setbits(qphy->base, qphy->cfg->regs[QUSB2PHY_PORT_POWERDOWN],
		      qphy->cfg->disable_ctrl);

	if (!qphy->has_se_clk_scheme)
		clk_disable_unprepare(qphy->ref_clk);

	reset_control_assert(qphy->phy_reset);

	clk_disable_unprepare(qphy->cfg_ahb_clk);
	clk_disable_unprepare(qphy->iface_clk);

	regulator_bulk_disable(ARRAY_SIZE(qphy->vregs), qphy->vregs);

	qphy->phy_initialized = false;

	return 0;
}

static const struct phy_ops qusb2_phy_gen_ops = {
	.init		= qusb2_phy_init,
	.exit		= qusb2_phy_exit,
	.set_mode	= qusb2_phy_set_mode,
	.owner		= THIS_MODULE,
};

static const struct of_device_id qusb2_phy_of_match_table[] = {
	{
		.compatible	= "qcom,msm8996-qusb2-phy",
		.data		= &msm8996_phy_cfg,
	}, {
		.compatible	= "qcom,qusb2-v2-phy",
		.data		= &qusb2_v2_phy_cfg,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, qusb2_phy_of_match_table);

static const struct dev_pm_ops qusb2_phy_pm_ops = {
	SET_RUNTIME_PM_OPS(qusb2_phy_runtime_suspend,
			   qusb2_phy_runtime_resume, NULL)
};

static int qusb2_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct qusb2_phy *qphy;
	struct phy_provider *phy_provider;
	struct phy *generic_phy;
	struct resource *res;
	int ret, i;
	int num;

	qphy = devm_kzalloc(dev, sizeof(*qphy), GFP_KERNEL);
	if (!qphy)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	qphy->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(qphy->base))
		return PTR_ERR(qphy->base);

	qphy->cfg_ahb_clk = devm_clk_get(dev, "cfg_ahb");
	if (IS_ERR(qphy->cfg_ahb_clk)) {
		ret = PTR_ERR(qphy->cfg_ahb_clk);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to get cfg ahb clk, %d\n", ret);
		return ret;
	}

	qphy->ref_clk = devm_clk_get(dev, "ref");
	if (IS_ERR(qphy->ref_clk)) {
		ret = PTR_ERR(qphy->ref_clk);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to get ref clk, %d\n", ret);
		return ret;
	}

	qphy->iface_clk = devm_clk_get(dev, "iface");
	if (IS_ERR(qphy->iface_clk)) {
		ret = PTR_ERR(qphy->iface_clk);
		if (ret == -EPROBE_DEFER)
			return ret;
		qphy->iface_clk = NULL;
		dev_dbg(dev, "failed to get iface clk, %d\n", ret);
	}

	qphy->phy_reset = devm_reset_control_get_by_index(&pdev->dev, 0);
	if (IS_ERR(qphy->phy_reset)) {
		dev_err(dev, "failed to get phy core reset\n");
		return PTR_ERR(qphy->phy_reset);
	}

	num = ARRAY_SIZE(qphy->vregs);
	for (i = 0; i < num; i++)
		qphy->vregs[i].supply = qusb2_phy_vreg_names[i];

	ret = devm_regulator_bulk_get(dev, num, qphy->vregs);
	if (ret) {
		dev_err(dev, "failed to get regulator supplies\n");
		return ret;
	}

	/* Get the specific init parameters of QMP phy */
	qphy->cfg = of_device_get_match_data(dev);

	qphy->tcsr = syscon_regmap_lookup_by_phandle(dev->of_node,
							"qcom,tcsr-syscon");
	if (IS_ERR(qphy->tcsr)) {
		dev_dbg(dev, "failed to lookup TCSR regmap\n");
		qphy->tcsr = NULL;
	}

	qphy->cell = devm_nvmem_cell_get(dev, NULL);
	if (IS_ERR(qphy->cell)) {
		if (PTR_ERR(qphy->cell) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		qphy->cell = NULL;
		dev_dbg(dev, "failed to lookup tune2 hstx trim value\n");
	}
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	/*
	 * Prevent runtime pm from being ON by default. Users can enable
	 * it using power/control in sysfs.
	 */
	pm_runtime_forbid(dev);

	generic_phy = devm_phy_create(dev, NULL, &qusb2_phy_gen_ops);
	if (IS_ERR(generic_phy)) {
		ret = PTR_ERR(generic_phy);
		dev_err(dev, "failed to create phy, %d\n", ret);
		pm_runtime_disable(dev);
		return ret;
	}
	qphy->phy = generic_phy;

	dev_set_drvdata(dev, qphy);
	phy_set_drvdata(generic_phy, qphy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (!IS_ERR(phy_provider))
		dev_info(dev, "Registered Qcom-QUSB2 phy\n");
	else
		pm_runtime_disable(dev);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static struct platform_driver qusb2_phy_driver = {
	.probe		= qusb2_phy_probe,
	.driver = {
		.name	= "qcom-qusb2-phy",
		.pm	= &qusb2_phy_pm_ops,
		.of_match_table = qusb2_phy_of_match_table,
	},
};

module_platform_driver(qusb2_phy_driver);

MODULE_AUTHOR("Vivek Gautam <vivek.gautam@codeaurora.org>");
MODULE_DESCRIPTION("Qualcomm QUSB2 PHY driver");
MODULE_LICENSE("GPL v2");
