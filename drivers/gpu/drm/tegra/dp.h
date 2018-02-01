/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2013-2019 NVIDIA Corporation.
 * Copyright (C) 2015 Rob Clark
 */

#ifndef DRM_TEGRA_DP_H
#define DRM_TEGRA_DP_H 1

#include <linux/types.h>

struct drm_display_info;
struct drm_display_mode;
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

	/**
	 * @channel_coding:
	 *
	 * ANSI 8B/10B channel coding capability
	 */
	bool channel_coding;

	/**
	 * @alternate_scrambler_reset:
	 *
	 * eDP alternate scrambler reset capability
	 */
	bool alternate_scrambler_reset;
};

void drm_dp_link_caps_copy(struct drm_dp_link_caps *dest,
			   const struct drm_dp_link_caps *src);

/**
 * struct drm_dp_link - DP link capabilities and configuration
 * @revision: DP specification revision supported on the link
 * @max_rate: maximum clock rate supported on the link
 * @max_lanes: maximum number of lanes supported on the link
 * @caps: capabilities supported on the link (see &drm_dp_link_caps)
 * @aux_rd_interval: AUX read interval to use for training (in microseconds)
 * @edp: eDP revision (0x11: eDP 1.1, 0x12: eDP 1.2, ...)
 * @rate: currently configured link rate
 * @lanes: currently configured number of lanes
 * @rates: additional supported link rates in kHz (eDP 1.4)
 * @num_rates: number of additional supported link rates (eDP 1.4)
 */
struct drm_dp_link {
	unsigned char revision;
	unsigned int max_rate;
	unsigned int max_lanes;

	struct drm_dp_link_caps caps;

	/**
	 * @cr: clock recovery read interval
	 * @ce: channel equalization read interval
	 */
	struct {
		unsigned int cr;
		unsigned int ce;
	} aux_rd_interval;

	unsigned char edp;

	unsigned int rate;
	unsigned int lanes;

	unsigned long rates[DP_MAX_SUPPORTED_RATES];
	unsigned int num_rates;
};

int drm_dp_link_add_rate(struct drm_dp_link *link, unsigned long rate);
int drm_dp_link_remove_rate(struct drm_dp_link *link, unsigned long rate);
void drm_dp_link_update_rates(struct drm_dp_link *link);

int drm_dp_link_probe(struct drm_dp_aux *aux, struct drm_dp_link *link);
int drm_dp_link_power_up(struct drm_dp_aux *aux, struct drm_dp_link *link);
int drm_dp_link_power_down(struct drm_dp_aux *aux, struct drm_dp_link *link);
int drm_dp_link_configure(struct drm_dp_aux *aux, struct drm_dp_link *link);
int drm_dp_link_choose(struct drm_dp_link *link,
		       const struct drm_display_mode *mode,
		       const struct drm_display_info *info);

#endif
