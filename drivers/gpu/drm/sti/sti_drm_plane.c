/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Benjamin Gaignard <benjamin.gaignard@st.com>
 *          Fabien Dessenne <fabien.dessenne@st.com>
 *          for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_plane_helper.h>

#include "sti_compositor.h"
#include "sti_drm_drv.h"
#include "sti_drm_plane.h"
#include "sti_vtg.h"

/* (Background) < GDP0 < GDP1 < HQVDP0 < GDP2 < GDP3 < (ForeGround) */
enum sti_plane_desc sti_plane_default_zorder[] = {
	STI_GDP_0,
	STI_GDP_1,
	STI_HQVDP_0,
	STI_GDP_2,
	STI_GDP_3,
};

const char *sti_plane_to_str(struct sti_plane *plane)
{
	switch (plane->desc) {
	case STI_GDP_0:
		return "GDP0";
	case STI_GDP_1:
		return "GDP1";
	case STI_GDP_2:
		return "GDP2";
	case STI_GDP_3:
		return "GDP3";
	case STI_HQVDP_0:
		return "HQVDP0";
	case STI_CURSOR:
		return "CURSOR";
	default:
		return "<UNKNOWN PLANE>";
	}
}
EXPORT_SYMBOL(sti_plane_to_str);

static int sti_plane_prepare(struct sti_plane *plane,
			     struct drm_crtc *crtc,
			     struct drm_framebuffer *fb,
			     struct drm_display_mode *mode, int mixer_id,
			     int dest_x, int dest_y, int dest_w, int dest_h,
			     int src_x, int src_y, int src_w, int src_h)
{
	struct drm_gem_cma_object *cma_obj;
	unsigned int i;
	int res;

	if (!plane || !fb || !mode) {
		DRM_ERROR("Null fb, plane or mode\n");
		return 1;
	}

	cma_obj = drm_fb_cma_get_gem_obj(fb, 0);
	if (!cma_obj) {
		DRM_ERROR("Can't get CMA GEM object for fb\n");
		return 1;
	}

	plane->fb = fb;
	plane->mode = mode;
	plane->mixer_id = mixer_id;
	plane->dst_x = dest_x;
	plane->dst_y = dest_y;
	plane->dst_w = clamp_val(dest_w, 0, mode->crtc_hdisplay - dest_x);
	plane->dst_h = clamp_val(dest_h, 0, mode->crtc_vdisplay - dest_y);
	plane->src_x = src_x;
	plane->src_y = src_y;
	plane->src_w = src_w;
	plane->src_h = src_h;
	plane->format = fb->pixel_format;
	plane->vaddr = cma_obj->vaddr;
	plane->paddr = cma_obj->paddr;
	for (i = 0; i < 4; i++) {
		plane->pitches[i] = fb->pitches[i];
		plane->offsets[i] = fb->offsets[i];
	}

	DRM_DEBUG_DRIVER("%s is associated with mixer_id %d\n",
			 sti_plane_to_str(plane),
			 plane->mixer_id);
	DRM_DEBUG_DRIVER("%s dst=(%dx%d)@(%d,%d) - src=(%dx%d)@(%d,%d)\n",
			 sti_plane_to_str(plane),
			 plane->dst_w, plane->dst_h, plane->dst_x, plane->dst_y,
			 plane->src_w, plane->src_h, plane->src_x,
			 plane->src_y);

	DRM_DEBUG_DRIVER("drm FB:%d format:%.4s phys@:0x%lx\n", fb->base.id,
			 (char *)&plane->format, (unsigned long)plane->paddr);

	if (!plane->ops->prepare) {
		DRM_ERROR("Cannot prepare\n");
		return 1;
	}

	res = plane->ops->prepare(plane, !plane->enabled);
	if (res) {
		DRM_ERROR("Plane prepare failed\n");
		return res;
	}

	plane->enabled = true;

	return 0;
}

static int sti_plane_commit(struct sti_plane *plane)
{
	if (!plane)
		return 1;

	if (!plane->ops->commit) {
		DRM_ERROR("Cannot commit\n");
		return 1;
	}

	return plane->ops->commit(plane);
}

static int sti_plane_disable(struct sti_plane *plane)
{
	int res;

	DRM_DEBUG_DRIVER("%s\n", sti_plane_to_str(plane));
	if (!plane)
		return 1;

	if (!plane->enabled)
		return 0;

	if (!plane->ops->disable) {
		DRM_ERROR("Cannot disable\n");
		return 1;
	}

	res = plane->ops->disable(plane);
	if (res) {
		DRM_ERROR("Plane disable failed\n");
		return res;
	}

	plane->enabled = false;

	return 0;
}

static void sti_drm_plane_destroy(struct drm_plane *drm_plane)
{
	DRM_DEBUG_DRIVER("\n");

	drm_plane_helper_disable(drm_plane);
	drm_plane_cleanup(drm_plane);
}

