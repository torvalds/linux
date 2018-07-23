/*
 * Copyright (C) 2014 Intel Corporation
 *
 * DRM universal plane helper functions
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/list.h>
#include <drm/drmP.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_rect.h>
#include <drm/drm_atomic.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_encoder.h>
#include <drm/drm_atomic_helper.h>

#define SUBPIXEL_MASK 0xffff

/**
 * DOC: overview
 *
 * This helper library has two parts. The first part has support to implement
 * primary plane support on top of the normal CRTC configuration interface.
 * Since the legacy &drm_mode_config_funcs.set_config interface ties the primary
 * plane together with the CRTC state this does not allow userspace to disable
 * the primary plane itself.  To avoid too much duplicated code use
 * drm_plane_helper_check_update() which can be used to enforce the same
 * restrictions as primary planes had thus. The default primary plane only
 * expose XRBG8888 and ARGB8888 as valid pixel formats for the attached
 * framebuffer.
 *
 * Drivers are highly recommended to implement proper support for primary
 * planes, and newly merged drivers must not rely upon these transitional
 * helpers.
 *
 * The second part also implements transitional helpers which allow drivers to
 * gradually switch to the atomic helper infrastructure for plane updates. Once
 * that switch is complete drivers shouldn't use these any longer, instead using
 * the proper legacy implementations for update and disable plane hooks provided
 * by the atomic helpers.
 *
 * Again drivers are strongly urged to switch to the new interfaces.
 *
 * The plane helpers share the function table structures with other helpers,
 * specifically also the atomic helpers. See &struct drm_plane_helper_funcs for
 * the details.
 */

/*
 * Returns the connectors currently associated with a CRTC.  This function
 * should be called twice:  once with a NULL connector list to retrieve
 * the list size, and once with the properly allocated list to be filled in.
 */
