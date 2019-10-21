/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2013-2019 NVIDIA Corporation.
 * Copyright (C) 2015 Rob Clark
 */

#ifndef DRM_TEGRA_DP_H
#define DRM_TEGRA_DP_H 1

struct drm_dp_aux;

#define DP_LINK_CAP_ENHANCED_FRAMING (1 << 0)

struct drm_dp_link {
	unsigned char revision;
	unsigned int rate;
	unsigned int num_lanes;
	unsigned long capabilities;
};

int drm_dp_link_probe(struct drm_dp_aux *aux, struct drm_dp_link *link);
int drm_dp_link_power_up(struct drm_dp_aux *aux, struct drm_dp_link *link);
int drm_dp_link_power_down(struct drm_dp_aux *aux, struct drm_dp_link *link);
int drm_dp_link_configure(struct drm_dp_aux *aux, struct drm_dp_link *link);

#endif
