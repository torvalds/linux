/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Benjamin Gaignard <benjamin.gaignard@st.com>
 *          Fabien Dessenne <fabien.dessenne@st.com>
 *          for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_plane_helper.h>

#include "sti_compositor.h"
#include "sti_drm_drv.h"
#include "sti_drm_plane.h"
#include "sti_vtg.h"

enum sti_layer_desc sti_layer_default_zorder[] = {
	STI_GDP_0,
	STI_VID_0,
	STI_GDP_1,
	STI_VID_1,
	STI_GDP_2,
	STI_GDP_3,
};

/* (Background) < GDP0 < VID0 < GDP1 < VID1 < GDP2 < GDP3 < (ForeGround) */

static int
sti_drm_update_plane(struct drm_plane *plane, struct drm_crtc *crtc,
		     struct drm_framebuffer *fb, int crtc_x, int crtc_y,
		     unsigned int crtc_w, unsigned int crtc_h,
		     uint32_t src_x, uint32_t src_y,
		     uint32_t src_w, uint32_t src_h)
{
	struct sti_layer *layer = to_sti_layer(plane);
	struct sti_mixer *mixer = to_sti_mixer(crtc);
	int res;

	DRM_DEBUG_KMS("CRTC:%d (%s) drm plane:%d (%s)\n",
		      crtc->base.id, sti_mixer_to_str(mixer),
		      plane->base.id, sti_layer_to_str(layer));
	DRM_DEBUG_KMS("(%dx%d)@(%d,%d)\n", crtc_w, crtc_h, crtc_x, crtc_y);

	res = sti_mixer_set_layer_depth(mixer, layer);
	if (res) {
		DRM_ERROR("Can not set layer depth\n");
		return res;
	}

	/* src_x are in 16.16 format. */
	res = sti_layer_prepare(layer, crtc, fb,
			&crtc->mode, mixer->id,
			crtc_x, crtc_y, crtc_w, crtc_h,
			src_x >> 16, src_y >> 16,
			src_w >> 16, src_h >> 16);
	if (res) {
		DRM_ERROR("Layer prepare failed\n");
		return res;
	}

	res = sti_layer_commit(layer);
	if (res) {
		DRM_ERROR("Layer commit failed\n");
		return res;
	}

	res = sti_mixer_set_layer_status(mixer, layer, true);
	if (res) {
		DRM_ERROR("Can not enable layer at mixer\n");
		return res;
	}

	return 0;
}

static int sti_drm_disable_plane(struct drm_plane *plane)
{
	struct sti_layer *layer;
	struct sti_mixer *mixer;
	int lay_res, mix_res;

	if (!plane->crtc) {
		DRM_DEBUG_DRIVER("drm plane:%d not enabled\n", plane->base.id);
		return 0;
	}
	layer = to_sti_layer(plane);
	mixer = to_sti_mixer(plane->crtc);

	DRM_DEBUG_DRIVER("CRTC:%d (%s) drm plane:%d (%s)\n",
			plane->crtc->base.id, sti_mixer_to_str(mixer),
			plane->base.id, sti_layer_to_str(layer));

	/* Disable layer at mixer level */
	mix_res = sti_mixer_set_layer_status(mixer, layer, false);
	if (mix_res)
		DRM_ERROR("Can not disable layer at mixer\n");

	/* Wait a while to be sure that a Vsync event is received */
	msleep(WAIT_NEXT_VSYNC_MS);

	/* Then disable layer itself */
	lay_res = sti_layer_disable(layer);
	if (lay_res)
		DRM_ERROR("Layer disable failed\n");

	if (lay_res || mix_res)
		return -EINVAL;

	return 0;
}

static void sti_drm_plane_destroy(struct drm_plane *plane)
{
	DRM_DEBUG_DRIVER("\n");

	drm_plane_helper_disable(plane);
	drm_plane_cleanup(plane);
}

