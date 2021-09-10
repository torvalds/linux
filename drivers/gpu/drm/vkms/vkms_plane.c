// SPDX-License-Identifier: GPL-2.0+

#include <linux/dma-buf-map.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_plane_helper.h>

#include "vkms_drv.h"

static const u32 vkms_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static const u32 vkms_plane_formats[] = {
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888
};

static struct drm_plane_state *
vkms_plane_duplicate_state(struct drm_plane *plane)
{
	struct vkms_plane_state *vkms_state;
	struct vkms_composer *composer;

	vkms_state = kzalloc(sizeof(*vkms_state), GFP_KERNEL);
	if (!vkms_state)
		return NULL;

	composer = kzalloc(sizeof(*composer), GFP_KERNEL);
	if (!composer) {
		DRM_DEBUG_KMS("Couldn't allocate composer\n");
		kfree(vkms_state);
		return NULL;
	}

	vkms_state->composer = composer;

	__drm_gem_duplicate_shadow_plane_state(plane, &vkms_state->base);

	return &vkms_state->base.base;
}

static void vkms_plane_destroy_state(struct drm_plane *plane,
				     struct drm_plane_state *old_state)
{
	struct vkms_plane_state *vkms_state = to_vkms_plane_state(old_state);
	struct drm_crtc *crtc = vkms_state->base.base.crtc;

	if (crtc) {
		/* dropping the reference we acquired in
		 * vkms_primary_plane_update()
		 */
		if (drm_framebuffer_read_refcount(&vkms_state->composer->fb))
			drm_framebuffer_put(&vkms_state->composer->fb);
	}

	kfree(vkms_state->composer);
	vkms_state->composer = NULL;

	__drm_gem_destroy_shadow_plane_state(&vkms_state->base);
	kfree(vkms_state);
}

static void vkms_plane_reset(struct drm_plane *plane)
{
	struct vkms_plane_state *vkms_state;

	if (plane->state) {
		vkms_plane_destroy_state(plane, plane->state);
		plane->state = NULL; /* must be set to NULL here */
	}

	vkms_state = kzalloc(sizeof(*vkms_state), GFP_KERNEL);
	if (!vkms_state) {
		DRM_ERROR("Cannot allocate vkms_plane_state\n");
		return;
	}

	__drm_gem_reset_shadow_plane(plane, &vkms_state->base);
}

static const struct drm_plane_funcs vkms_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.reset			= vkms_plane_reset,
	.atomic_duplicate_state = vkms_plane_duplicate_state,
	.atomic_destroy_state	= vkms_plane_destroy_state,
};

static void vkms_plane_atomic_update(struct drm_plane *plane,
				     struct drm_atomic_state *state)
{
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state,
									   plane);
	struct vkms_plane_state *vkms_plane_state;
	struct drm_shadow_plane_state *shadow_plane_state;
	struct drm_framebuffer *fb = new_state->fb;
	struct vkms_composer *composer;

	if (!new_state->crtc || !fb)
		return;

	vkms_plane_state = to_vkms_plane_state(new_state);
	shadow_plane_state = &vkms_plane_state->base;

	composer = vkms_plane_state->composer;
	memcpy(&composer->src, &new_state->src, sizeof(struct drm_rect));
	memcpy(&composer->dst, &new_state->dst, sizeof(struct drm_rect));
	memcpy(&composer->fb, fb, sizeof(struct drm_framebuffer));
	memcpy(&composer->map, &shadow_plane_state->data, sizeof(composer->map));
	drm_framebuffer_get(&composer->fb);
	composer->offset = fb->offsets[0];
	composer->pitch = fb->pitches[0];
	composer->cpp = fb->format->cpp[0];
}

static int vkms_plane_atomic_check(struct drm_plane *plane,
				   struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct drm_crtc_state *crtc_state;
	bool can_position = false;
	int ret;

	if (!new_plane_state->fb || WARN_ON(!new_plane_state->crtc))
		return 0;

	crtc_state = drm_atomic_get_crtc_state(state,
					       new_plane_state->crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	if (plane->type != DRM_PLANE_TYPE_PRIMARY)
		can_position = true;

	ret = drm_atomic_helper_check_plane_state(new_plane_state, crtc_state,
						  DRM_PLANE_HELPER_NO_SCALING,
						  DRM_PLANE_HELPER_NO_SCALING,
						  can_position, true);
	if (ret != 0)
		return ret;

	/* for now primary plane must be visible and full screen */
	if (!new_plane_state->visible && !can_position)
		return -EINVAL;

	return 0;
}

static const struct drm_plane_helper_funcs vkms_primary_helper_funcs = {
	.atomic_update		= vkms_plane_atomic_update,
	.atomic_check		= vkms_plane_atomic_check,
	DRM_GEM_SHADOW_PLANE_HELPER_FUNCS,
};

struct vkms_plane *vkms_plane_init(struct vkms_device *vkmsdev,
				   enum drm_plane_type type, int index)
{
	struct drm_device *dev = &vkmsdev->drm;
	const struct drm_plane_helper_funcs *funcs;
	struct vkms_plane *plane;
	const u32 *formats;
	int nformats;

	switch (type) {
	case DRM_PLANE_TYPE_PRIMARY:
		formats = vkms_formats;
		nformats = ARRAY_SIZE(vkms_formats);
		funcs = &vkms_primary_helper_funcs;
		break;
	case DRM_PLANE_TYPE_CURSOR:
	case DRM_PLANE_TYPE_OVERLAY:
		formats = vkms_plane_formats;
		nformats = ARRAY_SIZE(vkms_plane_formats);
		funcs = &vkms_primary_helper_funcs;
		break;
	default:
		formats = vkms_formats;
		nformats = ARRAY_SIZE(vkms_formats);
		funcs = &vkms_primary_helper_funcs;
		break;
	}

	plane = drmm_universal_plane_alloc(dev, struct vkms_plane, base, 1 << index,
					   &vkms_plane_funcs,
					   formats, nformats,
					   NULL, type, NULL);
	if (IS_ERR(plane))
		return plane;

	drm_plane_helper_add(&plane->base, funcs);

	return plane;
}
