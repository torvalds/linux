// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek DisplayPort PHY driver
 *
 * Copyright (c) 2022, BayLibre Inc.
 * Copyright (c) 2022, MediaTek Inc.
 *
 * Major refactoring and new SoCs support
 * Copyright (c) 2026, Collabora Ltd.
 *                     AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#define MTK_DP_PHY_MAX_LANES		4

/* DP_PHYA_GLB_BIAS_GEN_0 (PHYA - Analog) */
#define XTP_GLB_BIAS_INT_R_CTRL		GENMASK(20, 16)

/* DP_PHYA_GLB_FORCE_CTRL_1 */
#define CKM_CKTX0_EN_FORCE_MODE		BIT(10)

/* DP_PHYA_GLB_DPAUX_TX */
#define CKM_PT0_CKTX_IMPSEL		GENMASK(23, 20)

/* DP_PHYA_LAN_LANE_TX_0 */
#define XTP_LN_TX_IMPSEL_PMOS		GENMASK(15, 12)
#define XTP_LN_TX_IMPSEL_NMOS		GENMASK(19, 16)

/* DP_PHYA_GLB_FORCE_CTRL_1 */
#define CKM_CKTX0_EN_FORCE_MODE		BIT(10)

/* DP_PHYD_PLL_CTL_1 */
#define TPLL_SSC_EN			BIT(3)

/* DP_PHYD_BIT_RATE */
#define PHYD_DIG_RG_BIT_RATE_V2		GENMASK(3, 0)
#define PHYD_DIG_RG_BIT_RATE		GENMASK(1, 0)

/* DP_PHYD_SW_RST */
#define PHYD_DIG_GLB_SW_RST_B		GENMASK(7, 0)
#  define DP_GLB_SW_RST_PHYD		BIT(0)
#  define DP_GLB_SW_RST_TFIFO_ANA	BIT(1)
#  define DP_GLB_SW_RST_XTAL_CLK	BIT(2)
#  define DP_GLB_SW_RST_MAIN_LINK	BIT(3)

/* DP_PHYD_AUX_RX_CTL */
#define PHYD_DIG_DPAUX_RX_EN		BIT(0)
#define PHYD_DIG_XTP_GLB_CKDET_EN	BIT(1)
#define PHYD_DIG_DPAUX_RX_DEGLITCH_EN	BIT(2)

#define DRIVING_PARAM_0_DEFAULT	0x0
#define DRIVING_PARAM_1_DEFAULT	0x0
#define DRIVING_PARAM_2_DEFAULT	0x0

/* DP_PHYD_TX_CTL_0 */
#define PHYD_TX_LN_EN			GENMASK(7, 4)
#define PHYD_TX_LN_EN_V2		GENMASK(3, 0)

/* DP_PHYD_DRIVING_FORCE */
#define PHYD_DP_TX_FORCE_VOLT_SWING_EN	BIT(0)
#define PHYD_DP_TX_FORCE_VOLT_SWING_VAL	GENMASK(2, 1)
#define PHYD_DP_TX_FORCE_PRE_EMPH_VAL	GENMASK(4, 3)

/*
 * DRIVING_PARAM_X (PHYD - Digital)
 *
 * Driving param registers are split in three sets, all containing settings
 * for Voltage Swing and Pre-Emphasis for each lane's differential pair.
 *
 * All three sets share the same layout, but for different physical signals;
 * In particular:
 * [0-2]: LC TX CM (Minus / Negative Edge)
 * [3-5]: LC TX C  (Logic State Change Point)
 * [6-8]: LC TX CP (Plus / Positive Edge)
 *
 * And they contain values for:
 * [0,3,6]: Swing 0 Pre[0-3]
 * [1,4,7]: Swing 1 Pre[0-2] and Swing 2 Pre0
 * [2,5,8]: Swing 2 Pre1 and Swing 3 Pre0
 */
#define PHYD_DIG_NUM_DRV_PARA_REGS	9
#define XTP_LN_TX_LCTXC_SW0_PRE0	GENMASK(5, 0)
#define XTP_LN_TX_LCTXC_SW0_PRE1	GENMASK(13, 8)
#define XTP_LN_TX_LCTXC_SW0_PRE2	GENMASK(21, 16)
#define XTP_LN_TX_LCTXC_SW0_PRE3	GENMASK(29, 24)

#define XTP_LN_TX_LCTXC_SW1_PRE0	GENMASK(5, 0)
#define XTP_LN_TX_LCTXC_SW1_PRE1	GENMASK(13, 8)
#define XTP_LN_TX_LCTXC_SW1_PRE2	GENMASK(21, 16)
#define XTP_LN_TX_LCTXC_SW2_PRE0	GENMASK(29, 24)

#define XTP_LN_TX_LCTXC_SW2_PRE1	GENMASK(5, 0)
#define XTP_LN_TX_LCTXC_SW3_PRE0	GENMASK(13, 8)

