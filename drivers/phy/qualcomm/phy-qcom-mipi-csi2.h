/* SPDX-License-Identifier: GPL-2.0 */
/*
 *
 * Qualcomm MIPI CSI2 CPHY/DPHY driver
 *
 * Copyright (C) 2025 Linaro Ltd.
 */
#ifndef __PHY_QCOM_MIPI_CSI2_H__
#define __PHY_QCOM_MIPI_CSI2_H__

#include <linux/phy/phy.h>

#define CSI2_MAX_DATA_LANES	4
#define CSI2_DEFAULT_CLK_LANE	7

struct mipi_csi2phy_lane {
	u8 pos;
	u8 pol;
};

struct mipi_csi2phy_lanes_cfg {
	struct mipi_csi2phy_lane data[CSI2_MAX_DATA_LANES];
	struct mipi_csi2phy_lane clk;
};

struct mipi_csi2phy_stream_cfg {
	s64 link_freq;
	u8 num_data_lanes;
	struct mipi_csi2phy_lanes_cfg lane_cfg;
};

struct mipi_csi2phy_device;

struct mipi_csi2phy_hw_ops {
	void (*hw_version_read)(struct mipi_csi2phy_device *csi2phy_dev);
	void (*reset)(struct mipi_csi2phy_device *csi2phy_dev);
	int (*lanes_enable)(struct mipi_csi2phy_device *csi2phy_dev,
			    struct mipi_csi2phy_stream_cfg *cfg);
	void (*lanes_disable)(struct mipi_csi2phy_device *csi2phy_dev,
			      struct mipi_csi2phy_stream_cfg *cfg);
};

struct mipi_csi2phy_lane_regs {
	const s32 reg_addr;
	const s32 reg_data;
	const u32 delay_us;
	const u32 param_type;
};

struct mipi_csi2phy_device_regs {
	const struct mipi_csi2phy_lane_regs *init_seq;
	const int lane_array_size;
	const u32 common_regs_offset;
};

struct mipi_csi2_genpd {
	const char *name;
	bool scaled;
};

struct mipi_csi2phy_soc_cfg {
	const struct mipi_csi2phy_hw_ops *ops;
	const struct mipi_csi2phy_device_regs reg_info;

	const char ** const supply_names;
	const unsigned int num_supplies;

	const char ** const clk_names;
	const unsigned int num_clk;

	const struct mipi_csi2_genpd *genpds;
	const unsigned int num_genpds;
};

struct mipi_csi2phy_device {
	struct device *dev;
	u8 phy_mode;

	struct phy *phy;
	void __iomem *base;

	struct clk_bulk_data *clks;
	struct clk *timer_clk;
	u32 timer_clk_rate;

	struct regulator_bulk_data *supplies;
	struct dev_pm_domain_list *pd_list;

	const struct mipi_csi2phy_soc_cfg *soc_cfg;
	struct mipi_csi2phy_stream_cfg stream_cfg;

	u32 hw_version;
};

extern const struct mipi_csi2phy_soc_cfg mipi_csi2_dphy_4nm_x1e;

#endif /* __PHY_QCOM_MIPI_CSI2_H__ */
