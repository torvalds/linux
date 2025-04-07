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
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/rational.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#define GRF_HDPTX_CON0			0x00
#define HDPTX_I_PLL_EN			BIT(7)
#define HDPTX_I_BIAS_EN			BIT(6)
#define HDPTX_I_BGR_EN			BIT(5)
#define HDPTX_MODE_SEL			BIT(0)
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
#define OVRD_LCPLL_EN_MASK		BIT(7)
#define LCPLL_EN_MASK			BIT(6)
#define LCPLL_LCVCO_MODE_EN_MASK	BIT(4)
/* CMN_REG(001e) */
#define LCPLL_PI_EN_MASK		BIT(5)
#define LCPLL_100M_CLK_EN_MASK		BIT(0)
/* CMN_REG(0025) */
#define LCPLL_PMS_IQDIV_RSTN_MASK	BIT(4)
/* CMN_REG(0028) */
#define LCPLL_SDC_FRAC_EN_MASK		BIT(2)
#define LCPLL_SDC_FRAC_RSTN_MASK	BIT(0)
/* CMN_REG(002d) */
#define LCPLL_SDC_N_MASK		GENMASK(3, 1)
/* CMN_REG(002e) */
#define LCPLL_SDC_NUMBERATOR_MASK	GENMASK(5, 0)
/* CMN_REG(002f) */
#define LCPLL_SDC_DENOMINATOR_MASK	GENMASK(7, 2)
#define LCPLL_SDC_NDIV_RSTN_MASK	BIT(0)
/* CMN_REG(003c) */
#define ANA_LCPLL_RESERVED7_MASK	BIT(7)
/* CMN_REG(003d) */
#define OVRD_ROPLL_EN_MASK		BIT(7)
#define ROPLL_EN_MASK			BIT(6)
#define ROPLL_LCVCO_EN_MASK		BIT(4)
/* CMN_REG(0046) */
#define ROPLL_ANA_CPP_CTRL_COARSE_MASK	GENMASK(7, 4)
#define ROPLL_ANA_CPP_CTRL_FINE_MASK	GENMASK(3, 0)
/* CMN_REG(0047) */
#define ROPLL_ANA_LPF_C_SEL_COARSE_MASK	GENMASK(5, 3)
#define ROPLL_ANA_LPF_C_SEL_FINE_MASK	GENMASK(2, 0)
/* CMN_REG(004e) */
#define ROPLL_PI_EN_MASK		BIT(5)
/* CMN_REG(0051) */
#define ROPLL_PMS_MDIV_MASK		GENMASK(7, 0)
/* CMN_REG(0055) */
#define ROPLL_PMS_MDIV_AFC_MASK		GENMASK(7, 0)
/* CMN_REG(0059) */
#define ANA_ROPLL_PMS_PDIV_MASK		GENMASK(7, 4)
#define ANA_ROPLL_PMS_REFDIV_MASK	GENMASK(3, 0)
/* CMN_REG(005a) */
#define ROPLL_PMS_SDIV_RBR_MASK		GENMASK(7, 4)
#define ROPLL_PMS_SDIV_HBR_MASK		GENMASK(3, 0)
/* CMN_REG(005b) */
#define ROPLL_PMS_SDIV_HBR2_MASK	GENMASK(7, 4)
/* CMN_REG(005c) */
#define ROPLL_PMS_IQDIV_RSTN_MASK	BIT(5)
/* CMN_REG(005e) */
#define ROPLL_SDM_EN_MASK		BIT(6)
#define OVRD_ROPLL_SDM_RSTN_MASK	BIT(5)
#define ROPLL_SDM_RSTN_MASK		BIT(4)
#define ROPLL_SDC_FRAC_EN_RBR_MASK	BIT(3)
#define ROPLL_SDC_FRAC_EN_HBR_MASK	BIT(2)
#define ROPLL_SDC_FRAC_EN_HBR2_MASK	BIT(1)
#define ROPLL_SDM_FRAC_EN_HBR3_MASK	BIT(0)
/* CMN_REG(005f) */
#define OVRD_ROPLL_SDC_RSTN_MASK	BIT(5)
#define ROPLL_SDC_RSTN_MASK		BIT(4)
/* CMN_REG(0060)  */
#define ROPLL_SDM_DENOMINATOR_MASK	GENMASK(7, 0)
/* CMN_REG(0064) */
#define ROPLL_SDM_NUM_SIGN_RBR_MASK	BIT(3)
#define ROPLL_SDM_NUM_SIGN_HBR_MASK	BIT(2)
#define ROPLL_SDM_NUM_SIGN_HBR2_MASK	BIT(1)
/* CMN_REG(0065) */
#define ROPLL_SDM_NUM_MASK		GENMASK(7, 0)
/* CMN_REG(0069) */
#define ROPLL_SDC_N_RBR_MASK		GENMASK(2, 0)
/* CMN_REG(006a) */
#define ROPLL_SDC_N_HBR_MASK		GENMASK(5, 3)
#define ROPLL_SDC_N_HBR2_MASK		GENMASK(2, 0)
/* CMN_REG(006b) */
#define ROPLL_SDC_N_HBR3_MASK		GENMASK(3, 1)
/* CMN_REG(006c) */
#define ROPLL_SDC_NUM_MASK		GENMASK(5, 0)
/* cmn_reg0070 */
#define ROPLL_SDC_DENO_MASK		GENMASK(5, 0)
/* CMN_REG(0074) */
#define OVRD_ROPLL_SDC_NDIV_RSTN_MASK	BIT(3)
#define ROPLL_SDC_NDIV_RSTN_MASK	BIT(2)
#define OVRD_ROPLL_SSC_EN_MASK		BIT(1)
#define ROPLL_SSC_EN_MASK		BIT(0)
/* CMN_REG(0075) */
#define ANA_ROPLL_SSC_FM_DEVIATION_MASK	GENMASK(5, 0)
/* CMN_REG(0076) */
#define ANA_ROPLL_SSC_FM_FREQ_MASK	GENMASK(6, 2)
/* CMN_REG(0077) */
#define ANA_ROPLL_SSC_CLK_DIV_SEL_MASK	GENMASK(6, 3)
/* CMN_REG(0081) */
#define OVRD_PLL_CD_CLK_EN_MASK		BIT(8)
#define ANA_PLL_CD_TX_SER_RATE_SEL_MASK	BIT(3)
#define ANA_PLL_CD_HSCLK_WEST_EN_MASK	BIT(1)
#define ANA_PLL_CD_HSCLK_EAST_EN_MASK	BIT(0)
/* CMN_REG(0082) */
#define ANA_PLL_CD_VREG_GAIN_CTRL_MASK	GENMASK(3, 0)
/* CMN_REG(0083) */
#define ANA_PLL_CD_VREG_ICTRL_MASK	GENMASK(6, 5)
/* CMN_REG(0084) */
#define PLL_LCRO_CLK_SEL_MASK		BIT(5)
/* CMN_REG(0085) */
#define ANA_PLL_SYNC_LOSS_DET_MODE_MASK	GENMASK(1, 0)
/* CMN_REG(0086) */
#define PLL_PCG_POSTDIV_SEL_MASK	GENMASK(7, 4)
#define PLL_PCG_CLK_SEL_MASK		GENMASK(3, 1)
#define PLL_PCG_CLK_EN_MASK		BIT(0)
/* CMN_REG(0087) */
#define ANA_PLL_FRL_MODE_EN_MASK	BIT(3)
#define ANA_PLL_TX_HS_CLK_EN_MASK	BIT(2)
/* CMN_REG(0089) */
#define LCPLL_ALONE_MODE_MASK		BIT(1)
/* CMN_REG(0095) */
#define DP_TX_LINK_BW_MASK		GENMASK(1, 0)
/* CMN_REG(0097) */
#define DIG_CLK_SEL_MASK		BIT(1)
#define LCPLL_REF			BIT(1)
#define ROPLL_REF			0
/* CMN_REG(0099) */
#define SSC_EN_MASK			GENMASK(7, 6)
#define CMN_ROPLL_ALONE_MODE_MASK	BIT(2)
#define ROPLL_ALONE_MODE		BIT(2)
/* CMN_REG(009a) */
#define HS_SPEED_SEL_MASK		BIT(0)
#define DIV_10_CLOCK			BIT(0)
/* CMN_REG(009b) */
#define LS_SPEED_SEL_MASK		BIT(4)
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
#define OVRD_SB_AUX_EN_MASK		BIT(1)
#define SB_AUX_EN_MASK			BIT(0)
/* SB_REG(0105) */
#define OVRD_SB_EARC_CMDC_EN_MASK	BIT(6)
#define SB_EARC_CMDC_EN_MASK		BIT(5)
#define ANA_SB_TX_HLVL_PROG_MASK	GENMASK(2, 0)
/* SB_REG(0106) */
#define ANA_SB_TX_LLVL_PROG_MASK	GENMASK(6, 4)
/* SB_REG(0109) */
#define ANA_SB_DMRX_AFC_DIV_RATIO_MASK	GENMASK(2, 0)
/* SB_REG(010d) */
#define ANA_SB_DMRX_LPBK_DATA_MASK	BIT(4)
/* SB_REG(010f) */
#define OVRD_SB_VREG_EN_MASK		BIT(7)
#define SB_VREG_EN_MASK			BIT(6)
#define OVRD_SB_VREG_LPF_BYPASS_MASK	BIT(5)
#define SB_VREG_LPF_BYPASS_MASK		BIT(4)
#define ANA_SB_VREG_GAIN_CTRL_MASK	GENMASK(3, 0)
/* SB_REG(0110) */
#define ANA_SB_VREG_OUT_SEL_MASK	BIT(1)
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
/* SB_REG(0118) */
#define SB_TG_EARC_DMRX_RECVRD_CLK_CNT_MASK	GENMASK(7, 0)
/* SB_REG(011a) */
#define SB_TG_CNT_RUN_NO_7_0_MASK	GENMASK(7, 0)
/* SB_REG(011b) */
#define SB_EARC_SIG_DET_BYPASS_MASK	BIT(4)
#define SB_AFC_TOL_MASK			GENMASK(3, 0)
/* SB_REG(011c) */
#define SB_AFC_STB_NUM_MASK		GENMASK(3, 0)
/* SB_REG(011d) */
#define SB_TG_OSC_CNT_MIN_MASK		GENMASK(7, 0)
/* SB_REG(011e) */
#define SB_TG_OSC_CNT_MAX_MASK		GENMASK(7, 0)
/* SB_REG(011f) */
#define SB_PWM_AFC_CTRL_MASK		GENMASK(7, 2)
#define SB_RCAL_RSTN_MASK		BIT(1)
/* SB_REG(0120) */
#define SB_AUX_EN_IN_MASK		BIT(7)
#define SB_EARC_EN_MASK			BIT(1)
#define SB_EARC_AFC_EN_MASK		BIT(2)
/* SB_REG(0123) */
#define OVRD_SB_READY_MASK		BIT(5)
#define SB_READY_MASK			BIT(4)

