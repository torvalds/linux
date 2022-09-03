// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip HDMI/DP Combo PHY with Samsung IP block
 *
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#define HDPTXPHY_GRF_CON0			0x0000
#define RO_REF_CLK_SEL				GENMASK(11, 10)
#define LC_REF_CLK_SEL				GENMASK(9, 8)
#define PLL_EN					BIT(7)
#define BIAS_EN					BIT(6)
#define BGR_EN					BIT(5)
#define HDPTX_MODE_SEL				BIT(0)
#define HDPTXPHY_GRF_STATUS0			0x0080
#define PLL_LOCK_DONE				BIT(3)
#define PHY_CLK_RDY				BIT(2)
#define PHY_RDY					BIT(1)
#define SB_RDY					BIT(0)

/* cmn_reg0008 */
#define OVRD_LCPLL_EN				BIT(7)
#define LCPLL_EN				BIT(6)

/* cmn_reg003C */
#define ANA_LCPLL_RESERVED7			BIT(7)

/* cmn_reg003D */
#define OVRD_ROPLL_EN				BIT(7)
#define ROPLL_EN				BIT(6)

/* cmn_reg0046 */
#define ROPLL_ANA_CPP_CTRL_COARSE		GENMASK(7, 4)
#define ROPLL_ANA_CPP_CTRL_FINE			GENMASK(3, 0)

/* cmn_reg0047 */
#define ROPLL_ANA_LPF_C_SEL_COARSE		GENMASK(5, 3)
#define ROPLL_ANA_LPF_C_SEL_FINE		GENMASK(2, 0)

/* cmn_reg004E */
#define ANA_ROPLL_PI_EN				BIT(5)

/* cmn_reg0051 */
#define ROPLL_PMS_MDIV				GENMASK(7, 0)

/* cmn_reg0055 */
#define ROPLL_PMS_MDIV_AFC			GENMASK(7, 0)

/* cmn_reg0059 */
#define ANA_ROPLL_PMS_PDIV			GENMASK(7, 4)
#define ANA_ROPLL_PMS_REFDIV			GENMASK(3, 0)

/* cmn_reg005A */
#define ROPLL_PMS_SDIV_RBR			GENMASK(7, 4)
#define ROPLL_PMS_SDIV_HBR			GENMASK(3, 0)

/* cmn_reg005B */
#define ROPLL_PMS_SDIV_HBR2			GENMASK(7, 4)
#define ROPLL_PMS_SDIV_HBR3			GENMASK(3, 0)

/* cmn_reg005D */
#define OVRD_ROPLL_REF_CLK_SEL			BIT(5)
#define ROPLL_REF_CLK_SEL			GENMASK(4, 3)

/* cmn_reg005E */
#define ANA_ROPLL_SDM_EN			BIT(6)
#define OVRD_ROPLL_SDM_RSTN			BIT(5)
#define ROPLL_SDM_RSTN				BIT(4)
#define ROPLL_SDC_FRACTIONAL_EN_RBR		BIT(3)
#define ROPLL_SDC_FRACTIONAL_EN_HBR		BIT(2)
#define ROPLL_SDC_FRACTIONAL_EN_HBR2		BIT(1)
#define ROPLL_SDC_FRACTIONAL_EN_HBR3		BIT(0)

/* cmn_reg005F */
#define OVRD_ROPLL_SDC_RSTN			BIT(5)
#define ROPLL_SDC_RSTN				BIT(4)

/* cmn_reg0060 */
#define ROPLL_SDM_DENOMINATOR			GENMASK(7, 0)

/* cmn_reg0064 */
#define ROPLL_SDM_NUMERATOR_SIGN_RBR		BIT(3)
#define ROPLL_SDM_NUMERATOR_SIGN_HBR		BIT(2)
#define ROPLL_SDM_NUMERATOR_SIGN_HBR2		BIT(1)
#define ROPLL_SDM_NUMERATOR_SIGN_HBR3		BIT(0)

/* cmn_reg0065 */
#define ROPLL_SDM_NUMERATOR			GENMASK(7, 0)

/* cmn_reg0069 */
#define ROPLL_SDC_N_RBR				GENMASK(2, 0)

/* cmn_reg006A */
#define ROPLL_SDC_N_HBR				GENMASK(5, 3)
#define ROPLL_SDC_N_HBR2			GENMASK(2, 0)

/* cmn_reg006B */
#define ROPLL_SDC_N_HBR3			GENMASK(3, 1)

/* cmn_reg006C */
#define ROPLL_SDC_NUMERATOR			GENMASK(5, 0)

/* cmn_reg0070 */
#define ROPLL_SDC_DENOMINATOR			GENMASK(5, 0)

/* cmn_reg0074 */
#define OVRD_ROPLL_SDC_NDIV_RSTN		BIT(3)
#define ROPLL_SDC_NDIV_RSTN			BIT(2)
#define OVRD_ROPLL_SSC_EN			BIT(1)
#define ROPLL_SSC_EN				BIT(0)

/* cmn_reg0075 */
#define ANA_ROPLL_SSC_FM_DEVIATION		GENMASK(5, 0)

/* cmn_reg0076 */
#define ANA_ROPLL_SSC_FM_FREQ			GENMASK(6, 2)

/* cmn_reg0077 */
#define ANA_ROPLL_SSC_CLK_DIV_SEL		GENMASK(6, 3)

/* cmn_reg0081 */
#define ANA_PLL_CD_TX_SER_RATE_SEL		BIT(3)
#define ANA_PLL_CD_HSCLK_WEST_EN		BIT(1)
#define ANA_PLL_CD_HSCLK_EAST_EN		BIT(0)

/* cmn_reg0082 */
#define ANA_PLL_CD_VREG_GAIN_CTRL		GENMASK(3, 0)

/* cmn_reg0083 */
#define ANA_PLL_CD_VREG_ICTRL			GENMASK(6, 5)

/* cmn_reg0084 */
#define PLL_LCRO_CLK_SEL			BIT(5)

/* cmn_reg0085 */
#define ANA_PLL_SYNC_LOSS_DET_MODE		GENMASK(1, 0)

/* cmn_reg0087 */
#define ANA_PLL_TX_HS_CLK_EN			BIT(2)

/* cmn_reg0095 */
#define DP_TX_LINK_BW				GENMASK(1, 0)

/* cmn_reg0097 */
#define DIG_CLK_SEL				BIT(1)

/* cmn_reg0099 */
#define SSC_EN					GENMASK(7, 6)
#define CMN_ROPLL_ALONE_MODE			BIT(2)

/* cmn_reg009A */
#define HS_SPEED_SEL				BIT(0)

/* cmn_reg009B */
#define LS_SPEED_SEL				BIT(4)

/* sb_reg0102 */
#define OVRD_SB_RXTERM_EN			BIT(5)
#define SB_RXRERM_EN				BIT(4)
#define ANA_SB_RXTERM_OFFSP			GENMASK(3, 0)

/* sb_reg0103 */
#define ANA_SB_RXTERM_OFFSN			GENMASK(6, 3)
#define OVRD_SB_RX_RESCAL_DONE			BIT(1)
#define SB_RX_RESCAL_DONE			BIT(0)

/* sb_reg0104 */
#define OVRD_SB_EN				BIT(5)
#define SB_EN					BIT(4)
#define OVRD_SB_AUX_EN				BIT(1)
#define SB_AUX_EN				BIT(0)

