// SPDX-License-Identifier: GPL-2.0
/*
 * phy-zynqmp.c - PHY driver for Xilinx ZynqMP GT.
 *
 * Copyright (C) 2018-2020 Xilinx Inc.
 *
 * Author: Anurag Kumar Vulisha <anuragku@xilinx.com>
 * Author: Subbaraya Sundeep <sundeep.lkml@gmail.com>
 * Author: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * This driver is tested for USB, SATA and Display Port currently.
 * Other controllers PCIe and SGMII should also work but that is
 * experimental as of now.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <dt-bindings/phy/phy.h>

/*
 * Lane Registers
 */

/* TX De-emphasis parameters */
#define L0_TX_ANA_TM_18			0x0048
#define L0_TX_ANA_TM_118		0x01d8
#define L0_TX_ANA_TM_118_FORCE_17_0	BIT(0)

/* DN Resistor calibration code parameters */
#define L0_TXPMA_ST_3			0x0b0c
#define L0_DN_CALIB_CODE		0x3f

/* PMA control parameters */
#define L0_TXPMD_TM_45			0x0cb4
#define L0_TXPMD_TM_48			0x0cc0
#define L0_TXPMD_TM_45_OVER_DP_MAIN	BIT(0)
#define L0_TXPMD_TM_45_ENABLE_DP_MAIN	BIT(1)
#define L0_TXPMD_TM_45_OVER_DP_POST1	BIT(2)
#define L0_TXPMD_TM_45_ENABLE_DP_POST1	BIT(3)
#define L0_TXPMD_TM_45_OVER_DP_POST2	BIT(4)
#define L0_TXPMD_TM_45_ENABLE_DP_POST2	BIT(5)

/* PCS control parameters */
#define L0_TM_DIG_6			0x106c
#define L0_TM_DIS_DESCRAMBLE_DECODER	0x0f
#define L0_TX_DIG_61			0x00f4
#define L0_TM_DISABLE_SCRAMBLE_ENCODER	0x0f

/* PLL Test Mode register parameters */
#define L0_TM_PLL_DIG_37		0x2094
#define L0_TM_COARSE_CODE_LIMIT		0x10

/* PLL SSC step size offsets */
#define L0_PLL_SS_STEPS_0_LSB		0x2368
#define L0_PLL_SS_STEPS_1_MSB		0x236c
#define L0_PLL_SS_STEP_SIZE_0_LSB	0x2370
#define L0_PLL_SS_STEP_SIZE_1		0x2374
#define L0_PLL_SS_STEP_SIZE_2		0x2378
#define L0_PLL_SS_STEP_SIZE_3_MSB	0x237c
#define L0_PLL_STATUS_READ_1		0x23e4

/* SSC step size parameters */
#define STEP_SIZE_0_MASK		0xff
#define STEP_SIZE_1_MASK		0xff
#define STEP_SIZE_2_MASK		0xff
#define STEP_SIZE_3_MASK		0x3
#define STEP_SIZE_SHIFT			8
#define FORCE_STEP_SIZE			0x10
#define FORCE_STEPS			0x20
#define STEPS_0_MASK			0xff
#define STEPS_1_MASK			0x07

/* Reference clock selection parameters */
#define L0_Ln_REF_CLK_SEL(n)		(0x2860 + (n) * 4)
#define L0_REF_CLK_SEL_MASK		0x8f

/* Calibration digital logic parameters */
#define L3_TM_CALIB_DIG19		0xec4c
#define L3_CALIB_DONE_STATUS		0xef14
#define L3_TM_CALIB_DIG18		0xec48
#define L3_TM_CALIB_DIG19_NSW		0x07
#define L3_TM_CALIB_DIG18_NSW		0xe0
#define L3_TM_OVERRIDE_NSW_CODE         0x20
#define L3_CALIB_DONE			0x02
#define L3_NSW_SHIFT			5
#define L3_NSW_PIPE_SHIFT		4
#define L3_NSW_CALIB_SHIFT		3

#define PHY_REG_OFFSET			0x4000

/*
 * Global Registers
 */

/* Refclk selection parameters */
#define PLL_REF_SEL(n)			(0x10000 + (n) * 4)
#define PLL_FREQ_MASK			0x1f
#define PLL_STATUS_LOCKED		0x10

/* Inter Connect Matrix parameters */
#define ICM_CFG0			0x10010
#define ICM_CFG1			0x10014
#define ICM_CFG0_L0_MASK		0x07
#define ICM_CFG0_L1_MASK		0x70
#define ICM_CFG1_L2_MASK		0x07
#define ICM_CFG2_L3_MASK		0x70
#define ICM_CFG_SHIFT			4

/* Inter Connect Matrix allowed protocols */
#define ICM_PROTOCOL_PD			0x0
#define ICM_PROTOCOL_PCIE		0x1
#define ICM_PROTOCOL_SATA		0x2
#define ICM_PROTOCOL_USB		0x3
#define ICM_PROTOCOL_DP			0x4
#define ICM_PROTOCOL_SGMII		0x5