#define BUILD_DRIVING_PARAM_0(sw0_pre0, sw0_pre1, sw0_pre2, sw0_pre3) (	\
	FIELD_PREP_CONST(XTP_LN_TX_LCTXC_SW0_PRE0, sw0_pre0) |		\
	FIELD_PREP_CONST(XTP_LN_TX_LCTXC_SW0_PRE1, sw0_pre1) |		\
	FIELD_PREP_CONST(XTP_LN_TX_LCTXC_SW0_PRE2, sw0_pre2) |		\
	FIELD_PREP_CONST(XTP_LN_TX_LCTXC_SW0_PRE3, sw0_pre3)		\
)

#define BUILD_DRIVING_PARAM_12(sw1_pre0, sw1_pre1, sw1_pre2, sw2_pre0) (\
	FIELD_PREP_CONST(XTP_LN_TX_LCTXC_SW1_PRE0, sw1_pre0) |		\
	FIELD_PREP_CONST(XTP_LN_TX_LCTXC_SW1_PRE1, sw1_pre1) |		\
	FIELD_PREP_CONST(XTP_LN_TX_LCTXC_SW1_PRE2, sw1_pre2) |		\
	FIELD_PREP_CONST(XTP_LN_TX_LCTXC_SW2_PRE0, sw2_pre0)		\
)

#define BUILD_DRIVING_PARAM_23(sw2_pre1, sw3_pre0) (			\
	FIELD_PREP_CONST(XTP_LN_TX_LCTXC_SW2_PRE1, sw2_pre1) |		\
	FIELD_PREP_CONST(XTP_LN_TX_LCTXC_SW3_PRE0, sw3_pre0)		\
)

/* MT8195: Logic State Change Point (LC TX C) */
#define MT8195_DRIVING_PARAM_3_DEFAULT	BUILD_DRIVING_PARAM_0( 16, 20, 24, 32)
#define MT8195_DRIVING_PARAM_4_DEFAULT	BUILD_DRIVING_PARAM_12(24, 30, 36, 32)
#define MT8195_DRIVING_PARAM_5_DEFAULT	BUILD_DRIVING_PARAM_23(40, 48)

/* MT8195: Positive Edge (LC TX CP) */
#define MT8195_DRIVING_PARAM_6_DEFAULT	BUILD_DRIVING_PARAM_0( 0, 4, 8, 16)
#define MT8195_DRIVING_PARAM_7_DEFAULT	BUILD_DRIVING_PARAM_12(0, 6, 12, 0)
#define MT8195_DRIVING_PARAM_8_DEFAULT	BUILD_DRIVING_PARAM_23(8, 0)

/* MT8196/MT6991: Logic State Change Point (LC TX C) */
#define MT8196_DRIVING_PARAM_3_DEFAULT	BUILD_DRIVING_PARAM_0( 10, 12, 14, 17)
#define MT8196_DRIVING_PARAM_4_DEFAULT	BUILD_DRIVING_PARAM_12(14, 17, 18, 18)
#define MT8196_DRIVING_PARAM_5_DEFAULT	BUILD_DRIVING_PARAM_23(21, 24)

/* MT8196/MT6991: Positive Edge (LC TX CP) */
#define MT8196_DRIVING_PARAM_6_DEFAULT	BUILD_DRIVING_PARAM_0( 0, 2, 4, 7)
#define MT8196_DRIVING_PARAM_7_DEFAULT	BUILD_DRIVING_PARAM_12(0, 3, 6, 0)
#define MT8196_DRIVING_PARAM_8_DEFAULT	BUILD_DRIVING_PARAM_23(3, 0)

enum mtk_dp_phya_ana_glb_regidx {
	DP_PHYA_GLB_BIAS_GEN_0,
	DP_PHYA_GLB_BIAS_GEN_1,
	DP_PHYA_GLB_DPAUX_TX,
	DP_PHYA_GLB_FORCE_CTRL_0,
	DP_PHYA_GLB_FORCE_CTRL_1,
	DP_PHYA_GLOBAL_MAX
};

enum mtk_dp_phya_ana_lane_regidx {
	DP_PHYA_LAN_LANE_TX_0,
	DP_PHYA_LAN_MAX
};

enum mtk_dp_phyd_dig_lane_regidx {
	DP_PHYD_LAN_DRIVING_FORCE,
	DP_PHYD_LAN_DRIVING_PARAM_0,
	DP_PHYD_LAN_MAX
};

enum mtk_dp_phyd_dig_glb_regidx {
	DP_PHYD_PLL_CTL_0,
	DP_PHYD_PLL_CTL_1,
	DP_PHYD_SW_RST,
	DP_PHYD_BIT_RATE,
	DP_PHYD_AUX_RX_CTL,
	DP_PHYD_TX_CTL_0,
	DP_PHYD_GLOBAL_MAX
};