/* sb_reg0105 */
#define ANA_SB_TX_HLVL_PROG			GENMASK(2, 0)

/* sb_reg0106 */
#define ANA_SB_TX_LLVL_PROG			GENMASK(6, 4)

/* sb_reg010D */
#define ANA_SB_DMRX_LPBK_DATA			BIT(4)

/* sb_reg010F */
#define OVRD_SB_VREG_EN				BIT(7)
#define SB_VREG_EN				BIT(6)
#define ANA_SB_VREG_GAIN_CTRL			GENMASK(3, 0)

/* sb_reg0110 */
#define ANA_SB_VREG_OUT_SEL			BIT(1)
#define ANA_SB_VREG_REF_SEL			BIT(0)

/* sb_reg0113 */
#define SB_RX_RCAL_OPT_CODE			GENMASK(5, 4)
#define SB_RX_RTERM_CTRL			GENMASK(3, 0)

/* sb_reg0114 */
#define SB_TG_SB_EN_DELAY_TIME			GENMASK(5, 3)
#define SB_TG_RXTERN_EN_DELAY_TIME		GENMASK(2, 0)

/* sb_reg0115 */
#define SB_READY_DELAY_TIME			GENMASK(5, 3)
#define SB_TG_OSC_EN_DELAY_TIME			GENMASK(2, 0)

/* sb_reg0116 */
#define SB_TG_OSC_EN_TO_AFC_RSTN_DELAT_TIME	GENMASK(6, 4)

/* sb_reg0117 */
#define SB_TG_PLL_CD_VREG_FAST_PULSE_TIME	GENMASK(3, 0)

/* sb_reg0118 */
#define SB_TG_EARC_DMRX_RECVRD_CLK_CNT		GENMASK(7, 0)

/* sb_reg011A */
#define SB_TG_CNT_RUN_NO_7_0			GENMASK(7, 0)

/* sb_reg011B */
#define SB_EARC_SIG_DET_BYPASS			BIT(4)
#define SB_AFC_TOL				GENMASK(3, 0)

/* sb_reg011C */
#define SB_AFC_STB_NUM				GENMASK(3, 0)

/* sb_reg011D */
#define SB_TG_OSC_CNT_MIN			GENMASK(7, 0)

/* sb_reg011E */
#define SB_TG_OSC_CNT_MAX			GENMASK(7, 0)

/* sb_reg011F */
#define SB_PWM_AFC_CTRL				GENMASK(7, 2)
#define SB_RCAL_RSTN				BIT(1)

/* sb_reg0120 */
#define SB_AUX_EN_IN				BIT(7)

/* sb_reg0123 */
#define OVRD_SB_READY				BIT(5)
#define SB_READY				BIT(4)

/* lntop_reg0200 */
#define PROTOCOL_SEL				BIT(2)

/* lntop_reg0206 */
#define DATA_BUS_WIDTH				GENMASK(2, 1)
#define BUS_WIDTH_SEL				BIT(0)

/* lntop_reg0207 */
#define LANE_EN					GENMASK(3, 0)

/* lane_reg0301 */
#define OVRD_LN_TX_DRV_EI_EN			BIT(7)
#define LN_TX_DRV_EI_EN				BIT(6)

/* lane_reg0303 */
#define OVRD_LN_TX_DRV_LVL_CTRL			BIT(5)
#define LN_TX_DRV_LVL_CTRL			GENMASK(4, 0)

/* lane_reg0304 */
#define OVRD_LN_TX_DRV_POST_LVL_CTRL		BIT(4)
#define LN_TX_DRV_POST_LVL_CTRL			GENMASK(3, 0)

/* lane_reg0305 */
#define OVRD_LN_TX_DRV_PRE_LVL_CTRL		BIT(6)
#define LN_TX_DRV_PRE_LVL_CTRL			GENMASK(5, 2)

/* lane_reg0306 */
#define LN_ANA_TX_DRV_IDRV_IDN_CTRL		GENMASK(7, 5)
#define LN_ANA_TX_DRV_IDRV_IUP_CTRL		GENMASK(4, 2)
#define LN_ANA_TX_DRV_ACCDRV_EN			BIT(0)

/* lane_reg0307 */
#define LN_ANA_TX_DRV_ACCDRV_POL_SEL		BIT(6)
#define LN_ANA_TX_DRV_ACCDRV_CTRL		GENMASK(5, 3)

/* lane_reg030A */
#define LN_ANA_TX_JEQ_EN			BIT(4)
#define LN_TX_JEQ_EVEN_CTRL_RBR			GENMASK(3, 0)

/* lane_reg030B */
#define LN_TX_JEQ_EVEN_CTRL_HBR			GENMASK(7, 4)
#define LN_TX_JEQ_EVEN_CTRL_HBR2		GENMASK(3, 0)

/* lane_reg030C */
#define LN_TX_JEQ_EVEN_CTRL_HBR3		GENMASK(7, 4)
#define LN_TX_JEQ_ODD_CTRL_RBR			GENMASK(3, 0)

/* lane_reg030D */
#define LN_TX_JEQ_ODD_CTRL_HBR			GENMASK(7, 4)
#define LN_TX_JEQ_ODD_CTRL_HBR2			GENMASK(3, 0)

/* lane_reg030E */
#define LN_TX_JEQ_ODD_CTRL_HBR3			GENMASK(7, 4)

/* lane_reg0310 */
#define LN_ANA_TX_SYNC_LOSS_DET_MODE		GENMASK(1, 0)

/* lane_reg0311 */
#define LN_TX_SER_40BIT_EN_RBR			BIT(3)
#define LN_TX_SER_40BIT_EN_HBR			BIT(2)
#define LN_TX_SER_40BIT_EN_HBR2			BIT(1)
#define LN_TX_SER_40BIT_EN_HBR3			BIT(0)

/* lane_reg0316 */
#define LN_ANA_TX_SER_VREG_GAIN_CTRL		GENMASK(3, 0)

/* lane_reg031B */
#define LN_ANA_TX_RESERVED			GENMASK(7, 0)

/* lane_reg031E */
#define LN_POLARITY_INV				BIT(2)

#define LANE_REG(lane, offset)			(0x400 * (lane) + (offset))

struct rockchip_hdptx_phy {
	struct device *dev;
	struct clk_bulk_data *clks;
	int nr_clks;
	struct reset_control *apb_reset;
	struct reset_control *cmn_reset;
	struct reset_control *init_reset;
	struct reset_control *lane_reset;
	struct regmap *regmap;
	struct regmap *grf;
	u32 lane_polarity_invert[4];
};

enum {
	DP_BW_RBR,
	DP_BW_HBR,
	DP_BW_HBR2,
	DP_BW_HBR3,
};

struct tx_drv_ctrl {
	u8 tx_drv_lvl_ctrl;
	u8 tx_drv_post_lvl_ctrl;
	u8 ana_tx_drv_idrv_idn_ctrl;
	u8 ana_tx_drv_idrv_iup_ctrl;
	u8 ana_tx_drv_accdrv_en;
	u8 ana_tx_drv_accdrv_ctrl;
} __packed;