static int get_connectors_for_crtc(struct drm_crtc *crtc,
				   struct drm_connector **connector_list,
				   int num_connectors)
{
	struct drm_device *dev = crtc->dev;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	int count = 0;

	/*
	 * Note: Once we change the plane hooks to more fine-grained locking we
	 * need to grab the connection_mutex here to be able to make these
	 * checks.
	 */
	WARN_ON(!drm_modeset_is_locked(&dev->mode_config.connection_mutex));

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (connector->encoder && connector->encoder->crtc == crtc) {
			if (connector_list != NULL && count < num_connectors)
				*(connector_list++) = connector;

			count++;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	return count;
}

/**
 * drm_plane_helper_check_update() - Check plane update for validity
 * @plane: plane object to update
 * @crtc: owning CRTC of owning plane
 * @fb: framebuffer to flip onto plane
 * @src: source coordinates in 16.16 fixed point
 * @dst: integer destination coordinates
 * @rotation: plane rotation
 * @min_scale: minimum @src:@dest scaling factor in 16.16 fixed point
 * @max_scale: maximum @src:@dest scaling factor in 16.16 fixed point
 * @can_position: is it legal to position the plane such that it
 *                doesn't cover the entire crtc?  This will generally
 *                only be false for primary planes.
 * @can_update_disabled: can the plane be updated while the crtc
 *                       is disabled?
 * @visible: output parameter indicating whether plane is still visible after
 *           clipping
 *
 * Checks that a desired plane update is valid.  Drivers that provide
 * their own plane handling rather than helper-provided implementations may
 * still wish to call this function to avoid duplication of error checking
 * code.
 *
 * RETURNS:
 * Zero if update appears valid, error code on failure
 */
int drm_plane_helper_check_update(struct drm_plane *plane,
				  struct drm_crtc *crtc,
				  struct drm_framebuffer *fb,
				  struct drm_rect *src,
				  struct drm_rect *dst,
				  unsigned int rotation,
				  int min_scale,
				  int max_scale,
				  bool can_position,
				  bool can_update_disabled,
				  bool *visible)
{
	struct drm_plane_state plane_state = {
		.plane = plane,
		.crtc = crtc,
		.fb = fb,
		.src_x = src->x1,
		.src_y = src->y1,
		.src_w = drm_rect_width(src),
		.src_h = drm_rect_height(src),
		.crtc_x = dst->x1,
		.crtc_y = dst->y1,
		.crtc_w = drm_rect_width(dst),
		.crtc_h = drm_rect_height(dst),
		.rotation = rotation,
		.visible = *visible,
	};
	struct drm_crtc_state crtc_state = {
		.crtc = crtc,
		.enable = crtc->enabled,
		.mode = crtc->mode,
	};
	int ret;

	ret = drm_atomic_helper_check_plane_state(&plane_state, &crtc_state,
						  min_scale, max_scale,
						  can_position,
						  can_update_disabled);
	if (ret)
		return ret;

	*src = plane_state.src;
	*dst = plane_state.dst;
	*visible = plane_state.visible;

	return 0;
}
EXPORT_SYMBOL(drm_plane_helper_check_update);

/**
 * drm_primary_helper_update() - Helper for primary plane update
 * @plane: plane object to update
 * @crtc: owning CRTC of owning plane
 * @fb: framebuffer to flip onto plane
 * @crtc_x: x offset of primary plane on crtc
 * @crtc_y: y offset of primary plane on crtc
 * @crtc_w: width of primary plane rectangle on crtc
 * @crtc_h: height of primary plane rectangle on crtc
 * @src_x: x offset of @fb for panning
 * @src_y: y offset of @fb for panning
 * @src_w: width of source rectangle in @fb
 * @src_h: height of source rectangle in @fb
 * @ctx: lock acquire context, not used here
 *
 * Provides a default plane update handler for primary planes.  This is handler
 * is called in response to a userspace SetPlane operation on the plane with a
 * non-NULL framebuffer.  We call the driver's modeset handler to update the
 * framebuffer.
 *
 * SetPlane() on a primary plane of a disabled CRTC is not supported, and will
 * return an error.
 *
 * Note that we make some assumptions about hardware limitations that may not be
 * true for all hardware --
 *
 * 1. Primary plane cannot be repositioned.
 * 2. Primary plane cannot be scaled.
 * 3. Primary plane must cover the entire CRTC.
 * 4. Subpixel positioning is not supported.
 *
 * Drivers for hardware that don't have these restrictions can provide their
 * own implementation rather than using this helper.
 *
 * RETURNS:
 * Zero on success, error code on failure
 */
int drm_primary_helper_update(struct drm_plane *plane, struct drm_crtc *crtc,
			      struct drm_framebuffer *fb,
			      int crtc_x, int crtc_y,
			      unsigned int crtc_w, unsigned int crtc_h,
			      uint32_t src_x, uint32_t src_y,
			      uint32_t src_w, uint32_t src_h,
			      struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_mode_set set = {
		.crtc = crtc,
		.fb = fb,
		.mode = &crtc->mode,
		.x = src_x >> 16,
		.y = src_y >> 16,
	};
	struct drm_rect src = {
		.x1 = src_x,
		.y1 = src_y,
		.x2 = src_x + src_w,
		.y2 = src_y + src_h,
	};
	struct drm_rect dest = {
		.x1 = crtc_x,
		.y1 = crtc_y,
		.x2 = crtc_x + crtc_w,
		.y2 = crtc_y + crtc_h,
	};
	struct drm_connector **connector_list;
	int num_connectors, ret;
	bool visible;

	ret = drm_plane_helper_check_update(plane, crtc, fb,
					    &src, &dest,
					    DRM_MODE_ROTATE_0,
					    DRM_PLANE_HELPER_NO_SCALING,
					    DRM_PLANE_HELPER_NO_SCALING,
					    false, false, &visible);
	if (ret)
		return ret;

	if (!visible)
		/*
		 * Primary plane isn't visible.  Note that unless a driver
		 * provides their own disable function, this will just
		 * wind up returning -EINVAL to userspace.
		 */
		return plane->funcs->disable_plane(plane, ctx);

	/* Find current connectors for CRTC */
	num_connectors = get_connectors_for_crtc(crtc, NULL, 0);
	BUG_ON(num_connectors == 0);
	connector_list = kcalloc(num_connectors, sizeof(*connector_list),
				 GFP_KERNEL);
	if (!connector_list)
		return -ENOMEM;
	get_connectors_for_crtc(crtc, connector_list, num_connectors);

	set.connectors = connector_list;
	set.num_connectors = num_connectors;

	/*
	 * We call set_config() directly here rather than using
	 * drm_mode_set_config_internal.  We're reprogramming the same
	 * connectors that were already in use, so we shouldn't need the extra
	 * cross-CRTC fb refcounting to accomodate stealing connectors.
	 * drm_mode_setplane() already handles the basic refcounting for the
	 * framebuffers involved in this operation.
	 */
	ret = crtc->funcs->set_config(&set, ctx);

	kfree(connector_list);
	return ret;
}
EXPORT_SYMBOL(drm_primary_helper_update);

/**
 * drm_primary_helper_disable() - Helper for primary plane disable
 * @plane: plane to disable
 * @ctx: lock acquire context, not used here
 *
 * Provides a default plane disable handler for primary planes.  This is handler
 * is called in response to a userspace SetPlane operation on the plane with a
 * NULL framebuffer parameter.  It unconditionally fails the disable call with
 * -EINVAL the only way to disable the primary plane without driver support is
 * to disable the entire CRTC. Which does not match the plane
 * &drm_plane_funcs.disable_plane hook.
 *
 * Note that some hardware may be able to disable the primary plane without
 * disabling the whole CRTC.  Drivers for such hardware should provide their
 * own disable handler that disables just the primary plane (and they'll likely
 * need to provide their own update handler as well to properly re-enable a
 * disabled primary plane).
 *
 * RETURNS:
 * Unconditionally returns -EINVAL.
 */
int drm_primary_helper_disable(struct drm_plane *plane,
			       struct drm_modeset_acquire_ctx *ctx)
{
	return -EINVAL;
}
EXPORT_SYMBOL(drm_primary_helper_disable);

/**
 * drm_primary_helper_destroy() - Helper for primary plane destruction
 * @plane: plane to destroy
 *
 * Provides a default plane destroy handler for primary planes.  This handler
 * is called during CRTC destruction.  We disable the primary plane, remove
 * it from the DRM plane list, and deallocate the plane structure.
 */
void drm_primary_helper_destroy(struct drm_plane *plane)
{
	drm_plane_cleanup(plane);
	kfree(plane);
}
EXPORT_SYMBOL(drm_primary_helper_destroy);

const struct drm_plane_funcs drm_primary_helper_funcs = {
	.update_plane = drm_primary_helper_update,
	.disable_plane = drm_primary_helper_disable,
	.destroy = drm_primary_helper_destroy,
};
EXPORT_SYMBOL(drm_primary_helper_funcs);

int drm_plane_helper_commit(struct drm_plane *plane,
			    struct drm_plane_state *plane_state,
			    struct drm_framebuffer *old_fb)
{
	const struct drm_plane_helper_funcs *plane_funcs;
	struct drm_crtc *crtc[2];
	const struct drm_crtc_helper_funcs *crtc_funcs[2];
	int i, ret = 0;

	plane_funcs = plane->helper_private;

	/* Since this is a transitional helper we can't assume that plane->state
	 * is always valid. Hence we need to use plane->crtc instead of
	 * plane->state->crtc as the old crtc. */
	crtc[0] = plane->crtc;
	crtc[1] = crtc[0] != plane_state->crtc ? plane_state->crtc : NULL;

	for (i = 0; i < 2; i++)
		crtc_funcs[i] = crtc[i] ? crtc[i]->helper_private : NULL;

	if (plane_funcs->atomic_check) {
		ret = plane_funcs->atomic_check(plane, plane_state);
		if (ret)
			goto out;
	}

	if (plane_funcs->prepare_fb && plane_state->fb != old_fb) {
		ret = plane_funcs->prepare_fb(plane,
					      plane_state);
		if (ret)
			goto out;
	}

	/* Point of no return, commit sw state. */
	swap(plane->state, plane_state);

	for (i = 0; i < 2; i++) {
		if (crtc_funcs[i] && crtc_funcs[i]->atomic_begin)
			crtc_funcs[i]->atomic_begin(crtc[i], crtc[i]->state);
	}

	/*
	 * Drivers may optionally implement the ->atomic_disable callback, so
	 * special-case that here.
	 */
	if (drm_atomic_plane_disabling(plane_state, plane->state) &&
	    plane_funcs->atomic_disable)
		plane_funcs->atomic_disable(plane, plane_state);
	else
		plane_funcs->atomic_update(plane, plane_state);

	for (i = 0; i < 2; i++) {
		if (crtc_funcs[i] && crtc_funcs[i]->atomic_flush)
			crtc_funcs[i]->atomic_flush(crtc[i], crtc[i]->state);
	}

	/*
	 * If we only moved the plane and didn't change fb's, there's no need to
	 * wait for vblank.
	 */
	if (plane->state->fb == old_fb)
		goto out;

	for (i = 0; i < 2; i++) {
		if (!crtc[i])
			continue;

		if (crtc[i]->cursor == plane)
			continue;

		/* There's no other way to figure out whether the crtc is running. */
		ret = drm_crtc_vblank_get(crtc[i]);
		if (ret == 0) {
			drm_crtc_wait_one_vblank(crtc[i]);
			drm_crtc_vblank_put(crtc[i]);
		}

		ret = 0;
	}

	if (plane_funcs->cleanup_fb)
		plane_funcs->cleanup_fb(plane, plane_state);
out:
	if (plane->funcs->atomic_destroy_state)
		plane->funcs->atomic_destroy_state(plane, plane_state);
	else
		drm_atomic_helper_plane_destroy_state(plane, plane_state);

	return ret;
}

/**
 * drm_plane_helper_update() - Transitional helper for plane update
 * @plane: plane object to update
 * @crtc: owning CRTC of owning plane
 * @fb: framebuffer to flip onto plane
 * @crtc_x: x offset of primary plane on crtc
 * @crtc_y: y offset of primary plane on crtc
 * @crtc_w: width of primary plane rectangle on crtc
 * @crtc_h: height of primary plane rectangle on crtc
 * @src_x: x offset of @fb for panning
 * @src_y: y offset of @fb for panning
 * @src_w: width of source rectangle in @fb
 * @src_h: height of source rectangle in @fb
 * @ctx: lock acquire context, not used here
 *
 * Provides a default plane update handler using the atomic plane update
 * functions. It is fully left to the driver to check plane constraints and
 * handle corner-cases like a fully occluded or otherwise invisible plane.
 *
 * This is useful for piecewise transitioning of a driver to the atomic helpers.
 *
 * RETURNS:
 * Zero on success, error code on failure
 */
int drm_plane_helper_update(struct drm_plane *plane, struct drm_crtc *crtc,
			    struct drm_framebuffer *fb,
			    int crtc_x, int crtc_y,
			    unsigned int crtc_w, unsigned int crtc_h,
			    uint32_t src_x, uint32_t src_y,
			    uint32_t src_w, uint32_t src_h,
			    struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_plane_state *plane_state;

	if (plane->funcs->atomic_duplicate_state)
		plane_state = plane->funcs->atomic_duplicate_state(plane);
	else {
		if (!plane->state)
			drm_atomic_helper_plane_reset(plane);

		plane_state = drm_atomic_helper_plane_duplicate_state(plane);
	}
	if (!plane_state)
		return -ENOMEM;
	plane_state->plane = plane;

	plane_state->crtc = crtc;
	drm_atomic_set_fb_for_plane(plane_state, fb);
	plane_state->crtc_x = crtc_x;
	plane_state->crtc_y = crtc_y;
	plane_state->crtc_h = crtc_h;
	plane_state->crtc_w = crtc_w;
	plane_state->src_x = src_x;
	plane_state->src_y = src_y;
	plane_state->src_h = src_h;
	plane_state->src_w = src_w;

	return drm_plane_helper_commit(plane, plane_state, plane->fb);
}
EXPORT_SYMBOL(drm_plane_helper_update);

/**
 * drm_plane_helper_disable() - Transitional helper for plane disable
 * @plane: plane to disable
 * @ctx: lock acquire context, not used here
 *
 * Provides a default plane disable handler using the atomic plane update
 * functions. It is fully left to the driver to check plane constraints and
 * handle corner-cases like a fully occluded or otherwise invisible plane.
 *
 * This is useful for piecewise transitioning of a driver to the atomic helpers.
 *
 * RETURNS:
 * Zero on success, error code on failure
 */
int drm_plane_helper_disable(struct drm_plane *plane,
			     struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_plane_state *plane_state;
	struct drm_framebuffer *old_fb;

	/* crtc helpers love to call disable functions for already disabled hw
	 * functions. So cope with that. */
	if (!plane->crtc)
		return 0;

	if (plane->funcs->atomic_duplicate_state)
		plane_state = plane->funcs->atomic_duplicate_state(plane);
	else {
		if (!plane->state)
			drm_atomic_helper_plane_reset(plane);

		plane_state = drm_atomic_helper_plane_duplicate_state(plane);
	}
	if (!plane_state)
		return -ENOMEM;
	plane_state->plane = plane;

	plane_state->crtc = NULL;
	old_fb = plane_state->fb;
	drm_atomic_set_fb_for_plane(plane_state, NULL);

	return drm_plane_helper_commit(plane, plane_state, old_fb);
}
EXPORT_SYMBOL(drm_plane_helper_disable);
