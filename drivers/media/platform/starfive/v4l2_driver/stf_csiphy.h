/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 */
#ifndef STF_CSIPHY_H
#define STF_CSIPHY_H

#include <media/v4l2-subdev.h>
#include <media/v4l2-device.h>
#include <media/media-entity.h>
#include <video/stf-vin.h>

#define STF_CSIPHY_NAME "stf_csiphy"

#define STF_CSIPHY_PAD_SINK     0
#define STF_CSIPHY_PAD_SRC      1
#define STF_CSIPHY_PADS_NUM     2

#define STF_CSI2_MAX_DATA_LANES      4

union static_config {
	u32 raw;
	struct {
		u32 sel                 : 2;
		u32 rsvd_6              : 2;
		u32 v2p0_support_enable : 1;
		u32 rsvd_5              : 3;
		u32 lane_nb             : 3;
		u32 rsvd_4              : 5;
		u32 dl0_map             : 3;
		u32 rsvd_3              : 1;
		u32 dl1_map             : 3;
		u32 rsvd_2              : 1;
		u32 dl2_map             : 3;
		u32 rsvd_1              : 1;
		u32 dl3_map             : 3;
		u32 rsvd_0              : 1;
	} bits;
};

union error_bypass_cfg {
	u32 value;
	struct {
		u32 crc             :  1;
		u32 ecc             :  1;
		u32 data_id         :  1;
		u32 rsvd_0          : 29;
	};
};

union stream_monitor_ctrl {
	u32 value;
	struct {
		u32 lb_vc             : 4;
		u32 lb_en             : 1;
		u32 timer_vc          : 4;
		u32 timer_en          : 1;
		u32 timer_eof         : 1;
		u32 frame_mon_vc      : 4;
		u32 frame_mon_en      : 1;
		u32 frame_length      : 16;
	};
};

union stream_cfg {
	u32 value;
	struct {
		u32 interface_mode :  1;
		u32 ls_le_mode     :  1;
		u32 rsvd_3         :  2;
		u32 num_pixels     :  2;
		u32 rsvd_2         :  2;
		u32 fifo_mode      :  2;
		u32 rsvd_1         :  2;
		u32 bpp_bypass     :  3;
		u32 rsvd_0         :  1;
		u32 fifo_fill      : 16;
	};
};

union dphy_lane_ctrl {
	u32 raw;
	struct {
		u32 dl0_en    : 1;
		u32 dl1_en    : 1;
		u32 dl2_en    : 1;
		u32 dl3_en    : 1;
		u32 cl_en     : 1;
		u32 rsvd_1    : 7;
		u32 dl0_reset : 1;
		u32 dl1_reset : 1;
		u32 dl2_reset : 1;
		u32 dl3_reset : 1;
		u32 cl_reset  : 1;
		u32 rsvd_0    : 15;
	} bits;
};

union dphy_lane_swap {
	u32 raw;
	struct {
		u32 rx_1c2c_sel        : 1;
		u32 lane_swap_clk      : 3;
		u32 lane_swap_clk1     : 3;
		u32 lane_swap_lan0     : 3;
		u32 lane_swap_lan1     : 3;
		u32 lane_swap_lan2     : 3;
		u32 lane_swap_lan3     : 3;
		u32 dpdn_swap_clk      : 1;
		u32 dpdn_swap_clk1     : 1;
		u32 dpdn_swap_lan0     : 1;
		u32 dpdn_swap_lan1     : 1;
		u32 dpdn_swap_lan2     : 1;
		u32 dpdn_swap_lan3     : 1;
		u32 hs_freq_chang_clk0 : 1;
		u32 hs_freq_chang_clk1 : 1;
		u32 reserved           : 5;
	} bits;
};

union dphy_lane_en {
	u32 raw;
	struct {
		u32 gpio_en		: 6;
		u32 mp_test_mode_sel	: 5;
		u32 mp_test_en		: 1;
		u32 dphy_enable_lan0	: 1;
		u32 dphy_enable_lan1	: 1;
		u32 dphy_enable_lan2	: 1;
		u32 dphy_enable_lan3	: 1;
		u32 rsvd_0		: 16;
	} bits;
};

struct csiphy_format {
	u32 code;
	u8 bpp;
};

struct csi2phy_cfg {
	unsigned int flags;
	unsigned char data_lanes[STF_CSI2_MAX_DATA_LANES];
	unsigned char clock_lane;
	unsigned char num_data_lanes;
	bool lane_polarities[1 + STF_CSI2_MAX_DATA_LANES];
};

struct csi2phy_cfg2 {
	unsigned char data_lanes[STF_CSI2_MAX_DATA_LANES];
	unsigned char num_data_lanes;
	unsigned char num_clks;
	unsigned char clock_lane;
	unsigned char clock1_lane;
	bool lane_polarities[2 + STF_CSI2_MAX_DATA_LANES];
};

struct stf_csiphy_dev;

struct csiphy_hw_ops {
	int (*csiphy_clk_enable)(struct stf_csiphy_dev *csiphy_dev);
	int (*csiphy_clk_disable)(struct stf_csiphy_dev *csiphy_dev);
	int (*csiphy_config_set)(struct stf_csiphy_dev *csiphy_dev);
	int (*csiphy_stream_set)(struct stf_csiphy_dev *csiphy_dev, int on);
};

struct stf_csiphy_dev {
	struct stfcamss *stfcamss;
	struct csi2phy_cfg *csiphy;
	struct v4l2_subdev subdev;
	struct media_pad pads[STF_CSIPHY_PADS_NUM];
	struct v4l2_mbus_framefmt fmt[STF_CSIPHY_PADS_NUM];
	const struct csiphy_format *formats;
	unsigned int nformats;
	struct csiphy_hw_ops *hw_ops;
	struct mutex stream_lock;
	int stream_count;
};

extern int stf_csiphy_subdev_init(struct stfcamss *stfcamss);
extern int stf_csiphy_register(struct stf_csiphy_dev *csiphy_dev,
			struct v4l2_device *v4l2_dev);
extern int stf_csiphy_unregister(struct stf_csiphy_dev *csiphy_dev);

extern struct csiphy_hw_ops csiphy_ops;

#endif /* STF_CSIPHY_H */