/* LNTOP_REG(0200) */
#define PROTOCOL_SEL_MASK		BIT(2)
#define HDMI_MODE			BIT(2)
#define HDMI_TMDS_FRL_SEL		BIT(1)
/* LNTOP_REG(0206) */
#define DATA_BUS_WIDTH_MASK		GENMASK(2, 1)
#define DATA_BUS_WIDTH_SEL_MASK		BIT(0)
#define DATA_BUS_36_40			BIT(0)
/* LNTOP_REG(0207) */
#define LANE_EN_MASK			0xf
#define ALL_LANE_EN			0xf

/* LANE_REG(0301) */
#define OVRD_LN_TX_DRV_EI_EN_MASK	BIT(7)
#define LN_TX_DRV_EI_EN_MASK		BIT(6)
/* LANE_REG(0303) */
#define OVRD_LN_TX_DRV_LVL_CTRL_MASK	BIT(5)
#define LN_TX_DRV_LVL_CTRL_MASK		GENMASK(4, 0)
/* LANE_REG(0304)  */
#define OVRD_LN_TX_DRV_POST_LVL_CTRL_MASK	BIT(4)
#define LN_TX_DRV_POST_LVL_CTRL_MASK	GENMASK(3, 0)
/* LANE_REG(0305) */
#define OVRD_LN_TX_DRV_PRE_LVL_CTRL_MASK	BIT(6)
#define LN_TX_DRV_PRE_LVL_CTRL_MASK	GENMASK(5, 2)
/* LANE_REG(0306) */
#define LN_ANA_TX_DRV_IDRV_IDN_CTRL_MASK	GENMASK(7, 5)
#define LN_ANA_TX_DRV_IDRV_IUP_CTRL_MASK	GENMASK(4, 2)
#define LN_ANA_TX_DRV_ACCDRV_EN_MASK	BIT(0)
/* LANE_REG(0307) */
#define LN_ANA_TX_DRV_ACCDRV_POL_SEL_MASK	BIT(6)
#define LN_ANA_TX_DRV_ACCDRV_CTRL_MASK	GENMASK(5, 3)
/* LANE_REG(030a) */
#define LN_ANA_TX_JEQ_EN_MASK		BIT(4)
#define LN_TX_JEQ_EVEN_CTRL_RBR_MASK	GENMASK(3, 0)
/* LANE_REG(030b) */
#define LN_TX_JEQ_EVEN_CTRL_HBR_MASK	GENMASK(7, 4)
#define LN_TX_JEQ_EVEN_CTRL_HBR2_MASK	GENMASK(3, 0)
/* LANE_REG(030c) */
#define LN_TX_JEQ_ODD_CTRL_RBR_MASK	GENMASK(3, 0)
/* LANE_REG(030d) */
#define LN_TX_JEQ_ODD_CTRL_HBR_MASK	GENMASK(7, 4)
#define LN_TX_JEQ_ODD_CTRL_HBR2_MASK	GENMASK(3, 0)
/* LANE_REG(0310) */
#define LN_ANA_TX_SYNC_LOSS_DET_MODE_MASK	GENMASK(1, 0)
/* LANE_REG(0311) */
#define LN_TX_SER_40BIT_EN_RBR_MASK	BIT(3)
#define LN_TX_SER_40BIT_EN_HBR_MASK	BIT(2)
#define LN_TX_SER_40BIT_EN_HBR2_MASK	BIT(1)
/* LANE_REG(0312) */
#define LN0_TX_SER_RATE_SEL_RBR_MASK	BIT(5)
#define LN0_TX_SER_RATE_SEL_HBR_MASK	BIT(4)
#define LN0_TX_SER_RATE_SEL_HBR2_MASK	BIT(3)
#define LN0_TX_SER_RATE_SEL_HBR3_MASK	BIT(2)
/* LANE_REG(0316) */
#define LN_ANA_TX_SER_VREG_GAIN_CTRL_MASK	GENMASK(3, 0)
/* LANE_REG(031B) */
#define LN_ANA_TX_RESERVED_MASK		GENMASK(7, 0)
/* LANE_REG(031e) */
#define LN_POLARITY_INV_MASK		BIT(2)
#define LN_LANE_MODE_MASK		BIT(1)

/* LANE_REG(0412) */
#define LN1_TX_SER_RATE_SEL_RBR_MASK	BIT(5)
#define LN1_TX_SER_RATE_SEL_HBR_MASK	BIT(4)
#define LN1_TX_SER_RATE_SEL_HBR2_MASK	BIT(3)
#define LN1_TX_SER_RATE_SEL_HBR3_MASK	BIT(2)

/* LANE_REG(0512) */
#define LN2_TX_SER_RATE_SEL_RBR_MASK	BIT(5)
#define LN2_TX_SER_RATE_SEL_HBR_MASK	BIT(4)
#define LN2_TX_SER_RATE_SEL_HBR2_MASK	BIT(3)
#define LN2_TX_SER_RATE_SEL_HBR3_MASK	BIT(2)

/* LANE_REG(0612) */
#define LN3_TX_SER_RATE_SEL_RBR_MASK	BIT(5)
#define LN3_TX_SER_RATE_SEL_HBR_MASK	BIT(4)
#define LN3_TX_SER_RATE_SEL_HBR2_MASK	BIT(3)
#define LN3_TX_SER_RATE_SEL_HBR3_MASK	BIT(2)

#define HDMI20_MAX_RATE			600000000

enum dp_link_rate {
	DP_BW_RBR,
	DP_BW_HBR,
	DP_BW_HBR2,
};

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

struct tx_drv_ctrl {
	u8 tx_drv_lvl_ctrl;
	u8 tx_drv_post_lvl_ctrl;
	u8 ana_tx_drv_idrv_idn_ctrl;
	u8 ana_tx_drv_idrv_iup_ctrl;
	u8 ana_tx_drv_accdrv_en;
	u8 ana_tx_drv_accdrv_ctrl;
	u8 tx_drv_pre_lvl_ctrl;
	u8 ana_tx_jeq_en;
	u8 tx_jeq_even_ctrl;
	u8 tx_jeq_odd_ctrl;
};

enum rk_hdptx_reset {
	RST_APB = 0,
	RST_INIT,
	RST_CMN,
	RST_LANE,
	RST_MAX
};

#define MAX_HDPTX_PHY_NUM	2

struct rk_hdptx_phy_cfg {
	unsigned int num_phys;
	unsigned int phy_ids[MAX_HDPTX_PHY_NUM];
};

struct rk_hdptx_phy {
	struct device *dev;
	struct regmap *regmap;
	struct regmap *grf;

	/* PHY const config */
	const struct rk_hdptx_phy_cfg *cfgs;
	int phy_id;

	struct phy *phy;
	struct phy_config *phy_cfg;
	struct clk_bulk_data *clks;
	int nr_clks;
	struct reset_control_bulk_data rsts[RST_MAX];

	/* clk provider */
	struct clk_hw hw;
	unsigned long rate;

	atomic_t usage_count;

