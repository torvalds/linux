// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Icenowy Zheng <uwu@icenowy.me>
 */

#include <linux/regmap.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_plane.h>
#include <drm/drm_print.h>

#include "vs_crtc.h"
#include "vs_plane.h"
#include "vs_dc.h"
#include "vs_primary_plane_regs.h"

static int vs_primary_plane_atomic_check(struct drm_plane *plane,
					 struct drm_atomic_commit *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct vs_plane_state *new_vs_plane_state = to_vs_plane_state(new_plane_state);
	struct drm_framebuffer *fb = new_plane_state->fb;
	struct drm_crtc *crtc = new_plane_state->crtc;
	struct drm_crtc_state *crtc_state = NULL;
	int ret;

	if (crtc)
		crtc_state = drm_atomic_get_new_crtc_state(state, crtc);

	ret = drm_atomic_helper_check_plane_state(new_plane_state,
						  crtc_state,
						  DRM_PLANE_NO_SCALING,
						  DRM_PLANE_NO_SCALING,
						  false, true);
	if (ret)
		return ret;

	if (!new_plane_state->visible)
		return 0;

	ret = drm_format_to_vs_format(fb->format->format,
				      &new_vs_plane_state->format);
	if (drm_WARN_ON_ONCE(plane->dev, ret))
		return ret;

	return 0;
}

static void vs_primary_plane_commit(struct vs_dc *dc, unsigned int output)
{
	regmap_set_bits(dc->regs, VSDC_FB_CONFIG_EX(output),
			VSDC_FB_CONFIG_EX_COMMIT);
}

static void vs_primary_plane_atomic_enable(struct drm_plane *plane,
					   struct drm_atomic_commit *atomic_state)
{
	struct drm_plane_state *state = drm_atomic_get_new_plane_state(atomic_state,
								       plane);
	struct drm_crtc *crtc = state->crtc;
	struct vs_crtc *vcrtc = drm_crtc_to_vs_crtc(crtc);
	unsigned int output = vcrtc->id;
	struct vs_dc *dc = vcrtc->dc;

	regmap_set_bits(dc->regs, VSDC_FB_CONFIG_EX(output),
			VSDC_FB_CONFIG_EX_FB_EN);
	regmap_update_bits(dc->regs, VSDC_FB_CONFIG_EX(output),
			   VSDC_FB_CONFIG_EX_DISPLAY_ID_MASK,
			   VSDC_FB_CONFIG_EX_DISPLAY_ID(output));

	vs_primary_plane_commit(dc, output);
}

static void vs_primary_plane_atomic_disable(struct drm_plane *plane,
					    struct drm_atomic_commit *atomic_state)
{
	struct drm_plane_state *state = drm_atomic_get_old_plane_state(atomic_state,
								       plane);
	struct drm_crtc *crtc = state->crtc;
	struct vs_crtc *vcrtc = drm_crtc_to_vs_crtc(crtc);
	unsigned int output = vcrtc->id;
	struct vs_dc *dc = vcrtc->dc;

	regmap_set_bits(dc->regs, VSDC_FB_CONFIG_EX(output),
			VSDC_FB_CONFIG_EX_FB_EN);

	vs_primary_plane_commit(dc, output);
}

static void vs_primary_plane_atomic_update(struct drm_plane *plane,
					   struct drm_atomic_commit *atomic_state)
{
	struct drm_plane_state *state = drm_atomic_get_new_plane_state(atomic_state,
								       plane);
	struct vs_plane_state *vs_state = to_vs_plane_state(state);
	struct drm_framebuffer *fb = state->fb;
	struct drm_crtc *crtc = state->crtc;
	struct vs_dc *dc;
	struct vs_crtc *vcrtc;
	unsigned int output;
	dma_addr_t dma_addr;

	if (!state->visible) {
		vs_primary_plane_atomic_disable(plane, atomic_state);
		return;
	}

	vcrtc = drm_crtc_to_vs_crtc(crtc);
	output = vcrtc->id;
	dc = vcrtc->dc;

	regmap_update_bits(dc->regs, VSDC_FB_CONFIG(output),
			   VSDC_FB_CONFIG_FMT_MASK,
			   VSDC_FB_CONFIG_FMT(vs_state->format.color));
	regmap_update_bits(dc->regs, VSDC_FB_CONFIG(output),
			   VSDC_FB_CONFIG_SWIZZLE_MASK,
			   VSDC_FB_CONFIG_SWIZZLE(vs_state->format.swizzle));
	regmap_assign_bits(dc->regs, VSDC_FB_CONFIG(output),
			   VSDC_FB_CONFIG_UV_SWIZZLE_EN,
			   vs_state->format.uv_swizzle);

	dma_addr = vs_fb_get_dma_addr(fb, &state->src);

	regmap_write(dc->regs, VSDC_FB_ADDRESS(output),
		     lower_32_bits(dma_addr));
	regmap_write(dc->regs, VSDC_FB_STRIDE(output),
		     fb->pitches[0]);

	regmap_write(dc->regs, VSDC_FB_TOP_LEFT(output),
		     VSDC_MAKE_PLANE_POS(state->crtc_x, state->crtc_y));
	regmap_write(dc->regs, VSDC_FB_BOTTOM_RIGHT(output),
		     VSDC_MAKE_PLANE_POS(state->crtc_x + state->crtc_w,
					 state->crtc_y + state->crtc_h));
	regmap_write(dc->regs, VSDC_FB_SIZE(output),
		     VSDC_MAKE_PLANE_SIZE(state->crtc_w, state->crtc_h));

	regmap_write(dc->regs, VSDC_FB_BLEND_CONFIG(output),
		     VSDC_FB_BLEND_CONFIG_BLEND_DISABLE);

	vs_primary_plane_commit(dc, output);
}

static const struct drm_plane_helper_funcs vs_primary_plane_helper_funcs = {
	.atomic_check	= vs_primary_plane_atomic_check,
	.atomic_update	= vs_primary_plane_atomic_update,
	.atomic_enable	= vs_primary_plane_atomic_enable,
	.atomic_disable	= vs_primary_plane_atomic_disable,
};

static const struct drm_plane_funcs vs_primary_plane_funcs = {
	.atomic_destroy_state	= vs_plane_destroy_state,
	.atomic_duplicate_state	= vs_plane_duplicate_state,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.reset			= vs_plane_reset,
	.update_plane		= drm_atomic_helper_update_plane,
};

struct drm_plane *vs_primary_plane_init(struct drm_device *drm_dev, struct vs_dc *dc)
{
	struct drm_plane *plane;

	plane = drmm_universal_plane_alloc(drm_dev, struct drm_plane, dev, 0,
					   &vs_primary_plane_funcs,
					   dc->identity.formats->array,
					   dc->identity.formats->num,
					   NULL,
					   DRM_PLANE_TYPE_PRIMARY,
					   NULL);

	if (IS_ERR(plane))
		return plane;

	drm_plane_helper_add(plane, &vs_primary_plane_helper_funcs);

	return plane;
}