static struct tx_drv_ctrl tx_drv_ctrl_rbr[4][4] = {
	/* voltage swing 0, pre-emphasis 0->3 */
	{
		{ 0x1, 0x0, 0x4, 0x6, 0x0, 0x4 },
		{ 0x4, 0x3, 0x4, 0x6, 0x0, 0x4 },
		{ 0x7, 0x6, 0x4, 0x6, 0x0, 0x4 },
		{ 0xd, 0xb, 0x7, 0x7, 0x1, 0x7 },
	},

	/* voltage swing 1, pre-emphasis 0->2 */
	{
		{ 0x4, 0x0, 0x4, 0x6, 0x0, 0x4 },
		{ 0xa, 0x5, 0x4, 0x6, 0x0, 0x4 },
		{ 0xd, 0x8, 0x7, 0x7, 0x1, 0x7 },
	},

	/* voltage swing 2, pre-emphasis 0->1 */
	{
		{ 0x8, 0x0, 0x4, 0x6, 0x0, 0x4 },
		{ 0xd, 0x5, 0x7, 0x7, 0x1, 0x7 },
	},

	/* voltage swing 3, pre-emphasis 0 */
	{
		{ 0xd, 0x0, 0x7, 0x7, 0x1, 0x4 },
	}
};

static struct tx_drv_ctrl tx_drv_ctrl_hbr[4][4] = {
	/* voltage swing 0, pre-emphasis 0->3 */
	{
		{ 0x2, 0x1, 0x4, 0x6, 0x0, 0x4 },
		{ 0x5, 0x4, 0x4, 0x6, 0x0, 0x4 },
		{ 0x9, 0x8, 0x4, 0x6, 0x0, 0x4 },
		{ 0xd, 0xb, 0x7, 0x7, 0x1, 0x7 },
	},

	/* voltage swing 1, pre-emphasis 0->2 */
	{
		{ 0x6, 0x1, 0x4, 0x6, 0x0, 0x4 },
		{ 0xb, 0x6, 0x4, 0x6, 0x0, 0x4 },
		{ 0xd, 0x8, 0x7, 0x7, 0x1, 0x7 },
	},

	/* voltage swing 2, pre-emphasis 0->1 */
	{
		{ 0x9, 0x1, 0x4, 0x6, 0x0, 0x4 },
		{ 0xd, 0x6, 0x7, 0x7, 0x1, 0x7 },
	},

	/* voltage swing 3, pre-emphasis 0 */
	{
		{ 0xd, 0x1, 0x7, 0x7, 0x1, 0x4 },
	}
};

static struct tx_drv_ctrl tx_drv_ctrl_hbr2[4][4] = {
	/* voltage swing 0, pre-emphasis 0->3 */
	{
		{ 0x2, 0x1, 0x4, 0x6, 0x0, 0x4 },
		{ 0x5, 0x4, 0x4, 0x6, 0x0, 0x4 },
		{ 0x9, 0x8, 0x4, 0x6, 0x1, 0x4 },
		{ 0xd, 0xb, 0x7, 0x7, 0x1, 0x7 },
	},

	/* voltage swing 1, pre-emphasis 0->2 */
	{
		{ 0x6, 0x1, 0x4, 0x6, 0x0, 0x4 },
		{ 0xc, 0x7, 0x4, 0x6, 0x0, 0x4 },
		{ 0xd, 0x8, 0x7, 0x7, 0x1, 0x7 },
	},

	/* voltage swing 2, pre-emphasis 0->1 */
	{
		{ 0x9, 0x1, 0x4, 0x6, 0x0, 0x4 },
		{ 0xd, 0x6, 0x7, 0x7, 0x1, 0x7 },
	},

	/* voltage swing 3, pre-emphasis 0 */
	{
		{ 0xd, 0x0, 0x7, 0x7, 0x1, 0x4 },
	}
};

static int rockchip_hdptx_phy_parse_training_table(struct device *dev)
{
	size_t size = sizeof(struct tx_drv_ctrl) * 10;
	u8 *buf, *training_table;
	int i, j;

	buf = kzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (device_property_read_u8_array(dev, "training-table", buf, size)) {
		kfree(buf);
		return 0;
	}

	training_table = buf;

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 4; j++) {
			struct tx_drv_ctrl *ctrl;

			if (i + j > 3)
				continue;

			ctrl = (struct tx_drv_ctrl *)training_table;
			tx_drv_ctrl_rbr[i][j] = *ctrl;
			tx_drv_ctrl_hbr[i][j] = *ctrl;
			tx_drv_ctrl_hbr2[i][j] = *ctrl;
			training_table += sizeof(*ctrl);
		}
	}

	kfree(buf);

	return 0;
}

static int rockchip_grf_write(struct regmap *grf, unsigned int reg,
			      unsigned int mask, unsigned int val)
{
	return regmap_write(grf, reg, (mask << 16) | (val & mask));
}

static int rockchip_hdptx_phy_set_mode(struct phy *phy, enum phy_mode mode,
				       int submode)
{
	return 0;
}

static int rockchip_hdptx_phy_verify_config(struct rockchip_hdptx_phy *hdptx,
					    struct phy_configure_opts_dp *dp)
{
	int i;

	if (dp->set_rate) {
		switch (dp->link_rate) {
		case 1620:
		case 2700:
		case 5400:
			break;
		default:
			return -EINVAL;
		}
	}

	switch (dp->lanes) {
	case 0:
	case 1:
	case 2:
	case 4:
		break;
	default:
		return -EINVAL;
	}

	if (dp->set_voltages) {
		for (i = 0; i < dp->lanes; i++) {
			if (dp->voltage[i] > 3 || dp->pre[i] > 3)
				return -EINVAL;

			if (dp->voltage[i] + dp->pre[i] > 3)
				return -EINVAL;
		}
	}

	return 0;
}

