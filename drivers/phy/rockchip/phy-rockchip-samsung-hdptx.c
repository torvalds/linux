// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2021-2022 Rockchip Electronics Co., Ltd.
 * Copyright (c) 2024 Collabora Ltd.
 *
 * Author: Algea Cao <algea.cao@rock-chips.com>
 * Author: Cristian Ciocaltea <cristian.ciocaltea@collabora.com>
 */
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/rational.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#define GRF_HDPTX_CON0			0x00
#define HDPTX_I_PLL_EN			BIT(7)
#define HDPTX_I_BIAS_EN			BIT(6)
#define HDPTX_I_BGR_EN			BIT(5)
#define GRF_HDPTX_STATUS		0x80
#define HDPTX_O_PLL_LOCK_DONE		BIT(3)
#define HDPTX_O_PHY_CLK_RDY		BIT(2)
#define HDPTX_O_PHY_RDY			BIT(1)
#define HDPTX_O_SB_RDY			BIT(0)

#define HDTPX_REG(_n, _min, _max)				\
	(							\
		BUILD_BUG_ON_ZERO((0x##_n) < (0x##_min)) +	\
		BUILD_BUG_ON_ZERO((0x##_n) > (0x##_max)) +	\
		((0x##_n) * 4)					\
	)

#define CMN_REG(n)			HDTPX_REG(n, 0000, 00a7)
#define SB_REG(n)			HDTPX_REG(n, 0100, 0129)
#define LNTOP_REG(n)			HDTPX_REG(n, 0200, 0229)
#define LANE_REG(n)			HDTPX_REG(n, 0300, 062d)

/* CMN_REG(0008) */
#define LCPLL_EN_MASK			BIT(6)
#define LCPLL_LCVCO_MODE_EN_MASK	BIT(4)
/* CMN_REG(001e) */
#define LCPLL_PI_EN_MASK		BIT(5)
#define LCPLL_100M_CLK_EN_MASK		BIT(0)
/* CMN_REG(0025) */
#define LCPLL_PMS_IQDIV_RSTN		BIT(4)
/* CMN_REG(0028) */
#define LCPLL_SDC_FRAC_EN		BIT(2)
#define LCPLL_SDC_FRAC_RSTN		BIT(0)
/* CMN_REG(002d) */
#define LCPLL_SDC_N_MASK		GENMASK(3, 1)
/* CMN_REG(002e) */
#define LCPLL_SDC_NUMBERATOR_MASK	GENMASK(5, 0)
/* CMN_REG(002f) */
#define LCPLL_SDC_DENOMINATOR_MASK	GENMASK(7, 2)
#define LCPLL_SDC_NDIV_RSTN		BIT(0)
/* CMN_REG(003d) */
#define ROPLL_LCVCO_EN			BIT(4)
/* CMN_REG(004e) */
#define ROPLL_PI_EN			BIT(5)
/* CMN_REG(005c) */
#define ROPLL_PMS_IQDIV_RSTN		BIT(5)
/* CMN_REG(005e) */
#define ROPLL_SDM_EN_MASK		BIT(6)
#define ROPLL_SDM_FRAC_EN_RBR		BIT(3)
#define ROPLL_SDM_FRAC_EN_HBR		BIT(2)
#define ROPLL_SDM_FRAC_EN_HBR2		BIT(1)
#define ROPLL_SDM_FRAC_EN_HBR3		BIT(0)
/* CMN_REG(0064) */
#define ROPLL_SDM_NUM_SIGN_RBR_MASK	BIT(3)
/* CMN_REG(0069) */
#define ROPLL_SDC_N_RBR_MASK		GENMASK(2, 0)
/* CMN_REG(0074) */
#define ROPLL_SDC_NDIV_RSTN		BIT(2)
#define ROPLL_SSC_EN			BIT(0)
/* CMN_REG(0081) */
#define OVRD_PLL_CD_CLK_EN		BIT(8)
#define PLL_CD_HSCLK_EAST_EN		BIT(0)
/* CMN_REG(0086) */
#define PLL_PCG_POSTDIV_SEL_MASK	GENMASK(7, 4)
#define PLL_PCG_CLK_SEL_MASK		GENMASK(3, 1)
#define PLL_PCG_CLK_EN			BIT(0)
/* CMN_REG(0087) */
#define PLL_FRL_MODE_EN			BIT(3)
#define PLL_TX_HS_CLK_EN		BIT(2)
/* CMN_REG(0089) */
#define LCPLL_ALONE_MODE		BIT(1)
/* CMN_REG(0097) */
#define DIG_CLK_SEL			BIT(1)
#define ROPLL_REF			BIT(1)
#define LCPLL_REF			0
/* CMN_REG(0099) */
#define CMN_ROPLL_ALONE_MODE		BIT(2)
#define ROPLL_ALONE_MODE		BIT(2)
/* CMN_REG(009a) */
#define HS_SPEED_SEL			BIT(0)
#define DIV_10_CLOCK			BIT(0)
/* CMN_REG(009b) */
#define IS_SPEED_SEL			BIT(4)
#define LINK_SYMBOL_CLOCK		BIT(4)
#define LINK_SYMBOL_CLOCK1_2		0

/* SB_REG(0102) */
#define OVRD_SB_RXTERM_EN_MASK		BIT(5)
#define SB_RXTERM_EN_MASK		BIT(4)
#define ANA_SB_RXTERM_OFFSP_MASK	GENMASK(3, 0)
/* SB_REG(0103) */
#define ANA_SB_RXTERM_OFFSN_MASK	GENMASK(6, 3)
#define OVRD_SB_RX_RESCAL_DONE_MASK	BIT(1)
#define SB_RX_RESCAL_DONE_MASK		BIT(0)
/* SB_REG(0104) */
#define OVRD_SB_EN_MASK			BIT(5)
#define SB_EN_MASK			BIT(4)
/* SB_REG(0105) */
#define OVRD_SB_EARC_CMDC_EN_MASK	BIT(6)
#define SB_EARC_CMDC_EN_MASK		BIT(5)
#define ANA_SB_TX_HLVL_PROG_MASK	GENMASK(2, 0)
/* SB_REG(0106) */
#define ANA_SB_TX_LLVL_PROG_MASK	GENMASK(6, 4)
/* SB_REG(0109) */
#define ANA_SB_DMRX_AFC_DIV_RATIO_MASK	GENMASK(2, 0)
/* SB_REG(010f) */
#define OVRD_SB_VREG_EN_MASK		BIT(7)
#define SB_VREG_EN_MASK			BIT(6)
#define OVRD_SB_VREG_LPF_BYPASS_MASK	BIT(5)
#define SB_VREG_LPF_BYPASS_MASK		BIT(4)
#define ANA_SB_VREG_GAIN_CTRL_MASK	GENMASK(3, 0)
/* SB_REG(0110) */
#define ANA_SB_VREG_REF_SEL_MASK	BIT(0)
/* SB_REG(0113) */
#define SB_RX_RCAL_OPT_CODE_MASK	GENMASK(5, 4)
#define SB_RX_RTERM_CTRL_MASK		GENMASK(3, 0)
/* SB_REG(0114) */
#define SB_TG_SB_EN_DELAY_TIME_MASK	GENMASK(5, 3)
#define SB_TG_RXTERM_EN_DELAY_TIME_MASK	GENMASK(2, 0)
/* SB_REG(0115) */
#define SB_READY_DELAY_TIME_MASK	GENMASK(5, 3)
#define SB_TG_OSC_EN_DELAY_TIME_MASK	GENMASK(2, 0)
/* SB_REG(0116) */
#define AFC_RSTN_DELAY_TIME_MASK	GENMASK(6, 4)
/* SB_REG(0117) */
#define FAST_PULSE_TIME_MASK		GENMASK(3, 0)
/* SB_REG(011b) */
#define SB_EARC_SIG_DET_BYPASS_MASK	BIT(4)
#define SB_AFC_TOL_MASK			GENMASK(3, 0)
/* SB_REG(011f) */
#define SB_PWM_AFC_CTRL_MASK		GENMASK(7, 2)
#define SB_RCAL_RSTN_MASK		BIT(1)
/* SB_REG(0120) */
#define SB_EARC_EN_MASK			BIT(1)
#define SB_EARC_AFC_EN_MASK		BIT(2)
/* SB_REG(0123) */
#define OVRD_SB_READY_MASK		BIT(5)
#define SB_READY_MASK			BIT(4)

/* LNTOP_REG(0200) */
#define PROTOCOL_SEL			BIT(2)
#define HDMI_MODE			BIT(2)
#define HDMI_TMDS_FRL_SEL		BIT(1)
/* LNTOP_REG(0206) */
#define DATA_BUS_SEL			BIT(0)
#define DATA_BUS_36_40			BIT(0)
/* LNTOP_REG(0207) */
#define LANE_EN				0xf
#define ALL_LANE_EN			0xf

/* LANE_REG(0312) */
#define LN0_TX_SER_RATE_SEL_RBR		BIT(5)
#define LN0_TX_SER_RATE_SEL_HBR		BIT(4)
#define LN0_TX_SER_RATE_SEL_HBR2	BIT(3)
#define LN0_TX_SER_RATE_SEL_HBR3	BIT(2)
/* LANE_REG(0412) */
#define LN1_TX_SER_RATE_SEL_RBR		BIT(5)
#define LN1_TX_SER_RATE_SEL_HBR		BIT(4)
#define LN1_TX_SER_RATE_SEL_HBR2	BIT(3)
#define LN1_TX_SER_RATE_SEL_HBR3	BIT(2)
/* LANE_REG(0512) */
#define LN2_TX_SER_RATE_SEL_RBR		BIT(5)
#define LN2_TX_SER_RATE_SEL_HBR		BIT(4)
#define LN2_TX_SER_RATE_SEL_HBR2	BIT(3)
#define LN2_TX_SER_RATE_SEL_HBR3	BIT(2)
/* LANE_REG(0612) */
#define LN3_TX_SER_RATE_SEL_RBR		BIT(5)
#define LN3_TX_SER_RATE_SEL_HBR		BIT(4)
#define LN3_TX_SER_RATE_SEL_HBR2	BIT(3)
#define LN3_TX_SER_RATE_SEL_HBR3	BIT(2)

struct lcpll_config {
	u32 bit_rate;
	u8 lcvco_mode_en;
	u8 pi_en;
	u8 clk_en_100m;
	u8 pms_mdiv;
	u8 pms_mdiv_afc;
	u8 pms_pdiv;
	u8 pms_refdiv;
	u8 pms_sdiv;
	u8 pi_cdiv_rstn;
	u8 pi_cdiv_sel;
	u8 sdm_en;
	u8 sdm_rstn;
	u8 sdc_frac_en;
	u8 sdc_rstn;
	u8 sdm_deno;
	u8 sdm_num_sign;
	u8 sdm_num;
	u8 sdc_n;
	u8 sdc_n2;
	u8 sdc_num;
	u8 sdc_deno;
	u8 sdc_ndiv_rstn;
	u8 ssc_en;
	u8 ssc_fm_dev;
	u8 ssc_fm_freq;
	u8 ssc_clk_div_sel;
	u8 cd_tx_ser_rate_sel;
};

struct ropll_config {
	u32 bit_rate;
	u8 pms_mdiv;
	u8 pms_mdiv_afc;
	u8 pms_pdiv;
	u8 pms_refdiv;
	u8 pms_sdiv;
	u8 pms_iqdiv_rstn;
	u8 ref_clk_sel;
	u8 sdm_en;
	u8 sdm_rstn;
	u8 sdc_frac_en;
	u8 sdc_rstn;
	u8 sdm_clk_div;
	u8 sdm_deno;
	u8 sdm_num_sign;
	u8 sdm_num;
	u8 sdc_n;
	u8 sdc_num;
	u8 sdc_deno;
	u8 sdc_ndiv_rstn;
	u8 ssc_en;
	u8 ssc_fm_dev;
	u8 ssc_fm_freq;
	u8 ssc_clk_div_sel;
	u8 ana_cpp_ctrl;
	u8 ana_lpf_c_sel;
	u8 cd_tx_ser_rate_sel;
};

enum rk_hdptx_reset {
	RST_PHY = 0,
	RST_APB,
	RST_INIT,
	RST_CMN,
	RST_LANE,
	RST_ROPLL,
	RST_LCPLL,
	RST_MAX
};

struct rk_hdptx_phy {
	struct device *dev;
	struct regmap *regmap;
	struct regmap *grf;

	struct phy *phy;
	struct phy_config *phy_cfg;
	struct clk_bulk_data *clks;
	int nr_clks;
	struct reset_control_bulk_data rsts[RST_MAX];
};

static const struct ropll_config ropll_tmds_cfg[] = {
	{ 5940000, 124, 124, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 62, 1, 16, 5, 0,
	  1, 1, 0, 0x20, 0x0c, 1, 0x0e, 0, 0, },
	{ 3712500, 155, 155, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 62, 1, 16, 5, 0,
	  1, 1, 0, 0x20, 0x0c, 1, 0x0e, 0, 0, },
	{ 2970000, 124, 124, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 62, 1, 16, 5, 0,
	  1, 1, 0, 0x20, 0x0c, 1, 0x0e, 0, 0, },
	{ 1620000, 135, 135, 1, 1, 3, 1, 1, 0, 1, 1, 1, 1, 4, 0, 3, 5, 5, 0x10,
	  1, 0, 0x20, 0x0c, 1, 0x0e, 0, 0, },
	{ 1856250, 155, 155, 1, 1, 3, 1, 1, 1, 1, 1, 1, 1, 62, 1, 16, 5, 0,
	  1, 1, 0, 0x20, 0x0c, 1, 0x0e, 0, 0, },
	{ 1540000, 193, 193, 1, 1, 5, 1, 1, 1, 1, 1, 1, 1, 193, 1, 32, 2, 1,
	  1, 1, 0, 0x20, 0x0c, 1, 0x0e, 0, 0, },
	{ 1485000, 0x7b, 0x7b, 1, 1, 3, 1, 1, 1, 1, 1, 1, 1, 4, 0, 3, 5, 5,
	  0x10, 1, 0, 0x20, 0x0c, 1, 0x0e, 0, 0, },
	{ 1462500, 122, 122, 1, 1, 3, 1, 1, 1, 1, 1, 1, 1, 244, 1, 16, 2, 1, 1,
	  1, 0, 0x20, 0x0c, 1, 0x0e, 0, 0, },
	{ 1190000, 149, 149, 1, 1, 5, 1, 1, 1, 1, 1, 1, 1, 149, 1, 16, 2, 1, 1,
	  1, 0, 0x20, 0x0c, 1, 0x0e, 0, 0, },
	{ 1065000, 89, 89, 1, 1, 3, 1, 1, 1, 1, 1, 1, 1, 89, 1, 16, 1, 0, 1,
	  1, 0, 0x20, 0x0c, 1, 0x0e, 0, 0, },
	{ 1080000, 135, 135, 1, 1, 5, 1, 1, 0, 1, 0, 1, 1, 0x9, 0, 0x05, 0,
	  0x14, 0x18, 1, 0, 0x20, 0x0c, 1, 0x0e, 0, 0, },
	{ 855000, 214, 214, 1, 1, 11, 1, 1, 1, 1, 1, 1, 1, 214, 1, 16, 2, 1,
	  1, 1, 0, 0x20, 0x0c, 1, 0x0e, 0, 0, },
	{ 835000, 105, 105, 1, 1, 5, 1, 1, 1, 1, 1, 1, 1, 42, 1, 16, 1, 0,
	  1, 1, 0, 0x20, 0x0c, 1, 0x0e, 0, 0, },
	{ 928125, 155, 155, 1, 1, 7, 1, 1, 1, 1, 1, 1, 1, 62, 1, 16, 5, 0,
	  1, 1, 0, 0x20, 0x0c, 1, 0x0e, 0, 0, },
	{ 742500, 124, 124, 1, 1, 7, 1, 1, 1, 1, 1, 1, 1, 62, 1, 16, 5, 0,
	  1, 1, 0, 0x20, 0x0c, 1, 0x0e, 0, 0, },
	{ 650000, 162, 162, 1, 1, 11, 1, 1, 1, 1, 1, 1, 1, 54, 0, 16, 4, 1,
	  1, 1, 0, 0x20, 0x0c, 1, 0x0e, 0, 0, },
	{ 337500, 0x70, 0x70, 1, 1, 0xf, 1, 1, 1, 1, 1, 1, 1, 0x2, 0, 0x01, 5,
	  1, 1, 1, 0, 0x20, 0x0c, 1, 0x0e, 0, 0, },
	{ 400000, 100, 100, 1, 1, 11, 1, 1, 0, 1, 0, 1, 1, 0x9, 0, 0x05, 0,
	  0x14, 0x18, 1, 0, 0x20, 0x0c, 1, 0x0e, 0, 0, },
	{ 270000, 0x5a, 0x5a, 1, 1, 0xf, 1, 1, 0, 1, 0, 1, 1, 0x9, 0, 0x05, 0,
	  0x14, 0x18, 1, 0, 0x20, 0x0c, 1, 0x0e, 0, 0, },
	{ 251750, 84, 84, 1, 1, 0xf, 1, 1, 1, 1, 1, 1, 1, 168, 1, 16, 4, 1, 1,
	  1, 0, 0x20, 0x0c, 1, 0x0e, 0, 0, },
};

static const struct reg_sequence rk_hdtpx_common_cmn_init_seq[] = {
	REG_SEQ0(CMN_REG(0009), 0x0c),
	REG_SEQ0(CMN_REG(000a), 0x83),
	REG_SEQ0(CMN_REG(000b), 0x06),
	REG_SEQ0(CMN_REG(000c), 0x20),
	REG_SEQ0(CMN_REG(000d), 0xb8),
	REG_SEQ0(CMN_REG(000e), 0x0f),
	REG_SEQ0(CMN_REG(000f), 0x0f),
	REG_SEQ0(CMN_REG(0010), 0x04),
	REG_SEQ0(CMN_REG(0011), 0x00),
	REG_SEQ0(CMN_REG(0012), 0x26),
	REG_SEQ0(CMN_REG(0013), 0x22),
	REG_SEQ0(CMN_REG(0014), 0x24),
	REG_SEQ0(CMN_REG(0015), 0x77),
	REG_SEQ0(CMN_REG(0016), 0x08),
	REG_SEQ0(CMN_REG(0017), 0x00),
	REG_SEQ0(CMN_REG(0018), 0x04),
	REG_SEQ0(CMN_REG(0019), 0x48),
	REG_SEQ0(CMN_REG(001a), 0x01),
	REG_SEQ0(CMN_REG(001b), 0x00),
	REG_SEQ0(CMN_REG(001c), 0x01),
	REG_SEQ0(CMN_REG(001d), 0x64),
	REG_SEQ0(CMN_REG(001f), 0x00),
	REG_SEQ0(CMN_REG(0026), 0x53),
	REG_SEQ0(CMN_REG(0029), 0x01),
	REG_SEQ0(CMN_REG(0030), 0x00),
	REG_SEQ0(CMN_REG(0031), 0x20),
	REG_SEQ0(CMN_REG(0032), 0x30),
	REG_SEQ0(CMN_REG(0033), 0x0b),
	REG_SEQ0(CMN_REG(0034), 0x23),
	REG_SEQ0(CMN_REG(0035), 0x00),
	REG_SEQ0(CMN_REG(0038), 0x00),
	REG_SEQ0(CMN_REG(0039), 0x00),
	REG_SEQ0(CMN_REG(003a), 0x00),
	REG_SEQ0(CMN_REG(003b), 0x00),
	REG_SEQ0(CMN_REG(003c), 0x80),
	REG_SEQ0(CMN_REG(003e), 0x0c),
	REG_SEQ0(CMN_REG(003f), 0x83),
	REG_SEQ0(CMN_REG(0040), 0x06),
	REG_SEQ0(CMN_REG(0041), 0x20),
	REG_SEQ0(CMN_REG(0042), 0xb8),
	REG_SEQ0(CMN_REG(0043), 0x00),
	REG_SEQ0(CMN_REG(0044), 0x46),
	REG_SEQ0(CMN_REG(0045), 0x24),
	REG_SEQ0(CMN_REG(0046), 0xff),
	REG_SEQ0(CMN_REG(0047), 0x00),
	REG_SEQ0(CMN_REG(0048), 0x44),
	REG_SEQ0(CMN_REG(0049), 0xfa),
	REG_SEQ0(CMN_REG(004a), 0x08),
	REG_SEQ0(CMN_REG(004b), 0x00),
	REG_SEQ0(CMN_REG(004c), 0x01),
	REG_SEQ0(CMN_REG(004d), 0x64),
	REG_SEQ0(CMN_REG(004e), 0x14),
	REG_SEQ0(CMN_REG(004f), 0x00),
	REG_SEQ0(CMN_REG(0050), 0x00),
	REG_SEQ0(CMN_REG(005d), 0x0c),
	REG_SEQ0(CMN_REG(005f), 0x01),
	REG_SEQ0(CMN_REG(006b), 0x04),
	REG_SEQ0(CMN_REG(0073), 0x30),
	REG_SEQ0(CMN_REG(0074), 0x00),
	REG_SEQ0(CMN_REG(0075), 0x20),
	REG_SEQ0(CMN_REG(0076), 0x30),
	REG_SEQ0(CMN_REG(0077), 0x08),
	REG_SEQ0(CMN_REG(0078), 0x0c),
	REG_SEQ0(CMN_REG(0079), 0x00),
	REG_SEQ0(CMN_REG(007b), 0x00),
	REG_SEQ0(CMN_REG(007c), 0x00),
	REG_SEQ0(CMN_REG(007d), 0x00),
	REG_SEQ0(CMN_REG(007e), 0x00),
	REG_SEQ0(CMN_REG(007f), 0x00),
	REG_SEQ0(CMN_REG(0080), 0x00),
	REG_SEQ0(CMN_REG(0081), 0x09),
	REG_SEQ0(CMN_REG(0082), 0x04),
	REG_SEQ0(CMN_REG(0083), 0x24),
	REG_SEQ0(CMN_REG(0084), 0x20),
	REG_SEQ0(CMN_REG(0085), 0x03),
	REG_SEQ0(CMN_REG(0086), 0x01),
	REG_SEQ0(CMN_REG(0087), 0x0c),
	REG_SEQ0(CMN_REG(008a), 0x55),
	REG_SEQ0(CMN_REG(008b), 0x25),
	REG_SEQ0(CMN_REG(008c), 0x2c),
	REG_SEQ0(CMN_REG(008d), 0x22),
	REG_SEQ0(CMN_REG(008e), 0x14),
	REG_SEQ0(CMN_REG(008f), 0x20),
	REG_SEQ0(CMN_REG(0090), 0x00),
	REG_SEQ0(CMN_REG(0091), 0x00),
	REG_SEQ0(CMN_REG(0092), 0x00),
	REG_SEQ0(CMN_REG(0093), 0x00),
	REG_SEQ0(CMN_REG(009a), 0x11),
	REG_SEQ0(CMN_REG(009b), 0x10),
};

static const struct reg_sequence rk_hdtpx_tmds_cmn_init_seq[] = {
	REG_SEQ0(CMN_REG(0008), 0x00),
	REG_SEQ0(CMN_REG(0011), 0x01),
	REG_SEQ0(CMN_REG(0017), 0x20),
	REG_SEQ0(CMN_REG(001e), 0x14),
	REG_SEQ0(CMN_REG(0020), 0x00),
	REG_SEQ0(CMN_REG(0021), 0x00),
	REG_SEQ0(CMN_REG(0022), 0x11),
	REG_SEQ0(CMN_REG(0023), 0x00),
	REG_SEQ0(CMN_REG(0024), 0x00),
	REG_SEQ0(CMN_REG(0025), 0x53),
	REG_SEQ0(CMN_REG(0026), 0x00),
	REG_SEQ0(CMN_REG(0027), 0x00),
	REG_SEQ0(CMN_REG(0028), 0x01),
	REG_SEQ0(CMN_REG(002a), 0x00),
	REG_SEQ0(CMN_REG(002b), 0x00),
	REG_SEQ0(CMN_REG(002c), 0x00),
	REG_SEQ0(CMN_REG(002d), 0x00),
	REG_SEQ0(CMN_REG(002e), 0x04),
	REG_SEQ0(CMN_REG(002f), 0x00),
	REG_SEQ0(CMN_REG(0030), 0x20),
	REG_SEQ0(CMN_REG(0031), 0x30),
	REG_SEQ0(CMN_REG(0032), 0x0b),
	REG_SEQ0(CMN_REG(0033), 0x23),
	REG_SEQ0(CMN_REG(0034), 0x00),
	REG_SEQ0(CMN_REG(003d), 0x40),
	REG_SEQ0(CMN_REG(0042), 0x78),
	REG_SEQ0(CMN_REG(004e), 0x34),
	REG_SEQ0(CMN_REG(005c), 0x25),
	REG_SEQ0(CMN_REG(005e), 0x4f),
	REG_SEQ0(CMN_REG(0074), 0x04),
	REG_SEQ0(CMN_REG(0081), 0x01),
	REG_SEQ0(CMN_REG(0087), 0x04),
	REG_SEQ0(CMN_REG(0089), 0x00),
	REG_SEQ0(CMN_REG(0095), 0x00),
	REG_SEQ0(CMN_REG(0097), 0x02),
	REG_SEQ0(CMN_REG(0099), 0x04),
	REG_SEQ0(CMN_REG(009b), 0x00),
};

static const struct reg_sequence rk_hdtpx_common_sb_init_seq[] = {
	REG_SEQ0(SB_REG(0114), 0x00),
	REG_SEQ0(SB_REG(0115), 0x00),
	REG_SEQ0(SB_REG(0116), 0x00),
	REG_SEQ0(SB_REG(0117), 0x00),
};

static const struct reg_sequence rk_hdtpx_tmds_lntop_highbr_seq[] = {
	REG_SEQ0(LNTOP_REG(0201), 0x00),
	REG_SEQ0(LNTOP_REG(0202), 0x00),
	REG_SEQ0(LNTOP_REG(0203), 0x0f),
	REG_SEQ0(LNTOP_REG(0204), 0xff),
	REG_SEQ0(LNTOP_REG(0205), 0xff),
};

static const struct reg_sequence rk_hdtpx_tmds_lntop_lowbr_seq[] = {
	REG_SEQ0(LNTOP_REG(0201), 0x07),
	REG_SEQ0(LNTOP_REG(0202), 0xc1),
	REG_SEQ0(LNTOP_REG(0203), 0xf0),
	REG_SEQ0(LNTOP_REG(0204), 0x7c),
	REG_SEQ0(LNTOP_REG(0205), 0x1f),
};

static const struct reg_sequence rk_hdtpx_common_lane_init_seq[] = {
	REG_SEQ0(LANE_REG(0303), 0x0c),
	REG_SEQ0(LANE_REG(0307), 0x20),
	REG_SEQ0(LANE_REG(030a), 0x17),
	REG_SEQ0(LANE_REG(030b), 0x77),
	REG_SEQ0(LANE_REG(030c), 0x77),
	REG_SEQ0(LANE_REG(030d), 0x77),
	REG_SEQ0(LANE_REG(030e), 0x38),
	REG_SEQ0(LANE_REG(0310), 0x03),
	REG_SEQ0(LANE_REG(0311), 0x0f),
	REG_SEQ0(LANE_REG(0316), 0x02),
	REG_SEQ0(LANE_REG(031b), 0x01),
	REG_SEQ0(LANE_REG(031f), 0x15),
	REG_SEQ0(LANE_REG(0320), 0xa0),
	REG_SEQ0(LANE_REG(0403), 0x0c),
	REG_SEQ0(LANE_REG(0407), 0x20),
	REG_SEQ0(LANE_REG(040a), 0x17),
	REG_SEQ0(LANE_REG(040b), 0x77),
	REG_SEQ0(LANE_REG(040c), 0x77),
	REG_SEQ0(LANE_REG(040d), 0x77),
	REG_SEQ0(LANE_REG(040e), 0x38),
	REG_SEQ0(LANE_REG(0410), 0x03),
	REG_SEQ0(LANE_REG(0411), 0x0f),
	REG_SEQ0(LANE_REG(0416), 0x02),
	REG_SEQ0(LANE_REG(041b), 0x01),
	REG_SEQ0(LANE_REG(041f), 0x15),
	REG_SEQ0(LANE_REG(0420), 0xa0),
	REG_SEQ0(LANE_REG(0503), 0x0c),
	REG_SEQ0(LANE_REG(0507), 0x20),
	REG_SEQ0(LANE_REG(050a), 0x17),
	REG_SEQ0(LANE_REG(050b), 0x77),
	REG_SEQ0(LANE_REG(050c), 0x77),
	REG_SEQ0(LANE_REG(050d), 0x77),
	REG_SEQ0(LANE_REG(050e), 0x38),
	REG_SEQ0(LANE_REG(0510), 0x03),
	REG_SEQ0(LANE_REG(0511), 0x0f),
	REG_SEQ0(LANE_REG(0516), 0x02),
	REG_SEQ0(LANE_REG(051b), 0x01),
	REG_SEQ0(LANE_REG(051f), 0x15),
	REG_SEQ0(LANE_REG(0520), 0xa0),
	REG_SEQ0(LANE_REG(0603), 0x0c),
	REG_SEQ0(LANE_REG(0607), 0x20),
	REG_SEQ0(LANE_REG(060a), 0x17),
	REG_SEQ0(LANE_REG(060b), 0x77),
	REG_SEQ0(LANE_REG(060c), 0x77),
	REG_SEQ0(LANE_REG(060d), 0x77),
	REG_SEQ0(LANE_REG(060e), 0x38),
	REG_SEQ0(LANE_REG(0610), 0x03),
	REG_SEQ0(LANE_REG(0611), 0x0f),
	REG_SEQ0(LANE_REG(0616), 0x02),
	REG_SEQ0(LANE_REG(061b), 0x01),
	REG_SEQ0(LANE_REG(061f), 0x15),
	REG_SEQ0(LANE_REG(0620), 0xa0),
};

static const struct reg_sequence rk_hdtpx_tmds_lane_init_seq[] = {
	REG_SEQ0(LANE_REG(0312), 0x00),
	REG_SEQ0(LANE_REG(031e), 0x00),
	REG_SEQ0(LANE_REG(0412), 0x00),
	REG_SEQ0(LANE_REG(041e), 0x00),
	REG_SEQ0(LANE_REG(0512), 0x00),
	REG_SEQ0(LANE_REG(051e), 0x00),
	REG_SEQ0(LANE_REG(0612), 0x00),
	REG_SEQ0(LANE_REG(061e), 0x08),
	REG_SEQ0(LANE_REG(0303), 0x2f),
	REG_SEQ0(LANE_REG(0403), 0x2f),
	REG_SEQ0(LANE_REG(0503), 0x2f),
	REG_SEQ0(LANE_REG(0603), 0x2f),
	REG_SEQ0(LANE_REG(0305), 0x03),
	REG_SEQ0(LANE_REG(0405), 0x03),
	REG_SEQ0(LANE_REG(0505), 0x03),
	REG_SEQ0(LANE_REG(0605), 0x03),
	REG_SEQ0(LANE_REG(0306), 0x1c),
	REG_SEQ0(LANE_REG(0406), 0x1c),
	REG_SEQ0(LANE_REG(0506), 0x1c),
	REG_SEQ0(LANE_REG(0606), 0x1c),
};

static bool rk_hdptx_phy_is_rw_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x0000 ... 0x029c:
	case 0x0400 ... 0x04a4:
	case 0x0800 ... 0x08a4:
	case 0x0c00 ... 0x0cb4:
	case 0x1000 ... 0x10b4:
	case 0x1400 ... 0x14b4:
	case 0x1800 ... 0x18b4:
		return true;
	}

	return false;
}

static const struct regmap_config rk_hdptx_phy_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.writeable_reg = rk_hdptx_phy_is_rw_reg,
	.readable_reg = rk_hdptx_phy_is_rw_reg,
	.fast_io = true,
	.max_register = 0x18b4,
};

#define rk_hdptx_multi_reg_write(hdptx, seq) \
	regmap_multi_reg_write((hdptx)->regmap, seq, ARRAY_SIZE(seq))

static void rk_hdptx_pre_power_up(struct rk_hdptx_phy *hdptx)
{
	u32 val;

	reset_control_assert(hdptx->rsts[RST_APB].rstc);
	usleep_range(20, 25);
	reset_control_deassert(hdptx->rsts[RST_APB].rstc);

	reset_control_assert(hdptx->rsts[RST_LANE].rstc);
	reset_control_assert(hdptx->rsts[RST_CMN].rstc);
	reset_control_assert(hdptx->rsts[RST_INIT].rstc);

	val = (HDPTX_I_PLL_EN | HDPTX_I_BIAS_EN | HDPTX_I_BGR_EN) << 16;
	regmap_write(hdptx->grf, GRF_HDPTX_CON0, val);
}

static int rk_hdptx_post_enable_lane(struct rk_hdptx_phy *hdptx)
{
	u32 val;
	int ret;

	reset_control_deassert(hdptx->rsts[RST_LANE].rstc);

	val = (HDPTX_I_BIAS_EN | HDPTX_I_BGR_EN) << 16 |
	       HDPTX_I_BIAS_EN | HDPTX_I_BGR_EN;
	regmap_write(hdptx->grf, GRF_HDPTX_CON0, val);

	ret = regmap_read_poll_timeout(hdptx->grf, GRF_HDPTX_STATUS, val,
				       (val & HDPTX_O_PHY_RDY) &&
				       (val & HDPTX_O_PLL_LOCK_DONE),
				       100, 5000);
	if (ret) {
		dev_err(hdptx->dev, "Failed to get PHY lane lock: %d\n", ret);
		return ret;
	}

	dev_dbg(hdptx->dev, "PHY lane locked\n");

	return 0;
}

static int rk_hdptx_post_enable_pll(struct rk_hdptx_phy *hdptx)
{
	u32 val;
	int ret;

	val = (HDPTX_I_BIAS_EN | HDPTX_I_BGR_EN) << 16 |
	       HDPTX_I_BIAS_EN | HDPTX_I_BGR_EN;
	regmap_write(hdptx->grf, GRF_HDPTX_CON0, val);

	usleep_range(10, 15);
	reset_control_deassert(hdptx->rsts[RST_INIT].rstc);

	usleep_range(10, 15);
	val = HDPTX_I_PLL_EN << 16 | HDPTX_I_PLL_EN;
	regmap_write(hdptx->grf, GRF_HDPTX_CON0, val);

	usleep_range(10, 15);
	reset_control_deassert(hdptx->rsts[RST_CMN].rstc);

	ret = regmap_read_poll_timeout(hdptx->grf, GRF_HDPTX_STATUS, val,
				       val & HDPTX_O_PHY_CLK_RDY, 20, 400);
	if (ret) {
		dev_err(hdptx->dev, "Failed to get PHY clk ready: %d\n", ret);
		return ret;
	}

	dev_dbg(hdptx->dev, "PHY clk ready\n");

	return 0;
}

static void rk_hdptx_phy_disable(struct rk_hdptx_phy *hdptx)
{
	u32 val;

	/* reset phy and apb, or phy locked flag may keep 1 */
	reset_control_assert(hdptx->rsts[RST_PHY].rstc);
	usleep_range(20, 30);
	reset_control_deassert(hdptx->rsts[RST_PHY].rstc);

	reset_control_assert(hdptx->rsts[RST_APB].rstc);
	usleep_range(20, 30);
	reset_control_deassert(hdptx->rsts[RST_APB].rstc);

	regmap_write(hdptx->regmap, LANE_REG(0300), 0x82);
	regmap_write(hdptx->regmap, SB_REG(010f), 0xc1);
	regmap_write(hdptx->regmap, SB_REG(0110), 0x1);
	regmap_write(hdptx->regmap, LANE_REG(0301), 0x80);
	regmap_write(hdptx->regmap, LANE_REG(0401), 0x80);
	regmap_write(hdptx->regmap, LANE_REG(0501), 0x80);
	regmap_write(hdptx->regmap, LANE_REG(0601), 0x80);

	reset_control_assert(hdptx->rsts[RST_LANE].rstc);
	reset_control_assert(hdptx->rsts[RST_CMN].rstc);
	reset_control_assert(hdptx->rsts[RST_INIT].rstc);

	val = (HDPTX_I_PLL_EN | HDPTX_I_BIAS_EN | HDPTX_I_BGR_EN) << 16;
	regmap_write(hdptx->grf, GRF_HDPTX_CON0, val);
}

static bool rk_hdptx_phy_clk_pll_calc(unsigned int data_rate,
				      struct ropll_config *cfg)
{
	const unsigned int fout = data_rate / 2, fref = 24000;
	unsigned long k = 0, lc, k_sub, lc_sub;
	unsigned int fvco, sdc;
	u32 mdiv, sdiv, n = 8;

	if (fout > 0xfffffff)
		return false;

	for (sdiv = 16; sdiv >= 1; sdiv--) {
		if (sdiv % 2 && sdiv != 1)
			continue;

		fvco = fout * sdiv;

		if (fvco < 2000000 || fvco > 4000000)
			continue;

		mdiv = DIV_ROUND_UP(fvco, fref);
		if (mdiv < 20 || mdiv > 255)
			continue;

		if (fref * mdiv - fvco) {
			for (sdc = 264000; sdc <= 750000; sdc += fref)
				if (sdc * n > fref * mdiv)
					break;

			if (sdc > 750000)
				continue;

			rational_best_approximation(fref * mdiv - fvco,
						    sdc / 16,
						    GENMASK(6, 0),
						    GENMASK(7, 0),
						    &k, &lc);

			rational_best_approximation(sdc * n - fref * mdiv,
						    sdc,
						    GENMASK(6, 0),
						    GENMASK(7, 0),
						    &k_sub, &lc_sub);
		}

		break;
	}

	if (sdiv < 1)
		return false;

	if (cfg) {
		cfg->pms_mdiv = mdiv;
		cfg->pms_mdiv_afc = mdiv;
		cfg->pms_pdiv = 1;
		cfg->pms_refdiv = 1;
		cfg->pms_sdiv = sdiv - 1;

		cfg->sdm_en = k > 0 ? 1 : 0;
		if (cfg->sdm_en) {
			cfg->sdm_deno = lc;
			cfg->sdm_num_sign = 1;
			cfg->sdm_num = k;
			cfg->sdc_n = n - 3;
			cfg->sdc_num = k_sub;
			cfg->sdc_deno = lc_sub;
		}
	}

	return true;
}

static int rk_hdptx_ropll_tmds_cmn_config(struct rk_hdptx_phy *hdptx,
					  unsigned int rate)
{
	const struct ropll_config *cfg = NULL;
	struct ropll_config rc = {0};
	int i;

	for (i = 0; i < ARRAY_SIZE(ropll_tmds_cfg); i++)
		if (rate == ropll_tmds_cfg[i].bit_rate) {
			cfg = &ropll_tmds_cfg[i];
			break;
		}

	if (!cfg) {
		if (rk_hdptx_phy_clk_pll_calc(rate, &rc)) {
			cfg = &rc;
		} else {
			dev_err(hdptx->dev, "%s cannot find pll cfg\n", __func__);
			return -EINVAL;
		}
	}

	dev_dbg(hdptx->dev, "mdiv=%u, sdiv=%u, sdm_en=%u, k_sign=%u, k=%u, lc=%u\n",
		cfg->pms_mdiv, cfg->pms_sdiv + 1, cfg->sdm_en,
		cfg->sdm_num_sign, cfg->sdm_num, cfg->sdm_deno);

	rk_hdptx_pre_power_up(hdptx);

	reset_control_assert(hdptx->rsts[RST_ROPLL].rstc);
	usleep_range(20, 30);
	reset_control_deassert(hdptx->rsts[RST_ROPLL].rstc);

	rk_hdptx_multi_reg_write(hdptx, rk_hdtpx_common_cmn_init_seq);
	rk_hdptx_multi_reg_write(hdptx, rk_hdtpx_tmds_cmn_init_seq);

	regmap_write(hdptx->regmap, CMN_REG(0051), cfg->pms_mdiv);
	regmap_write(hdptx->regmap, CMN_REG(0055), cfg->pms_mdiv_afc);
	regmap_write(hdptx->regmap, CMN_REG(0059),
		     (cfg->pms_pdiv << 4) | cfg->pms_refdiv);
	regmap_write(hdptx->regmap, CMN_REG(005a), cfg->pms_sdiv << 4);

	regmap_update_bits(hdptx->regmap, CMN_REG(005e), ROPLL_SDM_EN_MASK,
			   FIELD_PREP(ROPLL_SDM_EN_MASK, cfg->sdm_en));
	if (!cfg->sdm_en)
		regmap_update_bits(hdptx->regmap, CMN_REG(005e), 0xf, 0);

	regmap_update_bits(hdptx->regmap, CMN_REG(0064), ROPLL_SDM_NUM_SIGN_RBR_MASK,
			   FIELD_PREP(ROPLL_SDM_NUM_SIGN_RBR_MASK, cfg->sdm_num_sign));

	regmap_write(hdptx->regmap, CMN_REG(0060), cfg->sdm_deno);
	regmap_write(hdptx->regmap, CMN_REG(0065), cfg->sdm_num);

	regmap_update_bits(hdptx->regmap, CMN_REG(0069), ROPLL_SDC_N_RBR_MASK,
			   FIELD_PREP(ROPLL_SDC_N_RBR_MASK, cfg->sdc_n));

	regmap_write(hdptx->regmap, CMN_REG(006c), cfg->sdc_num);
	regmap_write(hdptx->regmap, CMN_REG(0070), cfg->sdc_deno);

	regmap_update_bits(hdptx->regmap, CMN_REG(0086), PLL_PCG_POSTDIV_SEL_MASK,
			   FIELD_PREP(PLL_PCG_POSTDIV_SEL_MASK, cfg->pms_sdiv));

	regmap_update_bits(hdptx->regmap, CMN_REG(0086), PLL_PCG_CLK_EN,
			   PLL_PCG_CLK_EN);

	return rk_hdptx_post_enable_pll(hdptx);
}

static int rk_hdptx_ropll_tmds_mode_config(struct rk_hdptx_phy *hdptx,
					   unsigned int rate)
{
	u32 val;
	int ret;

	ret = regmap_read(hdptx->grf, GRF_HDPTX_STATUS, &val);
	if (ret)
		return ret;

	if (!(val & HDPTX_O_PLL_LOCK_DONE)) {
		ret = rk_hdptx_ropll_tmds_cmn_config(hdptx, rate);
		if (ret)
			return ret;
	}

	rk_hdptx_multi_reg_write(hdptx, rk_hdtpx_common_sb_init_seq);

	regmap_write(hdptx->regmap, LNTOP_REG(0200), 0x06);

	if (rate >= 3400000) {
		/* For 1/40 bitrate clk */
		rk_hdptx_multi_reg_write(hdptx, rk_hdtpx_tmds_lntop_highbr_seq);
	} else {
		/* For 1/10 bitrate clk */
		rk_hdptx_multi_reg_write(hdptx, rk_hdtpx_tmds_lntop_lowbr_seq);
	}

	regmap_write(hdptx->regmap, LNTOP_REG(0206), 0x07);
	regmap_write(hdptx->regmap, LNTOP_REG(0207), 0x0f);

	rk_hdptx_multi_reg_write(hdptx, rk_hdtpx_common_lane_init_seq);
	rk_hdptx_multi_reg_write(hdptx, rk_hdtpx_tmds_lane_init_seq);

	return rk_hdptx_post_enable_lane(hdptx);
}

static int rk_hdptx_phy_power_on(struct phy *phy)
{
	struct rk_hdptx_phy *hdptx = phy_get_drvdata(phy);
	int ret, bus_width = phy_get_bus_width(hdptx->phy);
	/*
	 * FIXME: Temporary workaround to pass pixel_clk_rate
	 * from the HDMI bridge driver until phy_configure_opts_hdmi
	 * becomes available in the PHY API.
	 */
	unsigned int rate = bus_width & 0xfffffff;

	dev_dbg(hdptx->dev, "%s bus_width=%x rate=%u\n",
		__func__, bus_width, rate);

	ret = pm_runtime_resume_and_get(hdptx->dev);
	if (ret) {
		dev_err(hdptx->dev, "Failed to resume phy: %d\n", ret);
		return ret;
	}

	ret = rk_hdptx_ropll_tmds_mode_config(hdptx, rate);
	if (ret)
		pm_runtime_put(hdptx->dev);

	return ret;
}

static int rk_hdptx_phy_power_off(struct phy *phy)
{
	struct rk_hdptx_phy *hdptx = phy_get_drvdata(phy);
	u32 val;
	int ret;

	ret = regmap_read(hdptx->grf, GRF_HDPTX_STATUS, &val);
	if (ret == 0 && (val & HDPTX_O_PLL_LOCK_DONE))
		rk_hdptx_phy_disable(hdptx);

	pm_runtime_put(hdptx->dev);

	return ret;
}

static const struct phy_ops rk_hdptx_phy_ops = {
	.power_on  = rk_hdptx_phy_power_on,
	.power_off = rk_hdptx_phy_power_off,
	.owner	   = THIS_MODULE,
};

static int rk_hdptx_phy_runtime_suspend(struct device *dev)
{
	struct rk_hdptx_phy *hdptx = dev_get_drvdata(dev);

	clk_bulk_disable_unprepare(hdptx->nr_clks, hdptx->clks);

	return 0;
}

static int rk_hdptx_phy_runtime_resume(struct device *dev)
{
	struct rk_hdptx_phy *hdptx = dev_get_drvdata(dev);
	int ret;

	ret = clk_bulk_prepare_enable(hdptx->nr_clks, hdptx->clks);
	if (ret)
		dev_err(hdptx->dev, "Failed to enable clocks: %d\n", ret);

	return ret;
}

static int rk_hdptx_phy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct rk_hdptx_phy *hdptx;
	void __iomem *regs;
	int ret;

	hdptx = devm_kzalloc(dev, sizeof(*hdptx), GFP_KERNEL);
	if (!hdptx)
		return -ENOMEM;

	hdptx->dev = dev;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return dev_err_probe(dev, PTR_ERR(regs),
				     "Failed to ioremap resource\n");

	ret = devm_clk_bulk_get_all(dev, &hdptx->clks);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get clocks\n");
	if (ret == 0)
		return dev_err_probe(dev, -EINVAL, "Missing clocks\n");

	hdptx->nr_clks = ret;

	hdptx->regmap = devm_regmap_init_mmio(dev, regs,
					      &rk_hdptx_phy_regmap_config);
	if (IS_ERR(hdptx->regmap))
		return dev_err_probe(dev, PTR_ERR(hdptx->regmap),
				     "Failed to init regmap\n");

	hdptx->rsts[RST_PHY].id = "phy";
	hdptx->rsts[RST_APB].id = "apb";
	hdptx->rsts[RST_INIT].id = "init";
	hdptx->rsts[RST_CMN].id = "cmn";
	hdptx->rsts[RST_LANE].id = "lane";
	hdptx->rsts[RST_ROPLL].id = "ropll";
	hdptx->rsts[RST_LCPLL].id = "lcpll";

	ret = devm_reset_control_bulk_get_exclusive(dev, RST_MAX, hdptx->rsts);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get resets\n");

	hdptx->grf = syscon_regmap_lookup_by_phandle(dev->of_node,
						     "rockchip,grf");
	if (IS_ERR(hdptx->grf))
		return dev_err_probe(dev, PTR_ERR(hdptx->grf),
				     "Could not get GRF syscon\n");

	hdptx->phy = devm_phy_create(dev, NULL, &rk_hdptx_phy_ops);
	if (IS_ERR(hdptx->phy))
		return dev_err_probe(dev, PTR_ERR(hdptx->phy),
				     "Failed to create HDMI PHY\n");

	platform_set_drvdata(pdev, hdptx);
	phy_set_drvdata(hdptx->phy, hdptx);
	phy_set_bus_width(hdptx->phy, 8);

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable runtime PM\n");

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider))
		return dev_err_probe(dev, PTR_ERR(phy_provider),
				     "Failed to register PHY provider\n");

	reset_control_deassert(hdptx->rsts[RST_APB].rstc);
	reset_control_deassert(hdptx->rsts[RST_CMN].rstc);
	reset_control_deassert(hdptx->rsts[RST_INIT].rstc);

	return 0;
}

static const struct dev_pm_ops rk_hdptx_phy_pm_ops = {
	RUNTIME_PM_OPS(rk_hdptx_phy_runtime_suspend,
		       rk_hdptx_phy_runtime_resume, NULL)
};

static const struct of_device_id rk_hdptx_phy_of_match[] = {
	{ .compatible = "rockchip,rk3588-hdptx-phy", },
	{}
};
MODULE_DEVICE_TABLE(of, rk_hdptx_phy_of_match);

static struct platform_driver rk_hdptx_phy_driver = {
	.probe  = rk_hdptx_phy_probe,
	.driver = {
		.name = "rockchip-hdptx-phy",
		.pm = &rk_hdptx_phy_pm_ops,
		.of_match_table = rk_hdptx_phy_of_match,
	},
};
module_platform_driver(rk_hdptx_phy_driver);

MODULE_AUTHOR("Algea Cao <algea.cao@rock-chips.com>");
MODULE_AUTHOR("Cristian Ciocaltea <cristian.ciocaltea@collabora.com>");
MODULE_DESCRIPTION("Samsung HDMI/eDP Transmitter Combo PHY Driver");
MODULE_LICENSE("GPL");
