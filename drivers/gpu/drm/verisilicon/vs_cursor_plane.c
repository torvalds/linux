// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Institute of Software, Chinese Academy of Sciences (ISCAS)
 *
 * Authors:
 * Icenowy Zheng <zhengxingda@iscas.ac.cn>
 */

#include <linux/log2.h>
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
#include "vs_hwdb.h"
#include "vs_cursor_plane_regs.h"

#define VSDC_MIN_CURSOR_SIZE 32
#define VSDC_MAX_CURSOR_SIZE 256

#define VSDC_CURSOR_LOCATION_MAX_POSITIVE BIT_MASK(15)
#define VSDC_CURSOR_LOCATION_MAX_NEGATIVE BIT_MASK(5)

static bool vs_cursor_plane_check_coord(int32_t coord)
{
	if (coord >= 0)
		return coord <= VSDC_CURSOR_LOCATION_MAX_POSITIVE;
	else
		return (-coord) <= VSDC_CURSOR_LOCATION_MAX_NEGATIVE;
}

static int vs_cursor_plane_atomic_check(struct drm_plane *plane,
					struct drm_atomic_commit *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct drm_crtc *crtc = new_plane_state->crtc;
	struct drm_framebuffer *fb = new_plane_state->fb;
	struct drm_crtc_state *crtc_state = NULL;
	struct vs_crtc *vcrtc;
	struct vs_dc *dc;
	int ret;

	if (crtc)
		crtc_state = drm_atomic_get_new_crtc_state(state, crtc);

	ret = drm_atomic_helper_check_plane_state(new_plane_state,
						  crtc_state,
						  DRM_PLANE_NO_SCALING,
						  DRM_PLANE_NO_SCALING,
						  true, true);
	if (ret)
		return ret;

	if (!new_plane_state->visible)
		return 0; /* Skip validity check */

	vcrtc = drm_crtc_to_vs_crtc(crtc);
	dc = vcrtc->dc;

	/* Only certain PoT square sizes is supported. */
	if (!is_power_of_2(new_plane_state->crtc_w) ||
	    new_plane_state->crtc_w < VSDC_MIN_CURSOR_SIZE ||
	    new_plane_state->crtc_w > dc->identity.max_cursor_size)
		return -EINVAL;

	if (new_plane_state->crtc_w != new_plane_state->crtc_h)
		return -EINVAL;

	/* Check if the cursor is inside the register fields' range */
	if (!vs_cursor_plane_check_coord(new_plane_state->crtc_x) ||
	    !vs_cursor_plane_check_coord(new_plane_state->crtc_y))
		return -EINVAL;

	/* Extra line padding isn't supported */
	if (fb->pitches[0] !=
	    drm_format_info_min_pitch(fb->format, 0, new_plane_state->crtc_w))
		return -EINVAL;

	return 0;
}

static void vs_cursor_plane_commit(struct vs_dc *dc, unsigned int output)
{
	regmap_set_bits(dc->regs, VSDC_CURSOR_CONFIG(output),
			VSDC_CURSOR_CONFIG_COMMIT |
			VSDC_CURSOR_CONFIG_IMG_UPDATE);
}

static void vs_cursor_plane_atomic_enable(struct drm_plane *plane,
					   struct drm_atomic_commit *atomic_state)
{
	struct drm_plane_state *state = drm_atomic_get_new_plane_state(atomic_state,
								       plane);
	struct drm_crtc *crtc = state->crtc;
	struct vs_crtc *vcrtc = drm_crtc_to_vs_crtc(crtc);
	unsigned int output = vcrtc->id;
	struct vs_dc *dc = vcrtc->dc;

	regmap_update_bits(dc->regs, VSDC_CURSOR_CONFIG(output),
			   VSDC_CURSOR_CONFIG_FMT_MASK,
			   VSDC_CURSOR_CONFIG_FMT_ARGB8888);

	vs_cursor_plane_commit(dc, output);
}

static void vs_cursor_plane_atomic_disable(struct drm_plane *plane,
					    struct drm_atomic_commit *atomic_state)
{
	struct drm_plane_state *state = drm_atomic_get_old_plane_state(atomic_state,
								       plane);
	struct drm_crtc *crtc = state->crtc;
	struct vs_crtc *vcrtc = drm_crtc_to_vs_crtc(crtc);
	unsigned int output = vcrtc->id;
	struct vs_dc *dc = vcrtc->dc;

	regmap_update_bits(dc->regs, VSDC_CURSOR_CONFIG(output),
			   VSDC_CURSOR_CONFIG_FMT_MASK,
			   VSDC_CURSOR_CONFIG_FMT_OFF);

	vs_cursor_plane_commit(dc, output);
}

static void vs_cursor_plane_atomic_update(struct drm_plane *plane,
					   struct drm_atomic_commit *atomic_state)
{
	struct drm_plane_state *state = drm_atomic_get_new_plane_state(atomic_state,
								       plane);
	struct drm_framebuffer *fb = state->fb;
	struct drm_crtc *crtc = state->crtc;
	struct vs_dc *dc;
	struct vs_crtc *vcrtc;
	unsigned int output;
	dma_addr_t dma_addr;

	if (!state->visible) {
		vs_cursor_plane_atomic_disable(plane, atomic_state);
		return;
	}