enum mtk_dp_phyd_bit_rate_regval {
	DP_PHYD_BIT_RATE_RBR,
	DP_PHYD_BIT_RATE_HBR,
	DP_PHYD_BIT_RATE_HBR2,
	DP_PHYD_BIT_RATE_HBR3,
	DP_PHYD_BIT_RATE_MAX,
};

static const u8 mt8195_phy_ana_glb_regs[DP_PHYA_GLOBAL_MAX] = {
	[DP_PHYA_GLB_BIAS_GEN_0] = 0x0,
	[DP_PHYA_GLB_BIAS_GEN_1] = 0x4,
	[DP_PHYA_GLB_DPAUX_TX] = 0x8,
	[DP_PHYA_GLB_FORCE_CTRL_0] = 0x30,
	[DP_PHYA_GLB_FORCE_CTRL_1] = 0x34,
};

static const u8 mt8195_phy_ana_lane_regs[DP_PHYA_LAN_MAX] = {
	[DP_PHYA_LAN_LANE_TX_0] = 0x4,
};

static const u8 mt8195_phy_dig_lane_regs[DP_PHYD_LAN_MAX] = {
	[DP_PHYD_LAN_DRIVING_FORCE] = 0x18,
	[DP_PHYD_LAN_DRIVING_PARAM_0] = 0x2c,
};

static const u8 mt8196_phy_dig_lane_regs[DP_PHYD_LAN_MAX] = {
	[DP_PHYD_LAN_DRIVING_FORCE] = 0x30,
	[DP_PHYD_LAN_DRIVING_PARAM_0] = 0x34,
};

static const u8 mt8195_phy_dig_glb_regs[DP_PHYD_GLOBAL_MAX] = {
	[DP_PHYD_PLL_CTL_0] = 0x10,
	[DP_PHYD_PLL_CTL_1] = 0x14,
	[DP_PHYD_SW_RST] = 0x38,
	[DP_PHYD_BIT_RATE] = 0x3c,
	[DP_PHYD_AUX_RX_CTL] = 0x40,
	[DP_PHYD_TX_CTL_0] = 0x44,
};

static const u8 mt8196_phy_dig_glb_regs[DP_PHYD_GLOBAL_MAX] = {
	[DP_PHYD_PLL_CTL_0] = 0x10,
	[DP_PHYD_PLL_CTL_1] = 0x14,
	[DP_PHYD_SW_RST] = 0x38,
	[DP_PHYD_BIT_RATE] = 0x3c,
	[DP_PHYD_AUX_RX_CTL] = 0x40,
	[DP_PHYD_TX_CTL_0] = 0x74,
};

static const u8 mt8195_phy_dig_bitrate_val[DP_PHYD_BIT_RATE_MAX] = {
	[DP_PHYD_BIT_RATE_RBR] = 0,
	[DP_PHYD_BIT_RATE_HBR] = 1,
	[DP_PHYD_BIT_RATE_HBR2] = 2,
	[DP_PHYD_BIT_RATE_HBR3] = 3
};

static const u8 mt8196_edp_phy_dig_bitrate_val[DP_PHYD_BIT_RATE_MAX] = {
	[DP_PHYD_BIT_RATE_RBR] = 1,
	[DP_PHYD_BIT_RATE_HBR] = 4,
	[DP_PHYD_BIT_RATE_HBR2] = 7,
	[DP_PHYD_BIT_RATE_HBR3] = 9
};

/**
 * struct mtk_dp_phya_imp_sel - Per-Lane Impedance Selection
 * @pmos: Impedance selection for P-Channel MOSFET
 * @nmos: Impedance selection for N-Channel MOSFET
 */
struct mtk_dp_phya_imp_sel {
	u8 pmos : 4;
	u8 nmos : 4;
};

/**
 * struct mtk_dp_phy_pdata - Platform data and defaults for MediaTek DP/eDP PHY
 * @off_ana_glb:    Base offset for dptx_phyd_sifslv_ana_glb
 * @off_ana_lane:   Base offsets for dptx_phyd_sifslv_ana_lan (for each lane)
 * @off_dig_glb:    Base offset for dptx_phyd_sifslv_dig_glb
 * @off_dig_lane:   Base offsets for dptx_phyd_sifslv_dig_lan (for each lane)
 * @regs_ana_glb:   Register (layout) offsets for ana_glb
 * @regs_ana_lane:  Register (layout) offsets for ana_lan
 * @regs_dig_glb:   Register (layout) offsets for dig_glb
 * @regs_dig_lane:  Register (layout) offsets for dig_lan
 * @mask_dig_tx_ln: Register mask for PHYD_TX_LN_EN field
 * @val_dig_bitrate:IP Version specific register values for Bit Rate setting
 * @ana_bias_r:     Internal resistance "R" Selection Settings (global)
 * @ana_cktx_imp:   TX Clock Impedance Selection Settings (global)
 * @ana_lanes_imp:  TX Impedance Selection Settings (for all lanes)
 * @driving_params: Voltage Swing and Pre-Emphasis settings (for all lanes)
 */
