// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2013-2019 NVIDIA Corporation
 * Copyright (C) 2015 Rob Clark
 */

#include <drm/drm_dp_helper.h>

#include "dp.h"

static void drm_dp_link_caps_reset(struct drm_dp_link_caps *caps)
{
	caps->enhanced_framing = false;
	caps->tps3_supported = false;
	caps->fast_training = false;
}

void drm_dp_link_caps_copy(struct drm_dp_link_caps *dest,
			   const struct drm_dp_link_caps *src)
{
	dest->enhanced_framing = src->enhanced_framing;
	dest->tps3_supported = src->tps3_supported;
	dest->fast_training = src->fast_training;
}

static void drm_dp_link_reset(struct drm_dp_link *link)
{
	if (!link)
		return;

	link->revision = 0;
	link->max_rate = 0;
	link->max_lanes = 0;

	drm_dp_link_caps_reset(&link->caps);

	link->rate = 0;
	link->lanes = 0;
}

/**
 * drm_dp_link_probe() - probe a DisplayPort link for capabilities
 * @aux: DisplayPort AUX channel
 * @link: pointer to structure in which to return link capabilities
 *
 * The structure filled in by this function can usually be passed directly
 * into drm_dp_link_power_up() and drm_dp_link_configure() to power up and
 * configure the link based on the link's capabilities.
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_dp_link_probe(struct drm_dp_aux *aux, struct drm_dp_link *link)
{
	u8 dpcd[DP_RECEIVER_CAP_SIZE];
	int err;

	drm_dp_link_reset(link);

	err = drm_dp_dpcd_read(aux, DP_DPCD_REV, dpcd, sizeof(dpcd));
	if (err < 0)
		return err;

	link->revision = dpcd[DP_DPCD_REV];
	link->max_rate = drm_dp_max_link_rate(dpcd);
	link->max_lanes = drm_dp_max_lane_count(dpcd);

	link->caps.enhanced_framing = drm_dp_enhanced_frame_cap(dpcd);
	link->caps.tps3_supported = drm_dp_tps3_supported(dpcd);
	link->caps.fast_training = drm_dp_fast_training_cap(dpcd);

	link->rate = link->max_rate;
	link->lanes = link->max_lanes;

	return 0;
}

/**
 * drm_dp_link_power_up() - power up a DisplayPort link
 * @aux: DisplayPort AUX channel
 * @link: pointer to a structure containing the link configuration
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_dp_link_power_up(struct drm_dp_aux *aux, struct drm_dp_link *link)
{
	u8 value;
	int err;

	/* DP_SET_POWER register is only available on DPCD v1.1 and later */
	if (link->revision < 0x11)
		return 0;

	err = drm_dp_dpcd_readb(aux, DP_SET_POWER, &value);
	if (err < 0)
		return err;

	value &= ~DP_SET_POWER_MASK;
	value |= DP_SET_POWER_D0;

	err = drm_dp_dpcd_writeb(aux, DP_SET_POWER, value);
	if (err < 0)
		return err;

	/*
	 * According to the DP 1.1 specification, a "Sink Device must exit the
	 * power saving state within 1 ms" (Section 2.5.3.1, Table 5-52, "Sink
	 * Control Field" (register 0x600).
	 */
	usleep_range(1000, 2000);

	return 0;
}

/**
 * drm_dp_link_power_down() - power down a DisplayPort link
 * @aux: DisplayPort AUX channel
 * @link: pointer to a structure containing the link configuration
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_dp_link_power_down(struct drm_dp_aux *aux, struct drm_dp_link *link)
{
	u8 value;
	int err;

	/* DP_SET_POWER register is only available on DPCD v1.1 and later */
	if (link->revision < 0x11)
		return 0;

	err = drm_dp_dpcd_readb(aux, DP_SET_POWER, &value);
	if (err < 0)
		return err;

	value &= ~DP_SET_POWER_MASK;
	value |= DP_SET_POWER_D3;

	err = drm_dp_dpcd_writeb(aux, DP_SET_POWER, value);
	if (err < 0)
		return err;

	return 0;
}

/**
 * drm_dp_link_configure() - configure a DisplayPort link
 * @aux: DisplayPort AUX channel
 * @link: pointer to a structure containing the link configuration
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_dp_link_configure(struct drm_dp_aux *aux, struct drm_dp_link *link)
{
	u8 values[2];
	int err;

	values[0] = drm_dp_link_rate_to_bw_code(link->rate);
	values[1] = link->lanes;

	if (link->caps.enhanced_framing)
		values[1] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;

	err = drm_dp_dpcd_write(aux, DP_LINK_BW_SET, values, sizeof(values));
	if (err < 0)
		return err;

	return 0;
}