/* Test Mode common reset control  parameters */
#define TM_CMN_RST			0x10018
#define TM_CMN_RST_EN			0x1
#define TM_CMN_RST_SET			0x2
#define TM_CMN_RST_MASK			0x3

/* Bus width parameters */
#define TX_PROT_BUS_WIDTH		0x10040
#define RX_PROT_BUS_WIDTH		0x10044
#define PROT_BUS_WIDTH_10		0x0
#define PROT_BUS_WIDTH_20		0x1
#define PROT_BUS_WIDTH_40		0x2
#define PROT_BUS_WIDTH_SHIFT		2

/* Number of GT lanes */
#define NUM_LANES			4

/* SIOU SATA control register */
#define SATA_CONTROL_OFFSET		0x0100

/* Total number of controllers */
#define CONTROLLERS_PER_LANE		5

/* Protocol Type parameters */
#define XPSGTR_TYPE_USB0		0  /* USB controller 0 */
#define XPSGTR_TYPE_USB1		1  /* USB controller 1 */
#define XPSGTR_TYPE_SATA_0		2  /* SATA controller lane 0 */
#define XPSGTR_TYPE_SATA_1		3  /* SATA controller lane 1 */
#define XPSGTR_TYPE_PCIE_0		4  /* PCIe controller lane 0 */
#define XPSGTR_TYPE_PCIE_1		5  /* PCIe controller lane 1 */
#define XPSGTR_TYPE_PCIE_2		6  /* PCIe controller lane 2 */
#define XPSGTR_TYPE_PCIE_3		7  /* PCIe controller lane 3 */
#define XPSGTR_TYPE_DP_0		8  /* Display Port controller lane 0 */
#define XPSGTR_TYPE_DP_1		9  /* Display Port controller lane 1 */
#define XPSGTR_TYPE_SGMII0		10 /* Ethernet SGMII controller 0 */
#define XPSGTR_TYPE_SGMII1		11 /* Ethernet SGMII controller 1 */
#define XPSGTR_TYPE_SGMII2		12 /* Ethernet SGMII controller 2 */
#define XPSGTR_TYPE_SGMII3		13 /* Ethernet SGMII controller 3 */

/* Timeout values */
#define TIMEOUT_US			1000

struct xpsgtr_dev;

/**
 * struct xpsgtr_ssc - structure to hold SSC settings for a lane
 * @refclk_rate: PLL reference clock frequency
 * @pll_ref_clk: value to be written to register for corresponding ref clk rate
 * @steps: number of steps of SSC (Spread Spectrum Clock)
 * @step_size: step size of each step
 */
struct xpsgtr_ssc {
	u32 refclk_rate;
	u8  pll_ref_clk;
	u32 steps;
	u32 step_size;
};

/**
 * struct xpsgtr_phy - representation of a lane
 * @phy: pointer to the kernel PHY device
 * @type: controller which uses this lane
 * @lane: lane number
 * @protocol: protocol in which the lane operates
 * @skip_phy_init: skip phy_init() if true
 * @dev: pointer to the xpsgtr_dev instance
 * @refclk: reference clock index
 */
struct xpsgtr_phy {
	struct phy *phy;
	u8 type;
	u8 lane;
	u8 protocol;
	bool skip_phy_init;
	struct xpsgtr_dev *dev;
	unsigned int refclk;
};

/**
 * struct xpsgtr_dev - representation of a ZynMP GT device
 * @dev: pointer to device
 * @serdes: serdes base address
 * @siou: siou base address
 * @gtr_mutex: mutex for locking
 * @phys: PHY lanes
 * @refclk_sscs: spread spectrum settings for the reference clocks
 * @clk: reference clocks
 * @tx_term_fix: fix for GT issue
 * @saved_icm_cfg0: stored value of ICM CFG0 register
 * @saved_icm_cfg1: stored value of ICM CFG1 register
 */
struct xpsgtr_dev {
	struct device *dev;
	void __iomem *serdes;
	void __iomem *siou;
	struct mutex gtr_mutex; /* mutex for locking */
	struct xpsgtr_phy phys[NUM_LANES];
	const struct xpsgtr_ssc *refclk_sscs[NUM_LANES];
	struct clk *clk[NUM_LANES];
	bool tx_term_fix;
	unsigned int saved_icm_cfg0;
	unsigned int saved_icm_cfg1;
};

/*
 * Configuration Data
 */

/* lookup table to hold all settings needed for a ref clock frequency */
static const struct xpsgtr_ssc ssc_lookup[] = {
	{  19200000, 0x05,  608, 264020 },
	{  20000000, 0x06,  634, 243454 },
	{  24000000, 0x07,  760, 168973 },
	{  26000000, 0x08,  824, 143860 },
	{  27000000, 0x09,  856,  86551 },
	{  38400000, 0x0a, 1218,  65896 },
	{  40000000, 0x0b,  634, 243454 },
	{  52000000, 0x0c,  824, 143860 },
	{ 100000000, 0x0d, 1058,  87533 },
	{ 108000000, 0x0e,  856,  86551 },
	{ 125000000, 0x0f,  992, 119497 },
	{ 135000000, 0x10, 1070,  55393 },
	{ 150000000, 0x11,  792, 187091 }
};