static int sti_drm_plane_set_property(struct drm_plane *plane,
				      struct drm_property *property,
				      uint64_t val)
{
	struct drm_device *dev = plane->dev;
	struct sti_drm_private *private = dev->dev_private;
	struct sti_layer *layer = to_sti_layer(plane);

	DRM_DEBUG_DRIVER("\n");

	if (property == private->plane_zorder_property) {
		layer->zorder = val;
		return 0;
	}

	return -EINVAL;
}

static struct drm_plane_funcs sti_drm_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = sti_drm_plane_destroy,
	.set_property = sti_drm_plane_set_property,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

static int sti_drm_plane_prepare_fb(struct drm_plane *plane,
				  struct drm_framebuffer *fb,
				  const struct drm_plane_state *new_state)
{
	return 0;
}

static void sti_drm_plane_cleanup_fb(struct drm_plane *plane,
				   struct drm_framebuffer *fb,
				   const struct drm_plane_state *old_fb)
{
}

static int sti_drm_plane_atomic_check(struct drm_plane *plane,
				      struct drm_plane_state *state)
{
	return 0;
}

static void sti_drm_plane_atomic_update(struct drm_plane *plane,
					struct drm_plane_state *oldstate)
{
	struct drm_plane_state *state = plane->state;

	sti_drm_update_plane(plane, state->crtc, state->fb,
			    state->crtc_x, state->crtc_y,
			    state->crtc_w, state->crtc_h,
			    state->src_x, state->src_y,
			    state->src_w, state->src_h);
}

static void sti_drm_plane_atomic_disable(struct drm_plane *plane,
					 struct drm_plane_state *oldstate)
{
	sti_drm_disable_plane(plane);
}

static const struct drm_plane_helper_funcs sti_drm_plane_helpers_funcs = {
	.prepare_fb = sti_drm_plane_prepare_fb,
	.cleanup_fb = sti_drm_plane_cleanup_fb,
	.atomic_check = sti_drm_plane_atomic_check,
	.atomic_update = sti_drm_plane_atomic_update,
	.atomic_disable = sti_drm_plane_atomic_disable,
};

static void sti_drm_plane_attach_zorder_property(struct drm_plane *plane,
						 uint64_t default_val)
{
	struct drm_device *dev = plane->dev;
	struct sti_drm_private *private = dev->dev_private;
	struct drm_property *prop;
	struct sti_layer *layer = to_sti_layer(plane);

	prop = private->plane_zorder_property;
	if (!prop) {
		prop = drm_property_create_range(dev, 0, "zpos", 0,
						 GAM_MIXER_NB_DEPTH_LEVEL - 1);
		if (!prop)
			return;

		private->plane_zorder_property = prop;
	}

	drm_object_attach_property(&plane->base, prop, default_val);
	layer->zorder = default_val;
}

struct drm_plane *sti_drm_plane_init(struct drm_device *dev,
				     struct sti_layer *layer,
				     unsigned int possible_crtcs,
				     enum drm_plane_type type)
{
	int err, i;
	uint64_t default_zorder = 0;

	err = drm_universal_plane_init(dev, &layer->plane, possible_crtcs,
			     &sti_drm_plane_funcs,
			     sti_layer_get_formats(layer),
			     sti_layer_get_nb_formats(layer), type);
	if (err) {
		DRM_ERROR("Failed to initialize plane\n");
		return NULL;
	}

	drm_plane_helper_add(&layer->plane, &sti_drm_plane_helpers_funcs);

	for (i = 0; i < ARRAY_SIZE(sti_layer_default_zorder); i++)
		if (sti_layer_default_zorder[i] == layer->desc)
			break;

	default_zorder = i + 1;

	if (type == DRM_PLANE_TYPE_OVERLAY)
		sti_drm_plane_attach_zorder_property(&layer->plane,
				default_zorder);

	DRM_DEBUG_DRIVER("drm plane:%d mapped to %s with zorder:%llu\n",
			 layer->plane.base.id,
			 sti_layer_to_str(layer), default_zorder);

	return &layer->plane;
}
EXPORT_SYMBOL(sti_drm_plane_init);
