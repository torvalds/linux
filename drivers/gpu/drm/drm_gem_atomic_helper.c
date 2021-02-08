// SPDX-License-Identifier: GPL-2.0-or-later

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "drm_internal.h"

/**
 * DOC: overview
 *
 * The GEM atomic helpers library implements generic atomic-commit
 * functions for drivers that use GEM objects. Currently, it provides
 * plane state and framebuffer BO mappings for planes with shadow
 * buffers.
 */

/*
 * Shadow-buffered Planes
 */

static struct drm_plane_state *
drm_gem_duplicate_shadow_plane_state(struct drm_plane *plane)
{
	struct drm_plane_state *plane_state = plane->state;
	struct drm_shadow_plane_state *new_shadow_plane_state;

	if (!plane_state)
		return NULL;

	new_shadow_plane_state = kzalloc(sizeof(*new_shadow_plane_state), GFP_KERNEL);
	if (!new_shadow_plane_state)
		return NULL;
	__drm_atomic_helper_plane_duplicate_state(plane, &new_shadow_plane_state->base);

	return &new_shadow_plane_state->base;
}

static void drm_gem_destroy_shadow_plane_state(struct drm_plane *plane,
					       struct drm_plane_state *plane_state)
{
	struct drm_shadow_plane_state *shadow_plane_state =
		to_drm_shadow_plane_state(plane_state);

	__drm_atomic_helper_plane_destroy_state(&shadow_plane_state->base);
	kfree(shadow_plane_state);
}

static void drm_gem_reset_shadow_plane(struct drm_plane *plane)
{
	struct drm_shadow_plane_state *shadow_plane_state;

	if (plane->state) {
		drm_gem_destroy_shadow_plane_state(plane, plane->state);
		plane->state = NULL; /* must be set to NULL here */
	}

	shadow_plane_state = kzalloc(sizeof(*shadow_plane_state), GFP_KERNEL);
	if (!shadow_plane_state)
		return;
	__drm_atomic_helper_plane_reset(plane, &shadow_plane_state->base);
}

static int drm_gem_prepare_shadow_fb(struct drm_plane *plane, struct drm_plane_state *plane_state)
{
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(plane_state);
	struct drm_framebuffer *fb = plane_state->fb;
	struct drm_gem_object *obj;
	struct dma_buf_map map;
	int ret;
	size_t i;

	if (!fb)
		return 0;

	ret = drm_gem_fb_prepare_fb(plane, plane_state);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(shadow_plane_state->map); ++i) {
		obj = drm_gem_fb_get_obj(fb, i);
		if (!obj)
			continue;
		ret = drm_gem_vmap(obj, &map);
		if (ret)
			goto err_drm_gem_vunmap;
		shadow_plane_state->map[i] = map;
	}

	return 0;

err_drm_gem_vunmap:
	while (i) {
		--i;
		obj = drm_gem_fb_get_obj(fb, i);
		if (!obj)
			continue;
		drm_gem_vunmap(obj, &shadow_plane_state->map[i]);
	}
	return ret;
}

static void drm_gem_cleanup_shadow_fb(struct drm_plane *plane, struct drm_plane_state *plane_state)
{
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(plane_state);
	struct drm_framebuffer *fb = plane_state->fb;
	size_t i = ARRAY_SIZE(shadow_plane_state->map);
	struct drm_gem_object *obj;

	if (!fb)
		return;

	while (i) {
		--i;
		obj = drm_gem_fb_get_obj(fb, i);
		if (!obj)
			continue;
		drm_gem_vunmap(obj, &shadow_plane_state->map[i]);
	}
}

/**
 * drm_gem_simple_kms_prepare_shadow_fb - prepares shadow framebuffers
 * @pipe: the simple display pipe
 * @plane_state: the plane state of type struct drm_shadow_plane_state
 *
 * This function implements struct drm_simple_display_funcs.prepare_fb. It
 * maps all buffer objects of the plane's framebuffer into kernel address
 * space and stores them in struct drm_shadow_plane_state.map. The
 * framebuffer will be synchronized as part of the atomic commit.
 *
 * See drm_gem_simple_kms_cleanup_shadow_fb() for cleanup.
 *
 * Returns:
 * 0 on success, or a negative errno code otherwise.
 */
int drm_gem_simple_kms_prepare_shadow_fb(struct drm_simple_display_pipe *pipe,
					 struct drm_plane_state *plane_state)
{
	return drm_gem_prepare_shadow_fb(&pipe->plane, plane_state);
}
EXPORT_SYMBOL(drm_gem_simple_kms_prepare_shadow_fb);

/**
 * drm_gem_simple_kms_cleanup_shadow_fb - releases shadow framebuffers
 * @pipe: the simple display pipe
 * @plane_state: the plane state of type struct drm_shadow_plane_state
 *
 * This function implements struct drm_simple_display_funcs.cleanup_fb.
 * This function unmaps all buffer objects of the plane's framebuffer.
 *
 * See drm_gem_simple_kms_prepare_shadow_fb().
 */
void drm_gem_simple_kms_cleanup_shadow_fb(struct drm_simple_display_pipe *pipe,
					  struct drm_plane_state *plane_state)
{
	drm_gem_cleanup_shadow_fb(&pipe->plane, plane_state);
}
EXPORT_SYMBOL(drm_gem_simple_kms_cleanup_shadow_fb);

/**
 * drm_gem_simple_kms_reset_shadow_plane - resets a shadow-buffered plane
 * @pipe: the simple display pipe
 *
 * This function implements struct drm_simple_display_funcs.reset_plane
 * for shadow-buffered planes.
 */
void drm_gem_simple_kms_reset_shadow_plane(struct drm_simple_display_pipe *pipe)
{
	drm_gem_reset_shadow_plane(&pipe->plane);
}
EXPORT_SYMBOL(drm_gem_simple_kms_reset_shadow_plane);

/**
 * drm_gem_simple_kms_duplicate_shadow_plane_state - duplicates shadow-buffered plane state
 * @pipe: the simple display pipe
 *
 * This function implements struct drm_simple_display_funcs.duplicate_plane_state
 * for shadow-buffered planes. It does not duplicate existing mappings of the shadow
 * buffers. Mappings are maintained during the atomic commit by the plane's prepare_fb
 * and cleanup_fb helpers.
 *
 * Returns:
 * A pointer to a new plane state on success, or NULL otherwise.
 */
struct drm_plane_state *
drm_gem_simple_kms_duplicate_shadow_plane_state(struct drm_simple_display_pipe *pipe)
{
	return drm_gem_duplicate_shadow_plane_state(&pipe->plane);
}
EXPORT_SYMBOL(drm_gem_simple_kms_duplicate_shadow_plane_state);

/**
 * drm_gem_simple_kms_destroy_shadow_plane_state - resets shadow-buffered plane state
 * @pipe: the simple display pipe
 * @plane_state: the plane state of type struct drm_shadow_plane_state
 *
 * This function implements struct drm_simple_display_funcs.destroy_plane_state
 * for shadow-buffered planes. It expects that mappings of shadow buffers
 * have been released already.
 */
void drm_gem_simple_kms_destroy_shadow_plane_state(struct drm_simple_display_pipe *pipe,
						   struct drm_plane_state *plane_state)
{
	drm_gem_destroy_shadow_plane_state(&pipe->plane, plane_state);
}
EXPORT_SYMBOL(drm_gem_simple_kms_destroy_shadow_plane_state);
