/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip Vehicle driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 *
 */

#ifndef _VEHICLE_SAMSUNG_DCPHY_COMMON_H_
#define _VEHICLE_SAMSUNG_DCPHY_COMMON_H_

#define MAX_NUM_CSI2_DPHY	(0x2)

/*redefine samsung_mipi_dcphy info*/
struct samsung_mipi_dcphy {
	struct device *dev;
	struct clk *ref_clk;
	struct clk *pclk;
	struct regmap *regmap;
	struct regmap *grf_regmap;
	struct reset_control *m_phy_rst;
	struct reset_control *s_phy_rst;
	struct reset_control *apb_rst;
	struct reset_control *grf_apb_rst;
	struct mutex mutex;
	struct csi2_dphy *dphy_dev[MAX_NUM_CSI2_DPHY];
	atomic_t stream_cnt;
	int dphy_dev_num;
	bool c_option;

	unsigned int lanes;

	struct {
		unsigned long long rate;
		u8 prediv;
		u16 fbdiv;
		long dsm;
		u8 scaler;

		bool ssc_en;
		u8 mfr;
		u8 mrr;
	} pll;

	int (*stream_on)(struct csi2_dphy *dphy, struct v4l2_subdev *sd);
	int (*stream_off)(struct csi2_dphy *dphy, struct v4l2_subdev *sd);

	/*for vehicle*/
	struct csi2_dphy_hw *dphy_vehicle[MAX_NUM_CSI2_DPHY];
	int dphy_vehicle_num;
};

#define UPDATE(x, h, l)	(((x) << (l)) & GENMASK((h), (l)))

/*samsung mipi dcphy register*/
#define BIAS_CON0		0x0000
#define BIAS_CON1		0x0004
#define BIAS_CON2		0x0008
#define BIAS_CON4		0x0010
#define I_MUX_SEL_MASK		GENMASK(6, 5)
#define I_MUX_SEL(x)		UPDATE(x, 6, 5)

#define PLL_CON0		0x0100
#define PLL_EN			BIT(12)
#define S_MASK			GENMASK(10, 8)
#define S(x)			UPDATE(x, 10, 8)
#define P_MASK			GENMASK(5, 0)
#define P(x)			UPDATE(x, 5, 0)
#define PLL_CON1		0x0104
#define PLL_CON2		0x0108
#define M_MASK			GENMASK(9, 0)
#define M(x)			UPDATE(x, 9, 0)
#define PLL_CON3		0x010c
#define MRR_MASK		GENMASK(13, 8)
#define MRR(x)			UPDATE(x, 13, 8)
#define MFR_MASK                GENMASK(7, 0)
#define MFR(x)			UPDATE(x, 7, 0)
#define PLL_CON4		0x0110
#define SSCG_EN			BIT(11)
#define PLL_CON5		0x0114
#define RESET_N_SEL		BIT(10)
#define PLL_ENABLE_SEL		BIT(8)
#define PLL_CON6		0x0118
#define PLL_CON7		0x011c
#define PLL_LOCK_CNT(x)		UPDATE(x, 15, 0)
#define PLL_CON8		0x0120
#define PLL_STB_CNT(x)		UPDATE(x, 15, 0)
#define PLL_STAT0		0x0140
#define PLL_LOCK		BIT(0)

#define DPHY_MC_GNR_CON0	0x0300
#define PHY_READY		BIT(1)
#define PHY_ENABLE		BIT(0)
#define DPHY_MC_GNR_CON1	0x0304
#define T_PHY_READY(x)		UPDATE(x, 15, 0)
#define DPHY_MC_ANA_CON0	0x0308
#define DPHY_MC_ANA_CON1	0x030c
#define DPHY_MC_ANA_CON2	0x0310
#define HS_VREG_AMP_ICON(x)	UPDATE(x, 1, 0)
#define DPHY_MC_TIME_CON0	0x0330
#define HSTX_CLK_SEL		BIT(12)
#define T_LPX(x)		UPDATE(x, 11, 4)
#define DPHY_MC_TIME_CON1	0x0334
#define T_CLK_ZERO(x)		UPDATE(x, 15, 8)
#define T_CLK_PREPARE(x)	UPDATE(x, 7, 0)
#define DPHY_MC_TIME_CON2	0x0338
#define T_HS_EXIT(x)		UPDATE(x, 15, 8)
#define T_CLK_TRAIL(x)		UPDATE(x, 7, 0)
#define DPHY_MC_TIME_CON3	0x033c
#define T_CLK_POST(x)		UPDATE(x, 7, 0)
#define DPHY_MC_TIME_CON4	0x0340
#define T_ULPS_EXIT(x)		UPDATE(x, 9, 0)
#define DPHY_MC_DESKEW_CON0	0x0350
#define SKEW_CAL_RUN_TIME(x)	UPDATE(x, 15, 12)

