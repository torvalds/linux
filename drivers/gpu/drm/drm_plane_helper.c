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

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_encoder.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_rect.h>

#define SUBPIXEL_MASK 0xffff

/**
 * DOC: overview
 *
 * This helper library has two parts. The first part has support to implement
 * primary plane support on top of the normal CRTC configuration interface.
 * Since the legacy &drm_mode_config_funcs.set_config interface ties the primary
 * plane together with the CRTC state this does not allow userspace to disable
 * the primary plane itself. The default primary plane only expose XRBG8888 and
 * ARGB8888 as valid pixel formats for the attached framebuffer.
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

static int drm_plane_helper_check_update(struct drm_plane *plane,
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

static int drm_primary_helper_update(struct drm_plane *plane, struct drm_crtc *crtc,
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
	 * cross-CRTC fb refcounting to accommodate stealing connectors.
	 * drm_mode_setplane() already handles the basic refcounting for the
	 * framebuffers involved in this operation.
	 */
	ret = crtc->funcs->set_config(&set, ctx);

	kfree(connector_list);
	return ret;
}

static int drm_primary_helper_disable(struct drm_plane *plane,
				      struct drm_modeset_acquire_ctx *ctx)
{
	return -EINVAL;
}

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
