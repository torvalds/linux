/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_LINK_H_
#define _DP_LINK_H_

#include "dp_aux.h"
#include <drm/display/drm_dp_helper.h>

#define DS_PORT_STATUS_CHANGED 0x200
#define DP_TEST_BIT_DEPTH_UNKNOWN 0xFFFFFFFF
#define DP_LINK_CAP_ENHANCED_FRAMING (1 << 0)

struct msm_dp_link_info {
	unsigned char revision;
	unsigned int rate;
	unsigned int num_lanes;
	unsigned long capabilities;
};

#define DP_TRAIN_LEVEL_MAX	3

struct msm_dp_link_test_video {
	u32 test_video_pattern;
	u32 test_bit_depth;
	u32 test_dyn_range;
	u32 test_h_total;
	u32 test_v_total;
	u32 test_h_start;
	u32 test_v_start;
	u32 test_hsync_pol;
	u32 test_hsync_width;
	u32 test_vsync_pol;
	u32 test_vsync_width;
	u32 test_h_width;
	u32 test_v_height;
	u32 test_rr_d;
	u32 test_rr_n;
};

struct msm_dp_link_test_audio {
	u32 test_audio_sampling_rate;
	u32 test_audio_channel_count;
	u32 test_audio_pattern_type;
	u32 test_audio_period_ch_1;
	u32 test_audio_period_ch_2;
	u32 test_audio_period_ch_3;
	u32 test_audio_period_ch_4;
	u32 test_audio_period_ch_5;
	u32 test_audio_period_ch_6;
	u32 test_audio_period_ch_7;
	u32 test_audio_period_ch_8;
};

struct msm_dp_link_phy_params {
	u32 phy_test_pattern_sel;
	u8 v_level;
	u8 p_level;
};

struct msm_dp_link {
	u8 lttpr_common_caps[DP_LTTPR_COMMON_CAP_SIZE];
	int lttpr_count;

	u32 sink_request;
	u32 test_response;

	u8 sink_count;
	struct msm_dp_link_test_video test_video;
	struct msm_dp_link_test_audio test_audio;
	struct msm_dp_link_phy_params phy_params;
	struct msm_dp_link_info link_params;
};

/**
 * mdss_dp_test_bit_depth_to_bpp() - convert test bit depth to bpp
 * @tbd: test bit depth
 *
 * Returns the bits per pixel (bpp) to be used corresponding to the
 * git bit depth value. This function assumes that bit depth has
 * already been validated.
 */
static inline u32 msm_dp_link_bit_depth_to_bpp(u32 tbd)
{
	/*
	 * Few simplistic rules and assumptions made here:
	 *    1. Bit depth is per color component
	 *    2. If bit depth is unknown return 0
	 *    3. Assume 3 color components
	 */
	switch (tbd) {
	case DP_TEST_BIT_DEPTH_6:
		return 18;
	case DP_TEST_BIT_DEPTH_8:
		return 24;
	case DP_TEST_BIT_DEPTH_10:
		return 30;
	case DP_TEST_BIT_DEPTH_UNKNOWN:
	default:
		return 0;
	}
}

void msm_dp_link_reset_phy_params_vx_px(struct msm_dp_link *msm_dp_link);
u32 msm_dp_link_get_test_bits_depth(struct msm_dp_link *msm_dp_link, u32 bpp);
int msm_dp_link_process_request(struct msm_dp_link *msm_dp_link);
int msm_dp_link_get_colorimetry_config(struct msm_dp_link *msm_dp_link);
int msm_dp_link_adjust_levels(struct msm_dp_link *msm_dp_link, u8 *link_status);
bool msm_dp_link_send_test_response(struct msm_dp_link *msm_dp_link);
int msm_dp_link_psm_config(struct msm_dp_link *msm_dp_link,
		struct msm_dp_link_info *link_info, bool enable);
bool msm_dp_link_send_edid_checksum(struct msm_dp_link *msm_dp_link, u8 checksum);

/**
 * msm_dp_link_get() - get the functionalities of dp test module
 *
 *
 * return: a pointer to msm_dp_link struct
 */
struct msm_dp_link *msm_dp_link_get(struct device *dev, struct drm_dp_aux *aux);

#endif /* _DP_LINK_H_ */
