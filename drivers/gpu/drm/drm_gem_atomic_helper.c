// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/dma-resv.h>

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "drm_internal.h"

/**
 * DOC: overview
 *
 * The GEM atomic helpers library implements generic atomic-commit
 * functions for drivers that use GEM objects. Currently, it provides
 * synchronization helpers, and plane state and framebuffer BO mappings
 * for planes with shadow buffers.
 *
 * Before scanout, a plane's framebuffer needs to be synchronized with
 * possible writers that draw into the framebuffer. All drivers should
 * call drm_gem_plane_helper_prepare_fb() from their implementation of
 * struct &drm_plane_helper.prepare_fb . It sets the plane's fence from
 * the framebuffer so that the DRM core can synchronize access automatically.
 *
 * drm_gem_plane_helper_prepare_fb() can also be used directly as
 * implementation of prepare_fb. For drivers based on
 * struct drm_simple_display_pipe, drm_gem_simple_display_pipe_prepare_fb()
 * provides equivalent functionality.
 *
 * .. code-block:: c
 *
 *	#include <drm/drm_gem_atomic_helper.h>
 *
 *	struct drm_plane_helper_funcs driver_plane_helper_funcs = {
 *		...,
 *		. prepare_fb = drm_gem_plane_helper_prepare_fb,
 *	};
 *
 *	struct drm_simple_display_pipe_funcs driver_pipe_funcs = {
 *		...,
 *		. prepare_fb = drm_gem_simple_display_pipe_prepare_fb,
 *	};
 *
 * A driver using a shadow buffer copies the content of the shadow buffers
 * into the HW's framebuffer memory during an atomic update. This requires
 * a mapping of the shadow buffer into kernel address space. The mappings
 * cannot be established by commit-tail functions, such as atomic_update,
 * as this would violate locking rules around dma_buf_vmap().
 *
 * The helpers for shadow-buffered planes establish and release mappings,
 * and provide struct drm_shadow_plane_state, which stores the plane's mapping
 * for commit-tail functons.
 *
 * Shadow-buffered planes can easily be enabled by using the provided macros
 * %DRM_GEM_SHADOW_PLANE_FUNCS and %DRM_GEM_SHADOW_PLANE_HELPER_FUNCS.
 * These macros set up the plane and plane-helper callbacks to point to the
 * shadow-buffer helpers.
 *
 * .. code-block:: c
 *
 *	#include <drm/drm_gem_atomic_helper.h>
 *
 *	struct drm_plane_funcs driver_plane_funcs = {
 *		...,
 *		DRM_GEM_SHADOW_PLANE_FUNCS,
 *	};
 *
 *	struct drm_plane_helper_funcs driver_plane_helper_funcs = {
 *		...,
 *		DRM_GEM_SHADOW_PLANE_HELPER_FUNCS,
 *	};
 *
 * In the driver's atomic-update function, shadow-buffer mappings are available
 * from the plane state. Use to_drm_shadow_plane_state() to upcast from
 * struct drm_plane_state.
 *
 * .. code-block:: c
 *
 *	void driver_plane_atomic_update(struct drm_plane *plane,
 *					struct drm_plane_state *old_plane_state)
 *	{
 *		struct drm_plane_state *plane_state = plane->state;
 *		struct drm_shadow_plane_state *shadow_plane_state =
 *			to_drm_shadow_plane_state(plane_state);
 *
 *		// access shadow buffer via shadow_plane_state->map
 *	}
 *
 * A mapping address for each of the framebuffer's buffer object is stored in
 * struct &drm_shadow_plane_state.map. The mappings are valid while the state
 * is being used.
 *
 * Drivers that use struct drm_simple_display_pipe can use
 * %DRM_GEM_SIMPLE_DISPLAY_PIPE_SHADOW_PLANE_FUNCS to initialize the rsp
 * callbacks. Access to shadow-buffer mappings is similar to regular
 * atomic_update.
 *
 * .. code-block:: c
 *
 *	struct drm_simple_display_pipe_funcs driver_pipe_funcs = {
 *		...,
 *		DRM_GEM_SIMPLE_DISPLAY_PIPE_SHADOW_PLANE_FUNCS,
 *	};
 *
 *	void driver_pipe_enable(struct drm_simple_display_pipe *pipe,
 *				struct drm_crtc_state *crtc_state,
 *				struct drm_plane_state *plane_state)
 *	{
 *		struct drm_shadow_plane_state *shadow_plane_state =
 *			to_drm_shadow_plane_state(plane_state);
 *
 *		// access shadow buffer via shadow_plane_state->map
 *	}
 */

/*
 * Plane Helpers
 */

/**
 * drm_gem_plane_helper_prepare_fb() - Prepare a GEM backed framebuffer
 * @plane: Plane
 * @state: Plane state the fence will be attached to
 *
 * This function extracts the exclusive fence from &drm_gem_object.resv and
 * attaches it to plane state for the atomic helper to wait on. This is
 * necessary to correctly implement implicit synchronization for any buffers
 * shared as a struct &dma_buf. This function can be used as the
 * &drm_plane_helper_funcs.prepare_fb callback.
 *
 * There is no need for &drm_plane_helper_funcs.cleanup_fb hook for simple
 * GEM based framebuffer drivers which have their buffers always pinned in
 * memory.
 *
 * See drm_atomic_set_fence_for_plane() for a discussion of implicit and
 * explicit fencing in atomic modeset updates.
 */