struct mtk_dp_phy_pdata {
	/* Register offsets */
	u16 off_ana_glb;
	u16 off_ana_lane[MTK_DP_PHY_MAX_LANES];
	u16 off_dig_glb;
	u16 off_dig_lane[MTK_DP_PHY_MAX_LANES];

	/* Register maps */
	const u8 *regs_ana_glb;
	const u8 *regs_ana_lane;
	const u8 *regs_dig_glb;
	const u8 *regs_dig_lane;

	/* Register masks */
	u32 mask_dig_tx_ln;

	/* IP-Version specific register value arrays */
	const u8 *val_dig_bitrate;

	/* Calibration defaults */
	u8 ana_bias_r;
	u8 ana_cktx_imp;
	struct mtk_dp_phya_imp_sel ana_lanes_imp;
	u32 driving_params[PHYD_DIG_NUM_DRV_PARA_REGS];
};

struct mtk_dp_phy {
	struct device *dev;
	struct regmap *regmap;
	const struct mtk_dp_phy_pdata *pdata;

	u8 ana_bias_r;
	u8 ana_cktx_imp;
	struct mtk_dp_phya_imp_sel ana_impsel[MTK_DP_PHY_MAX_LANES];
};

static void mtk_dp_phy_set_analog_calibration_params(struct mtk_dp_phy *dp_phy)
{
	const struct mtk_dp_phy_pdata *pdata = dp_phy->pdata;
	const u8 *regs_ana_glb = pdata->regs_ana_glb;
	const u8 *regs_ana_lane = pdata->regs_ana_lane;
	int i;

	regmap_update_bits(dp_phy->regmap,
			   pdata->off_ana_glb + regs_ana_glb[DP_PHYA_GLB_BIAS_GEN_0],
			   XTP_GLB_BIAS_INT_R_CTRL,
			   FIELD_PREP(XTP_GLB_BIAS_INT_R_CTRL, dp_phy->ana_bias_r));

	regmap_update_bits(dp_phy->regmap,
			   pdata->off_ana_glb + regs_ana_glb[DP_PHYA_GLB_DPAUX_TX],
			   CKM_PT0_CKTX_IMPSEL,
			   FIELD_PREP(CKM_PT0_CKTX_IMPSEL, dp_phy->ana_cktx_imp));

	for (i = 0; i < MTK_DP_PHY_MAX_LANES; i++) {
		struct mtk_dp_phya_imp_sel *ana_imp = &dp_phy->ana_impsel[i];
		u32 val = FIELD_PREP(XTP_LN_TX_IMPSEL_PMOS, ana_imp->pmos) |
			  FIELD_PREP(XTP_LN_TX_IMPSEL_NMOS, ana_imp->nmos);
		u32 off_ana_lane = pdata->off_ana_lane[i];

		regmap_update_bits(dp_phy->regmap,
				   off_ana_lane + regs_ana_lane[DP_PHYA_LAN_LANE_TX_0],
				   XTP_LN_TX_IMPSEL_PMOS | XTP_LN_TX_IMPSEL_NMOS, val);
	}
}

static void mtk_dp_phy_set_digital_drv_params(struct mtk_dp_phy *dp_phy)
{
	const struct mtk_dp_phy_pdata *pdata = dp_phy->pdata;
	const u32 reg = pdata->regs_dig_lane[DP_PHYD_LAN_DRIVING_PARAM_0];
	int i;

	/*
	 * Assume that all lanes need the same driving parameters: this
	 * will bulk write from DRIVING_PARAM_0 to DRIVING_PARAM_8 on
	 * all lanes (a grand total of [9 * num_lanes] 32-bit writes)
	 */
	for (i = 0; i < MTK_DP_PHY_MAX_LANES; i++)
		regmap_bulk_write(dp_phy->regmap, pdata->off_dig_lane[i] + reg,
				  pdata->driving_params,
				  ARRAY_SIZE(pdata->driving_params));
}

static int mtk_dp_phy_init(struct phy *phy)
{
	struct mtk_dp_phy *dp_phy = phy_get_drvdata(phy);

	mtk_dp_phy_set_digital_drv_params(dp_phy);
	mtk_dp_phy_set_analog_calibration_params(dp_phy);

	return 0;
}

