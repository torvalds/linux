/*
 * Copyright (C) 2016 Chris Zhong <zyw@rock-chips.com>
 * Copyright (C) 2016 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _CDN_DP_CORE_H
#define _CDN_DP_CORE_H

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_panel.h>
#ifdef CONFIG_SWITCH
#include <linux/switch.h>
#endif
#include "rockchip_drm_drv.h"

#define MAX_PHY		2

enum audio_format {
	AFMT_I2S = 0,
	AFMT_SPDIF = 1,
	AFMT_UNUSED,
};

struct audio_info {
	enum audio_format format;
	int sample_rate;
	int channels;
	int sample_width;
};

enum vic_pxl_encoding_format {
	PXL_RGB = 0x1,
	YCBCR_4_4_4 = 0x2,
	YCBCR_4_2_2 = 0x4,
	YCBCR_4_2_0 = 0x8,
	Y_ONLY = 0x10,
};

struct video_info {
	bool h_sync_polarity;
	bool v_sync_polarity;
	bool interlaced;
	int color_depth;
	enum vic_pxl_encoding_format color_fmt;
};

struct cdn_firmware_header {
	u32 size_bytes; /* size of the entire header+image(s) in bytes */
	u32 header_size; /* size of just the header in bytes */
	u32 iram_size; /* size of iram */
	u32 dram_size; /* size of dram */
};

struct cdn_dp_port {
	struct cdn_dp_device *dp;
	struct notifier_block event_nb;
	struct extcon_dev *extcon;
	struct phy *phy;
	u8 lanes;
	bool phy_enabled;
	u8 id;
};

struct cdn_dp_device {
	struct device *dev;
	struct drm_device *drm_dev;
	struct drm_connector connector;
	struct drm_encoder encoder;
	struct drm_display_mode mode;
	struct platform_device *audio_pdev;
	struct work_struct event_work;
	struct edid *edid;

	struct mutex lock;
	bool connected;
	bool active;
	bool suspended;

	const struct firmware *fw;	/* cdn dp firmware */
	unsigned int fw_version;	/* cdn fw version */
	bool fw_loaded;

	void __iomem *regs;
	struct regmap *grf;
	struct clk *core_clk;
	struct clk *pclk;
	struct clk *spdif_clk;
	struct clk *grf_clk;
	struct reset_control *spdif_rst;
	struct reset_control *dptx_rst;
	struct reset_control *apb_rst;
	struct reset_control *core_rst;
	struct audio_info audio_info;
	struct video_info video_info;
	struct drm_dp_link link;
	struct cdn_dp_port *port[MAX_PHY];
	u8 ports;
	u8 lanes;

	u8 dpcd[DP_RECEIVER_CAP_SIZE];
	bool sink_has_audio;
#ifdef CONFIG_SWITCH
	struct switch_dev switchdev;
#endif
};
#endif  /* _CDN_DP_CORE_H */
