/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_PANEL_H_
#define _DP_PANEL_H_

#include <drm/msm_drm.h>

#include "dp_aux.h"
#include "dp_link.h"

struct edid;

struct dp_display_mode {
	struct drm_display_mode drm_mode;
	u32 bpp;
	u32 h_active_low;
	u32 v_active_low;
	bool out_fmt_is_yuv_420;
};

struct dp_panel_in {
	struct device *dev;
	struct drm_dp_aux *aux;
	struct dp_link *link;
	struct dp_catalog *catalog;
};

struct dp_panel_psr {
	u8 version;
	u8 capabilities;
};

struct dp_panel {
	/* dpcd raw data */
	u8 dpcd[DP_RECEIVER_CAP_SIZE];
	u8 downstream_ports[DP_MAX_DOWNSTREAM_PORTS];

	struct dp_link_info link_info;
	const struct drm_edid *drm_edid;
	struct drm_connector *connector;
	struct dp_display_mode dp_mode;
	struct dp_panel_psr psr_cap;
	bool video_test;
	bool vsc_sdp_supported;

	u32 max_dp_lanes;
	u32 max_dp_link_rate;

	u32 max_bw_code;
};

int dp_panel_init_panel_info(struct dp_panel *dp_panel);
int dp_panel_deinit(struct dp_panel *dp_panel);
int dp_panel_timing_cfg(struct dp_panel *dp_panel);
void dp_panel_dump_regs(struct dp_panel *dp_panel);
int dp_panel_read_sink_caps(struct dp_panel *dp_panel,
		struct drm_connector *connector);
u32 dp_panel_get_mode_bpp(struct dp_panel *dp_panel, u32 mode_max_bpp,
			u32 mode_pclk_khz);
int dp_panel_get_modes(struct dp_panel *dp_panel,
		struct drm_connector *connector);
void dp_panel_handle_sink_request(struct dp_panel *dp_panel);
void dp_panel_tpg_config(struct dp_panel *dp_panel, bool enable);

/**
 * is_link_rate_valid() - validates the link rate
 * @lane_rate: link rate requested by the sink
 *
 * Returns true if the requested link rate is supported.
 */
static inline bool is_link_rate_valid(u32 bw_code)
{
	return (bw_code == DP_LINK_BW_1_62 ||
		bw_code == DP_LINK_BW_2_7 ||
		bw_code == DP_LINK_BW_5_4 ||
		bw_code == DP_LINK_BW_8_1);
}

/**
 * dp_link_is_lane_count_valid() - validates the lane count
 * @lane_count: lane count requested by the sink
 *
 * Returns true if the requested lane count is supported.
 */
static inline bool is_lane_count_valid(u32 lane_count)
{
	return (lane_count == 1 ||
		lane_count == 2 ||
		lane_count == 4);
}

struct dp_panel *dp_panel_get(struct dp_panel_in *in);
void dp_panel_put(struct dp_panel *dp_panel);
#endif /* _DP_PANEL_H_ */