static int mtk_dp_phy_configure(struct phy *phy, union phy_configure_opts *opts)
{
	struct mtk_dp_phy *dp_phy = phy_get_drvdata(phy);
	const struct mtk_dp_phy_pdata *pdata = dp_phy->pdata;
	u32 val;
	int i;

	if (opts->dp.set_rate) {
		const u32 reg_bit_rate = pdata->regs_dig_glb[DP_PHYD_BIT_RATE];
		enum mtk_dp_phyd_bit_rate_regval regval_idx;

		switch (opts->dp.link_rate) {
		default:
			dev_err(&phy->dev,
				"Implementation error, unknown linkrate %x\n",
				opts->dp.link_rate);
			return -EINVAL;
		case 1620:
			regval_idx = DP_PHYD_BIT_RATE_RBR;
			break;
		case 2700:
			regval_idx = DP_PHYD_BIT_RATE_HBR;
			break;
		case 5400:
			regval_idx = DP_PHYD_BIT_RATE_HBR2;
			break;
		case 8100:
			regval_idx = DP_PHYD_BIT_RATE_HBR3;
			break;
		}
		regmap_write(dp_phy->regmap, pdata->off_dig_glb + reg_bit_rate,
			     pdata->val_dig_bitrate[regval_idx]);
	}

	if (opts->dp.set_lanes) {
		const u32 reg_dig_tx_ctl = pdata->regs_dig_glb[DP_PHYD_TX_CTL_0];

		val = 0;
		for (i = 0; i < opts->dp.lanes; i++)
			val |= field_prep(pdata->mask_dig_tx_ln, BIT(i));

		regmap_update_bits(dp_phy->regmap, pdata->off_dig_glb + reg_dig_tx_ctl,
				   pdata->mask_dig_tx_ln, val);
	}

	if (opts->dp.set_voltages) {
		const u32 reg_drv_force = pdata->regs_dig_lane[DP_PHYD_LAN_DRIVING_FORCE];

		if (opts->dp.lanes > 4) {
			dev_err(&phy->dev, "Wrong lanes config %u\n", opts->dp.lanes);
			return -EINVAL;
		}

		for (i = 0; i < opts->dp.lanes; i++) {
			const u32 off_dig_lane = pdata->off_dig_lane[i];
			u32 val;

			val = FIELD_PREP(PHYD_DP_TX_FORCE_VOLT_SWING_VAL, opts->dp.voltage[i]);
			val |= FIELD_PREP(PHYD_DP_TX_FORCE_PRE_EMPH_VAL, opts->dp.pre[i]);
			val |= PHYD_DP_TX_FORCE_VOLT_SWING_EN;

			regmap_update_bits(dp_phy->regmap, off_dig_lane + reg_drv_force,
					   PHYD_DP_TX_FORCE_VOLT_SWING_EN |
					   PHYD_DP_TX_FORCE_VOLT_SWING_VAL |
					   PHYD_DP_TX_FORCE_PRE_EMPH_VAL,
					   val);
		}
	}

	regmap_update_bits(dp_phy->regmap,
			   pdata->off_dig_glb + pdata->regs_dig_glb[DP_PHYD_PLL_CTL_1],
			   TPLL_SSC_EN, opts->dp.ssc ? TPLL_SSC_EN : 0);

	return 0;
}

static int mtk_dp_phy_power_on(struct phy *phy)
{
	struct mtk_dp_phy *dp_phy = phy_get_drvdata(phy);
	const struct mtk_dp_phy_pdata *pdata = dp_phy->pdata;
	const u8 *regs_dig = pdata->regs_dig_glb;
	const u8 *regs_ana = pdata->regs_ana_glb;

	/* Enable AUX Channel with RX De-Glitch and input clock detection */
	regmap_write(dp_phy->regmap,
		     pdata->off_dig_glb + regs_dig[DP_PHYD_AUX_RX_CTL],
		     PHYD_DIG_DPAUX_RX_EN |
		     PHYD_DIG_XTP_GLB_CKDET_EN |
		     PHYD_DIG_DPAUX_RX_DEGLITCH_EN);

	regmap_clear_bits(dp_phy->regmap,
			  pdata->off_ana_glb + regs_ana[DP_PHYA_GLB_FORCE_CTRL_1],
			  CKM_CKTX0_EN_FORCE_MODE);

	return 0;
}

static int mtk_dp_phy_disable_all_lanes(struct mtk_dp_phy *dp_phy)
{
	const struct mtk_dp_phy_pdata *pdata = dp_phy->pdata;
	const u8 *regs = pdata->regs_dig_glb;
	u32 val;

	/* Get mask of currently enabled lane */
	regmap_read(dp_phy->regmap, pdata->off_dig_glb + regs[DP_PHYD_TX_CTL_0], &val);
	val = field_get(pdata->mask_dig_tx_ln, val);
	if (val == 0)
		return 0;

	/* Disable all lanes (needs to be done one by one, from last to first) */
	do {
		u32 lane_num = fls(val) - 1;
		val &= ~BIT(lane_num);

		regmap_clear_bits(dp_phy->regmap,
				  pdata->off_dig_glb + regs[DP_PHYD_TX_CTL_0],
				  field_prep(pdata->mask_dig_tx_ln, BIT(lane_num)));
	} while (val);

	return 0;
}