#define SKEW_CAL_INIT_RUN_TIME(x)	UPDATE(x, 11, 8)
#define SKEW_CAL_INIT_WAIT_TIME(x)	UPDATE(x, 7, 4)
#define SKEW_CAL_EN			BIT(0)

#define COMBO_MD0_GNR_CON0	0x0400
#define COMBO_MD0_GNR_CON1	0x0404
#define COMBO_MD0_ANA_CON0	0x0408
#define COMBO_MD0_ANA_CON1      0x040C
#define COMBO_MD0_ANA_CON2	0x0410

#define COMBO_MD0_TIME_CON0	0x0430
#define COMBO_MD0_TIME_CON1	0x0434
#define COMBO_MD0_TIME_CON2	0x0438
#define COMBO_MD0_TIME_CON3	0x043C
#define COMBO_MD0_TIME_CON4	0x0440
#define COMBO_MD0_DATA_CON0	0x0444

#define COMBO_MD1_GNR_CON0	0x0500
#define COMBO_MD1_GNR_CON1	0x0504
#define COMBO_MD1_ANA_CON0	0x0508
#define COMBO_MD1_ANA_CON1	0x050c
#define COMBO_MD1_ANA_CON2	0x0510
#define COMBO_MD1_TIME_CON0	0x0530
#define COMBO_MD1_TIME_CON1	0x0534
#define COMBO_MD1_TIME_CON2	0x0538
#define COMBO_MD1_TIME_CON3	0x053C
#define COMBO_MD1_TIME_CON4	0x0540
#define COMBO_MD1_DATA_CON0	0x0544

#define COMBO_MD2_GNR_CON0	0x0600
#define COMBO_MD2_GNR_CON1	0x0604
#define COMBO_MD2_ANA_CON0	0X0608
#define COMBO_MD2_ANA_CON1	0X060C
#define COMBO_MD2_ANA_CON2	0X0610
#define COMBO_MD2_TIME_CON0	0x0630
#define COMBO_MD2_TIME_CON1	0x0634
#define COMBO_MD2_TIME_CON2	0x0638
#define COMBO_MD2_TIME_CON3	0x063C
#define COMBO_MD2_TIME_CON4	0x0640
#define COMBO_MD2_DATA_CON0	0x0644

#define DPHY_MD3_GNR_CON0	0x0700
#define DPHY_MD3_GNR_CON1	0x0704
#define DPHY_MD3_ANA_CON0	0X0708
#define DPHY_MD3_ANA_CON1	0X070C
#define DPHY_MD3_ANA_CON2	0X0710
#define DPHY_MD3_TIME_CON0	0x0730
#define DPHY_MD3_TIME_CON1	0x0734
#define DPHY_MD3_TIME_CON2	0x0738
#define DPHY_MD3_TIME_CON3	0x073C
#define DPHY_MD3_TIME_CON4	0x0740
#define DPHY_MD3_DATA_CON0	0x0744

#define T_LP_EXIT_SKEW(x)	UPDATE(x, 3, 2)
#define T_LP_ENTRY_SKEW(x)	UPDATE(x, 1, 0)
#define T_HS_ZERO(x)		UPDATE(x, 15, 8)
#define T_HS_PREPARE(x)		UPDATE(x, 7, 0)
#define T_HS_EXIT(x)		UPDATE(x, 15, 8)
#define T_HS_TRAIL(x)		UPDATE(x, 7, 0)
#define T_TA_GET(x)		UPDATE(x, 7, 4)
#define T_TA_GO(x)		UPDATE(x, 3, 0)

/* MIPI_CDPHY_GRF registers */
#define MIPI_DCPHY_GRF_CON0	0x0000
#define S_CPHY_MODE		HIWORD_UPDATE(1, 3, 3)
#define M_CPHY_MODE		HIWORD_UPDATE(1, 0, 0)

