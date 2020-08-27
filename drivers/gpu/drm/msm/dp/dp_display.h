/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_DISPLAY_H_
#define _DP_DISPLAY_H_

#include "dp_panel.h"

struct msm_dp {
	struct drm_device *drm_dev;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	bool is_connected;
	struct mutex connect_mutex;
	u32 max_pclk_khz;
	u32 max_dp_lanes;
};

int dp_display_validate_mode(struct msm_dp *dp_display, u32 mode_pclk_khz);
int dp_display_get_modes(struct msm_dp *dp_display,
		struct dp_display_mode *dp_mode);
int dp_display_request_irq(struct msm_dp *dp_display);
bool dp_display_check_video_test(struct msm_dp *dp_display);
int dp_display_get_test_bpp(struct msm_dp *dp_display);

void __init msm_dp_pll_driver_register(void);
void __exit msm_dp_pll_driver_unregister(void);

#endif /* _DP_DISPLAY_H_ */
