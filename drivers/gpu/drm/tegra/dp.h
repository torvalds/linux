/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2013-2019 NVIDIA Corporation.
 * Copyright (C) 2015 Rob Clark
 */

#ifndef DRM_TEGRA_DP_H
#define DRM_TEGRA_DP_H 1

#include <linux/types.h>

struct drm_dp_aux;

/**
 * struct drm_dp_link_caps - DP link capabilities
 */
struct drm_dp_link_caps {
	/**
	 * @enhanced_framing:
	 *
	 * enhanced framing capability (mandatory as of DP 1.2)
	 */
	bool enhanced_framing;

	/**
	 * tps3_supported:
	 *
	 * training pattern sequence 3 supported for equalization
	 */
	bool tps3_supported;

	/**
	 * @fast_training:
	 *
	 * AUX CH handshake not required for link training
	 */
	bool fast_training;
};

void drm_dp_link_caps_copy(struct drm_dp_link_caps *dest,
			   const struct drm_dp_link_caps *src);

/**
 * struct drm_dp_link - DP link capabilities and configuration
 * @revision: DP specification revision supported on the link
 * @max_rate: maximum clock rate supported on the link
 * @max_lanes: maximum number of lanes supported on the link
 * @caps: capabilities supported on the link (see &drm_dp_link_caps)
 * @rate: currently configured link rate
 * @lanes: currently configured number of lanes
 */
struct drm_dp_link {
	unsigned char revision;
	unsigned int max_rate;
	unsigned int max_lanes;

	struct drm_dp_link_caps caps;

	unsigned int rate;
	unsigned int lanes;
};

int drm_dp_link_probe(struct drm_dp_aux *aux, struct drm_dp_link *link);
int drm_dp_link_power_up(struct drm_dp_aux *aux, struct drm_dp_link *link);
int drm_dp_link_power_down(struct drm_dp_aux *aux, struct drm_dp_link *link);
int drm_dp_link_configure(struct drm_dp_aux *aux, struct drm_dp_link *link);

#endif
