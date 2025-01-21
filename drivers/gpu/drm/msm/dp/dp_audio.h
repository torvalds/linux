/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_AUDIO_H_
#define _DP_AUDIO_H_

#include <linux/platform_device.h>

#include "dp_panel.h"
#include "dp_catalog.h"
#include <sound/hdmi-codec.h>

/**
 * struct msm_dp_audio
 * @lane_count: number of lanes configured in current session
 * @bw_code: link rate's bandwidth code for current session
 */
struct msm_dp_audio {
	u32 lane_count;
	u32 bw_code;
};

/**
 * msm_dp_audio_get()
 *
 * Creates and instance of dp audio.
 *
 * @pdev: caller's platform device instance.
 * @panel: an instance of msm_dp_panel module.
 * @catalog: an instance of msm_dp_catalog module.
 *
 * Returns the error code in case of failure, otherwize
 * an instance of newly created msm_dp_module.
 */
struct msm_dp_audio *msm_dp_audio_get(struct platform_device *pdev,
			struct msm_dp_panel *panel,
			struct msm_dp_catalog *catalog);

/**
 * msm_dp_register_audio_driver()
 *
 * Registers DP device with hdmi_codec interface.
 *
 * @dev: DP device instance.
 * @msm_dp_audio: an instance of msm_dp_audio module.
 *
 *
 * Returns the error code in case of failure, otherwise
 * zero on success.
 */
int msm_dp_register_audio_driver(struct device *dev,
		struct msm_dp_audio *msm_dp_audio);

void msm_dp_unregister_audio_driver(struct device *dev, struct msm_dp_audio *msm_dp_audio);

/**
 * msm_dp_audio_put()
 *
 * Cleans the msm_dp_audio instance.
 *
 * @msm_dp_audio: an instance of msm_dp_audio.
 */
void msm_dp_audio_put(struct msm_dp_audio *msm_dp_audio);

int msm_dp_audio_hw_params(struct device *dev,
	void *data,
	struct hdmi_codec_daifmt *daifmt,
	struct hdmi_codec_params *params);

#endif /* _DP_AUDIO_H_ */