	/* used for dp mode */
	unsigned int link_rate;
	unsigned int lanes;
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

static struct tx_drv_ctrl tx_drv_ctrl_rbr[4][4] = {
	/* voltage swing 0, pre-emphasis 0->3 */
	{
		{ 0x2, 0x0, 0x4, 0x6, 0x0, 0x4, 0x1, 0x1, 0x7, 0x7 },
		{ 0x4, 0x3, 0x4, 0x6, 0x0, 0x4, 0x0, 0x1, 0x7, 0x7 },
		{ 0x7, 0x6, 0x4, 0x6, 0x0, 0x4, 0x0, 0x1, 0x7, 0x7 },
		{ 0xd, 0xc, 0x7, 0x7, 0x1, 0x7, 0x0, 0x1, 0x7, 0x7 },
	},

	/* voltage swing 1, pre-emphasis 0->2 */
	{
		{ 0x4, 0x0, 0x4, 0x6, 0x0, 0x4, 0x1, 0x1, 0x7, 0x7 },
		{ 0x9, 0x5, 0x4, 0x6, 0x0, 0x4, 0x0, 0x1, 0x7, 0x7 },
		{ 0xc, 0x8, 0x7, 0x7, 0x1, 0x7, 0x0, 0x1, 0x7, 0x7 },
	},

	/* voltage swing 2, pre-emphasis 0->1 */
	{
		{ 0x8, 0x0, 0x4, 0x6, 0x0, 0x4, 0x1, 0x1, 0x7, 0x7 },
		{ 0xc, 0x5, 0x7, 0x7, 0x1, 0x7, 0x0, 0x1, 0x7, 0x7 },
	},

	/* voltage swing 3, pre-emphasis 0 */
	{
		{ 0xb, 0x0, 0x7, 0x7, 0x1, 0x4, 0x1, 0x1, 0x7, 0x7 },
	}
};

static struct tx_drv_ctrl tx_drv_ctrl_hbr[4][4] = {
	/* voltage swing 0, pre-emphasis 0->3 */
	{
		{ 0x2, 0x0, 0x4, 0x6, 0x0, 0x4, 0x1, 0x1, 0x7, 0x7 },
		{ 0x5, 0x4, 0x4, 0x6, 0x0, 0x4, 0x0, 0x1, 0x7, 0x7 },
		{ 0x9, 0x8, 0x4, 0x6, 0x0, 0x4, 0x0, 0x1, 0x7, 0x7 },
		{ 0xd, 0xc, 0x7, 0x7, 0x1, 0x7, 0x0, 0x1, 0x7, 0x7 },
	},

	/* voltage swing 1, pre-emphasis 0->2 */
	{
		{ 0x6, 0x1, 0x4, 0x6, 0x0, 0x4, 0x1, 0x1, 0x7, 0x7 },
		{ 0xa, 0x6, 0x4, 0x6, 0x0, 0x4, 0x0, 0x1, 0x7, 0x7 },
		{ 0xc, 0x8, 0x7, 0x7, 0x1, 0x7, 0x0, 0x1, 0x7, 0x7 },
	},

	/* voltage swing 2, pre-emphasis 0->1 */
	{
		{ 0x9, 0x1, 0x4, 0x6, 0x0, 0x4, 0x1, 0x1, 0x7, 0x7 },
		{ 0xd, 0x6, 0x7, 0x7, 0x1, 0x7, 0x0, 0x1, 0x7, 0x7 },
	},

	/* voltage swing 3, pre-emphasis 0 */
	{
		{ 0xc, 0x1, 0x7, 0x7, 0x1, 0x4, 0x1, 0x1, 0x7, 0x7 },
	}
};

static struct tx_drv_ctrl tx_drv_ctrl_hbr2[4][4] = {
	/* voltage swing 0, pre-emphasis 0->3 */
	{
		{ 0x2, 0x1, 0x4, 0x6, 0x0, 0x4, 0x0, 0x1, 0x7, 0x7 },
		{ 0x5, 0x4, 0x4, 0x6, 0x0, 0x4, 0x0, 0x1, 0x7, 0x7 },
		{ 0x9, 0x8, 0x4, 0x6, 0x1, 0x4, 0x0, 0x1, 0x7, 0x7 },
		{ 0xd, 0xc, 0x7, 0x7, 0x1, 0x7, 0x0, 0x1, 0x7, 0x7 },
	},

	/* voltage swing 1, pre-emphasis 0->2 */
	{
		{ 0x6, 0x1, 0x4, 0x6, 0x0, 0x4, 0x1, 0x1, 0x7, 0x7 },
		{ 0xb, 0x7, 0x4, 0x6, 0x0, 0x4, 0x0, 0x1, 0x7, 0x7 },
		{ 0xd, 0x9, 0x7, 0x7, 0x1, 0x7, 0x0, 0x1, 0x7, 0x7 },
	},

	/* voltage swing 2, pre-emphasis 0->1 */
	{
		{ 0x8, 0x1, 0x4, 0x6, 0x0, 0x4, 0x1, 0x1, 0x7, 0x7 },
		{ 0xc, 0x6, 0x7, 0x7, 0x1, 0x7, 0x0, 0x1, 0x7, 0x7 },
	},

	/* voltage swing 3, pre-emphasis 0 */
	{
		{ 0xb, 0x0, 0x7, 0x7, 0x1, 0x4, 0x1, 0x1, 0x7, 0x7 },
	}
};

static bool rk_hdptx_phy_is_rw_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x0000 ... 0x029c: /* CMN Register */
	case 0x0400 ... 0x04a4: /* Sideband Register */
	case 0x0800 ... 0x08a4: /* Lane Top Register */
	case 0x0c00 ... 0x0cb4: /* Lane 0 Register */
	case 0x1000 ... 0x10b4: /* Lane 1 Register */
	case 0x1400 ... 0x14b4: /* Lane 2 Register */
	case 0x1800 ... 0x18b4: /* Lane 3 Register */
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

	hdptx->rate = rate * 100;

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

	regmap_update_bits(hdptx->regmap, CMN_REG(0086), PLL_PCG_CLK_EN_MASK,
			   FIELD_PREP(PLL_PCG_CLK_EN_MASK, 0x1));

	return rk_hdptx_post_enable_pll(hdptx);
}

