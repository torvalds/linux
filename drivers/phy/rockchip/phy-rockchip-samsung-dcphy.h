/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip Samsung mipi dcphy driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 */

#ifndef _PHY_ROCKCHIP_SAMSUNG_DCPHY_H_
#define _PHY_ROCKCHIP_SAMSUNG_DCPHY_H_

#define MAX_NUM_CSI2_DPHY	(0x2)

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
};

#endif