	vcrtc = drm_crtc_to_vs_crtc(crtc);
	output = vcrtc->id;
	dc = vcrtc->dc;

	/* Other sizes should be rejected by atomic_check */
	switch (state->crtc_w) {
	case 32:
		regmap_update_bits(dc->regs, VSDC_CURSOR_CONFIG(output),
				   VSDC_CURSOR_CONFIG_SIZE_MASK,
				   VSDC_CURSOR_CONFIG_SIZE_32);
		break;
	case 64:
		regmap_update_bits(dc->regs, VSDC_CURSOR_CONFIG(output),
				   VSDC_CURSOR_CONFIG_SIZE_MASK,
				   VSDC_CURSOR_CONFIG_SIZE_64);
		break;
	case 128:
		regmap_update_bits(dc->regs, VSDC_CURSOR_CONFIG(output),
				   VSDC_CURSOR_CONFIG_SIZE_MASK,
				   VSDC_CURSOR_CONFIG_SIZE_128);
		break;
	case 256:
		regmap_update_bits(dc->regs, VSDC_CURSOR_CONFIG(output),
				   VSDC_CURSOR_CONFIG_SIZE_MASK,
				   VSDC_CURSOR_CONFIG_SIZE_256);
		break;
	}

	dma_addr = vs_fb_get_dma_addr(fb, &state->src);

	regmap_write(dc->regs, VSDC_CURSOR_ADDRESS(output),
		     lower_32_bits(dma_addr));

	/*
	 * The X_OFF and Y_OFF fields define which point does the LOCATION
	 * register represent in the cursor image, and LOCATION register
	 * values are unsigned. To for positive left-top  coordinates the
	 * offset is set to 0 and the location is set to the coordinate, for
	 * negative coordinates the location is set to 0 and the offset
	 * is set to the opposite number of the coordinate to offset the
	 * cursor image partly off-screen.
	 */
	if (state->crtc_x >= 0) {
		regmap_update_bits(dc->regs, VSDC_CURSOR_CONFIG(output),
				   VSDC_CURSOR_CONFIG_X_OFF_MASK, 0);
		regmap_update_bits(dc->regs, VSDC_CURSOR_LOCATION(output),
				   VSDC_CURSOR_LOCATION_X_MASK,
				   VSDC_CURSOR_LOCATION_X(state->crtc_x));
	} else {
		regmap_update_bits(dc->regs, VSDC_CURSOR_CONFIG(output),
				   VSDC_CURSOR_CONFIG_X_OFF_MASK,
				   -state->crtc_x);
		regmap_update_bits(dc->regs, VSDC_CURSOR_LOCATION(output),
				   VSDC_CURSOR_LOCATION_X_MASK, 0);
	}

	if (state->crtc_y >= 0) {
		regmap_update_bits(dc->regs, VSDC_CURSOR_CONFIG(output),
				   VSDC_CURSOR_CONFIG_Y_OFF_MASK, 0);
		regmap_update_bits(dc->regs, VSDC_CURSOR_LOCATION(output),
				   VSDC_CURSOR_LOCATION_Y_MASK,
				   VSDC_CURSOR_LOCATION_Y(state->crtc_y));
	} else {
		regmap_update_bits(dc->regs, VSDC_CURSOR_CONFIG(output),
				   VSDC_CURSOR_CONFIG_Y_OFF_MASK,
				   -state->crtc_y);
		regmap_update_bits(dc->regs, VSDC_CURSOR_LOCATION(output),
				   VSDC_CURSOR_LOCATION_Y_MASK, 0);
	}

	vs_cursor_plane_commit(dc, output);
}

static const struct drm_plane_helper_funcs vs_cursor_plane_helper_funcs = {
	.atomic_check	= vs_cursor_plane_atomic_check,
	.atomic_update	= vs_cursor_plane_atomic_update,
	.atomic_enable	= vs_cursor_plane_atomic_enable,
	.atomic_disable	= vs_cursor_plane_atomic_disable,
};

static const struct drm_plane_funcs vs_cursor_plane_funcs = {
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.reset			= drm_atomic_helper_plane_reset,
	.update_plane		= drm_atomic_helper_update_plane,
};

static const u32 vs_cursor_plane_formats[] = {
	DRM_FORMAT_ARGB8888,
};

static const u64 vs_cursor_plane_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID, /* sentinel */
};

struct drm_plane *vs_cursor_plane_init(struct drm_device *drm_dev,
				       struct vs_dc *dc)
{
	int32_t max_cursor_size = dc->identity.max_cursor_size;
	struct drm_plane *plane;

	if (drm_WARN_ON_ONCE(drm_dev, max_cursor_size < VSDC_MIN_CURSOR_SIZE ||
				      max_cursor_size > VSDC_MAX_CURSOR_SIZE))
		return ERR_PTR(-EINVAL);

	plane = drmm_universal_plane_alloc(drm_dev, struct drm_plane, dev, 0,
					   &vs_cursor_plane_funcs,
					   vs_cursor_plane_formats,
					   ARRAY_SIZE(vs_cursor_plane_formats),
					   vs_cursor_plane_modifiers,
					   DRM_PLANE_TYPE_CURSOR,
					   NULL);

	if (IS_ERR(plane))
		return plane;

	drm_plane_helper_add(plane, &vs_cursor_plane_helper_funcs);

	return plane;
}