/*
 * I/O Accessors
 */

static inline u32 xpsgtr_read(struct xpsgtr_dev *gtr_dev, u32 reg)
{
	return readl(gtr_dev->serdes + reg);
}

static inline void xpsgtr_write(struct xpsgtr_dev *gtr_dev, u32 reg, u32 value)
{
	writel(value, gtr_dev->serdes + reg);
}

static inline void xpsgtr_clr_set(struct xpsgtr_dev *gtr_dev, u32 reg,
				  u32 clr, u32 set)
{
	u32 value = xpsgtr_read(gtr_dev, reg);

	value &= ~clr;
	value |= set;
	xpsgtr_write(gtr_dev, reg, value);
}

static inline u32 xpsgtr_read_phy(struct xpsgtr_phy *gtr_phy, u32 reg)
{
	void __iomem *addr = gtr_phy->dev->serdes
			   + gtr_phy->lane * PHY_REG_OFFSET + reg;

	return readl(addr);
}

static inline void xpsgtr_write_phy(struct xpsgtr_phy *gtr_phy,
				    u32 reg, u32 value)
{
	void __iomem *addr = gtr_phy->dev->serdes
			   + gtr_phy->lane * PHY_REG_OFFSET + reg;

	writel(value, addr);
}

static inline void xpsgtr_clr_set_phy(struct xpsgtr_phy *gtr_phy,
				      u32 reg, u32 clr, u32 set)
{
	void __iomem *addr = gtr_phy->dev->serdes
			   + gtr_phy->lane * PHY_REG_OFFSET + reg;

	writel((readl(addr) & ~clr) | set, addr);
}

/*
 * Hardware Configuration
 */

/* Wait for the PLL to lock (with a timeout). */
static int xpsgtr_wait_pll_lock(struct phy *phy)
{
	struct xpsgtr_phy *gtr_phy = phy_get_drvdata(phy);
	struct xpsgtr_dev *gtr_dev = gtr_phy->dev;
	unsigned int timeout = TIMEOUT_US;
	int ret;

	dev_dbg(gtr_dev->dev, "Waiting for PLL lock\n");

	while (1) {
		u32 reg = xpsgtr_read_phy(gtr_phy, L0_PLL_STATUS_READ_1);

		if ((reg & PLL_STATUS_LOCKED) == PLL_STATUS_LOCKED) {
			ret = 0;
			break;
		}

		if (--timeout == 0) {
			ret = -ETIMEDOUT;
			break;
		}

		udelay(1);
	}

	if (ret == -ETIMEDOUT)
		dev_err(gtr_dev->dev,
			"lane %u (type %u, protocol %u): PLL lock timeout\n",
			gtr_phy->lane, gtr_phy->type, gtr_phy->protocol);

	return ret;
}

/* Configure PLL and spread-sprectrum clock. */
static void xpsgtr_configure_pll(struct xpsgtr_phy *gtr_phy)
{
	const struct xpsgtr_ssc *ssc;
	u32 step_size;

	ssc = gtr_phy->dev->refclk_sscs[gtr_phy->refclk];
	step_size = ssc->step_size;

	xpsgtr_clr_set(gtr_phy->dev, PLL_REF_SEL(gtr_phy->lane),
		       PLL_FREQ_MASK, ssc->pll_ref_clk);

	/* Enable lane clock sharing, if required */
	if (gtr_phy->refclk != gtr_phy->lane) {
		/* Lane3 Ref Clock Selection Register */
		xpsgtr_clr_set(gtr_phy->dev, L0_Ln_REF_CLK_SEL(gtr_phy->lane),
			       L0_REF_CLK_SEL_MASK, 1 << gtr_phy->refclk);
	}

	/* SSC step size [7:0] */
	xpsgtr_clr_set_phy(gtr_phy, L0_PLL_SS_STEP_SIZE_0_LSB,
			   STEP_SIZE_0_MASK, step_size & STEP_SIZE_0_MASK);

	/* SSC step size [15:8] */
	step_size >>= STEP_SIZE_SHIFT;
	xpsgtr_clr_set_phy(gtr_phy, L0_PLL_SS_STEP_SIZE_1,
			   STEP_SIZE_1_MASK, step_size & STEP_SIZE_1_MASK);

	/* SSC step size [23:16] */
	step_size >>= STEP_SIZE_SHIFT;
	xpsgtr_clr_set_phy(gtr_phy, L0_PLL_SS_STEP_SIZE_2,
			   STEP_SIZE_2_MASK, step_size & STEP_SIZE_2_MASK);

	/* SSC steps [7:0] */
	xpsgtr_clr_set_phy(gtr_phy, L0_PLL_SS_STEPS_0_LSB,
			   STEPS_0_MASK, ssc->steps & STEPS_0_MASK);

	/* SSC steps [10:8] */
	xpsgtr_clr_set_phy(gtr_phy, L0_PLL_SS_STEPS_1_MSB,
			   STEPS_1_MASK,
			   (ssc->steps >> STEP_SIZE_SHIFT) & STEPS_1_MASK);

	/* SSC step size [24:25] */
	step_size >>= STEP_SIZE_SHIFT;
	xpsgtr_clr_set_phy(gtr_phy, L0_PLL_SS_STEP_SIZE_3_MSB,
			   STEP_SIZE_3_MASK, (step_size & STEP_SIZE_3_MASK) |
			   FORCE_STEP_SIZE | FORCE_STEPS);
}

