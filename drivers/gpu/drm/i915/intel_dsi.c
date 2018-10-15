// SPDX-License-Identifier: MIT
/*
 * Copyright © 2018 Intel Corporation
 */

#include <drm/drm_mipi_dsi.h>
#include "intel_dsi.h"

int intel_dsi_bitrate(const struct intel_dsi *intel_dsi)
{
	int bpp = mipi_dsi_pixel_format_to_bpp(intel_dsi->pixel_format);

	if (WARN_ON(bpp < 0))
		bpp = 16;

	return intel_dsi->pclk * bpp / intel_dsi->lane_count;
}
