// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/vs_drm.h>

#include "vs_type.h"
#include "vs_crtc.h"
#include "vs_plane.h"
#include "vs_gem.h"
#include "vs_fb.h"

void vs_plane_destory(struct drm_plane *plane)
{
	struct vs_plane *vs_plane = to_vs_plane(plane);

	drm_plane_cleanup(plane);
	kfree(vs_plane);
}

static void vs_plane_reset(struct drm_plane *plane)
{
	struct vs_plane_state *state;
	struct vs_plane *vs_plane = to_vs_plane(plane);

	if (plane->state) {
		__drm_atomic_helper_plane_destroy_state(plane->state);

		state = to_vs_plane_state(plane->state);
		kfree(state);
		plane->state = NULL;
	}

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state == NULL)
		return;

	__drm_atomic_helper_plane_reset(plane, &state->base);

	state->degamma = VS_DEGAMMA_DISABLE;
	state->degamma_changed = false;
	state->base.zpos = vs_plane->id;

	memset(&state->status, 0, sizeof(state->status));
}

static void _vs_plane_duplicate_blob(struct vs_plane_state *state,
									 struct vs_plane_state *ori_state)
{
	state->watermark = ori_state->watermark;
	state->color_mgmt = ori_state->color_mgmt;
	state->roi = ori_state->roi;

	if (state->watermark)
		drm_property_blob_get(state->watermark);
	if (state->color_mgmt)
		drm_property_blob_get(state->color_mgmt);
	if (state->roi)
		drm_property_blob_get(state->roi);
}

static int
_vs_plane_set_property_blob_from_id(struct drm_device *dev,
									struct drm_property_blob **blob,
									uint64_t blob_id,
									size_t expected_size)
{
	struct drm_property_blob *new_blob = NULL;

	if (blob_id) {
		new_blob = drm_property_lookup_blob(dev, blob_id);
		if (new_blob == NULL)
			return -EINVAL;

		if (new_blob->length != expected_size) {
			drm_property_blob_put(new_blob);
			return -EINVAL;
		}
	}

	drm_property_replace_blob(blob, new_blob);
	drm_property_blob_put(new_blob);

	return 0;
}

static struct drm_plane_state *
vs_plane_atomic_duplicate_state(struct drm_plane *plane)
{
	struct vs_plane_state *ori_state;
	struct vs_plane_state *state;

	if (WARN_ON(!plane->state))
		return NULL;

	ori_state = to_vs_plane_state(plane->state);
	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane, &state->base);

	state->degamma = ori_state->degamma;
	state->degamma_changed = ori_state->degamma_changed;

	_vs_plane_duplicate_blob(state, ori_state);
	memcpy(&state->status, &ori_state->status, sizeof(ori_state->status));

	return &state->base;
}

static void vs_plane_atomic_destroy_state(struct drm_plane *plane,
					  struct drm_plane_state *state)
{
	struct vs_plane_state *vs_plane_state = to_vs_plane_state(state);

	__drm_atomic_helper_plane_destroy_state(state);

	drm_property_blob_put(vs_plane_state->watermark);
	drm_property_blob_put(vs_plane_state->color_mgmt);
	drm_property_blob_put(vs_plane_state->roi);
	kfree(vs_plane_state);
}

static int vs_plane_atomic_set_property(struct drm_plane *plane,
					struct drm_plane_state *state,
					struct drm_property *property,
					uint64_t val)
{
	struct drm_device *dev = plane->dev;
	struct vs_plane *vs_plane = to_vs_plane(plane);
	struct vs_plane_state *vs_plane_state = to_vs_plane_state(state);
	int ret = 0;

	if (property == vs_plane->degamma_mode) {
		if (vs_plane_state->degamma != val) {
			vs_plane_state->degamma = val;
			vs_plane_state->degamma_changed = true;
		} else {
			vs_plane_state->degamma_changed = false;
		}
	} else if (property == vs_plane->watermark_prop) {
		ret = _vs_plane_set_property_blob_from_id(dev,
								&vs_plane_state->watermark,
								val, sizeof(struct drm_vs_watermark));
		return ret;
	} else if (property == vs_plane->color_mgmt_prop) {
		ret = _vs_plane_set_property_blob_from_id(dev,
								&vs_plane_state->color_mgmt,
								val, sizeof(struct drm_vs_color_mgmt));
		return ret;
	} else if (property == vs_plane->roi_prop) {
		ret = _vs_plane_set_property_blob_from_id(dev,
								&vs_plane_state->roi,
								val, sizeof(struct drm_vs_roi));
		return ret;
	} else {
		return -EINVAL;
	}

	return 0;
}

