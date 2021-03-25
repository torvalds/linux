// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <drm/drm_framebuffer.h>

#include "intel_display_types.h"
#include "intel_fb.h"

bool is_ccs_plane(const struct drm_framebuffer *fb, int plane)
{
	if (!is_ccs_modifier(fb->modifier))
		return false;

	return plane >= fb->format->num_planes / 2;
}

bool is_gen12_ccs_plane(const struct drm_framebuffer *fb, int plane)
{
	return is_gen12_ccs_modifier(fb->modifier) && is_ccs_plane(fb, plane);
}

bool is_gen12_ccs_cc_plane(const struct drm_framebuffer *fb, int plane)
{
	return fb->modifier == I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS_CC &&
	       plane == 2;
}
