/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_DISPLAY_H_
#define _DP_DISPLAY_H_

#include "dp_panel.h"
#include <sound/hdmi-codec.h>
#include "disp/msm_disp_snapshot.h"

struct msm_dp {
	struct drm_device *drm_dev;
	struct device *codec_dev;
	struct drm_bridge *bridge;
	struct drm_connector *connector;
	struct drm_bridge *next_bridge;
	bool is_connected;
	bool audio_enabled;
	bool power_on;
	unsigned int connector_type;
	bool is_edp;

	hdmi_codec_plugged_cb plugged_cb;

	bool wide_bus_en;

	u32 max_dp_lanes;
	struct dp_audio *dp_audio;
};

int dp_display_set_plugged_cb(struct msm_dp *dp_display,
		hdmi_codec_plugged_cb fn, struct device *codec_dev);
int dp_display_get_modes(struct msm_dp *dp_display);
int dp_display_request_irq(struct msm_dp *dp_display);
bool dp_display_check_video_test(struct msm_dp *dp_display);
int dp_display_get_test_bpp(struct msm_dp *dp_display);
void dp_display_signal_audio_start(struct msm_dp *dp_display);
void dp_display_signal_audio_complete(struct msm_dp *dp_display);

#endif /* _DP_DISPLAY_H_ */