#define MAX_DPHY_BW		4500000L
#define MAX_CPHY_BW		2000000L

#define RX_CLK_THS_SETTLE		(0xb30)
#define RX_LANE0_THS_SETTLE		(0xC30)
#define RX_LANE0_ERR_SOT_SYNC		(0xC34)
#define RX_LANE1_THS_SETTLE		(0xD30)
#define RX_LANE1_ERR_SOT_SYNC		(0xD34)
#define RX_LANE2_THS_SETTLE		(0xE30)
#define RX_LANE2_ERR_SOT_SYNC		(0xE34)
#define RX_LANE3_THS_SETTLE		(0xF30)
#define RX_LANE3_ERR_SOT_SYNC		(0xF34)
#define RX_CLK_LANE_ENABLE		(0xB00)
#define RX_DATA_LANE0_ENABLE		(0xC00)
#define RX_DATA_LANE1_ENABLE		(0xD00)
#define RX_DATA_LANE2_ENABLE		(0xE00)
#define RX_DATA_LANE3_ENABLE		(0xF00)

#define RX_S0C_GNR_CON1			(0xB04)
#define RX_S0C_ANA_CON1			(0xB0c)
#define RX_S0C_ANA_CON2			(0xB10)
#define RX_S0C_ANA_CON3			(0xB14)
#define RX_COMBO_S0D0_GNR_CON1		(0xC04)
#define RX_COMBO_S0D0_ANA_CON1		(0xC0c)
#define RX_COMBO_S0D0_ANA_CON2		(0xC10)
#define RX_COMBO_S0D0_ANA_CON3		(0xC14)
#define RX_COMBO_S0D0_ANA_CON6		(0xC20)
#define RX_COMBO_S0D0_ANA_CON7		(0xC24)
#define RX_COMBO_S0D0_DESKEW_CON0	(0xC40)
#define RX_COMBO_S0D0_DESKEW_CON2	(0xC48)
#define RX_COMBO_S0D0_DESKEW_CON4	(0xC50)
#define RX_COMBO_S0D0_CRC_CON1		(0xC64)
#define RX_COMBO_S0D0_CRC_CON2		(0xC68)
#define RX_COMBO_S0D1_GNR_CON1		(0xD04)
#define RX_COMBO_S0D1_ANA_CON1		(0xD0c)
#define RX_COMBO_S0D1_ANA_CON2		(0xD10)
#define RX_COMBO_S0D1_ANA_CON3		(0xD14)
#define RX_COMBO_S0D1_ANA_CON6		(0xD20)
#define RX_COMBO_S0D1_ANA_CON7		(0xD24)
#define RX_COMBO_S0D1_DESKEW_CON0	(0xD40)
#define RX_COMBO_S0D1_DESKEW_CON2	(0xD48)
#define RX_COMBO_S0D1_DESKEW_CON4	(0xD50)
#define RX_COMBO_S0D1_CRC_CON1		(0xD64)
#define RX_COMBO_S0D1_CRC_CON2		(0xD68)
#define RX_COMBO_S0D2_GNR_CON1		(0xE04)
#define RX_COMBO_S0D2_ANA_CON1		(0xE0c)
#define RX_COMBO_S0D2_ANA_CON2		(0xE10)
#define RX_COMBO_S0D2_ANA_CON3		(0xE14)
#define RX_COMBO_S0D2_ANA_CON6		(0xE20)
#define RX_COMBO_S0D2_ANA_CON7		(0xE24)
#define RX_COMBO_S0D2_DESKEW_CON0	(0xE40)
#define RX_COMBO_S0D2_DESKEW_CON2	(0xE48)
#define RX_COMBO_S0D2_DESKEW_CON4	(0xE50)
#define RX_COMBO_S0D2_CRC_CON1		(0xE64)
#define RX_COMBO_S0D2_CRC_CON2		(0xE68)
#define RX_S0D3_GNR_CON1		(0xF04)
#define RX_S0D3_ANA_CON1		(0xF0c)
#define RX_S0D3_ANA_CON2		(0xF10)
#define RX_S0D3_ANA_CON3		(0xF14)
#define RX_S0D3_DESKEW_CON0		(0xF40)
#define RX_S0D3_DESKEW_CON2		(0xF48)
#define RX_S0D3_DESKEW_CON4		(0xF50)

#endif
