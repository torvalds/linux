// SPDX-License-Identifier: GPL-2.0
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "vkms_drv.h"
#include <drm/drm_plane_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>

static struct drm_plane_state *
vkms_plane_duplicate_state(struct drm_plane *plane)
{
	struct vkms_plane_state *vkms_state;
	struct vkms_crc_data *crc_data;

	vkms_state = kzalloc(sizeof(*vkms_state), GFP_KERNEL);
	if (!vkms_state)
		return NULL;

	crc_data = kzalloc(sizeof(*crc_data), GFP_KERNEL);
	if (WARN_ON(!crc_data))
		DRM_INFO("Couldn't allocate crc_data");

	vkms_state->crc_data = crc_data;

	__drm_atomic_helper_plane_duplicate_state(plane,
						  &vkms_state->base);

	return &vkms_state->base;
}

static void vkms_plane_destroy_state(struct drm_plane *plane,
				     struct drm_plane_state *old_state)
{
	struct vkms_plane_state *vkms_state = to_vkms_plane_state(old_state);
	struct drm_crtc *crtc = vkms_state->base.crtc;

	if (crtc) {
		/* dropping the reference we acquired in
		 * vkms_primary_plane_update()
		 */
		if (drm_framebuffer_read_refcount(&vkms_state->crc_data->fb))
			drm_framebuffer_put(&vkms_state->crc_data->fb);
	}

	kfree(vkms_state->crc_data);
	vkms_state->crc_data = NULL;

	__drm_atomic_helper_plane_destroy_state(old_state);
	kfree(vkms_state);
}

static void vkms_plane_reset(struct drm_plane *plane)
{
	struct vkms_plane_state *vkms_state;

	if (plane->state)
		vkms_plane_destroy_state(plane, plane->state);

	vkms_state = kzalloc(sizeof(*vkms_state), GFP_KERNEL);
	if (!vkms_state) {
		DRM_ERROR("Cannot allocate vkms_plane_state\n");
		return;
	}

	plane->state = &vkms_state->base;
	plane->state->plane = plane;
}

static const struct drm_plane_funcs vkms_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	.reset			= vkms_plane_reset,
	.atomic_duplicate_state = vkms_plane_duplicate_state,
	.atomic_destroy_state	= vkms_plane_destroy_state,
};

static void vkms_plane_atomic_update(struct drm_plane *plane,
				     struct drm_plane_state *old_state)
{
	struct vkms_plane_state *vkms_plane_state;
	struct drm_framebuffer *fb = plane->state->fb;
	struct vkms_crc_data *crc_data;

	if (!plane->state->crtc || !fb)
		return;

	vkms_plane_state = to_vkms_plane_state(plane->state);

	crc_data = vkms_plane_state->crc_data;
	memcpy(&crc_data->src, &plane->state->src, sizeof(struct drm_rect));
	memcpy(&crc_data->dst, &plane->state->dst, sizeof(struct drm_rect));
	memcpy(&crc_data->fb, fb, sizeof(struct drm_framebuffer));
	drm_framebuffer_get(&crc_data->fb);
	crc_data->offset = fb->offsets[0];
	crc_data->pitch = fb->pitches[0];
	crc_data->cpp = fb->format->cpp[0];
}

static int vkms_plane_atomic_check(struct drm_plane *plane,
				   struct drm_plane_state *state)
{
	struct drm_crtc_state *crtc_state;
	bool can_position = false;
	int ret;

	if (!state->fb | !state->crtc)
		return 0;

	crtc_state = drm_atomic_get_crtc_state(state->state, state->crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	if (plane->type == DRM_PLANE_TYPE_CURSOR)
		can_position = true;

	ret = drm_atomic_helper_check_plane_state(state, crtc_state,
						  DRM_PLANE_HELPER_NO_SCALING,
						  DRM_PLANE_HELPER_NO_SCALING,
						  can_position, true);
	if (ret != 0)
		return ret;

	/* for now primary plane must be visible and full screen */
	if (!state->visible && !can_position)
		return -EINVAL;

	return 0;
}

static int vkms_prepare_fb(struct drm_plane *plane,
			   struct drm_plane_state *state)
{
	struct drm_gem_object *gem_obj;
	struct vkms_gem_object *vkms_obj;
	int ret;

	if (!state->fb)
		return 0;

	gem_obj = drm_gem_fb_get_obj(state->fb, 0);
	vkms_obj = drm_gem_to_vkms_gem(gem_obj);
	ret = vkms_gem_vmap(gem_obj);
	if (ret)
		DRM_ERROR("vmap failed: %d\n", ret);

	return drm_gem_fb_prepare_fb(plane, state);
}

static void vkms_cleanup_fb(struct drm_plane *plane,
			    struct drm_plane_state *old_state)
{
	struct drm_gem_object *gem_obj;

	if (!old_state->fb)
		return;

	gem_obj = drm_gem_fb_get_obj(old_state->fb, 0);
	vkms_gem_vunmap(gem_obj);
}

static const struct drm_plane_helper_funcs vkms_primary_helper_funcs = {
	.atomic_update		= vkms_plane_atomic_update,
	.atomic_check		= vkms_plane_atomic_check,
	.prepare_fb		= vkms_prepare_fb,
	.cleanup_fb		= vkms_cleanup_fb,
};

struct drm_plane *vkms_plane_init(struct vkms_device *vkmsdev,
				  enum drm_plane_type type)
{
	struct drm_device *dev = &vkmsdev->drm;
	const struct drm_plane_helper_funcs *funcs;
	struct drm_plane *plane;
	const u32 *formats;
	int ret, nformats;

	plane = kzalloc(sizeof(*plane), GFP_KERNEL);
	if (!plane)
		return ERR_PTR(-ENOMEM);

	if (type == DRM_PLANE_TYPE_CURSOR) {
		formats = vkms_cursor_formats;
		nformats = ARRAY_SIZE(vkms_cursor_formats);
		funcs = &vkms_primary_helper_funcs;
	} else {
		formats = vkms_formats;
		nformats = ARRAY_SIZE(vkms_formats);
		funcs = &vkms_primary_helper_funcs;
	}

	ret = drm_universal_plane_init(dev, plane, 0,
				       &vkms_plane_funcs,
				       formats, nformats,
				       NULL, type, NULL);
	if (ret) {
		kfree(plane);
		return ERR_PTR(ret);
	}

	drm_plane_helper_add(plane, funcs);

	return plane;
}