static int mtk_dp_phy_power_off(struct phy *phy)
{
	struct mtk_dp_phy *dp_phy = phy_get_drvdata(phy);
	const struct mtk_dp_phy_pdata *pdata = dp_phy->pdata;
	const u8 *regs_dig = pdata->regs_dig_glb;
	const u8 *regs_ana = pdata->regs_ana_glb;
	int ret;

	regmap_set_bits(dp_phy->regmap,
			pdata->off_ana_glb + regs_ana[DP_PHYA_GLB_FORCE_CTRL_1],
			CKM_CKTX0_EN_FORCE_MODE);

	/* Disable RX unconditionally */
	regmap_write(dp_phy->regmap,
		     pdata->off_dig_glb + regs_dig[DP_PHYD_AUX_RX_CTL], 0);

	ret = mtk_dp_phy_disable_all_lanes(dp_phy);
	if (ret) {
		dev_err(dp_phy->dev, "Could not disable lanes for poweroff!\n");
		return ret;
	}

	return 0;
}

static int mtk_dp_phy_reset(struct phy *phy)
{
	struct mtk_dp_phy *dp_phy = phy_get_drvdata(phy);
	const struct mtk_dp_phy_pdata *pdata = dp_phy->pdata;
	const u32 reg_rst = pdata->regs_dig_glb[DP_PHYD_SW_RST];
	const u32 reg_drv_force = pdata->regs_dig_lane[DP_PHYD_LAN_DRIVING_FORCE];
	int i, ret;

	/* Clearing bits sets reset state */
	regmap_clear_bits(dp_phy->regmap, pdata->off_dig_glb + reg_rst, DP_GLB_SW_RST_PHYD);

	/* PHYD needs 50uS to guarantee reset done */
	usleep_range(50, 200);

	/* Setting bits means go out of reset */
	regmap_set_bits(dp_phy->regmap, pdata->off_dig_glb + reg_rst, DP_GLB_SW_RST_PHYD);

	/* Disable all lanes and continue reset even if this fails, but notify */
	ret = mtk_dp_phy_disable_all_lanes(dp_phy);
	if (ret)
		dev_err(dp_phy->dev, "Could not disable lanes during reset!\n");

	/* Reset Voltage Swing and Preemphasis values */
	for (i = 0; i < MTK_DP_PHY_MAX_LANES; i++) {
		const u32 off_dig_lane = pdata->off_dig_lane[i];

		regmap_clear_bits(dp_phy->regmap, off_dig_lane + reg_drv_force,
				   PHYD_DP_TX_FORCE_VOLT_SWING_EN |
				   PHYD_DP_TX_FORCE_VOLT_SWING_VAL |
				   PHYD_DP_TX_FORCE_PRE_EMPH_VAL);
	}

	return 0;
}

static const struct phy_ops mtk_dp_phy_dev_ops = {
	.init = mtk_dp_phy_init,
	.power_on = mtk_dp_phy_power_on,
	.power_off = mtk_dp_phy_power_off,
	.configure = mtk_dp_phy_configure,
	.reset = mtk_dp_phy_reset,
	.owner = THIS_MODULE,
};

static void mtk_dp_phy_get_default_cal_data(struct mtk_dp_phy *dp_phy)
{
	const struct mtk_dp_phy_pdata *pdata = dp_phy->pdata;
	int i;

	dp_phy->ana_bias_r = pdata->ana_bias_r;
	dp_phy->ana_cktx_imp = pdata->ana_cktx_imp;

	/* Copy the default lane impedance settings to all lanes */
	for (i = 0; i < MTK_DP_PHY_MAX_LANES; i++)
		memcpy(&dp_phy->ana_impsel[i], &pdata->ana_lanes_imp,
		       sizeof(dp_phy->ana_impsel[0]));

	return;
}

static int mtk_dp_phy_get_one_cal_para(struct device *dev, const char *name, u8 max_val)
{
	u8 buf_byte;
	u16 buf;
	int ret;

	/*
	 * All of the calibrations are always max 8 bits long, but some may
	 * be split between two different 8-bits cells: handle this corner
	 * case by retrying reading as u16.
	 */
	ret = nvmem_cell_read_u8(dev, name, &buf_byte);
	if (ret)
		ret = nvmem_cell_read_u16(dev, name, &buf);
	else
		buf = buf_byte;

	if (ret)
		return dev_err_probe(dev, ret, "Cannot get calibration data for %s\n", name);

	if (buf == 0) {
		dev_warn(dev, "No calibration for %s. Using defaults\n", name);
		return -ENOENT;
	}

	if (buf > max_val)
		return dev_err_probe(dev, -ERANGE, "Bad value %u retrieved for %s\n", buf, name);

	return buf;
}

