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
 * struct dp_audio
 * @lane_count: number of lanes configured in current session
 * @bw_code: link rate's bandwidth code for current session
 */
struct dp_audio {
	u32 lane_count;
	u32 bw_code;
};

/**
 * dp_audio_get()
 *
 * Creates and instance of dp audio.
 *
 * @pdev: caller's platform device instance.
 * @panel: an instance of dp_panel module.
 * @catalog: an instance of dp_catalog module.
 *
 * Returns the error code in case of failure, otherwize
 * an instance of newly created dp_module.
 */
struct dp_audio *dp_audio_get(struct platform_device *pdev,
			struct dp_panel *panel,
			struct dp_catalog *catalog);

/**
 * dp_register_audio_driver()
 *
 * Registers DP device with hdmi_codec interface.
 *
 * @dev: DP device instance.
 * @dp_audio: an instance of dp_audio module.
 *
 *
 * Returns the error code in case of failure, otherwise
 * zero on success.
 */
int dp_register_audio_driver(struct device *dev,
		struct dp_audio *dp_audio);

/**
 * dp_audio_put()
 *
 * Cleans the dp_audio instance.
 *
 * @dp_audio: an instance of dp_audio.
 */
void dp_audio_put(struct dp_audio *dp_audio);

int dp_audio_hw_params(struct device *dev,
	void *data,
	struct hdmi_codec_daifmt *daifmt,
	struct hdmi_codec_params *params);

#endif /* _DP_AUDIO_H_ */