static int rk_hdptx_ropll_tmds_mode_config(struct rk_hdptx_phy *hdptx,
					   unsigned int rate)
{
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

static void rk_hdptx_dp_reset(struct rk_hdptx_phy *hdptx)
{
	reset_control_assert(hdptx->rsts[RST_LANE].rstc);
	reset_control_assert(hdptx->rsts[RST_CMN].rstc);
	reset_control_assert(hdptx->rsts[RST_INIT].rstc);

	reset_control_assert(hdptx->rsts[RST_APB].rstc);
	udelay(10);
	reset_control_deassert(hdptx->rsts[RST_APB].rstc);

	regmap_update_bits(hdptx->regmap, LANE_REG(0301),
			   OVRD_LN_TX_DRV_EI_EN_MASK | LN_TX_DRV_EI_EN_MASK,
			   FIELD_PREP(OVRD_LN_TX_DRV_EI_EN_MASK, 1) |
			   FIELD_PREP(LN_TX_DRV_EI_EN_MASK, 0));
	regmap_update_bits(hdptx->regmap, LANE_REG(0401),
			   OVRD_LN_TX_DRV_EI_EN_MASK | LN_TX_DRV_EI_EN_MASK,
			   FIELD_PREP(OVRD_LN_TX_DRV_EI_EN_MASK, 1) |
			   FIELD_PREP(LN_TX_DRV_EI_EN_MASK, 0));
	regmap_update_bits(hdptx->regmap, LANE_REG(0501),
			   OVRD_LN_TX_DRV_EI_EN_MASK | LN_TX_DRV_EI_EN_MASK,
			   FIELD_PREP(OVRD_LN_TX_DRV_EI_EN_MASK, 1) |
			   FIELD_PREP(LN_TX_DRV_EI_EN_MASK, 0));
	regmap_update_bits(hdptx->regmap, LANE_REG(0601),
			   OVRD_LN_TX_DRV_EI_EN_MASK | LN_TX_DRV_EI_EN_MASK,
			   FIELD_PREP(OVRD_LN_TX_DRV_EI_EN_MASK, 1) |
			   FIELD_PREP(LN_TX_DRV_EI_EN_MASK, 0));

	regmap_write(hdptx->grf, GRF_HDPTX_CON0,
		     HDPTX_I_PLL_EN << 16 | FIELD_PREP(HDPTX_I_PLL_EN, 0x0));
	regmap_write(hdptx->grf, GRF_HDPTX_CON0,
		     HDPTX_I_BIAS_EN << 16 | FIELD_PREP(HDPTX_I_BIAS_EN, 0x0));
	regmap_write(hdptx->grf, GRF_HDPTX_CON0,
		     HDPTX_I_BGR_EN << 16 | FIELD_PREP(HDPTX_I_BGR_EN, 0x0));
}

static int rk_hdptx_phy_consumer_get(struct rk_hdptx_phy *hdptx,
				     unsigned int rate)
{
	enum phy_mode mode = phy_get_mode(hdptx->phy);
	u32 status;
	int ret;

	if (atomic_inc_return(&hdptx->usage_count) > 1)
		return 0;

	ret = regmap_read(hdptx->grf, GRF_HDPTX_STATUS, &status);
	if (ret)
		goto dec_usage;

	if (status & HDPTX_O_PLL_LOCK_DONE)
		dev_warn(hdptx->dev, "PLL locked by unknown consumer!\n");

	if (mode == PHY_MODE_DP) {
		rk_hdptx_dp_reset(hdptx);
	} else {
		if (rate) {
			ret = rk_hdptx_ropll_tmds_cmn_config(hdptx, rate);
			if (ret)
				goto dec_usage;
		}
	}

	return 0;

dec_usage:
	atomic_dec(&hdptx->usage_count);
	return ret;
}

static int rk_hdptx_phy_consumer_put(struct rk_hdptx_phy *hdptx, bool force)
{
	enum phy_mode mode = phy_get_mode(hdptx->phy);
	u32 status;
	int ret;

	ret = atomic_dec_return(&hdptx->usage_count);
	if (ret > 0)
		return 0;

	if (ret < 0) {
		dev_warn(hdptx->dev, "Usage count underflow!\n");
		ret = -EINVAL;
	} else {
		ret = regmap_read(hdptx->grf, GRF_HDPTX_STATUS, &status);
		if (!ret) {
			if (status & HDPTX_O_PLL_LOCK_DONE) {
				if (mode == PHY_MODE_DP)
					rk_hdptx_dp_reset(hdptx);
				else
					rk_hdptx_phy_disable(hdptx);
			}
			return 0;
		} else if (force) {
			return 0;
		}
	}

	atomic_inc(&hdptx->usage_count);
	return ret;
}

static void rk_hdptx_dp_pll_init(struct rk_hdptx_phy *hdptx)
{
	regmap_update_bits(hdptx->regmap, CMN_REG(003c), ANA_LCPLL_RESERVED7_MASK,
			   FIELD_PREP(ANA_LCPLL_RESERVED7_MASK, 0x1));

	regmap_update_bits(hdptx->regmap, CMN_REG(0046),
			   ROPLL_ANA_CPP_CTRL_COARSE_MASK | ROPLL_ANA_CPP_CTRL_FINE_MASK,
			   FIELD_PREP(ROPLL_ANA_CPP_CTRL_COARSE_MASK, 0xe) |
			   FIELD_PREP(ROPLL_ANA_CPP_CTRL_FINE_MASK, 0xe));
	regmap_update_bits(hdptx->regmap, CMN_REG(0047),
			   ROPLL_ANA_LPF_C_SEL_COARSE_MASK |
			   ROPLL_ANA_LPF_C_SEL_FINE_MASK,
			   FIELD_PREP(ROPLL_ANA_LPF_C_SEL_COARSE_MASK, 0x4) |
			   FIELD_PREP(ROPLL_ANA_LPF_C_SEL_FINE_MASK, 0x4));

	regmap_write(hdptx->regmap, CMN_REG(0051), FIELD_PREP(ROPLL_PMS_MDIV_MASK, 0x87));
	regmap_write(hdptx->regmap, CMN_REG(0052), FIELD_PREP(ROPLL_PMS_MDIV_MASK, 0x71));
	regmap_write(hdptx->regmap, CMN_REG(0053), FIELD_PREP(ROPLL_PMS_MDIV_MASK, 0x71));

	regmap_write(hdptx->regmap, CMN_REG(0055),
		     FIELD_PREP(ROPLL_PMS_MDIV_AFC_MASK, 0x87));
	regmap_write(hdptx->regmap, CMN_REG(0056),
		     FIELD_PREP(ROPLL_PMS_MDIV_AFC_MASK, 0x71));
	regmap_write(hdptx->regmap, CMN_REG(0057),
		     FIELD_PREP(ROPLL_PMS_MDIV_AFC_MASK, 0x71));

	regmap_write(hdptx->regmap, CMN_REG(0059),
		     FIELD_PREP(ANA_ROPLL_PMS_PDIV_MASK, 0x1) |
		     FIELD_PREP(ANA_ROPLL_PMS_REFDIV_MASK, 0x1));
	regmap_write(hdptx->regmap, CMN_REG(005a),
		     FIELD_PREP(ROPLL_PMS_SDIV_RBR_MASK, 0x3) |
		     FIELD_PREP(ROPLL_PMS_SDIV_HBR_MASK, 0x1));
	regmap_update_bits(hdptx->regmap, CMN_REG(005b), ROPLL_PMS_SDIV_HBR2_MASK,
			   FIELD_PREP(ROPLL_PMS_SDIV_HBR2_MASK, 0x0));

	regmap_update_bits(hdptx->regmap, CMN_REG(005e), ROPLL_SDM_EN_MASK,
			   FIELD_PREP(ROPLL_SDM_EN_MASK, 0x1));
	regmap_update_bits(hdptx->regmap, CMN_REG(005e),
			   OVRD_ROPLL_SDM_RSTN_MASK | ROPLL_SDM_RSTN_MASK,
			   FIELD_PREP(OVRD_ROPLL_SDM_RSTN_MASK, 0x1) |
			   FIELD_PREP(ROPLL_SDM_RSTN_MASK, 0x1));
	regmap_update_bits(hdptx->regmap, CMN_REG(005e), ROPLL_SDC_FRAC_EN_RBR_MASK,
			   FIELD_PREP(ROPLL_SDC_FRAC_EN_RBR_MASK, 0x1));
	regmap_update_bits(hdptx->regmap, CMN_REG(005e), ROPLL_SDC_FRAC_EN_HBR_MASK,
			   FIELD_PREP(ROPLL_SDC_FRAC_EN_HBR_MASK, 0x1));
	regmap_update_bits(hdptx->regmap, CMN_REG(005e), ROPLL_SDC_FRAC_EN_HBR2_MASK,
			   FIELD_PREP(ROPLL_SDC_FRAC_EN_HBR2_MASK, 0x1));

	regmap_update_bits(hdptx->regmap, CMN_REG(005f),
			   OVRD_ROPLL_SDC_RSTN_MASK | ROPLL_SDC_RSTN_MASK,
			   FIELD_PREP(OVRD_ROPLL_SDC_RSTN_MASK, 0x1) |
			   FIELD_PREP(ROPLL_SDC_RSTN_MASK, 0x1));
	regmap_write(hdptx->regmap, CMN_REG(0060),
		     FIELD_PREP(ROPLL_SDM_DENOMINATOR_MASK, 0x21));
	regmap_write(hdptx->regmap, CMN_REG(0061),
		     FIELD_PREP(ROPLL_SDM_DENOMINATOR_MASK, 0x27));
	regmap_write(hdptx->regmap, CMN_REG(0062),
		     FIELD_PREP(ROPLL_SDM_DENOMINATOR_MASK, 0x27));

	regmap_update_bits(hdptx->regmap, CMN_REG(0064),
			   ROPLL_SDM_NUM_SIGN_RBR_MASK |
			   ROPLL_SDM_NUM_SIGN_HBR_MASK |
			   ROPLL_SDM_NUM_SIGN_HBR2_MASK,
			   FIELD_PREP(ROPLL_SDM_NUM_SIGN_RBR_MASK, 0x0) |
			   FIELD_PREP(ROPLL_SDM_NUM_SIGN_HBR_MASK, 0x1) |
			   FIELD_PREP(ROPLL_SDM_NUM_SIGN_HBR2_MASK, 0x1));
	regmap_write(hdptx->regmap, CMN_REG(0065),
		     FIELD_PREP(ROPLL_SDM_NUM_MASK, 0x0));
	regmap_write(hdptx->regmap, CMN_REG(0066),
		     FIELD_PREP(ROPLL_SDM_NUM_MASK, 0xd));
	regmap_write(hdptx->regmap, CMN_REG(0067),
		     FIELD_PREP(ROPLL_SDM_NUM_MASK, 0xd));

	regmap_update_bits(hdptx->regmap, CMN_REG(0069), ROPLL_SDC_N_RBR_MASK,
			   FIELD_PREP(ROPLL_SDC_N_RBR_MASK, 0x2));

	regmap_update_bits(hdptx->regmap, CMN_REG(006a),
			   ROPLL_SDC_N_HBR_MASK | ROPLL_SDC_N_HBR2_MASK,
			   FIELD_PREP(ROPLL_SDC_N_HBR_MASK, 0x1) |
			   FIELD_PREP(ROPLL_SDC_N_HBR2_MASK, 0x1));

	regmap_write(hdptx->regmap, CMN_REG(006c),
		     FIELD_PREP(ROPLL_SDC_NUM_MASK, 0x3));
	regmap_write(hdptx->regmap, CMN_REG(006d),
		     FIELD_PREP(ROPLL_SDC_NUM_MASK, 0x7));
	regmap_write(hdptx->regmap, CMN_REG(006e),
		     FIELD_PREP(ROPLL_SDC_NUM_MASK, 0x7));

	regmap_write(hdptx->regmap, CMN_REG(0070),
		     FIELD_PREP(ROPLL_SDC_DENO_MASK, 0x8));
	regmap_write(hdptx->regmap, CMN_REG(0071),
		     FIELD_PREP(ROPLL_SDC_DENO_MASK, 0x18));
	regmap_write(hdptx->regmap, CMN_REG(0072),
		     FIELD_PREP(ROPLL_SDC_DENO_MASK, 0x18));

	regmap_update_bits(hdptx->regmap, CMN_REG(0074),
			   OVRD_ROPLL_SDC_NDIV_RSTN_MASK | ROPLL_SDC_NDIV_RSTN_MASK,
			   FIELD_PREP(OVRD_ROPLL_SDC_NDIV_RSTN_MASK, 0x1) |
			   FIELD_PREP(ROPLL_SDC_NDIV_RSTN_MASK, 0x1));

	regmap_update_bits(hdptx->regmap, CMN_REG(0077), ANA_ROPLL_SSC_CLK_DIV_SEL_MASK,
			   FIELD_PREP(ANA_ROPLL_SSC_CLK_DIV_SEL_MASK, 0x1));

	regmap_update_bits(hdptx->regmap, CMN_REG(0081), ANA_PLL_CD_TX_SER_RATE_SEL_MASK,
			   FIELD_PREP(ANA_PLL_CD_TX_SER_RATE_SEL_MASK, 0x0));
	regmap_update_bits(hdptx->regmap, CMN_REG(0081),
			   ANA_PLL_CD_HSCLK_EAST_EN_MASK | ANA_PLL_CD_HSCLK_WEST_EN_MASK,
			   FIELD_PREP(ANA_PLL_CD_HSCLK_EAST_EN_MASK, 0x1) |
			   FIELD_PREP(ANA_PLL_CD_HSCLK_WEST_EN_MASK, 0x0));

	regmap_update_bits(hdptx->regmap, CMN_REG(0082), ANA_PLL_CD_VREG_GAIN_CTRL_MASK,
			   FIELD_PREP(ANA_PLL_CD_VREG_GAIN_CTRL_MASK, 0x4));
	regmap_update_bits(hdptx->regmap, CMN_REG(0083), ANA_PLL_CD_VREG_ICTRL_MASK,
			   FIELD_PREP(ANA_PLL_CD_VREG_ICTRL_MASK, 0x1));
	regmap_update_bits(hdptx->regmap, CMN_REG(0084), PLL_LCRO_CLK_SEL_MASK,
			   FIELD_PREP(PLL_LCRO_CLK_SEL_MASK, 0x1));
	regmap_update_bits(hdptx->regmap, CMN_REG(0085), ANA_PLL_SYNC_LOSS_DET_MODE_MASK,
			   FIELD_PREP(ANA_PLL_SYNC_LOSS_DET_MODE_MASK, 0x3));

	regmap_update_bits(hdptx->regmap, CMN_REG(0087), ANA_PLL_TX_HS_CLK_EN_MASK,
			   FIELD_PREP(ANA_PLL_TX_HS_CLK_EN_MASK, 0x1));

	regmap_update_bits(hdptx->regmap, CMN_REG(0097), DIG_CLK_SEL_MASK,
			   FIELD_PREP(DIG_CLK_SEL_MASK, 0x1));

	regmap_update_bits(hdptx->regmap, CMN_REG(0099), CMN_ROPLL_ALONE_MODE_MASK,
			   FIELD_PREP(CMN_ROPLL_ALONE_MODE_MASK, 0x1));
	regmap_update_bits(hdptx->regmap, CMN_REG(009a), HS_SPEED_SEL_MASK,
			   FIELD_PREP(HS_SPEED_SEL_MASK, 0x1));
	regmap_update_bits(hdptx->regmap, CMN_REG(009b), LS_SPEED_SEL_MASK,
			   FIELD_PREP(LS_SPEED_SEL_MASK, 0x1));
}

static int rk_hdptx_dp_aux_init(struct rk_hdptx_phy *hdptx)
{
	u32 status;
	int ret;

	regmap_update_bits(hdptx->regmap, SB_REG(0102), ANA_SB_RXTERM_OFFSP_MASK,
			   FIELD_PREP(ANA_SB_RXTERM_OFFSP_MASK, 0x3));
	regmap_update_bits(hdptx->regmap, SB_REG(0103), ANA_SB_RXTERM_OFFSN_MASK,
			   FIELD_PREP(ANA_SB_RXTERM_OFFSN_MASK, 0x3));
	regmap_update_bits(hdptx->regmap, SB_REG(0104), SB_AUX_EN_MASK,
			   FIELD_PREP(SB_AUX_EN_MASK, 0x1));
	regmap_update_bits(hdptx->regmap, SB_REG(0105), ANA_SB_TX_HLVL_PROG_MASK,
			   FIELD_PREP(ANA_SB_TX_HLVL_PROG_MASK, 0x7));
	regmap_update_bits(hdptx->regmap, SB_REG(0106), ANA_SB_TX_LLVL_PROG_MASK,
			   FIELD_PREP(ANA_SB_TX_LLVL_PROG_MASK, 0x7));

	regmap_update_bits(hdptx->regmap, SB_REG(010d), ANA_SB_DMRX_LPBK_DATA_MASK,
			   FIELD_PREP(ANA_SB_DMRX_LPBK_DATA_MASK, 0x1));

	regmap_update_bits(hdptx->regmap, SB_REG(010f), ANA_SB_VREG_GAIN_CTRL_MASK,
			   FIELD_PREP(ANA_SB_VREG_GAIN_CTRL_MASK, 0x0));
	regmap_update_bits(hdptx->regmap, SB_REG(0110),
			   ANA_SB_VREG_OUT_SEL_MASK | ANA_SB_VREG_REF_SEL_MASK,
			   FIELD_PREP(ANA_SB_VREG_OUT_SEL_MASK, 0x1) |
			   FIELD_PREP(ANA_SB_VREG_REF_SEL_MASK, 0x1));

	regmap_update_bits(hdptx->regmap, SB_REG(0113),
			   SB_RX_RCAL_OPT_CODE_MASK | SB_RX_RTERM_CTRL_MASK,
			   FIELD_PREP(SB_RX_RCAL_OPT_CODE_MASK, 0x1) |
			   FIELD_PREP(SB_RX_RTERM_CTRL_MASK, 0x3));
	regmap_update_bits(hdptx->regmap, SB_REG(0114),
			   SB_TG_SB_EN_DELAY_TIME_MASK | SB_TG_RXTERM_EN_DELAY_TIME_MASK,
			   FIELD_PREP(SB_TG_SB_EN_DELAY_TIME_MASK, 0x2) |
			   FIELD_PREP(SB_TG_RXTERM_EN_DELAY_TIME_MASK, 0x2));
	regmap_update_bits(hdptx->regmap, SB_REG(0115),
			   SB_READY_DELAY_TIME_MASK | SB_TG_OSC_EN_DELAY_TIME_MASK,
			   FIELD_PREP(SB_READY_DELAY_TIME_MASK, 0x2) |
			   FIELD_PREP(SB_TG_OSC_EN_DELAY_TIME_MASK, 0x2));
	regmap_update_bits(hdptx->regmap, SB_REG(0116),
			   AFC_RSTN_DELAY_TIME_MASK,
			   FIELD_PREP(AFC_RSTN_DELAY_TIME_MASK, 0x2));
	regmap_update_bits(hdptx->regmap, SB_REG(0117),
			   FAST_PULSE_TIME_MASK,
			   FIELD_PREP(FAST_PULSE_TIME_MASK, 0x4));
	regmap_update_bits(hdptx->regmap, SB_REG(0118),
			   SB_TG_EARC_DMRX_RECVRD_CLK_CNT_MASK,
			   FIELD_PREP(SB_TG_EARC_DMRX_RECVRD_CLK_CNT_MASK, 0xa));

	regmap_update_bits(hdptx->regmap, SB_REG(011a), SB_TG_CNT_RUN_NO_7_0_MASK,
			   FIELD_PREP(SB_TG_CNT_RUN_NO_7_0_MASK, 0x3));
	regmap_update_bits(hdptx->regmap, SB_REG(011b),
			   SB_EARC_SIG_DET_BYPASS_MASK | SB_AFC_TOL_MASK,
			   FIELD_PREP(SB_EARC_SIG_DET_BYPASS_MASK, 0x1) |
			   FIELD_PREP(SB_AFC_TOL_MASK, 0x3));
	regmap_update_bits(hdptx->regmap, SB_REG(011c), SB_AFC_STB_NUM_MASK,
			   FIELD_PREP(SB_AFC_STB_NUM_MASK, 0x4));
	regmap_update_bits(hdptx->regmap, SB_REG(011d), SB_TG_OSC_CNT_MIN_MASK,
			   FIELD_PREP(SB_TG_OSC_CNT_MIN_MASK, 0x67));
	regmap_update_bits(hdptx->regmap, SB_REG(011e), SB_TG_OSC_CNT_MAX_MASK,
			   FIELD_PREP(SB_TG_OSC_CNT_MAX_MASK, 0x6a));
	regmap_update_bits(hdptx->regmap, SB_REG(011f), SB_PWM_AFC_CTRL_MASK,
			   FIELD_PREP(SB_PWM_AFC_CTRL_MASK, 0x5));
	regmap_update_bits(hdptx->regmap, SB_REG(011f), SB_RCAL_RSTN_MASK,
			   FIELD_PREP(SB_RCAL_RSTN_MASK, 0x1));
	regmap_update_bits(hdptx->regmap, SB_REG(0120), SB_AUX_EN_IN_MASK,
			   FIELD_PREP(SB_AUX_EN_IN_MASK, 0x1));

	regmap_update_bits(hdptx->regmap, SB_REG(0102), OVRD_SB_RXTERM_EN_MASK,
			   FIELD_PREP(OVRD_SB_RXTERM_EN_MASK, 0x1));
	regmap_update_bits(hdptx->regmap, SB_REG(0103), OVRD_SB_RX_RESCAL_DONE_MASK,
			   FIELD_PREP(OVRD_SB_RX_RESCAL_DONE_MASK, 0x1));
	regmap_update_bits(hdptx->regmap, SB_REG(0104), OVRD_SB_EN_MASK,
			   FIELD_PREP(OVRD_SB_EN_MASK, 0x1));
	regmap_update_bits(hdptx->regmap, SB_REG(0104), OVRD_SB_AUX_EN_MASK,
			   FIELD_PREP(OVRD_SB_AUX_EN_MASK, 0x1));

	regmap_update_bits(hdptx->regmap, SB_REG(010f), OVRD_SB_VREG_EN_MASK,
			   FIELD_PREP(OVRD_SB_VREG_EN_MASK, 0x1));

	regmap_write(hdptx->grf, GRF_HDPTX_CON0,
		     HDPTX_I_BGR_EN << 16 | FIELD_PREP(HDPTX_I_BGR_EN, 0x1));
	regmap_write(hdptx->grf, GRF_HDPTX_CON0,
		     HDPTX_I_BIAS_EN << 16 | FIELD_PREP(HDPTX_I_BIAS_EN, 0x1));
	usleep_range(20, 25);

	reset_control_deassert(hdptx->rsts[RST_INIT].rstc);
	usleep_range(20, 25);
	reset_control_deassert(hdptx->rsts[RST_CMN].rstc);
	usleep_range(20, 25);

	regmap_update_bits(hdptx->regmap, SB_REG(0103), OVRD_SB_RX_RESCAL_DONE_MASK,
			   FIELD_PREP(OVRD_SB_RX_RESCAL_DONE_MASK, 0x1));
	usleep_range(100, 110);
	regmap_update_bits(hdptx->regmap, SB_REG(0104), SB_EN_MASK,
			   FIELD_PREP(SB_EN_MASK, 0x1));
	usleep_range(100, 110);
	regmap_update_bits(hdptx->regmap, SB_REG(0102), SB_RXTERM_EN_MASK,
			   FIELD_PREP(SB_RXTERM_EN_MASK, 0x1));
	usleep_range(20, 25);
	regmap_update_bits(hdptx->regmap, SB_REG(010f), SB_VREG_EN_MASK,
			   FIELD_PREP(SB_VREG_EN_MASK, 0x1));
	usleep_range(20, 25);
	regmap_update_bits(hdptx->regmap, SB_REG(0104), SB_AUX_EN_MASK,
			   FIELD_PREP(SB_AUX_EN_MASK, 0x1));
	usleep_range(100, 110);

	ret = regmap_read_poll_timeout(hdptx->grf, GRF_HDPTX_STATUS,
				       status, FIELD_GET(HDPTX_O_SB_RDY, status),
				       50, 1000);
	if (ret) {
		dev_err(hdptx->dev, "Failed to get phy sb ready: %d\n", ret);
		return ret;
	}

	return 0;
}

static int rk_hdptx_phy_power_on(struct phy *phy)
{
	struct rk_hdptx_phy *hdptx = phy_get_drvdata(phy);
	int bus_width = phy_get_bus_width(hdptx->phy);
	enum phy_mode mode = phy_get_mode(phy);
	int ret, lane;

	/*
	 * FIXME: Temporary workaround to pass pixel_clk_rate
	 * from the HDMI bridge driver until phy_configure_opts_hdmi
	 * becomes available in the PHY API.
	 */
	unsigned int rate = bus_width & 0xfffffff;

	dev_dbg(hdptx->dev, "%s bus_width=%x rate=%u\n",
		__func__, bus_width, rate);

	ret = rk_hdptx_phy_consumer_get(hdptx, rate);
	if (ret)
		return ret;

	if (mode == PHY_MODE_DP) {
		regmap_write(hdptx->grf, GRF_HDPTX_CON0,
			     HDPTX_MODE_SEL << 16 | FIELD_PREP(HDPTX_MODE_SEL, 0x1));

		for (lane = 0; lane < 4; lane++) {
			regmap_update_bits(hdptx->regmap, LANE_REG(031e) + 0x400 * lane,
					   LN_POLARITY_INV_MASK | LN_LANE_MODE_MASK,
					   FIELD_PREP(LN_POLARITY_INV_MASK, 0) |
					   FIELD_PREP(LN_LANE_MODE_MASK, 1));
		}

		regmap_update_bits(hdptx->regmap, LNTOP_REG(0200), PROTOCOL_SEL_MASK,
				   FIELD_PREP(PROTOCOL_SEL_MASK, 0x0));
		regmap_update_bits(hdptx->regmap, LNTOP_REG(0206), DATA_BUS_WIDTH_MASK,
				   FIELD_PREP(DATA_BUS_WIDTH_MASK, 0x1));
		regmap_update_bits(hdptx->regmap, LNTOP_REG(0206), DATA_BUS_WIDTH_SEL_MASK,
				   FIELD_PREP(DATA_BUS_WIDTH_SEL_MASK, 0x0));

		rk_hdptx_dp_pll_init(hdptx);

		ret = rk_hdptx_dp_aux_init(hdptx);
		if (ret)
			rk_hdptx_phy_consumer_put(hdptx, true);
	} else {
		regmap_write(hdptx->grf, GRF_HDPTX_CON0,
			     HDPTX_MODE_SEL << 16 | FIELD_PREP(HDPTX_MODE_SEL, 0x0));

		ret = rk_hdptx_ropll_tmds_mode_config(hdptx, rate);
		if (ret)
			rk_hdptx_phy_consumer_put(hdptx, true);
	}

	return ret;
}

static int rk_hdptx_phy_power_off(struct phy *phy)
{
	struct rk_hdptx_phy *hdptx = phy_get_drvdata(phy);

	return rk_hdptx_phy_consumer_put(hdptx, false);
}

static int rk_hdptx_phy_verify_config(struct rk_hdptx_phy *hdptx,
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

	if (dp->set_lanes) {
		switch (dp->lanes) {
		case 1:
		case 2:
		case 4:
			break;
		default:
			return -EINVAL;
		}
	}

	if (dp->set_voltages) {
		for (i = 0; i < hdptx->lanes; i++) {
			if (dp->voltage[i] > 3 || dp->pre[i] > 3)
				return -EINVAL;

			if (dp->voltage[i] + dp->pre[i] > 3)
				return -EINVAL;
		}
	}

	return 0;
}

static int rk_hdptx_phy_set_rate(struct rk_hdptx_phy *hdptx,
				 struct phy_configure_opts_dp *dp)
{
	u32 bw, status;
	int ret;

	regmap_write(hdptx->grf, GRF_HDPTX_CON0,
		     HDPTX_I_PLL_EN << 16 | FIELD_PREP(HDPTX_I_PLL_EN, 0x0));

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
	hdptx->link_rate = dp->link_rate;

	regmap_update_bits(hdptx->regmap, CMN_REG(0008), OVRD_LCPLL_EN_MASK | LCPLL_EN_MASK,
			   FIELD_PREP(OVRD_LCPLL_EN_MASK, 0x1) |
			   FIELD_PREP(LCPLL_EN_MASK, 0x0));

	regmap_update_bits(hdptx->regmap, CMN_REG(003d), OVRD_ROPLL_EN_MASK | ROPLL_EN_MASK,
			   FIELD_PREP(OVRD_ROPLL_EN_MASK, 0x1) |
			   FIELD_PREP(ROPLL_EN_MASK, 0x1));

	if (dp->ssc) {
		regmap_update_bits(hdptx->regmap, CMN_REG(0074),
				   OVRD_ROPLL_SSC_EN_MASK | ROPLL_SSC_EN_MASK,
				   FIELD_PREP(OVRD_ROPLL_SSC_EN_MASK, 0x1) |
				   FIELD_PREP(ROPLL_SSC_EN_MASK, 0x1));
		regmap_write(hdptx->regmap, CMN_REG(0075),
			     FIELD_PREP(ANA_ROPLL_SSC_FM_DEVIATION_MASK, 0xc));
		regmap_update_bits(hdptx->regmap, CMN_REG(0076),
				   ANA_ROPLL_SSC_FM_FREQ_MASK,
				   FIELD_PREP(ANA_ROPLL_SSC_FM_FREQ_MASK, 0x1f));

		regmap_update_bits(hdptx->regmap, CMN_REG(0099), SSC_EN_MASK,
				   FIELD_PREP(SSC_EN_MASK, 0x2));
	} else {
		regmap_update_bits(hdptx->regmap, CMN_REG(0074),
				   OVRD_ROPLL_SSC_EN_MASK | ROPLL_SSC_EN_MASK,
				   FIELD_PREP(OVRD_ROPLL_SSC_EN_MASK, 0x1) |
				   FIELD_PREP(ROPLL_SSC_EN_MASK, 0x0));
		regmap_write(hdptx->regmap, CMN_REG(0075),
			     FIELD_PREP(ANA_ROPLL_SSC_FM_DEVIATION_MASK, 0x20));
		regmap_update_bits(hdptx->regmap, CMN_REG(0076),
				   ANA_ROPLL_SSC_FM_FREQ_MASK,
				   FIELD_PREP(ANA_ROPLL_SSC_FM_FREQ_MASK, 0xc));

		regmap_update_bits(hdptx->regmap, CMN_REG(0099), SSC_EN_MASK,
				   FIELD_PREP(SSC_EN_MASK, 0x0));
	}

	regmap_update_bits(hdptx->regmap, CMN_REG(0095), DP_TX_LINK_BW_MASK,
			   FIELD_PREP(DP_TX_LINK_BW_MASK, bw));

	regmap_write(hdptx->grf, GRF_HDPTX_CON0,
		     HDPTX_I_PLL_EN << 16 | FIELD_PREP(HDPTX_I_PLL_EN, 0x1));

	ret = regmap_read_poll_timeout(hdptx->grf, GRF_HDPTX_STATUS,
				       status, FIELD_GET(HDPTX_O_PLL_LOCK_DONE, status),
				       50, 1000);
	if (ret) {
		dev_err(hdptx->dev, "Failed to get phy pll lock: %d\n", ret);
		return ret;
	}

	return 0;
}

static int rk_hdptx_phy_set_lanes(struct rk_hdptx_phy *hdptx,
				  struct phy_configure_opts_dp *dp)
{
	hdptx->lanes = dp->lanes;

	regmap_update_bits(hdptx->regmap, LNTOP_REG(0207), LANE_EN_MASK,
			   FIELD_PREP(LANE_EN_MASK, GENMASK(hdptx->lanes - 1, 0)));

	return 0;
}

static void rk_hdptx_phy_set_voltage(struct rk_hdptx_phy *hdptx,
				     struct phy_configure_opts_dp *dp,
				     u8 lane)
{
	const struct tx_drv_ctrl *ctrl;
	u32 offset = lane * 0x400;

	switch (hdptx->link_rate) {
	case 1620:
		ctrl = &tx_drv_ctrl_rbr[dp->voltage[lane]][dp->pre[lane]];
		regmap_update_bits(hdptx->regmap, LANE_REG(030a) + offset,
				   LN_TX_JEQ_EVEN_CTRL_RBR_MASK,
				   FIELD_PREP(LN_TX_JEQ_EVEN_CTRL_RBR_MASK,
				   ctrl->tx_jeq_even_ctrl));
		regmap_update_bits(hdptx->regmap, LANE_REG(030c) + offset,
				   LN_TX_JEQ_ODD_CTRL_RBR_MASK,
				   FIELD_PREP(LN_TX_JEQ_ODD_CTRL_RBR_MASK,
				   ctrl->tx_jeq_odd_ctrl));
		regmap_update_bits(hdptx->regmap, LANE_REG(0311) + offset,
				   LN_TX_SER_40BIT_EN_RBR_MASK,
				   FIELD_PREP(LN_TX_SER_40BIT_EN_RBR_MASK, 0x1));
		break;
	case 2700:
		ctrl = &tx_drv_ctrl_hbr[dp->voltage[lane]][dp->pre[lane]];
		regmap_update_bits(hdptx->regmap, LANE_REG(030b) + offset,
				   LN_TX_JEQ_EVEN_CTRL_HBR_MASK,
				   FIELD_PREP(LN_TX_JEQ_EVEN_CTRL_HBR_MASK,
				   ctrl->tx_jeq_even_ctrl));
		regmap_update_bits(hdptx->regmap, LANE_REG(030d) + offset,
				   LN_TX_JEQ_ODD_CTRL_HBR_MASK,
				   FIELD_PREP(LN_TX_JEQ_ODD_CTRL_HBR_MASK,
				   ctrl->tx_jeq_odd_ctrl));
		regmap_update_bits(hdptx->regmap, LANE_REG(0311) + offset,
				   LN_TX_SER_40BIT_EN_HBR_MASK,
				   FIELD_PREP(LN_TX_SER_40BIT_EN_HBR_MASK, 0x1));
		break;
	case 5400:
	default:
		ctrl = &tx_drv_ctrl_hbr2[dp->voltage[lane]][dp->pre[lane]];
		regmap_update_bits(hdptx->regmap, LANE_REG(030b) + offset,
				   LN_TX_JEQ_EVEN_CTRL_HBR2_MASK,
				   FIELD_PREP(LN_TX_JEQ_EVEN_CTRL_HBR2_MASK,
				   ctrl->tx_jeq_even_ctrl));
		regmap_update_bits(hdptx->regmap, LANE_REG(030d) + offset,
				   LN_TX_JEQ_ODD_CTRL_HBR2_MASK,
				   FIELD_PREP(LN_TX_JEQ_ODD_CTRL_HBR2_MASK,
				   ctrl->tx_jeq_odd_ctrl));
		regmap_update_bits(hdptx->regmap, LANE_REG(0311) + offset,
				   LN_TX_SER_40BIT_EN_HBR2_MASK,
				   FIELD_PREP(LN_TX_SER_40BIT_EN_HBR2_MASK, 0x1));
		break;
	}

	regmap_update_bits(hdptx->regmap, LANE_REG(0303) + offset,
			   OVRD_LN_TX_DRV_LVL_CTRL_MASK | LN_TX_DRV_LVL_CTRL_MASK,
			   FIELD_PREP(OVRD_LN_TX_DRV_LVL_CTRL_MASK, 0x1) |
			   FIELD_PREP(LN_TX_DRV_LVL_CTRL_MASK,
				      ctrl->tx_drv_lvl_ctrl));
	regmap_update_bits(hdptx->regmap, LANE_REG(0304) + offset,
			   OVRD_LN_TX_DRV_POST_LVL_CTRL_MASK |
			   LN_TX_DRV_POST_LVL_CTRL_MASK,
			   FIELD_PREP(OVRD_LN_TX_DRV_POST_LVL_CTRL_MASK, 0x1) |
			   FIELD_PREP(LN_TX_DRV_POST_LVL_CTRL_MASK,
				      ctrl->tx_drv_post_lvl_ctrl));
	regmap_update_bits(hdptx->regmap, LANE_REG(0305) + offset,
			   OVRD_LN_TX_DRV_PRE_LVL_CTRL_MASK |
			   LN_TX_DRV_PRE_LVL_CTRL_MASK,
			   FIELD_PREP(OVRD_LN_TX_DRV_PRE_LVL_CTRL_MASK, 0x1) |
			   FIELD_PREP(LN_TX_DRV_PRE_LVL_CTRL_MASK,
				      ctrl->tx_drv_pre_lvl_ctrl));
	regmap_update_bits(hdptx->regmap, LANE_REG(0306) + offset,
			   LN_ANA_TX_DRV_IDRV_IDN_CTRL_MASK |
			   LN_ANA_TX_DRV_IDRV_IUP_CTRL_MASK |
			   LN_ANA_TX_DRV_ACCDRV_EN_MASK,
			   FIELD_PREP(LN_ANA_TX_DRV_IDRV_IDN_CTRL_MASK,
				      ctrl->ana_tx_drv_idrv_idn_ctrl) |
			   FIELD_PREP(LN_ANA_TX_DRV_IDRV_IUP_CTRL_MASK,
				      ctrl->ana_tx_drv_idrv_iup_ctrl) |
			   FIELD_PREP(LN_ANA_TX_DRV_ACCDRV_EN_MASK,
				      ctrl->ana_tx_drv_accdrv_en));
	regmap_update_bits(hdptx->regmap, LANE_REG(0307) + offset,
			   LN_ANA_TX_DRV_ACCDRV_POL_SEL_MASK |
			   LN_ANA_TX_DRV_ACCDRV_CTRL_MASK,
			   FIELD_PREP(LN_ANA_TX_DRV_ACCDRV_POL_SEL_MASK, 0x1) |
			   FIELD_PREP(LN_ANA_TX_DRV_ACCDRV_CTRL_MASK,
				      ctrl->ana_tx_drv_accdrv_ctrl));

	regmap_update_bits(hdptx->regmap, LANE_REG(030a) + offset,
			   LN_ANA_TX_JEQ_EN_MASK,
			   FIELD_PREP(LN_ANA_TX_JEQ_EN_MASK, ctrl->ana_tx_jeq_en));

	regmap_update_bits(hdptx->regmap, LANE_REG(0310) + offset,
			   LN_ANA_TX_SYNC_LOSS_DET_MODE_MASK,
			   FIELD_PREP(LN_ANA_TX_SYNC_LOSS_DET_MODE_MASK, 0x3));

	regmap_update_bits(hdptx->regmap, LANE_REG(0316) + offset,
			   LN_ANA_TX_SER_VREG_GAIN_CTRL_MASK,
			   FIELD_PREP(LN_ANA_TX_SER_VREG_GAIN_CTRL_MASK, 0x2));

	regmap_update_bits(hdptx->regmap, LANE_REG(031b) + offset,
			   LN_ANA_TX_RESERVED_MASK,
			   FIELD_PREP(LN_ANA_TX_RESERVED_MASK, 0x1));
}

static int rk_hdptx_phy_set_voltages(struct rk_hdptx_phy *hdptx,
				     struct phy_configure_opts_dp *dp)
{
	u8 lane;
	u32 status;
	int ret;

	for (lane = 0; lane < hdptx->lanes; lane++)
		rk_hdptx_phy_set_voltage(hdptx, dp, lane);

	reset_control_deassert(hdptx->rsts[RST_LANE].rstc);

	ret = regmap_read_poll_timeout(hdptx->grf, GRF_HDPTX_STATUS,
				       status, FIELD_GET(HDPTX_O_PHY_RDY, status),
				       50, 5000);
	if (ret) {
		dev_err(hdptx->dev, "Failed to get phy ready: %d\n", ret);
		return ret;
	}

	return 0;
}

static int rk_hdptx_phy_configure(struct phy *phy, union phy_configure_opts *opts)
{
	struct rk_hdptx_phy *hdptx = phy_get_drvdata(phy);
	enum phy_mode mode = phy_get_mode(phy);
	int ret;

	if (mode != PHY_MODE_DP)
		return 0;

	ret = rk_hdptx_phy_verify_config(hdptx, &opts->dp);
	if (ret) {
		dev_err(hdptx->dev, "invalid params for phy configure\n");
		return ret;
	}

	if (opts->dp.set_rate) {
		ret = rk_hdptx_phy_set_rate(hdptx, &opts->dp);
		if (ret) {
			dev_err(hdptx->dev, "failed to set rate: %d\n", ret);
			return ret;
		}
	}

	if (opts->dp.set_lanes) {
		ret = rk_hdptx_phy_set_lanes(hdptx, &opts->dp);
		if (ret) {
			dev_err(hdptx->dev, "failed to set lanes: %d\n", ret);
			return ret;
		}
	}

	if (opts->dp.set_voltages) {
		ret = rk_hdptx_phy_set_voltages(hdptx, &opts->dp);
		if (ret) {
			dev_err(hdptx->dev, "failed to set voltages: %d\n",
				ret);
			return ret;
		}
	}

	return 0;
}

static const struct phy_ops rk_hdptx_phy_ops = {
	.power_on  = rk_hdptx_phy_power_on,
	.power_off = rk_hdptx_phy_power_off,
	.configure = rk_hdptx_phy_configure,
	.owner	   = THIS_MODULE,
};

static struct rk_hdptx_phy *to_rk_hdptx_phy(struct clk_hw *hw)
{
	return container_of(hw, struct rk_hdptx_phy, hw);
}

static int rk_hdptx_phy_clk_prepare(struct clk_hw *hw)
{
	struct rk_hdptx_phy *hdptx = to_rk_hdptx_phy(hw);

	return rk_hdptx_phy_consumer_get(hdptx, hdptx->rate / 100);
}

static void rk_hdptx_phy_clk_unprepare(struct clk_hw *hw)
{
	struct rk_hdptx_phy *hdptx = to_rk_hdptx_phy(hw);

	rk_hdptx_phy_consumer_put(hdptx, true);
}

static unsigned long rk_hdptx_phy_clk_recalc_rate(struct clk_hw *hw,
						  unsigned long parent_rate)
{
	struct rk_hdptx_phy *hdptx = to_rk_hdptx_phy(hw);

	return hdptx->rate;
}

static long rk_hdptx_phy_clk_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *parent_rate)
{
	u32 bit_rate = rate / 100;
	int i;

	if (rate > HDMI20_MAX_RATE)
		return rate;

	for (i = 0; i < ARRAY_SIZE(ropll_tmds_cfg); i++)
		if (bit_rate == ropll_tmds_cfg[i].bit_rate)
			break;

	if (i == ARRAY_SIZE(ropll_tmds_cfg) &&
	    !rk_hdptx_phy_clk_pll_calc(bit_rate, NULL))
		return -EINVAL;

	return rate;
}