static int mtk_dp_phy_get_calibration_data(struct mtk_dp_phy *dp_phy)
{
	char mtk_dp_cal_lane_imp_name[] = "impedance-laneXM";
	struct device *dev = dp_phy->dev;
	int i, ret;

	ret = mtk_dp_phy_get_one_cal_para(dev, "rbias-trim", FIELD_MAX(XTP_GLB_BIAS_INT_R_CTRL));
	if (ret < 0)
		goto end;
	dp_phy->ana_bias_r = ret;

	ret = mtk_dp_phy_get_one_cal_para(dev, "impedance-txclk", FIELD_MAX(CKM_PT0_CKTX_IMPSEL));
	if (ret < 0)
		goto end;
	dp_phy->ana_cktx_imp = ret;

	/* Get impedance params for each lane */
	for (i = 0; i < MTK_DP_PHY_MAX_LANES; i++) {
		/* P-MOSFET first */
		snprintf(mtk_dp_cal_lane_imp_name, ARRAY_SIZE(mtk_dp_cal_lane_imp_name),
			 "impedance-lane%dp", i);
		ret = mtk_dp_phy_get_one_cal_para(dev, mtk_dp_cal_lane_imp_name,
						  FIELD_MAX(XTP_LN_TX_IMPSEL_PMOS));
		if (ret < 0)
			goto end;
		dp_phy->ana_impsel[i].pmos = ret;

		/* ...and then N-MOSFET too */
		snprintf(mtk_dp_cal_lane_imp_name, ARRAY_SIZE(mtk_dp_cal_lane_imp_name),
			 "impedance-lane%dn", i);
		ret = mtk_dp_phy_get_one_cal_para(dev, mtk_dp_cal_lane_imp_name,
						  FIELD_MAX(XTP_LN_TX_IMPSEL_NMOS));
		if (ret < 0)
			goto end;
		dp_phy->ana_impsel[i].nmos = ret;
	}
end:
	if (ret < 0) {
		/*
		 * If any of the calibration values is missing, or if there
		 * is no calibration at all in the eFuses, copy the default
		 * one entirely (as partial values shall not be mixed!)
		 */
		if (ret == -ENOENT) {
			dev_info(dev, "Using calibration default values\n");
			mtk_dp_phy_get_default_cal_data(dp_phy);
			return 0;
		}
		return ret;
	};

	return 0;
}

static void mtk_dp_phy_legacy_remove_lookup(void *data)
{
	struct phy *phy = data;
	struct mtk_dp_phy *dp_phy = phy_get_drvdata(phy);

	phy_remove_lookup(phy, "dp", dev_name(dp_phy->dev));
}

static const struct mtk_dp_phy_pdata mt8195_dp_phy_data;

static int mtk_dp_phy_legacy_probe(struct platform_device *pdev, struct mtk_dp_phy *dp_phy)
{
	struct device *dev = &pdev->dev;
	struct phy *phy;
	int ret;

	/*
	 * If legacy platform driver probe, assume this is MT8195 or compatible
	 * with a devicetree that was not migrated to the new, proper bindings.
	 */
	dp_phy->pdata = &mt8195_dp_phy_data;
	dp_phy->regmap = *(struct regmap **)dev->platform_data;
	if (!dp_phy->regmap)
		return dev_err_probe(dev, -EINVAL, "No platform data available\n");

	phy = devm_phy_create(dev, NULL, &mtk_dp_phy_dev_ops);
	if (IS_ERR(phy))
		return dev_err_probe(dev, PTR_ERR(phy),
				     "Failed to create DP PHY\n");

	phy_set_drvdata(phy, dp_phy);

	/*
	 * Set default calibration data before exposing the PHY.
	 * For legacy probe, mtk_dp will set calibrations from eFuse, if found.
	 */
	mtk_dp_phy_get_default_cal_data(dp_phy);

	ret = phy_create_lookup(phy, "dp", dev_name(dev));
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, mtk_dp_phy_legacy_remove_lookup, phy);
	if (ret)
		return ret;

	return 0;
}

static const struct regmap_config mtk_dp_phy_regmap_cfg = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.disable_locking = true,
};

