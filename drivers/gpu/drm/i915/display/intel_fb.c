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

bool is_surface_linear(const struct drm_framebuffer *fb, int color_plane)
{
	return fb->modifier == DRM_FORMAT_MOD_LINEAR ||
	       is_gen12_ccs_plane(fb, color_plane);
}

int main_to_ccs_plane(const struct drm_framebuffer *fb, int main_plane)
{
	drm_WARN_ON(fb->dev, !is_ccs_modifier(fb->modifier) ||
		    (main_plane && main_plane >= fb->format->num_planes / 2));

	return fb->format->num_planes / 2 + main_plane;
}

int skl_ccs_to_main_plane(const struct drm_framebuffer *fb, int ccs_plane)
{
	drm_WARN_ON(fb->dev, !is_ccs_modifier(fb->modifier) ||
		    ccs_plane < fb->format->num_planes / 2);

	if (is_gen12_ccs_cc_plane(fb, ccs_plane))
		return 0;

	return ccs_plane - fb->format->num_planes / 2;
}

int skl_main_to_aux_plane(const struct drm_framebuffer *fb, int main_plane)
{
	struct drm_i915_private *i915 = to_i915(fb->dev);

	if (is_ccs_modifier(fb->modifier))
		return main_to_ccs_plane(fb, main_plane);
	else if (DISPLAY_VER(i915) < 11 &&
		 intel_format_info_is_yuv_semiplanar(fb->format, fb->modifier))
		return 1;
	else
		return 0;
}