/* Configure the lane protocol. */
static void xpsgtr_lane_set_protocol(struct xpsgtr_phy *gtr_phy)
{
	struct xpsgtr_dev *gtr_dev = gtr_phy->dev;
	u8 protocol = gtr_phy->protocol;

	switch (gtr_phy->lane) {
	case 0:
		xpsgtr_clr_set(gtr_dev, ICM_CFG0, ICM_CFG0_L0_MASK, protocol);
		break;
	case 1:
		xpsgtr_clr_set(gtr_dev, ICM_CFG0, ICM_CFG0_L1_MASK,
			       protocol << ICM_CFG_SHIFT);
		break;
	case 2:
		xpsgtr_clr_set(gtr_dev, ICM_CFG1, ICM_CFG0_L0_MASK, protocol);
		break;
	case 3:
		xpsgtr_clr_set(gtr_dev, ICM_CFG1, ICM_CFG0_L1_MASK,
			       protocol << ICM_CFG_SHIFT);
		break;
	default:
		/* We already checked 0 <= lane <= 3 */
		break;
	}
}

/* Bypass (de)scrambler and 8b/10b decoder and encoder. */
static void xpsgtr_bypass_scrambler_8b10b(struct xpsgtr_phy *gtr_phy)
{
	xpsgtr_write_phy(gtr_phy, L0_TM_DIG_6, L0_TM_DIS_DESCRAMBLE_DECODER);
	xpsgtr_write_phy(gtr_phy, L0_TX_DIG_61, L0_TM_DISABLE_SCRAMBLE_ENCODER);
}

/* DP-specific initialization. */
static void xpsgtr_phy_init_dp(struct xpsgtr_phy *gtr_phy)
{
	xpsgtr_write_phy(gtr_phy, L0_TXPMD_TM_45,
			 L0_TXPMD_TM_45_OVER_DP_MAIN |
			 L0_TXPMD_TM_45_ENABLE_DP_MAIN |
			 L0_TXPMD_TM_45_OVER_DP_POST1 |
			 L0_TXPMD_TM_45_OVER_DP_POST2 |
			 L0_TXPMD_TM_45_ENABLE_DP_POST2);
	xpsgtr_write_phy(gtr_phy, L0_TX_ANA_TM_118,
			 L0_TX_ANA_TM_118_FORCE_17_0);
}

/* SATA-specific initialization. */
static void xpsgtr_phy_init_sata(struct xpsgtr_phy *gtr_phy)
{
	struct xpsgtr_dev *gtr_dev = gtr_phy->dev;

	xpsgtr_bypass_scrambler_8b10b(gtr_phy);

	writel(gtr_phy->lane, gtr_dev->siou + SATA_CONTROL_OFFSET);
}

/* SGMII-specific initialization. */
static void xpsgtr_phy_init_sgmii(struct xpsgtr_phy *gtr_phy)
{
	struct xpsgtr_dev *gtr_dev = gtr_phy->dev;

	/* Set SGMII protocol TX and RX bus width to 10 bits. */
	xpsgtr_write(gtr_dev, TX_PROT_BUS_WIDTH,
		     PROT_BUS_WIDTH_10 << (gtr_phy->lane * PROT_BUS_WIDTH_SHIFT));
	xpsgtr_write(gtr_dev, RX_PROT_BUS_WIDTH,
		     PROT_BUS_WIDTH_10 << (gtr_phy->lane * PROT_BUS_WIDTH_SHIFT));

	xpsgtr_bypass_scrambler_8b10b(gtr_phy);
}

/* Configure TX de-emphasis and margining for DP. */
static void xpsgtr_phy_configure_dp(struct xpsgtr_phy *gtr_phy, unsigned int pre,
				    unsigned int voltage)
{
	static const u8 voltage_swing[4][4] = {
		{ 0x2a, 0x27, 0x24, 0x20 },
		{ 0x27, 0x23, 0x20, 0xff },
		{ 0x24, 0x20, 0xff, 0xff },
		{ 0xff, 0xff, 0xff, 0xff }
	};
	static const u8 pre_emphasis[4][4] = {
		{ 0x02, 0x02, 0x02, 0x02 },
		{ 0x01, 0x01, 0x01, 0xff },
		{ 0x00, 0x00, 0xff, 0xff },
		{ 0xff, 0xff, 0xff, 0xff }
	};

	xpsgtr_write_phy(gtr_phy, L0_TXPMD_TM_48, voltage_swing[pre][voltage]);
	xpsgtr_write_phy(gtr_phy, L0_TX_ANA_TM_18, pre_emphasis[pre][voltage]);
}