static int sti_drm_plane_set_property(struct drm_plane *drm_plane,
				      struct drm_property *property,
				      uint64_t val)
{
	struct drm_device *dev = drm_plane->dev;
	struct sti_drm_private *private = dev->dev_private;
	struct sti_plane *plane = to_sti_plane(drm_plane);

	DRM_DEBUG_DRIVER("\n");

	if (property == private->plane_zorder_property) {
		plane->zorder = val;
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

static int sti_drm_plane_atomic_check(struct drm_plane *drm_plane,
				      struct drm_plane_state *state)
{
	return 0;
}

static void sti_drm_plane_atomic_update(struct drm_plane *drm_plane,
					struct drm_plane_state *oldstate)
{
	struct drm_plane_state *state = drm_plane->state;
	struct sti_plane *plane = to_sti_plane(drm_plane);
	struct sti_mixer *mixer = to_sti_mixer(state->crtc);
	int res;

	DRM_DEBUG_KMS("CRTC:%d (%s) drm plane:%d (%s)\n",
		      state->crtc->base.id, sti_mixer_to_str(mixer),
		      drm_plane->base.id, sti_plane_to_str(plane));
	DRM_DEBUG_KMS("(%dx%d)@(%d,%d)\n",
		      state->crtc_w, state->crtc_h,
		      state->crtc_x, state->crtc_y);

	res = sti_mixer_set_plane_depth(mixer, plane);
	if (res) {
		DRM_ERROR("Cannot set plane depth\n");
		return;
	}

	/* src_x are in 16.16 format */
	res = sti_plane_prepare(plane, state->crtc, state->fb,
				&state->crtc->mode, mixer->id,
				state->crtc_x, state->crtc_y,
				state->crtc_w, state->crtc_h,
				state->src_x >> 16, state->src_y >> 16,
				state->src_w >> 16, state->src_h >> 16);
	if (res) {
		DRM_ERROR("Plane prepare failed\n");
		return;
	}

	res = sti_plane_commit(plane);
	if (res) {
		DRM_ERROR("Plane commit failed\n");
		return;
	}

	res = sti_mixer_set_plane_status(mixer, plane, true);
	if (res) {
		DRM_ERROR("Cannot enable plane at mixer\n");
		return;
	}
}

static void sti_drm_plane_atomic_disable(struct drm_plane *drm_plane,
					 struct drm_plane_state *oldstate)
{
	struct sti_plane *plane = to_sti_plane(drm_plane);
	struct sti_mixer *mixer = to_sti_mixer(drm_plane->crtc);
	int res;

	if (!drm_plane->crtc) {
		DRM_DEBUG_DRIVER("drm plane:%d not enabled\n",
				 drm_plane->base.id);
		return;
	}

	DRM_DEBUG_DRIVER("CRTC:%d (%s) drm plane:%d (%s)\n",
			 drm_plane->crtc->base.id, sti_mixer_to_str(mixer),
			 drm_plane->base.id, sti_plane_to_str(plane));

	/* Disable plane at mixer level */
	res = sti_mixer_set_plane_status(mixer, plane, false);
	if (res) {
		DRM_ERROR("Cannot disable plane at mixer\n");
		return;
	}

	/* Wait a while to be sure that a Vsync event is received */
	msleep(WAIT_NEXT_VSYNC_MS);

	/* Then disable plane itself */
	res = sti_plane_disable(plane);
	if (res) {
		DRM_ERROR("Plane disable failed\n");
		return;
	}
}

static const struct drm_plane_helper_funcs sti_drm_plane_helpers_funcs = {
	.atomic_check = sti_drm_plane_atomic_check,
	.atomic_update = sti_drm_plane_atomic_update,
	.atomic_disable = sti_drm_plane_atomic_disable,
};

static void sti_drm_plane_attach_zorder_property(struct drm_plane *drm_plane)
{
	struct drm_device *dev = drm_plane->dev;
	struct sti_drm_private *private = dev->dev_private;
	struct sti_plane *plane = to_sti_plane(drm_plane);
	struct drm_property *prop;

	prop = private->plane_zorder_property;
	if (!prop) {
		prop = drm_property_create_range(dev, 0, "zpos", 1,
						 GAM_MIXER_NB_DEPTH_LEVEL);
		if (!prop)
			return;

		private->plane_zorder_property = prop;
	}

	drm_object_attach_property(&drm_plane->base, prop, plane->zorder);
}

struct drm_plane *sti_drm_plane_init(struct drm_device *dev,
				 struct sti_plane *plane,
				 unsigned int possible_crtcs,
				 enum drm_plane_type type)
{
	int err, i;

	err = drm_universal_plane_init(dev, &plane->drm_plane,
				       possible_crtcs,
				       &sti_drm_plane_funcs,
				       plane->ops->get_formats(plane),
				       plane->ops->get_nb_formats(plane),
				       type);
	if (err) {
		DRM_ERROR("Failed to initialize universal plane\n");
		return NULL;
	}

	drm_plane_helper_add(&plane->drm_plane,
			     &sti_drm_plane_helpers_funcs);

	for (i = 0; i < ARRAY_SIZE(sti_plane_default_zorder); i++)
		if (sti_plane_default_zorder[i] == plane->desc)
			break;

	plane->zorder = i + 1;

	if (type == DRM_PLANE_TYPE_OVERLAY)
		sti_drm_plane_attach_zorder_property(&plane->drm_plane);

	DRM_DEBUG_DRIVER("drm plane:%d mapped to %s with zorder:%d\n",
			 plane->drm_plane.base.id,
			 sti_plane_to_str(plane), plane->zorder);

	return &plane->drm_plane;
}
EXPORT_SYMBOL(sti_drm_plane_init);