static void rockchip_hdptx_phy_set_voltage(struct rockchip_hdptx_phy *hdptx,
					   struct phy_configure_opts_dp *dp,
					   u8 lane)
{
	const struct tx_drv_ctrl *ctrl;

	switch (dp->link_rate) {
	case 1620:
		ctrl = &tx_drv_ctrl_rbr[dp->voltage[lane]][dp->pre[lane]];
		regmap_update_bits(hdptx->regmap, LANE_REG(lane, 0x0c44),
				   LN_TX_SER_40BIT_EN_RBR,
				   FIELD_PREP(LN_TX_SER_40BIT_EN_RBR, 0x1));
		break;
	case 2700:
		ctrl = &tx_drv_ctrl_hbr[dp->voltage[lane]][dp->pre[lane]];
		regmap_update_bits(hdptx->regmap, LANE_REG(lane, 0x0c44),
				   LN_TX_SER_40BIT_EN_HBR,
				   FIELD_PREP(LN_TX_SER_40BIT_EN_HBR, 0x1));
		break;
	case 5400:
	default:
		ctrl = &tx_drv_ctrl_hbr2[dp->voltage[lane]][dp->pre[lane]];
		regmap_update_bits(hdptx->regmap, LANE_REG(lane, 0x0c44),
				   LN_TX_SER_40BIT_EN_HBR2,
				   FIELD_PREP(LN_TX_SER_40BIT_EN_HBR2, 0x1));
		break;
	}

	regmap_update_bits(hdptx->regmap, LANE_REG(lane, 0x0c0c),
			   OVRD_LN_TX_DRV_LVL_CTRL | LN_TX_DRV_LVL_CTRL,
			   FIELD_PREP(OVRD_LN_TX_DRV_LVL_CTRL, 0x1) |
			   FIELD_PREP(LN_TX_DRV_LVL_CTRL,
				      ctrl->tx_drv_lvl_ctrl));

	regmap_update_bits(hdptx->regmap, LANE_REG(lane, 0x0c10),
			   OVRD_LN_TX_DRV_POST_LVL_CTRL |
			   LN_TX_DRV_POST_LVL_CTRL,
			   FIELD_PREP(OVRD_LN_TX_DRV_POST_LVL_CTRL, 0x1) |
			   FIELD_PREP(LN_TX_DRV_POST_LVL_CTRL,
				      ctrl->tx_drv_post_lvl_ctrl));

	regmap_update_bits(hdptx->regmap, LANE_REG(lane, 0x0c18),
			   LN_ANA_TX_DRV_IDRV_IDN_CTRL |
			   LN_ANA_TX_DRV_IDRV_IUP_CTRL |
			   LN_ANA_TX_DRV_ACCDRV_EN,
			   FIELD_PREP(LN_ANA_TX_DRV_IDRV_IDN_CTRL,
				      ctrl->ana_tx_drv_idrv_idn_ctrl) |
			   FIELD_PREP(LN_ANA_TX_DRV_IDRV_IUP_CTRL,
				      ctrl->ana_tx_drv_idrv_iup_ctrl) |
			   FIELD_PREP(LN_ANA_TX_DRV_ACCDRV_EN,
				      ctrl->ana_tx_drv_accdrv_en));

	regmap_update_bits(hdptx->regmap, LANE_REG(lane, 0x0c1c),
			   LN_ANA_TX_DRV_ACCDRV_POL_SEL |
			   LN_ANA_TX_DRV_ACCDRV_CTRL,
			   FIELD_PREP(LN_ANA_TX_DRV_ACCDRV_POL_SEL, 0x1) |
			   FIELD_PREP(LN_ANA_TX_DRV_ACCDRV_CTRL,
				      ctrl->ana_tx_drv_accdrv_ctrl));
	regmap_update_bits(hdptx->regmap, LANE_REG(lane, 0x0c6c),
			   LN_ANA_TX_RESERVED,
			   FIELD_PREP(LN_ANA_TX_RESERVED, 0x1));
	regmap_update_bits(hdptx->regmap, LANE_REG(lane, 0x0c58),
			   LN_ANA_TX_SER_VREG_GAIN_CTRL,
			   FIELD_PREP(LN_ANA_TX_SER_VREG_GAIN_CTRL, 0x2));
	regmap_update_bits(hdptx->regmap, LANE_REG(lane, 0x0c40),
			   LN_ANA_TX_SYNC_LOSS_DET_MODE,
			   FIELD_PREP(LN_ANA_TX_SYNC_LOSS_DET_MODE, 0x3));
}

static int rockchip_hdptx_phy_set_voltages(struct rockchip_hdptx_phy *hdptx,
					   struct phy_configure_opts_dp *dp)
{
	u8 lane;

	for (lane = 0; lane < dp->lanes; lane++)
		rockchip_hdptx_phy_set_voltage(hdptx, dp, lane);

	return 0;
}

static void rockchip_hdptx_phy_lane_disable(struct rockchip_hdptx_phy *hdptx)
{
	reset_control_assert(hdptx->lane_reset);

	regmap_update_bits(hdptx->regmap, 0x081c, LANE_EN,
			   FIELD_PREP(LANE_EN, 0x0));

	rockchip_grf_write(hdptx->grf, HDPTXPHY_GRF_CON0, PLL_EN,
			   FIELD_PREP(PLL_EN, 0x0));

	regmap_update_bits(hdptx->regmap, 0x0020, OVRD_LCPLL_EN | LCPLL_EN,
			   FIELD_PREP(OVRD_LCPLL_EN, 0x1) |
			   FIELD_PREP(LCPLL_EN, 0x0));
	regmap_update_bits(hdptx->regmap, 0x00f4, OVRD_ROPLL_EN | ROPLL_EN,
			   FIELD_PREP(OVRD_ROPLL_EN, 0x1) |
			   FIELD_PREP(ROPLL_EN, 0x0));
}

static int rockchip_hdptx_phy_set_lanes(struct rockchip_hdptx_phy *hdptx,
					struct phy_configure_opts_dp *dp)
{
	u32 status;
	int ret;

	if (!dp->lanes) {
		rockchip_hdptx_phy_lane_disable(hdptx);
		return 0;
	}

	regmap_update_bits(hdptx->regmap, 0x081c, LANE_EN,
			   FIELD_PREP(LANE_EN, GENMASK(dp->lanes - 1, 0)));

	reset_control_deassert(hdptx->lane_reset);

	ret = regmap_read_poll_timeout(hdptx->grf, HDPTXPHY_GRF_STATUS0,
				       status, FIELD_GET(PHY_RDY, status),
				       50, 5000);
	if (ret) {
		dev_err(hdptx->dev, "timeout waiting for phy_rdy\n");
		return ret;
	}

	return 0;
}

static int rockchip_hdptx_phy_set_rate(struct rockchip_hdptx_phy *hdptx,
				       struct phy_configure_opts_dp *dp)
{
	u32 bw, status;
	int ret;

	rockchip_grf_write(hdptx->grf, HDPTXPHY_GRF_CON0, PLL_EN,
			   FIELD_PREP(PLL_EN, 0x0));

	switch (dp->link_rate) {
	case 1620:
		bw = DP_BW_RBR;
		break;
	case 2700:
		bw = DP_BW_HBR;
		break;
	case 5400:
		bw = DP_BW_HBR2;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(hdptx->regmap, 0x0254, DP_TX_LINK_BW,
			   FIELD_PREP(DP_TX_LINK_BW, bw));

	if (dp->ssc) {
		regmap_update_bits(hdptx->regmap, 0x01d0,
				   OVRD_ROPLL_SSC_EN | ROPLL_SSC_EN,
				   FIELD_PREP(OVRD_ROPLL_SSC_EN, 0x1) |
				   FIELD_PREP(ROPLL_SSC_EN, 0x1));
		regmap_write(hdptx->regmap, 0x01d4,
			     FIELD_PREP(ANA_ROPLL_SSC_FM_DEVIATION, 0xc));
		regmap_update_bits(hdptx->regmap, 0x01d8,
				   ANA_ROPLL_SSC_FM_FREQ,
				   FIELD_PREP(ANA_ROPLL_SSC_FM_FREQ, 0x1f));
		regmap_update_bits(hdptx->regmap, 0x0264, SSC_EN,
				   FIELD_PREP(SSC_EN, 0x2));
	} else {
		regmap_update_bits(hdptx->regmap, 0x01d0,
				   OVRD_ROPLL_SSC_EN | ROPLL_SSC_EN,
				   FIELD_PREP(OVRD_ROPLL_SSC_EN, 0x1) |
				   FIELD_PREP(ROPLL_SSC_EN, 0x0));
		regmap_write(hdptx->regmap, 0x01d4,
			     FIELD_PREP(ANA_ROPLL_SSC_FM_DEVIATION, 0x20));
		regmap_update_bits(hdptx->regmap, 0x01d8,
				   ANA_ROPLL_SSC_FM_FREQ,
				   FIELD_PREP(ANA_ROPLL_SSC_FM_FREQ, 0xc));
		regmap_update_bits(hdptx->regmap, 0x0264, SSC_EN,
				   FIELD_PREP(SSC_EN, 0x0));
	}

	regmap_update_bits(hdptx->regmap, 0x0020, OVRD_LCPLL_EN | LCPLL_EN,
			   FIELD_PREP(OVRD_LCPLL_EN, 0x1) |
			   FIELD_PREP(LCPLL_EN, 0x0));
	regmap_update_bits(hdptx->regmap, 0x00f4, OVRD_ROPLL_EN | ROPLL_EN,
			   FIELD_PREP(OVRD_ROPLL_EN, 0x1) |
			   FIELD_PREP(ROPLL_EN, 0x1));

	rockchip_grf_write(hdptx->grf, HDPTXPHY_GRF_CON0, PLL_EN,
			   FIELD_PREP(PLL_EN, 0x1));

	ret = regmap_read_poll_timeout(hdptx->grf, HDPTXPHY_GRF_STATUS0,
				       status, FIELD_GET(PLL_LOCK_DONE, status),
				       50, 1000);
	if (ret) {
		dev_err(hdptx->dev, "timeout waiting for pll_lock_done\n");
		return ret;
	}

	return 0;
}

static int rockchip_hdptx_phy_configure(struct phy *phy,
					union phy_configure_opts *opts)
{
	struct rockchip_hdptx_phy *hdptx = phy_get_drvdata(phy);
	enum phy_mode mode = phy_get_mode(phy);
	int ret;