/*
 * PHY Operations
 */

static bool xpsgtr_phy_init_required(struct xpsgtr_phy *gtr_phy)
{
	/*
	 * As USB may save the snapshot of the states during hibernation, doing
	 * phy_init() will put the USB controller into reset, resulting in the
	 * losing of the saved snapshot. So try to avoid phy_init() for USB
	 * except when gtr_phy->skip_phy_init is false (this happens when FPD is
	 * shutdown during suspend or when gt lane is changed from current one)
	 */
	if (gtr_phy->protocol == ICM_PROTOCOL_USB && gtr_phy->skip_phy_init)
		return false;
	else
		return true;
}

/*
 * There is a functional issue in the GT. The TX termination resistance can be
 * out of spec due to a issue in the calibration logic. This is the workaround
 * to fix it, required for XCZU9EG silicon.
 */
static int xpsgtr_phy_tx_term_fix(struct xpsgtr_phy *gtr_phy)
{
	struct xpsgtr_dev *gtr_dev = gtr_phy->dev;
	u32 timeout = TIMEOUT_US;
	u32 nsw;

	/* Enabling Test Mode control for CMN Rest */
	xpsgtr_clr_set(gtr_dev, TM_CMN_RST, TM_CMN_RST_MASK, TM_CMN_RST_SET);

	/* Set Test Mode reset */
	xpsgtr_clr_set(gtr_dev, TM_CMN_RST, TM_CMN_RST_MASK, TM_CMN_RST_EN);

	xpsgtr_write(gtr_dev, L3_TM_CALIB_DIG18, 0x00);
	xpsgtr_write(gtr_dev, L3_TM_CALIB_DIG19, L3_TM_OVERRIDE_NSW_CODE);

	/*
	 * As a part of work around sequence for PMOS calibration fix,
	 * we need to configure any lane ICM_CFG to valid protocol. This
	 * will deassert the CMN_Resetn signal.
	 */
	xpsgtr_lane_set_protocol(gtr_phy);

	/* Clear Test Mode reset */
	xpsgtr_clr_set(gtr_dev, TM_CMN_RST, TM_CMN_RST_MASK, TM_CMN_RST_SET);

	dev_dbg(gtr_dev->dev, "calibrating...\n");

	do {
		u32 reg = xpsgtr_read(gtr_dev, L3_CALIB_DONE_STATUS);

		if ((reg & L3_CALIB_DONE) == L3_CALIB_DONE)
			break;

		if (!--timeout) {
			dev_err(gtr_dev->dev, "calibration time out\n");
			return -ETIMEDOUT;
		}

		udelay(1);
	} while (timeout > 0);

	dev_dbg(gtr_dev->dev, "calibration done\n");

	/* Reading NMOS Register Code */
	nsw = xpsgtr_read(gtr_dev, L0_TXPMA_ST_3) & L0_DN_CALIB_CODE;

	/* Set Test Mode reset */
	xpsgtr_clr_set(gtr_dev, TM_CMN_RST, TM_CMN_RST_MASK, TM_CMN_RST_EN);

	/* Writing NMOS register values back [5:3] */
	xpsgtr_write(gtr_dev, L3_TM_CALIB_DIG19, nsw >> L3_NSW_CALIB_SHIFT);

	/* Writing NMOS register value [2:0] */
	xpsgtr_write(gtr_dev, L3_TM_CALIB_DIG18,
		     ((nsw & L3_TM_CALIB_DIG19_NSW) << L3_NSW_SHIFT) |
		     (1 << L3_NSW_PIPE_SHIFT));

	/* Clear Test Mode reset */
	xpsgtr_clr_set(gtr_dev, TM_CMN_RST, TM_CMN_RST_MASK, TM_CMN_RST_SET);

	return 0;
}

static int xpsgtr_phy_init(struct phy *phy)
{
	struct xpsgtr_phy *gtr_phy = phy_get_drvdata(phy);
	struct xpsgtr_dev *gtr_dev = gtr_phy->dev;
	int ret = 0;

	mutex_lock(&gtr_dev->gtr_mutex);

	/* Skip initialization if not required. */
	if (!xpsgtr_phy_init_required(gtr_phy))
		goto out;

	if (gtr_dev->tx_term_fix) {
		ret = xpsgtr_phy_tx_term_fix(gtr_phy);
		if (ret < 0)
			goto out;

		gtr_dev->tx_term_fix = false;
	}

	/* Enable coarse code saturation limiting logic. */
	xpsgtr_write_phy(gtr_phy, L0_TM_PLL_DIG_37, L0_TM_COARSE_CODE_LIMIT);

	/*
	 * Configure the PLL, the lane protocol, and perform protocol-specific
	 * initialization.
	 */
	xpsgtr_configure_pll(gtr_phy);
	xpsgtr_lane_set_protocol(gtr_phy);

	switch (gtr_phy->protocol) {
	case ICM_PROTOCOL_DP:
		xpsgtr_phy_init_dp(gtr_phy);
		break;

	case ICM_PROTOCOL_SATA:
		xpsgtr_phy_init_sata(gtr_phy);
		break;

	case ICM_PROTOCOL_SGMII:
		xpsgtr_phy_init_sgmii(gtr_phy);
		break;
	}

out:
	mutex_unlock(&gtr_dev->gtr_mutex);
	return ret;
}