static int rk_hdptx_phy_clk_set_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long parent_rate)
{
	struct rk_hdptx_phy *hdptx = to_rk_hdptx_phy(hw);

	return rk_hdptx_ropll_tmds_cmn_config(hdptx, rate / 100);
}

static const struct clk_ops hdptx_phy_clk_ops = {
	.prepare = rk_hdptx_phy_clk_prepare,
	.unprepare = rk_hdptx_phy_clk_unprepare,
	.recalc_rate = rk_hdptx_phy_clk_recalc_rate,
	.round_rate = rk_hdptx_phy_clk_round_rate,
	.set_rate = rk_hdptx_phy_clk_set_rate,
};

static int rk_hdptx_phy_clk_register(struct rk_hdptx_phy *hdptx)
{
	struct device *dev = hdptx->dev;
	const char *name, *pname;
	struct clk *refclk;
	int ret;

	refclk = devm_clk_get(dev, "ref");
	if (IS_ERR(refclk))
		return dev_err_probe(dev, PTR_ERR(refclk),
				     "Failed to get ref clock\n");

	name = hdptx->phy_id > 0 ? "clk_hdmiphy_pixel1" : "clk_hdmiphy_pixel0";
	pname = __clk_get_name(refclk);

	hdptx->hw.init = CLK_HW_INIT(name, pname, &hdptx_phy_clk_ops,
				     CLK_GET_RATE_NOCACHE);

	ret = devm_clk_hw_register(dev, &hdptx->hw);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register clock\n");

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, &hdptx->hw);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to register clk provider\n");
	return 0;
}

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
	struct resource *res;
	void __iomem *regs;
	int ret, id;

	hdptx = devm_kzalloc(dev, sizeof(*hdptx), GFP_KERNEL);
	if (!hdptx)
		return -ENOMEM;

	hdptx->dev = dev;

	regs = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(regs))
		return dev_err_probe(dev, PTR_ERR(regs),
				     "Failed to ioremap resource\n");

	hdptx->cfgs = device_get_match_data(dev);
	if (!hdptx->cfgs)
		return dev_err_probe(dev, -EINVAL, "missing match data\n");

	/* find the phy-id from the io address */
	hdptx->phy_id = -ENODEV;
	for (id = 0; id < hdptx->cfgs->num_phys; id++) {
		if (res->start == hdptx->cfgs->phy_ids[id]) {
			hdptx->phy_id = id;
			break;
		}
	}

	if (hdptx->phy_id < 0)
		return dev_err_probe(dev, -ENODEV, "no matching device found\n");

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

	hdptx->rsts[RST_APB].id = "apb";
	hdptx->rsts[RST_INIT].id = "init";
	hdptx->rsts[RST_CMN].id = "cmn";
	hdptx->rsts[RST_LANE].id = "lane";

	ret = devm_reset_control_bulk_get_exclusive(dev, RST_MAX, hdptx->rsts);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get resets\n");

	hdptx->grf = syscon_regmap_lookup_by_phandle(dev->of_node,
						     "rockchip,grf");
	if (IS_ERR(hdptx->grf))
		return dev_err_probe(dev, PTR_ERR(hdptx->grf),
				     "Could not get GRF syscon\n");

	platform_set_drvdata(pdev, hdptx);

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable runtime PM\n");

	hdptx->phy = devm_phy_create(dev, NULL, &rk_hdptx_phy_ops);
	if (IS_ERR(hdptx->phy))
		return dev_err_probe(dev, PTR_ERR(hdptx->phy),
				     "Failed to create HDMI PHY\n");

	phy_set_drvdata(hdptx->phy, hdptx);
	phy_set_bus_width(hdptx->phy, 8);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider))
		return dev_err_probe(dev, PTR_ERR(phy_provider),
				     "Failed to register PHY provider\n");

	reset_control_deassert(hdptx->rsts[RST_APB].rstc);
	reset_control_deassert(hdptx->rsts[RST_CMN].rstc);
	reset_control_deassert(hdptx->rsts[RST_INIT].rstc);

	return rk_hdptx_phy_clk_register(hdptx);
}

static const struct dev_pm_ops rk_hdptx_phy_pm_ops = {
	RUNTIME_PM_OPS(rk_hdptx_phy_runtime_suspend,
		       rk_hdptx_phy_runtime_resume, NULL)
};

static const struct rk_hdptx_phy_cfg rk3576_hdptx_phy_cfgs = {
	.num_phys = 1,
	.phy_ids = {
		0x2b000000,
	},
};

static const struct rk_hdptx_phy_cfg rk3588_hdptx_phy_cfgs = {
	.num_phys = 2,
	.phy_ids = {
		0xfed60000,
		0xfed70000,
	},
};

static const struct of_device_id rk_hdptx_phy_of_match[] = {
	{
		.compatible = "rockchip,rk3576-hdptx-phy",
		.data = &rk3576_hdptx_phy_cfgs
	},
	{
		.compatible = "rockchip,rk3588-hdptx-phy",
		.data = &rk3588_hdptx_phy_cfgs
	},
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
MODULE_AUTHOR("Damon Ding <damon.ding@rock-chips.com>");
MODULE_DESCRIPTION("Samsung HDMI/eDP Transmitter Combo PHY Driver");
MODULE_LICENSE("GPL");