	if (mode != PHY_MODE_DP)
		return -EINVAL;

	ret = rockchip_hdptx_phy_verify_config(hdptx, &opts->dp);
	if (ret) {
		dev_err(hdptx->dev, "invalid params for phy configure\n");
		return ret;
	}

	if (opts->dp.set_rate) {
		ret = rockchip_hdptx_phy_set_rate(hdptx, &opts->dp);
		if (ret) {
			dev_err(hdptx->dev, "failed to set rate: %d\n", ret);
			return ret;
		}
	}

	if (opts->dp.set_lanes) {
		ret = rockchip_hdptx_phy_set_lanes(hdptx, &opts->dp);
		if (ret) {
			dev_err(hdptx->dev, "failed to set lanes: %d\n", ret);
			return ret;
		}
	}

	if (opts->dp.set_voltages) {
		ret = rockchip_hdptx_phy_set_voltages(hdptx, &opts->dp);
		if (ret) {
			dev_err(hdptx->dev, "failed to set voltages: %d\n",
				ret);
			return ret;
		}
	}

	return 0;
}

static void rockchip_hdptx_phy_dp_pll_init(struct rockchip_hdptx_phy *hdptx)
{
	regmap_write(hdptx->regmap, 0x0144, FIELD_PREP(ROPLL_PMS_MDIV, 0x87));
	regmap_write(hdptx->regmap, 0x0148, FIELD_PREP(ROPLL_PMS_MDIV, 0x71));
	regmap_write(hdptx->regmap, 0x014c, FIELD_PREP(ROPLL_PMS_MDIV, 0x71));

	regmap_write(hdptx->regmap, 0x0154,
		     FIELD_PREP(ROPLL_PMS_MDIV_AFC, 0x87));
	regmap_write(hdptx->regmap, 0x0158,
		     FIELD_PREP(ROPLL_PMS_MDIV_AFC, 0x71));
	regmap_write(hdptx->regmap, 0x015c,
		     FIELD_PREP(ROPLL_PMS_MDIV_AFC, 0x71));

	regmap_write(hdptx->regmap, 0x0164,
		     FIELD_PREP(ANA_ROPLL_PMS_PDIV, 0x1) |
		     FIELD_PREP(ANA_ROPLL_PMS_REFDIV, 0x1));

	regmap_write(hdptx->regmap, 0x0168,
		     FIELD_PREP(ROPLL_PMS_SDIV_RBR, 0x3) |
		     FIELD_PREP(ROPLL_PMS_SDIV_HBR, 0x1));
	regmap_update_bits(hdptx->regmap, 0x016c, ROPLL_PMS_SDIV_HBR2,
			   FIELD_PREP(ROPLL_PMS_SDIV_HBR2, 0x0));

	regmap_update_bits(hdptx->regmap, 0x0178, ANA_ROPLL_SDM_EN,
			   FIELD_PREP(ANA_ROPLL_SDM_EN, 0x1));
	regmap_update_bits(hdptx->regmap, 0x0178,
			   OVRD_ROPLL_SDM_RSTN | ROPLL_SDM_RSTN,
			   FIELD_PREP(OVRD_ROPLL_SDM_RSTN, 0x1) |
			   FIELD_PREP(ROPLL_SDM_RSTN, 0x1));
	regmap_update_bits(hdptx->regmap, 0x0178, ROPLL_SDC_FRACTIONAL_EN_RBR,
			   FIELD_PREP(ROPLL_SDC_FRACTIONAL_EN_RBR, 0x1));
	regmap_update_bits(hdptx->regmap, 0x0178, ROPLL_SDC_FRACTIONAL_EN_HBR,
			   FIELD_PREP(ROPLL_SDC_FRACTIONAL_EN_HBR, 0x1));
	regmap_update_bits(hdptx->regmap, 0x0178, ROPLL_SDC_FRACTIONAL_EN_HBR2,
			   FIELD_PREP(ROPLL_SDC_FRACTIONAL_EN_HBR2, 0x1));
	regmap_update_bits(hdptx->regmap, 0x017c,
			   OVRD_ROPLL_SDC_RSTN | ROPLL_SDC_RSTN,
			   FIELD_PREP(OVRD_ROPLL_SDC_RSTN, 0x1) |
			   FIELD_PREP(ROPLL_SDC_RSTN, 0x1));

	regmap_write(hdptx->regmap, 0x0180,
		     FIELD_PREP(ROPLL_SDM_DENOMINATOR, 0x21));
	regmap_write(hdptx->regmap, 0x0184,
		     FIELD_PREP(ROPLL_SDM_DENOMINATOR, 0x27));
	regmap_write(hdptx->regmap, 0x0188,
		     FIELD_PREP(ROPLL_SDM_DENOMINATOR, 0x27));

	regmap_update_bits(hdptx->regmap, 0x0190,
			   ROPLL_SDM_NUMERATOR_SIGN_RBR |
			   ROPLL_SDM_NUMERATOR_SIGN_HBR |
			   ROPLL_SDM_NUMERATOR_SIGN_HBR2,
			   FIELD_PREP(ROPLL_SDM_NUMERATOR_SIGN_RBR, 0x0) |
			   FIELD_PREP(ROPLL_SDM_NUMERATOR_SIGN_HBR, 0x1) |
			   FIELD_PREP(ROPLL_SDM_NUMERATOR_SIGN_HBR2, 0x1));

	regmap_write(hdptx->regmap, 0x0194,
		     FIELD_PREP(ROPLL_SDM_NUMERATOR, 0x0));
	regmap_write(hdptx->regmap, 0x0198,
		     FIELD_PREP(ROPLL_SDM_NUMERATOR, 0xd));
	regmap_write(hdptx->regmap, 0x019c,
		     FIELD_PREP(ROPLL_SDM_NUMERATOR, 0xd));

	regmap_update_bits(hdptx->regmap, 0x01a4, ROPLL_SDC_N_RBR,
			   FIELD_PREP(ROPLL_SDC_N_RBR, 0x2));
	regmap_update_bits(hdptx->regmap, 0x01a8,
			   ROPLL_SDC_N_HBR | ROPLL_SDC_N_HBR2,
			   FIELD_PREP(ROPLL_SDC_N_HBR, 0x2) |
			   FIELD_PREP(ROPLL_SDC_N_HBR2, 0x2));

	regmap_write(hdptx->regmap, 0x01b0,
		     FIELD_PREP(ROPLL_SDC_NUMERATOR, 0x3));
	regmap_write(hdptx->regmap, 0x01b4,
		     FIELD_PREP(ROPLL_SDC_NUMERATOR, 0x7));
	regmap_write(hdptx->regmap, 0x01b8,
		     FIELD_PREP(ROPLL_SDC_NUMERATOR, 0x7));

	regmap_write(hdptx->regmap, 0x01c0,
		     FIELD_PREP(ROPLL_SDC_DENOMINATOR, 0x8));
	regmap_write(hdptx->regmap, 0x01c4,
		     FIELD_PREP(ROPLL_SDC_DENOMINATOR, 0x18));
	regmap_write(hdptx->regmap, 0x01c8,
		     FIELD_PREP(ROPLL_SDC_DENOMINATOR, 0x18));

	regmap_update_bits(hdptx->regmap, 0x01d0,
			   OVRD_ROPLL_SDC_NDIV_RSTN | ROPLL_SDC_NDIV_RSTN,
			   FIELD_PREP(OVRD_ROPLL_SDC_NDIV_RSTN, 0x1) |
			   FIELD_PREP(ROPLL_SDC_NDIV_RSTN, 0x1));
	regmap_update_bits(hdptx->regmap, 0x01dc, ANA_ROPLL_SSC_CLK_DIV_SEL,
			   FIELD_PREP(ANA_ROPLL_SSC_CLK_DIV_SEL, 0x1));

	regmap_update_bits(hdptx->regmap, 0x0118,
			   ROPLL_ANA_CPP_CTRL_COARSE | ROPLL_ANA_CPP_CTRL_FINE,
			   FIELD_PREP(ROPLL_ANA_CPP_CTRL_COARSE, 0xe) |
			   FIELD_PREP(ROPLL_ANA_CPP_CTRL_FINE, 0xe));
	regmap_update_bits(hdptx->regmap, 0x011c,
			   ROPLL_ANA_LPF_C_SEL_COARSE |
			   ROPLL_ANA_LPF_C_SEL_FINE,
			   FIELD_PREP(ROPLL_ANA_LPF_C_SEL_COARSE, 0x4) |
			   FIELD_PREP(ROPLL_ANA_LPF_C_SEL_FINE, 0x4));

	regmap_update_bits(hdptx->regmap, 0x0204, ANA_PLL_CD_TX_SER_RATE_SEL,
			   FIELD_PREP(ANA_PLL_CD_TX_SER_RATE_SEL, 0x0));

	regmap_update_bits(hdptx->regmap, 0x025c, DIG_CLK_SEL,
			   FIELD_PREP(DIG_CLK_SEL, 0x1));
	regmap_update_bits(hdptx->regmap, 0x021c, ANA_PLL_TX_HS_CLK_EN,
			   FIELD_PREP(ANA_PLL_TX_HS_CLK_EN, 0x1));
	regmap_update_bits(hdptx->regmap, 0x0204,
			   ANA_PLL_CD_HSCLK_EAST_EN | ANA_PLL_CD_HSCLK_WEST_EN,
			   FIELD_PREP(ANA_PLL_CD_HSCLK_EAST_EN, 0x1) |
			   FIELD_PREP(ANA_PLL_CD_HSCLK_WEST_EN, 0x0));
	regmap_update_bits(hdptx->regmap, 0x0264, CMN_ROPLL_ALONE_MODE,
			   FIELD_PREP(CMN_ROPLL_ALONE_MODE, 0x1));
	regmap_update_bits(hdptx->regmap, 0x0208, ANA_PLL_CD_VREG_GAIN_CTRL,
			   FIELD_PREP(ANA_PLL_CD_VREG_GAIN_CTRL, 0x4));
	regmap_update_bits(hdptx->regmap, 0x00f0, ANA_LCPLL_RESERVED7,
			   FIELD_PREP(ANA_LCPLL_RESERVED7, 0x1));
	regmap_update_bits(hdptx->regmap, 0x020c, ANA_PLL_CD_VREG_ICTRL,
			   FIELD_PREP(ANA_PLL_CD_VREG_ICTRL, 0x1));
	regmap_update_bits(hdptx->regmap, 0x0214, ANA_PLL_SYNC_LOSS_DET_MODE,
			   FIELD_PREP(ANA_PLL_SYNC_LOSS_DET_MODE, 0x3));
	regmap_update_bits(hdptx->regmap, 0x0210, PLL_LCRO_CLK_SEL,
			   FIELD_PREP(PLL_LCRO_CLK_SEL, 0x1));
	regmap_update_bits(hdptx->regmap, 0x0268, HS_SPEED_SEL,
			   FIELD_PREP(HS_SPEED_SEL, 0x1));
	regmap_update_bits(hdptx->regmap, 0x026c, LS_SPEED_SEL,
			   FIELD_PREP(LS_SPEED_SEL, 0x1));
}

static int rockchip_hdptx_phy_dp_aux_init(struct rockchip_hdptx_phy *hdptx)
{
	u32 status;
	int ret;

	regmap_update_bits(hdptx->regmap, 0x0414, ANA_SB_TX_HLVL_PROG,
			   FIELD_PREP(ANA_SB_TX_HLVL_PROG, 0x7));
	regmap_update_bits(hdptx->regmap, 0x0418, ANA_SB_TX_LLVL_PROG,
			   FIELD_PREP(ANA_SB_TX_LLVL_PROG, 0x7));

	regmap_update_bits(hdptx->regmap, 0x044c,
			   SB_RX_RCAL_OPT_CODE | SB_RX_RTERM_CTRL,
			   FIELD_PREP(SB_RX_RCAL_OPT_CODE, 0x1) |
			   FIELD_PREP(SB_RX_RTERM_CTRL, 0x3));
	regmap_update_bits(hdptx->regmap, 0x0450,
			   SB_TG_SB_EN_DELAY_TIME | SB_TG_RXTERN_EN_DELAY_TIME,
			   FIELD_PREP(SB_TG_SB_EN_DELAY_TIME, 0x2) |
			   FIELD_PREP(SB_TG_RXTERN_EN_DELAY_TIME, 0x2));
	regmap_update_bits(hdptx->regmap, 0x0454,
			   SB_READY_DELAY_TIME | SB_TG_OSC_EN_DELAY_TIME,
			   FIELD_PREP(SB_READY_DELAY_TIME, 0x2) |
			   FIELD_PREP(SB_TG_OSC_EN_DELAY_TIME, 0x2));
	regmap_update_bits(hdptx->regmap, 0x0458,
			   SB_TG_OSC_EN_TO_AFC_RSTN_DELAT_TIME,
			   FIELD_PREP(SB_TG_OSC_EN_TO_AFC_RSTN_DELAT_TIME, 0x2));
	regmap_update_bits(hdptx->regmap, 0x045c,
			   SB_TG_PLL_CD_VREG_FAST_PULSE_TIME,
			   FIELD_PREP(SB_TG_PLL_CD_VREG_FAST_PULSE_TIME, 0x4));
	regmap_update_bits(hdptx->regmap, 0x0460,
			   SB_TG_EARC_DMRX_RECVRD_CLK_CNT,
			   FIELD_PREP(SB_TG_EARC_DMRX_RECVRD_CLK_CNT, 0xa));
	regmap_update_bits(hdptx->regmap, 0x0468, SB_TG_CNT_RUN_NO_7_0,
			   FIELD_PREP(SB_TG_CNT_RUN_NO_7_0, 0x3));
	regmap_update_bits(hdptx->regmap, 0x046c,
			   SB_EARC_SIG_DET_BYPASS | SB_AFC_TOL,
			   FIELD_PREP(SB_EARC_SIG_DET_BYPASS, 0x1) |
			   FIELD_PREP(SB_AFC_TOL, 0x3));
	regmap_update_bits(hdptx->regmap, 0x0470, SB_AFC_STB_NUM,
			   FIELD_PREP(SB_AFC_STB_NUM, 0x4));
	regmap_update_bits(hdptx->regmap, 0x0474, SB_TG_OSC_CNT_MIN,
			   FIELD_PREP(SB_TG_OSC_CNT_MIN, 0x67));
	regmap_update_bits(hdptx->regmap, 0x0478, SB_TG_OSC_CNT_MAX,
			   FIELD_PREP(SB_TG_OSC_CNT_MAX, 0x6a));
	regmap_update_bits(hdptx->regmap, 0x047c, SB_PWM_AFC_CTRL,
			   FIELD_PREP(SB_PWM_AFC_CTRL, 0x5));
	regmap_update_bits(hdptx->regmap, 0x0434, ANA_SB_DMRX_LPBK_DATA,
			   FIELD_PREP(ANA_SB_DMRX_LPBK_DATA, 0x1));
	regmap_update_bits(hdptx->regmap, 0x0440,
			   ANA_SB_VREG_OUT_SEL | ANA_SB_VREG_REF_SEL,
			   FIELD_PREP(ANA_SB_VREG_OUT_SEL, 0x1) |
			   FIELD_PREP(ANA_SB_VREG_REF_SEL, 0x1));
	regmap_update_bits(hdptx->regmap, 0x043c, ANA_SB_VREG_GAIN_CTRL,
			   FIELD_PREP(ANA_SB_VREG_GAIN_CTRL, 0x0));
	regmap_update_bits(hdptx->regmap, 0x0408, ANA_SB_RXTERM_OFFSP,
			   FIELD_PREP(ANA_SB_RXTERM_OFFSP, 0x3));
	regmap_update_bits(hdptx->regmap, 0x040c, ANA_SB_RXTERM_OFFSN,
			   FIELD_PREP(ANA_SB_RXTERM_OFFSN, 0x3));
	regmap_update_bits(hdptx->regmap, 0x047c, SB_RCAL_RSTN,
			   FIELD_PREP(SB_RCAL_RSTN, 0x1));
	regmap_update_bits(hdptx->regmap, 0x0410, SB_AUX_EN,
			   FIELD_PREP(SB_AUX_EN, 0x1));
	regmap_update_bits(hdptx->regmap, 0x0480, SB_AUX_EN_IN,
			   FIELD_PREP(SB_AUX_EN_IN, 0x1));
	regmap_update_bits(hdptx->regmap, 0x040c, OVRD_SB_RX_RESCAL_DONE,
			   FIELD_PREP(OVRD_SB_RX_RESCAL_DONE, 0x1));
	regmap_update_bits(hdptx->regmap, 0x0410, OVRD_SB_EN,
			   FIELD_PREP(OVRD_SB_EN, 0x1));
	regmap_update_bits(hdptx->regmap, 0x0408, OVRD_SB_RXTERM_EN,
			   FIELD_PREP(OVRD_SB_RXTERM_EN, 0x1));
	regmap_update_bits(hdptx->regmap, 0x043c, OVRD_SB_VREG_EN,
			   FIELD_PREP(OVRD_SB_VREG_EN, 0x1));
	regmap_update_bits(hdptx->regmap, 0x0410, OVRD_SB_AUX_EN,
			   FIELD_PREP(OVRD_SB_AUX_EN, 0x1));

	rockchip_grf_write(hdptx->grf, HDPTXPHY_GRF_CON0, BGR_EN,
			   FIELD_PREP(BGR_EN, 0x1));
	rockchip_grf_write(hdptx->grf, HDPTXPHY_GRF_CON0, BIAS_EN,
			   FIELD_PREP(BIAS_EN, 0x1));
	udelay(20);

	reset_control_deassert(hdptx->init_reset);
	udelay(20);
	reset_control_deassert(hdptx->cmn_reset);
	udelay(20);

	regmap_update_bits(hdptx->regmap, 0x040c, SB_RX_RESCAL_DONE,
			   FIELD_PREP(SB_RX_RESCAL_DONE, 0x1));
	udelay(100);
	regmap_update_bits(hdptx->regmap, 0x0410, SB_EN,
			   FIELD_PREP(SB_EN, 0x1));
	udelay(100);
	regmap_update_bits(hdptx->regmap, 0x0408, SB_RXRERM_EN,
			   FIELD_PREP(SB_RXRERM_EN, 0x1));
	udelay(20);
	regmap_update_bits(hdptx->regmap, 0x043c, SB_VREG_EN,
			   FIELD_PREP(SB_VREG_EN, 0x1));
	udelay(20);
	regmap_update_bits(hdptx->regmap, 0x0410, SB_AUX_EN,
			   FIELD_PREP(SB_AUX_EN, 0x1));
	udelay(100);

	ret = regmap_read_poll_timeout(hdptx->grf, HDPTXPHY_GRF_STATUS0,
				       status, FIELD_GET(SB_RDY, status),
				       50, 1000);
	if (ret) {
		dev_err(hdptx->dev, "timeout waiting for sb_rdy\n");
		return ret;
	}

	return 0;
}

static void rockchip_hdptx_phy_reset(struct rockchip_hdptx_phy *hdptx)
{
	u32 lane;

	reset_control_assert(hdptx->lane_reset);
	reset_control_assert(hdptx->cmn_reset);
	reset_control_assert(hdptx->init_reset);

	reset_control_assert(hdptx->apb_reset);
	udelay(10);
	reset_control_deassert(hdptx->apb_reset);

	for (lane = 0; lane < 4; lane++)
		regmap_update_bits(hdptx->regmap, LANE_REG(lane, 0x0c04),
				   OVRD_LN_TX_DRV_EI_EN | LN_TX_DRV_EI_EN,
				   FIELD_PREP(OVRD_LN_TX_DRV_EI_EN, 1) |
				   FIELD_PREP(LN_TX_DRV_EI_EN, 0));

	rockchip_grf_write(hdptx->grf, HDPTXPHY_GRF_CON0, PLL_EN,
			   FIELD_PREP(PLL_EN, 0));
	rockchip_grf_write(hdptx->grf, HDPTXPHY_GRF_CON0, BIAS_EN,
			   FIELD_PREP(BIAS_EN, 0));
	rockchip_grf_write(hdptx->grf, HDPTXPHY_GRF_CON0, BGR_EN,
			   FIELD_PREP(BGR_EN, 0));
}

static bool rockchip_hdptx_phy_enabled(struct rockchip_hdptx_phy *hdptx)
{
	u32 status;

	regmap_read(hdptx->grf, HDPTXPHY_GRF_STATUS0, &status);

	return FIELD_GET(SB_RDY, status);
}

static int rockchip_hdptx_phy_power_on(struct phy *phy)
{
	struct rockchip_hdptx_phy *hdptx = phy_get_drvdata(phy);
	enum phy_mode mode = phy_get_mode(phy);
	u32 lane;
	int ret;

	ret = clk_bulk_prepare_enable(hdptx->nr_clks, hdptx->clks);
	if (ret)
		return ret;

	if (rockchip_hdptx_phy_enabled(hdptx))
		return 0;

	rockchip_hdptx_phy_reset(hdptx);

	for (lane = 0; lane < 4; lane++) {
		u32 invert = hdptx->lane_polarity_invert[lane];

		regmap_update_bits(hdptx->regmap, LANE_REG(lane, 0x0c78),
				   LN_POLARITY_INV,
				   FIELD_PREP(LN_POLARITY_INV, invert));
	}

	if (mode == PHY_MODE_DP) {
		rockchip_grf_write(hdptx->grf, HDPTXPHY_GRF_CON0,
				   HDPTX_MODE_SEL,
				   FIELD_PREP(HDPTX_MODE_SEL, 0x1));

		regmap_update_bits(hdptx->regmap, 0x0800, PROTOCOL_SEL,
				   FIELD_PREP(PROTOCOL_SEL, 0x0));
		regmap_update_bits(hdptx->regmap, 0x0818, DATA_BUS_WIDTH,
				   FIELD_PREP(DATA_BUS_WIDTH, 0x1));
		regmap_update_bits(hdptx->regmap, 0x0818, BUS_WIDTH_SEL,
				   FIELD_PREP(BUS_WIDTH_SEL, 0x0));

		rockchip_hdptx_phy_dp_pll_init(hdptx);
		rockchip_hdptx_phy_dp_aux_init(hdptx);
	} else {
		rockchip_grf_write(hdptx->grf, HDPTXPHY_GRF_CON0,
				   HDPTX_MODE_SEL,
				   FIELD_PREP(HDPTX_MODE_SEL, 0x0));

		regmap_update_bits(hdptx->regmap, 0x0800, PROTOCOL_SEL,
				   FIELD_PREP(PROTOCOL_SEL, 0x1));
	}

	return 0;
}

static int rockchip_hdptx_phy_power_off(struct phy *phy)
{
	struct rockchip_hdptx_phy *hdptx = phy_get_drvdata(phy);

	rockchip_hdptx_phy_reset(hdptx);

	clk_bulk_disable_unprepare(hdptx->nr_clks, hdptx->clks);

	return 0;
}

static const struct phy_ops rockchip_hdptx_phy_ops = {
	.set_mode	= rockchip_hdptx_phy_set_mode,
	.configure	= rockchip_hdptx_phy_configure,
	.power_on	= rockchip_hdptx_phy_power_on,
	.power_off	= rockchip_hdptx_phy_power_off,
	.owner		= THIS_MODULE,
};

static bool rockchip_hdptx_phy_is_accissible_reg(struct device *dev,
						 unsigned int reg)
{
	switch (reg) {
	case 0x0000 ... 0x029c:	/* CMN Register */
	case 0x0400 ... 0x04a4:	/* Sideband Register */
	case 0x0800 ... 0x08a4:	/* Lane Top Register */
	case 0x0c00 ... 0x0cb4:	/* Lane 0 Register */
	case 0x1000 ... 0x10b4:	/* Lane 1 Register */
	case 0x1400 ... 0x14b4:	/* Lane 2 Register */
	case 0x1800 ... 0x18b4:	/* Lane 3 Register */
		return true;
	default:
		return false;
	}
}

static const struct regmap_config rockchip_hdptx_phy_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.max_register = 0x18b4,
	.readable_reg = rockchip_hdptx_phy_is_accissible_reg,
	.writeable_reg = rockchip_hdptx_phy_is_accissible_reg,
};