static int vs_plane_atomic_get_property(struct drm_plane *plane,
					   const struct drm_plane_state *state,
					   struct drm_property *property,
					   uint64_t *val)
{
	struct vs_plane *vs_plane = to_vs_plane(plane);
	const struct vs_plane_state *vs_plane_state =
		container_of(state, const struct vs_plane_state, base);

	if (property == vs_plane->degamma_mode)
		*val = vs_plane_state->degamma;
	else if (property == vs_plane->watermark_prop)
		*val = (vs_plane_state->watermark) ?
					vs_plane_state->watermark->base.id : 0;
	else if (property == vs_plane->color_mgmt_prop)
		*val = (vs_plane_state->color_mgmt) ?
					vs_plane_state->color_mgmt->base.id : 0;
	else if (property == vs_plane->roi_prop)
		*val = (vs_plane_state->roi) ?
					vs_plane_state->roi->base.id : 0;
	else
		return -EINVAL;

	return 0;
}

static bool vs_format_mod_supported(struct drm_plane *plane,
					uint32_t format,
					uint64_t modifier)
{
   int i;

   /*
	* We always have to allow these modifiers:
	* 1. Core DRM checks for LINEAR support if userspace does not provide modifiers.
	* 2. Not passing any modifiers is the same as explicitly passing INVALID.
	*/
   if (modifier == DRM_FORMAT_MOD_LINEAR) {
	   return true;
   }

   /* Check that the modifier is on the list of the plane's supported modifiers. */
   for (i = 0; i < plane->modifier_count; i++) {
	   if (modifier == plane->modifiers[i])
		   break;
   }
   if (i == plane->modifier_count)
	   return false;

   return true;
}


const struct drm_plane_funcs vs_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= vs_plane_destory,
	.reset			= vs_plane_reset,
	.atomic_duplicate_state = vs_plane_atomic_duplicate_state,
	.atomic_destroy_state	= vs_plane_atomic_destroy_state,
	.atomic_set_property	= vs_plane_atomic_set_property,
	.atomic_get_property	= vs_plane_atomic_get_property,
	.format_mod_supported	= vs_format_mod_supported,
};

static unsigned char vs_get_plane_number(struct drm_framebuffer *fb)
{
	const struct drm_format_info *info;

	if (!fb)
		return 0;

	info = drm_format_info(fb->format->format);
	if (!info || info->num_planes > MAX_NUM_PLANES)
		return 0;

	return info->num_planes;
}


static int vs_plane_atomic_check(struct drm_plane *plane,
			   struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct vs_plane *vs_plane = to_vs_plane(plane);
	struct drm_framebuffer *fb = new_plane_state->fb;
	struct drm_crtc *crtc = new_plane_state->crtc;
	struct vs_crtc *vs_crtc = to_vs_crtc(crtc);

	if (!crtc || !fb)
		return 0;

	//return vs_plane->funcs->check(vs_crtc->dev, vs_plane, new_plane_state);
	return vs_plane->funcs->check(vs_crtc->dev, plane, state);
}

static void vs_plane_atomic_update(struct drm_plane *plane,
		struct drm_atomic_state *state)
{
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state,
									   plane);
	unsigned char i, num_planes;
	struct drm_framebuffer *fb;
	struct vs_plane *vs_plane = to_vs_plane(plane);
	//struct drm_plane_state *state = plane->state;
	struct vs_crtc *vs_crtc = to_vs_crtc(new_state->crtc);
	struct vs_plane_state *plane_state = to_vs_plane_state(new_state);
	//struct drm_format_name_buf *name = &plane_state->status.format_name;

	if (!new_state->fb || !new_state->crtc)
		return;

	fb = new_state->fb;

	num_planes = vs_get_plane_number(fb);

	for (i = 0; i < num_planes; i++) {
		struct vs_gem_object *vs_obj;

		vs_obj = vs_fb_get_gem_obj(fb, i);
		vs_plane->dma_addr[i] = vs_obj->iova + fb->offsets[i];
	}

	plane_state->status.src = drm_plane_state_src(new_state);
	plane_state->status.dest = drm_plane_state_dest(new_state);
	//drm_get_format_name(fb->format->format, name);

	vs_plane->funcs->update(vs_crtc->dev, vs_plane, plane, state);
}

static void vs_plane_atomic_disable(struct drm_plane *plane,
					 struct drm_atomic_state *state)
{
	struct drm_plane_state *old_state = drm_atomic_get_old_plane_state(state,
										   plane);
	struct vs_plane *vs_plane = to_vs_plane(plane);
	struct vs_crtc *vs_crtc = to_vs_crtc(old_state->crtc);