static int xpsgtr_phy_exit(struct phy *phy)
{
	struct xpsgtr_phy *gtr_phy = phy_get_drvdata(phy);

	gtr_phy->skip_phy_init = false;

	return 0;
}

static int xpsgtr_phy_power_on(struct phy *phy)
{
	struct xpsgtr_phy *gtr_phy = phy_get_drvdata(phy);
	int ret = 0;

	/* Skip initialization if not required. */
	if (!xpsgtr_phy_init_required(gtr_phy))
		return ret;
	/*
	 * Wait for the PLL to lock. For DP, only wait on DP0 to avoid
	 * cumulating waits for both lanes. The user is expected to initialize
	 * lane 0 last.
	 */
	if (gtr_phy->protocol != ICM_PROTOCOL_DP ||
	    gtr_phy->type == XPSGTR_TYPE_DP_0)
		ret = xpsgtr_wait_pll_lock(phy);

	return ret;
}

static int xpsgtr_phy_configure(struct phy *phy, union phy_configure_opts *opts)
{
	struct xpsgtr_phy *gtr_phy = phy_get_drvdata(phy);

	if (gtr_phy->protocol != ICM_PROTOCOL_DP)
		return 0;

	xpsgtr_phy_configure_dp(gtr_phy, opts->dp.pre[0], opts->dp.voltage[0]);

	return 0;
}

static const struct phy_ops xpsgtr_phyops = {
	.init		= xpsgtr_phy_init,
	.exit		= xpsgtr_phy_exit,
	.power_on	= xpsgtr_phy_power_on,
	.configure	= xpsgtr_phy_configure,
	.owner		= THIS_MODULE,
};

/*
 * OF Xlate Support
 */

/* Set the lane type and protocol based on the PHY type and instance number. */
static int xpsgtr_set_lane_type(struct xpsgtr_phy *gtr_phy, u8 phy_type,
				unsigned int phy_instance)
{
	unsigned int num_phy_types;
	const int *phy_types;

	switch (phy_type) {
	case PHY_TYPE_SATA: {
		static const int types[] = {
			XPSGTR_TYPE_SATA_0,
			XPSGTR_TYPE_SATA_1,
		};

		phy_types = types;
		num_phy_types = ARRAY_SIZE(types);
		gtr_phy->protocol = ICM_PROTOCOL_SATA;
		break;
	}
	case PHY_TYPE_USB3: {
		static const int types[] = {
			XPSGTR_TYPE_USB0,
			XPSGTR_TYPE_USB1,
		};

		phy_types = types;
		num_phy_types = ARRAY_SIZE(types);
		gtr_phy->protocol = ICM_PROTOCOL_USB;
		break;
	}
	case PHY_TYPE_DP: {
		static const int types[] = {
			XPSGTR_TYPE_DP_0,
			XPSGTR_TYPE_DP_1,
		};

		phy_types = types;
		num_phy_types = ARRAY_SIZE(types);
		gtr_phy->protocol = ICM_PROTOCOL_DP;
		break;
	}
	case PHY_TYPE_PCIE: {
		static const int types[] = {
			XPSGTR_TYPE_PCIE_0,
			XPSGTR_TYPE_PCIE_1,
			XPSGTR_TYPE_PCIE_2,
			XPSGTR_TYPE_PCIE_3,
		};

		phy_types = types;
		num_phy_types = ARRAY_SIZE(types);
		gtr_phy->protocol = ICM_PROTOCOL_PCIE;
		break;
	}
	case PHY_TYPE_SGMII: {
		static const int types[] = {
			XPSGTR_TYPE_SGMII0,
			XPSGTR_TYPE_SGMII1,
			XPSGTR_TYPE_SGMII2,
			XPSGTR_TYPE_SGMII3,
		};

		phy_types = types;
		num_phy_types = ARRAY_SIZE(types);
		gtr_phy->protocol = ICM_PROTOCOL_SGMII;
		break;
	}
	default:
		return -EINVAL;
	}

	if (phy_instance >= num_phy_types)
		return -EINVAL;

	gtr_phy->type = phy_types[phy_instance];
	return 0;
}

/*
 * Valid combinations of controllers and lanes (Interconnect Matrix).
 */