static int rockchip_hdptx_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_hdptx_phy *hdptx;
	struct phy *phy;
	struct phy_provider *phy_provider;
	void __iomem *regs;
	int ret;

	hdptx = devm_kzalloc(dev, sizeof(*hdptx), GFP_KERNEL);
	if (!hdptx)
		return -ENOMEM;

	hdptx->dev = dev;
	platform_set_drvdata(pdev, hdptx);

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	hdptx->regmap = devm_regmap_init_mmio(dev, regs,
					&rockchip_hdptx_phy_regmap_config);
	if (IS_ERR(hdptx->regmap))
		return dev_err_probe(dev, PTR_ERR(hdptx->regmap),
				     "failed to create regmap\n");

	ret = devm_clk_bulk_get_all(dev, &hdptx->clks);
	if (ret < 1)
		return dev_err_probe(dev, ret, "failed to get clocks\n");

	hdptx->nr_clks = ret;

	hdptx->apb_reset = devm_reset_control_get(dev, "apb");
	if (IS_ERR(hdptx->apb_reset))
		return dev_err_probe(dev, PTR_ERR(hdptx->apb_reset),
				     "failed to get apb reset\n");

	hdptx->init_reset = devm_reset_control_get(dev, "init");
	if (IS_ERR(hdptx->init_reset))
		return dev_err_probe(dev, PTR_ERR(hdptx->init_reset),
				     "failed to get init reset\n");

	hdptx->cmn_reset = devm_reset_control_get(dev, "cmn");
	if (IS_ERR(hdptx->cmn_reset))
		return dev_err_probe(dev, PTR_ERR(hdptx->cmn_reset),
				     "failed to get cmn reset\n");

	hdptx->lane_reset = devm_reset_control_get(dev, "lane");
	if (IS_ERR(hdptx->lane_reset))
		return dev_err_probe(dev, PTR_ERR(hdptx->lane_reset),
				     "failed to get lane reset\n");

	hdptx->grf = syscon_regmap_lookup_by_phandle(dev->of_node,
						     "rockchip,grf");
	if (IS_ERR(hdptx->grf))
		return dev_err_probe(dev, PTR_ERR(hdptx->grf),
				     "failed to get grf regmap\n");

	device_property_read_u32_array(dev, "lane-polarity-invert",
				       hdptx->lane_polarity_invert, 4);

	ret = rockchip_hdptx_phy_parse_training_table(dev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to parse training table\n");

	phy = devm_phy_create(dev, NULL, &rockchip_hdptx_phy_ops);
	if (IS_ERR(phy))
		return dev_err_probe(dev, PTR_ERR(phy), "failed to create PHY\n");

	phy_set_drvdata(phy, hdptx);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id rockchip_hdptx_phy_of_match[] = {
	{ .compatible = "rockchip,rk3588-hdptx-phy", },
	{}
};
MODULE_DEVICE_TABLE(of, rockchip_hdptx_phy_of_match);

static struct platform_driver rockchip_hdptx_phy_driver = {
	.probe	= rockchip_hdptx_phy_probe,
	.driver = {
		.name = "rockchip-hdptx-phy",
		.of_match_table	= rockchip_hdptx_phy_of_match,
	}
};
module_platform_driver(rockchip_hdptx_phy_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip HDMI/DP Combo PHY with Samsung IP block");
MODULE_LICENSE("GPL v2");