int drm_gem_plane_helper_prepare_fb(struct drm_plane *plane, struct drm_plane_state *state)
{
	struct drm_gem_object *obj;
	struct dma_fence *fence;

	if (!state->fb)
		return 0;

	obj = drm_gem_fb_get_obj(state->fb, 0);
	fence = dma_resv_get_excl_unlocked(obj->resv);
	drm_atomic_set_fence_for_plane(state, fence);

	return 0;
}
EXPORT_SYMBOL_GPL(drm_gem_plane_helper_prepare_fb);

/**
 * drm_gem_simple_display_pipe_prepare_fb - prepare_fb helper for &drm_simple_display_pipe
 * @pipe: Simple display pipe
 * @plane_state: Plane state
 *
 * This function uses drm_gem_plane_helper_prepare_fb() to extract the exclusive fence
 * from &drm_gem_object.resv and attaches it to plane state for the atomic
 * helper to wait on. This is necessary to correctly implement implicit
 * synchronization for any buffers shared as a struct &dma_buf. Drivers can use
 * this as their &drm_simple_display_pipe_funcs.prepare_fb callback.
 *
 * See drm_atomic_set_fence_for_plane() for a discussion of implicit and
 * explicit fencing in atomic modeset updates.
 */
int drm_gem_simple_display_pipe_prepare_fb(struct drm_simple_display_pipe *pipe,
					   struct drm_plane_state *plane_state)
{
	return drm_gem_plane_helper_prepare_fb(&pipe->plane, plane_state);
}
EXPORT_SYMBOL(drm_gem_simple_display_pipe_prepare_fb);

/*
 * Shadow-buffered Planes
 */

/**
 * drm_gem_duplicate_shadow_plane_state - duplicates shadow-buffered plane state
 * @plane: the plane
 *
 * This function implements struct &drm_plane_funcs.atomic_duplicate_state for
 * shadow-buffered planes. It assumes the existing state to be of type
 * struct drm_shadow_plane_state and it allocates the new state to be of this
 * type.
 *
 * The function does not duplicate existing mappings of the shadow buffers.
 * Mappings are maintained during the atomic commit by the plane's prepare_fb
 * and cleanup_fb helpers. See drm_gem_prepare_shadow_fb() and drm_gem_cleanup_shadow_fb()
 * for corresponding helpers.
 *
 * Returns:
 * A pointer to a new plane state on success, or NULL otherwise.
 */
struct drm_plane_state *
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
EXPORT_SYMBOL(drm_gem_duplicate_shadow_plane_state);

/**
 * drm_gem_destroy_shadow_plane_state - deletes shadow-buffered plane state
 * @plane: the plane
 * @plane_state: the plane state of type struct drm_shadow_plane_state
 *
 * This function implements struct &drm_plane_funcs.atomic_destroy_state
 * for shadow-buffered planes. It expects that mappings of shadow buffers
 * have been released already.
 */
void drm_gem_destroy_shadow_plane_state(struct drm_plane *plane,
					struct drm_plane_state *plane_state)
{
	struct drm_shadow_plane_state *shadow_plane_state =
		to_drm_shadow_plane_state(plane_state);

	__drm_atomic_helper_plane_destroy_state(&shadow_plane_state->base);
	kfree(shadow_plane_state);
}
EXPORT_SYMBOL(drm_gem_destroy_shadow_plane_state);

/**
 * drm_gem_reset_shadow_plane - resets a shadow-buffered plane
 * @plane: the plane
 *
 * This function implements struct &drm_plane_funcs.reset_plane for
 * shadow-buffered planes. It assumes the current plane state to be
 * of type struct drm_shadow_plane and it allocates the new state of
 * this type.
 */
void drm_gem_reset_shadow_plane(struct drm_plane *plane)
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
EXPORT_SYMBOL(drm_gem_reset_shadow_plane);

/**
 * drm_gem_prepare_shadow_fb - prepares shadow framebuffers
 * @plane: the plane
 * @plane_state: the plane state of type struct drm_shadow_plane_state
 *
 * This function implements struct &drm_plane_helper_funcs.prepare_fb. It
 * maps all buffer objects of the plane's framebuffer into kernel address
 * space and stores them in &struct drm_shadow_plane_state.map. The
 * framebuffer will be synchronized as part of the atomic commit.
 *
 * See drm_gem_cleanup_shadow_fb() for cleanup.
 *
 * Returns:
 * 0 on success, or a negative errno code otherwise.
 */
int drm_gem_prepare_shadow_fb(struct drm_plane *plane, struct drm_plane_state *plane_state)
{
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(plane_state);
	struct drm_framebuffer *fb = plane_state->fb;
	struct drm_gem_object *obj;
	struct dma_buf_map map;
	int ret;
	size_t i;

	if (!fb)
		return 0;

	ret = drm_gem_plane_helper_prepare_fb(plane, plane_state);
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
EXPORT_SYMBOL(drm_gem_prepare_shadow_fb);

/**
 * drm_gem_cleanup_shadow_fb - releases shadow framebuffers
 * @plane: the plane
 * @plane_state: the plane state of type struct drm_shadow_plane_state
 *
 * This function implements struct &drm_plane_helper_funcs.cleanup_fb.
 * This function unmaps all buffer objects of the plane's framebuffer.
 *
 * See drm_gem_prepare_shadow_fb() for more inforamtion.
 */
void drm_gem_cleanup_shadow_fb(struct drm_plane *plane, struct drm_plane_state *plane_state)
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
EXPORT_SYMBOL(drm_gem_cleanup_shadow_fb);

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
