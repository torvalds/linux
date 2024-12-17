/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_DISPLAY_H_
#define _DP_DISPLAY_H_

#include "dp_panel.h"
#include <sound/hdmi-codec.h>
#include "disp/msm_disp_snapshot.h"

#define DP_MAX_PIXEL_CLK_KHZ	675000

struct msm_dp {
	struct drm_device *drm_dev;
	struct platform_device *pdev;
	struct device *codec_dev;
	struct drm_connector *connector;
	struct drm_bridge *next_bridge;
	bool link_ready;
	bool audio_enabled;
	bool power_on;
	unsigned int connector_type;
	bool is_edp;
	bool internal_hpd;

	hdmi_codec_plugged_cb plugged_cb;

	struct msm_dp_audio *msm_dp_audio;
	bool psr_supported;
};

int msm_dp_display_set_plugged_cb(struct msm_dp *msm_dp_display,
		hdmi_codec_plugged_cb fn, struct device *codec_dev);
int msm_dp_display_get_modes(struct msm_dp *msm_dp_display);
bool msm_dp_display_check_video_test(struct msm_dp *msm_dp_display);
int msm_dp_display_get_test_bpp(struct msm_dp *msm_dp_display);
void msm_dp_display_signal_audio_start(struct msm_dp *msm_dp_display);
void msm_dp_display_signal_audio_complete(struct msm_dp *msm_dp_display);
void msm_dp_display_set_psr(struct msm_dp *dp, bool enter);
void msm_dp_display_debugfs_init(struct msm_dp *msm_dp_display, struct dentry *dentry, bool is_edp);

#endif /* _DP_DISPLAY_H_ */