static int mtk_dp_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phy_provider *provider;
	struct mtk_dp_phy *dp_phy;
	void __iomem *base;
	struct phy *phy;
	int ret;

	dp_phy = devm_kzalloc(dev, sizeof(*dp_phy), GFP_KERNEL);
	if (!dp_phy)
		return -ENOMEM;

	dp_phy->dev = dev;

	/* If there's no devicetree, go for legacy pdev probe */
	if (!dev->of_node)
		return mtk_dp_phy_legacy_probe(pdev, dp_phy);

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	dp_phy->regmap = devm_regmap_init_mmio(dev, base, &mtk_dp_phy_regmap_cfg);
	if (IS_ERR(dp_phy->regmap))
		return PTR_ERR(dp_phy->regmap);

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	dp_phy->pdata = device_get_match_data(dev);

	if (IS_REACHABLE(CONFIG_NVMEM)) {
		ret = mtk_dp_phy_get_calibration_data(dp_phy);
		if (ret)
			return ret;
	} else {
		/* Use default calibration data */
		mtk_dp_phy_get_default_cal_data(dp_phy);
	}

	phy = devm_phy_create(dev, NULL, &mtk_dp_phy_dev_ops);
	if (IS_ERR(phy))
		return dev_err_probe(dev, PTR_ERR(phy),
				     "Failed to create DP PHY\n");

	phy_set_drvdata(phy, dp_phy);

	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(provider))
		return PTR_ERR(provider);

	return 0;
}

static const struct mtk_dp_phy_pdata mt8195_dp_phy_data = {
	.off_ana_glb = 0,
	.off_ana_lane = { 0x100, 0x200, 0x300, 0x400 },
	.off_dig_glb = 0x1000,
	.off_dig_lane = { 0x1100, 0x1200, 0x1300, 0x1400 },
	.regs_ana_glb = mt8195_phy_ana_glb_regs,
	.regs_ana_lane = mt8195_phy_ana_lane_regs,
	.regs_dig_glb = mt8195_phy_dig_glb_regs,
	.regs_dig_lane = mt8195_phy_dig_lane_regs,
	.mask_dig_tx_ln = PHYD_TX_LN_EN,
	.val_dig_bitrate = mt8195_phy_dig_bitrate_val,
	.ana_bias_r = 15,
	.ana_cktx_imp = 8,
	.ana_lanes_imp = {
		.pmos = 8,
		.nmos = 8,
	},
	.driving_params = {
		[0] = 0,
		[1] = 0,
		[2] = 0,
		[3] = MT8195_DRIVING_PARAM_3_DEFAULT,
		[4] = MT8195_DRIVING_PARAM_4_DEFAULT,
		[5] = MT8195_DRIVING_PARAM_5_DEFAULT,
		[6] = MT8195_DRIVING_PARAM_6_DEFAULT,
		[7] = MT8195_DRIVING_PARAM_7_DEFAULT,
		[8] = MT8195_DRIVING_PARAM_8_DEFAULT
	},
};

static const struct mtk_dp_phy_pdata mt8196_edp_phy_data = {
	.off_ana_glb = 0x400,
	.off_ana_lane = { 0x0, 0x100, 0x200, 0x300 },
	.off_dig_glb = 0x1400,
	.off_dig_lane = { 0x1000, 0x1100, 0x1200, 0x1300 },
	.regs_ana_glb = mt8195_phy_ana_glb_regs,
	.regs_ana_lane = mt8195_phy_ana_lane_regs,
	.regs_dig_glb = mt8196_phy_dig_glb_regs,
	.regs_dig_lane = mt8196_phy_dig_lane_regs,
	.mask_dig_tx_ln = PHYD_TX_LN_EN_V2,
	.val_dig_bitrate = mt8196_edp_phy_dig_bitrate_val,
	.ana_bias_r = 15,
	.ana_cktx_imp = 8,
	.ana_lanes_imp = {
		.pmos = 8,
		.nmos = 8,
	},
	.driving_params = {
		[0] = 0,
		[1] = 0,
		[2] = 0,
		[3] = MT8196_DRIVING_PARAM_3_DEFAULT,
		[4] = MT8196_DRIVING_PARAM_4_DEFAULT,
		[5] = MT8196_DRIVING_PARAM_5_DEFAULT,
		[6] = MT8196_DRIVING_PARAM_6_DEFAULT,
		[7] = MT8196_DRIVING_PARAM_7_DEFAULT,
		[8] = MT8196_DRIVING_PARAM_8_DEFAULT
	},
};

static const struct of_device_id mtk_dp_phy_of_match[] = {
	{ .compatible = "mediatek,mt8195-dp-phy", .data = &mt8195_dp_phy_data },
	{ .compatible = "mediatek,mt8196-edp-phy", .data = &mt8196_edp_phy_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_dp_phy_of_match);

static struct platform_driver mtk_dp_phy_driver = {
	.probe = mtk_dp_phy_probe,
	.driver = {
		.name = "mediatek-dp-phy",
		.of_match_table = mtk_dp_phy_of_match,
	},
};
module_platform_driver(mtk_dp_phy_driver);

MODULE_AUTHOR("AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>");
MODULE_AUTHOR("Markus Schneider-Pargmann <msp@baylibre.com>");
MODULE_DESCRIPTION("MediaTek DisplayPort PHY Driver");
MODULE_LICENSE("GPL");