	vs_plane->funcs->disable(vs_crtc->dev, vs_plane, old_state);
}

const struct drm_plane_helper_funcs vs_plane_helper_funcs = {
	.atomic_check	= vs_plane_atomic_check,
	.atomic_update	= vs_plane_atomic_update,
	.atomic_disable = vs_plane_atomic_disable,
};

static const struct drm_prop_enum_list vs_degamma_mode_enum_list[] = {
	{ VS_DEGAMMA_DISABLE,	"disabled" },
	{ VS_DEGAMMA_BT709, "preset degamma for BT709" },
	{ VS_DEGAMMA_BT2020,	"preset degamma for BT2020" },
};

struct vs_plane *vs_plane_create(struct drm_device *drm_dev,
				 struct vs_plane_info *info,
				 unsigned int layer_num,
				 unsigned int possible_crtcs)
{
	struct vs_plane *plane;
	int ret;

	if (!info)
		return NULL;

	plane = kzalloc(sizeof(struct vs_plane), GFP_KERNEL);
	if (!plane)
		return NULL;

	ret = drm_universal_plane_init(drm_dev, &plane->base, possible_crtcs,
				&vs_plane_funcs, info->formats,
				info->num_formats, info->modifiers, info->type,
				info->name ? info->name : NULL);
	if (ret)
		goto err_free_plane;

	drm_plane_helper_add(&plane->base, &vs_plane_helper_funcs);

	/* Set up the plane properties */
	if (info->degamma_size) {
		plane->degamma_mode = drm_property_create_enum(drm_dev, 0,
					  "DEGAMMA_MODE",
					  vs_degamma_mode_enum_list,
					  ARRAY_SIZE(vs_degamma_mode_enum_list));

		if (!plane->degamma_mode)
			goto error_cleanup_plane;

		drm_object_attach_property(&plane->base.base,
					   plane->degamma_mode,
					   VS_DEGAMMA_DISABLE);
	}

	if (info->rotation) {
		ret = drm_plane_create_rotation_property(&plane->base,
							 DRM_MODE_ROTATE_0,
							 info->rotation);
		if (ret)
			goto error_cleanup_plane;
	}

	if (info->blend_mode) {
		ret = drm_plane_create_blend_mode_property(&plane->base,
							   info->blend_mode);
		if (ret)
			goto error_cleanup_plane;
		ret = drm_plane_create_alpha_property(&plane->base);
		if (ret)
			goto error_cleanup_plane;
	}

	if (info->color_encoding) {
		ret = drm_plane_create_color_properties(&plane->base,
					   info->color_encoding,
					   BIT(DRM_COLOR_YCBCR_LIMITED_RANGE),
					   DRM_COLOR_YCBCR_BT709,
					   DRM_COLOR_YCBCR_LIMITED_RANGE);
		if (ret)
			goto error_cleanup_plane;
	}

	if (info->zpos != 255) {
		ret = drm_plane_create_zpos_property(&plane->base, info->zpos, 0, layer_num - 1);
		if (ret)
			goto error_cleanup_plane;
	}
#if KERNEL_VERSION(5, 8, 0) <= LINUX_VERSION_CODE
	else {
		ret = drm_plane_create_zpos_immutable_property(&plane->base,
													   info->zpos);
		if (ret)
			goto error_cleanup_plane;
	}
#endif

	if (info->watermark) {
		plane->watermark_prop = drm_property_create(drm_dev, DRM_MODE_PROP_BLOB,
								 "WATERMARK", 0);
		if (!plane->watermark_prop)
			goto error_cleanup_plane;

		drm_object_attach_property(&plane->base.base, plane->watermark_prop, 0);
	}

	if (info->color_mgmt) {
		plane->color_mgmt_prop = drm_property_create(drm_dev, DRM_MODE_PROP_BLOB,
								 "COLOR_CONFIG", 0);
		if (!plane->color_mgmt_prop)
			goto error_cleanup_plane;

		drm_object_attach_property(&plane->base.base, plane->color_mgmt_prop, 0);
	}

	if (info->roi) {
		plane->roi_prop = drm_property_create(drm_dev, DRM_MODE_PROP_BLOB,
							 "ROI", 0);
		if (!plane->roi_prop)
			goto error_cleanup_plane;

		drm_object_attach_property(&plane->base.base, plane->roi_prop, 0);
	}

	return plane;

error_cleanup_plane:
	drm_plane_cleanup(&plane->base);
err_free_plane:
	kfree(plane);
	return NULL;
}
