// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2013-2019 NVIDIA Corporation
 * Copyright (C) 2015 Rob Clark
 */

#include <drm/drm_dp_helper.h>

#include "dp.h"

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
	u8 values[3];
	int err;

	memset(link, 0, sizeof(*link));

	err = drm_dp_dpcd_read(aux, DP_DPCD_REV, values, sizeof(values));
	if (err < 0)
		return err;

	link->revision = values[0];
	link->rate = drm_dp_bw_code_to_link_rate(values[1]);
	link->num_lanes = values[2] & DP_MAX_LANE_COUNT_MASK;

	if (values[2] & DP_ENHANCED_FRAME_CAP)
		link->capabilities |= DP_LINK_CAP_ENHANCED_FRAMING;

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
	values[1] = link->num_lanes;

	if (link->capabilities & DP_LINK_CAP_ENHANCED_FRAMING)
		values[1] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;

	err = drm_dp_dpcd_write(aux, DP_LINK_BW_SET, values, sizeof(values));
	if (err < 0)
		return err;

	return 0;
}