static const unsigned int icm_matrix[NUM_LANES][CONTROLLERS_PER_LANE] = {
	{ XPSGTR_TYPE_PCIE_0, XPSGTR_TYPE_SATA_0, XPSGTR_TYPE_USB0,
		XPSGTR_TYPE_DP_1, XPSGTR_TYPE_SGMII0 },
	{ XPSGTR_TYPE_PCIE_1, XPSGTR_TYPE_SATA_1, XPSGTR_TYPE_USB0,
		XPSGTR_TYPE_DP_0, XPSGTR_TYPE_SGMII1 },
	{ XPSGTR_TYPE_PCIE_2, XPSGTR_TYPE_SATA_0, XPSGTR_TYPE_USB0,
		XPSGTR_TYPE_DP_1, XPSGTR_TYPE_SGMII2 },
	{ XPSGTR_TYPE_PCIE_3, XPSGTR_TYPE_SATA_1, XPSGTR_TYPE_USB1,
		XPSGTR_TYPE_DP_0, XPSGTR_TYPE_SGMII3 }
};

/* Translate OF phandle and args to PHY instance. */
static struct phy *xpsgtr_xlate(struct device *dev,
				struct of_phandle_args *args)
{
	struct xpsgtr_dev *gtr_dev = dev_get_drvdata(dev);
	struct xpsgtr_phy *gtr_phy;
	unsigned int phy_instance;
	unsigned int phy_lane;
	unsigned int phy_type;
	unsigned int refclk;
	unsigned int i;
	int ret;

	if (args->args_count != 4) {
		dev_err(dev, "Invalid number of cells in 'phy' property\n");
		return ERR_PTR(-EINVAL);
	}

	/*
	 * Get the PHY parameters from the OF arguments and derive the lane
	 * type.
	 */
	phy_lane = args->args[0];
	if (phy_lane >= ARRAY_SIZE(gtr_dev->phys)) {
		dev_err(dev, "Invalid lane number %u\n", phy_lane);
		return ERR_PTR(-ENODEV);
	}

	gtr_phy = &gtr_dev->phys[phy_lane];
	phy_type = args->args[1];
	phy_instance = args->args[2];

	ret = xpsgtr_set_lane_type(gtr_phy, phy_type, phy_instance);
	if (ret < 0) {
		dev_err(gtr_dev->dev, "Invalid PHY type and/or instance\n");
		return ERR_PTR(ret);
	}

	refclk = args->args[3];
	if (refclk >= ARRAY_SIZE(gtr_dev->refclk_sscs) ||
	    !gtr_dev->refclk_sscs[refclk]) {
		dev_err(dev, "Invalid reference clock number %u\n", refclk);
		return ERR_PTR(-EINVAL);
	}

	gtr_phy->refclk = refclk;

	/*
	 * Ensure that the Interconnect Matrix is obeyed, i.e a given lane type
	 * is allowed to operate on the lane.
	 */
	for (i = 0; i < CONTROLLERS_PER_LANE; i++) {
		if (icm_matrix[phy_lane][i] == gtr_phy->type)
			return gtr_phy->phy;
	}

	return ERR_PTR(-EINVAL);
}

/*
 * Power Management
 */

static int __maybe_unused xpsgtr_suspend(struct device *dev)
{
	struct xpsgtr_dev *gtr_dev = dev_get_drvdata(dev);
	unsigned int i;

	/* Save the snapshot ICM_CFG registers. */
	gtr_dev->saved_icm_cfg0 = xpsgtr_read(gtr_dev, ICM_CFG0);
	gtr_dev->saved_icm_cfg1 = xpsgtr_read(gtr_dev, ICM_CFG1);

	for (i = 0; i < ARRAY_SIZE(gtr_dev->clk); i++)
		clk_disable_unprepare(gtr_dev->clk[i]);

	return 0;
}

static int __maybe_unused xpsgtr_resume(struct device *dev)
{
	struct xpsgtr_dev *gtr_dev = dev_get_drvdata(dev);
	unsigned int icm_cfg0, icm_cfg1;
	unsigned int i;
	bool skip_phy_init;
	int err;

	for (i = 0; i < ARRAY_SIZE(gtr_dev->clk); i++) {
		err = clk_prepare_enable(gtr_dev->clk[i]);
		if (err)
			goto err_clk_put;
	}

	icm_cfg0 = xpsgtr_read(gtr_dev, ICM_CFG0);
	icm_cfg1 = xpsgtr_read(gtr_dev, ICM_CFG1);

	/* Return if no GT lanes got configured before suspend. */
	if (!gtr_dev->saved_icm_cfg0 && !gtr_dev->saved_icm_cfg1)
		return 0;

	/* Check if the ICM configurations changed after suspend. */
	if (icm_cfg0 == gtr_dev->saved_icm_cfg0 &&
	    icm_cfg1 == gtr_dev->saved_icm_cfg1)
		skip_phy_init = true;
	else
		skip_phy_init = false;

	/* Update the skip_phy_init for all gtr_phy instances. */
	for (i = 0; i < ARRAY_SIZE(gtr_dev->phys); i++)
		gtr_dev->phys[i].skip_phy_init = skip_phy_init;

	return 0;

err_clk_put:
	while (i--)
		clk_disable_unprepare(gtr_dev->clk[i]);

	return err;
}

