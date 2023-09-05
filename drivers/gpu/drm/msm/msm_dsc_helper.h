/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved
 *
 * Helper methods for MSM-specific DSC calculations that are common between timing engine,
 * DSI, and DP.
 */

#ifndef MSM_DSC_HELPER_H_
#define MSM_DSC_HELPER_H_

#include <linux/math.h>
#include <drm/display/drm_dsc_helper.h>

/**
 * msm_dsc_get_slices_per_intf() - calculate number of slices per interface
 * @dsc: Pointer to drm dsc config struct
 * @intf_width: interface width in pixels
 * Returns: Integer representing the number of slices for the given interface
 */
static inline u32 msm_dsc_get_slices_per_intf(const struct drm_dsc_config *dsc, u32 intf_width)
{
	return DIV_ROUND_UP(intf_width, dsc->slice_width);
}

/**
 * msm_dsc_get_bytes_per_line() - calculate bytes per line
 * @dsc: Pointer to drm dsc config struct
 * Returns: Integer value representing bytes per line. DSI and DP need
 *          to perform further calculations to turn this into pclk_per_intf,
 *          such as dividing by different values depending on if widebus is enabled.
 */
static inline u32 msm_dsc_get_bytes_per_line(const struct drm_dsc_config *dsc)
{
	return dsc->slice_count * dsc->slice_chunk_size;
}

#endif /* MSM_DSC_HELPER_H_ */
