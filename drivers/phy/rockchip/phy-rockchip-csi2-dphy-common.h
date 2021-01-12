/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip MIPI CSI2 DPHY driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 */

#ifndef _PHY_ROCKCHIP_CSI2_DPHY_COMMON_H_
#define _PHY_ROCKCHIP_CSI2_DPHY_COMMON_H_

/* add new chip id in tail by time order */
enum csi2_dphy_chip_id {
	CHIP_ID_RK3568 = 0x0,
};

enum csi2_dphy_rx_pads {
	CSI2_DPHY_RX_PAD_SINK = 0,
	CSI2_DPHY_RX_PAD_SOURCE,
	CSI2_DPHY_RX_PADS_NUM,
};

enum csi2_dphy_lane_mode {
	LANE_MODE_UNDEF = 0x0,
	LANE_MODE_FULL,
	LANE_MODE_SPLIT,
};

struct grf_reg {
	u32 offset;
	u32 mask;
	u32 shift;
};

struct csi2dphy_reg {
	u32 offset;
};

#define MAX_DPHY_SENSORS	(2)
#define MAX_NUM_CSI2_DPHY	(0x2)

struct csi2_sensor {
	struct v4l2_subdev *sd;
	struct v4l2_mbus_config mbus;
	struct v4l2_mbus_framefmt format;
	int lanes;
};

struct csi2_dphy_hw;

struct csi2_dphy {
	struct device *dev;
	struct list_head list;
	struct csi2_dphy_hw *dphy_hw;
	struct v4l2_async_notifier notifier;
	struct v4l2_subdev sd;
	struct mutex mutex; /* lock for updating protection */
	struct media_pad pads[CSI2_DPHY_RX_PADS_NUM];
	struct csi2_sensor sensors[MAX_DPHY_SENSORS];
	u64 data_rate_mbps;
	int num_sensors;
	int phy_index;
	bool is_streaming;
	enum csi2_dphy_lane_mode lane_mode;
};

struct dphy_hw_drv_data {
	const struct	clk_bulk_data *clks;
	int num_clks;
	const struct hsfreq_range *hsfreq_ranges;
	int num_hsfreq_ranges;
	const struct grf_reg *grf_regs;
	const struct txrx_reg *txrx_regs;
	const struct csi2dphy_reg *csi2dphy_regs;
	void (*individual_init)(struct csi2_dphy_hw *hw);
	enum csi2_dphy_chip_id chip_id;
};

struct csi2_dphy_hw {
	struct device *dev;
	struct regmap *regmap_grf;
	const struct grf_reg *grf_regs;
	const struct txrx_reg *txrx_regs;
	const struct csi2dphy_reg *csi2dphy_regs;
	const struct dphy_hw_drv_data *drv_data;
	void __iomem *hw_base_addr;
	struct clk_bulk_data	*clks;
	struct csi2_dphy *dphy_dev[MAX_NUM_CSI2_DPHY];
	struct v4l2_subdev sd;
	struct mutex mutex; /* lock for updating protection */
	atomic_t stream_cnt;
	int num_clks;
	int num_sensors;
	int dphy_dev_num;
	enum csi2_dphy_lane_mode lane_mode;

	int (*stream_on)(struct csi2_dphy *dphy, struct v4l2_subdev *sd);
	int (*stream_off)(struct csi2_dphy *dphy, struct v4l2_subdev *sd);
};

extern struct platform_driver rockchip_csi2_dphy_driver;

#endif