static const struct dev_pm_ops xpsgtr_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(xpsgtr_suspend, xpsgtr_resume)
};

/*
 * Probe & Platform Driver
 */

static int xpsgtr_get_ref_clocks(struct xpsgtr_dev *gtr_dev)
{
	unsigned int refclk;
	int ret;

	for (refclk = 0; refclk < ARRAY_SIZE(gtr_dev->refclk_sscs); ++refclk) {
		unsigned long rate;
		unsigned int i;
		struct clk *clk;
		char name[8];

		snprintf(name, sizeof(name), "ref%u", refclk);
		clk = devm_clk_get_optional(gtr_dev->dev, name);
		if (IS_ERR(clk)) {
			ret = dev_err_probe(gtr_dev->dev, PTR_ERR(clk),
					    "Failed to get reference clock %u\n",
					    refclk);
			goto err_clk_put;
		}

		if (!clk)
			continue;

		ret = clk_prepare_enable(clk);
		if (ret)
			goto err_clk_put;

		gtr_dev->clk[refclk] = clk;

		/*
		 * Get the spread spectrum (SSC) settings for the reference
		 * clock rate.
		 */
		rate = clk_get_rate(clk);

		for (i = 0 ; i < ARRAY_SIZE(ssc_lookup); i++) {
			if (rate == ssc_lookup[i].refclk_rate) {
				gtr_dev->refclk_sscs[refclk] = &ssc_lookup[i];
				break;
			}
		}

		if (i == ARRAY_SIZE(ssc_lookup)) {
			dev_err(gtr_dev->dev,
				"Invalid rate %lu for reference clock %u\n",
				rate, refclk);
			ret = -EINVAL;
			goto err_clk_put;
		}
	}

	return 0;

err_clk_put:
	while (refclk--)
		clk_disable_unprepare(gtr_dev->clk[refclk]);

	return ret;
}

static int xpsgtr_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct xpsgtr_dev *gtr_dev;
	struct phy_provider *provider;
	unsigned int port;
	unsigned int i;
	int ret;

	gtr_dev = devm_kzalloc(&pdev->dev, sizeof(*gtr_dev), GFP_KERNEL);
	if (!gtr_dev)
		return -ENOMEM;

	gtr_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, gtr_dev);

	mutex_init(&gtr_dev->gtr_mutex);

	if (of_device_is_compatible(np, "xlnx,zynqmp-psgtr"))
		gtr_dev->tx_term_fix =
			of_property_read_bool(np, "xlnx,tx-termination-fix");

	/* Acquire resources. */
	gtr_dev->serdes = devm_platform_ioremap_resource_byname(pdev, "serdes");
	if (IS_ERR(gtr_dev->serdes))
		return PTR_ERR(gtr_dev->serdes);

	gtr_dev->siou = devm_platform_ioremap_resource_byname(pdev, "siou");
	if (IS_ERR(gtr_dev->siou))
		return PTR_ERR(gtr_dev->siou);

	ret = xpsgtr_get_ref_clocks(gtr_dev);
	if (ret)
		return ret;

	/* Create PHYs. */
	for (port = 0; port < ARRAY_SIZE(gtr_dev->phys); ++port) {
		struct xpsgtr_phy *gtr_phy = &gtr_dev->phys[port];
		struct phy *phy;

		gtr_phy->lane = port;
		gtr_phy->dev = gtr_dev;

		phy = devm_phy_create(&pdev->dev, np, &xpsgtr_phyops);
		if (IS_ERR(phy)) {
			dev_err(&pdev->dev, "failed to create PHY\n");
			ret = PTR_ERR(phy);
			goto err_clk_put;
		}

		gtr_phy->phy = phy;
		phy_set_drvdata(phy, gtr_phy);
	}

	/* Register the PHY provider. */
	provider = devm_of_phy_provider_register(&pdev->dev, xpsgtr_xlate);
	if (IS_ERR(provider)) {
		dev_err(&pdev->dev, "registering provider failed\n");
		ret = PTR_ERR(provider);
		goto err_clk_put;
	}
	return 0;

err_clk_put:
	for (i = 0; i < ARRAY_SIZE(gtr_dev->clk); i++)
		clk_disable_unprepare(gtr_dev->clk[i]);

	return ret;
}

static const struct of_device_id xpsgtr_of_match[] = {
	{ .compatible = "xlnx,zynqmp-psgtr", },
	{ .compatible = "xlnx,zynqmp-psgtr-v1.1", },
	{},
};
MODULE_DEVICE_TABLE(of, xpsgtr_of_match);

static struct platform_driver xpsgtr_driver = {
	.probe = xpsgtr_probe,
	.driver = {
		.name = "xilinx-psgtr",
		.of_match_table	= xpsgtr_of_match,
		.pm =  &xpsgtr_pm_ops,
	},
};

module_platform_driver(xpsgtr_driver);

MODULE_AUTHOR("Xilinx Inc.");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Xilinx ZynqMP High speed Gigabit Transceiver");
