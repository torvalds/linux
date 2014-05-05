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
#include <drm/drm_rect.h>

#define SUBPIXEL_MASK 0xffff

/*
 * This is the minimal list of formats that seem to be safe for modeset use
 * with all current DRM drivers.  Most hardware can actually support more
 * formats than this and drivers may specify a more accurate list when
 * creating the primary plane.  However drivers that still call
 * drm_plane_init() will use this minimal format list as the default.
 */
const static uint32_t safe_modeset_formats[] = {
       DRM_FORMAT_XRGB8888,
       DRM_FORMAT_ARGB8888,
};

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
	int count = 0;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head)
		if (connector->encoder && connector->encoder->crtc == crtc) {
			if (connector_list != NULL && count < num_connectors)
				*(connector_list++) = connector;

			count++;
		}

	return count;
}

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
 *   1) Primary plane cannot be repositioned.
 *   2) Primary plane cannot be scaled.
 *   3) Primary plane must cover the entire CRTC.
 *   4) Subpixel positioning is not supported.
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
			      uint32_t src_w, uint32_t src_h)
{
	struct drm_mode_set set = {
		.crtc = crtc,
		.fb = fb,
		.mode = &crtc->mode,
		.x = src_x >> 16,
		.y = src_y >> 16,
	};
	struct drm_rect dest = {
		.x1 = crtc_x,
		.y1 = crtc_y,
		.x2 = crtc_x + crtc_w,
		.y2 = crtc_y + crtc_h,
	};
	struct drm_rect clip = {
		.x2 = crtc->mode.hdisplay,
		.y2 = crtc->mode.vdisplay,
	};
	struct drm_connector **connector_list;
	struct drm_framebuffer *tmpfb;
	int num_connectors, ret;

	if (!crtc->enabled) {
		DRM_DEBUG_KMS("Cannot update primary plane of a disabled CRTC.\n");
		return -EINVAL;
	}

	/* Disallow subpixel positioning */
	if ((src_x | src_y | src_w | src_h) & SUBPIXEL_MASK) {
		DRM_DEBUG_KMS("Primary plane does not support subpixel positioning\n");
		return -EINVAL;
	}

	/* Primary planes are locked to their owning CRTC */
	if (plane->possible_crtcs != drm_crtc_mask(crtc)) {
		DRM_DEBUG_KMS("Cannot change primary plane CRTC\n");
		return -EINVAL;
	}

	/* Disallow scaling */
	if (crtc_w != src_w || crtc_h != src_h) {
		DRM_DEBUG_KMS("Can't scale primary plane\n");
		return -EINVAL;
	}

	/* Make sure primary plane covers entire CRTC */
	drm_rect_intersect(&dest, &clip);
	if (dest.x1 != 0 || dest.y1 != 0 ||
	    dest.x2 != crtc->mode.hdisplay || dest.y2 != crtc->mode.vdisplay) {
		DRM_DEBUG_KMS("Primary plane must cover entire CRTC\n");
		return -EINVAL;
	}

	/* Framebuffer must be big enough to cover entire plane */
	ret = drm_crtc_check_viewport(crtc, crtc_x, crtc_y, &crtc->mode, fb);
	if (ret)
		return ret;

	/* Find current connectors for CRTC */
	num_connectors = get_connectors_for_crtc(crtc, NULL, 0);
	BUG_ON(num_connectors == 0);
	connector_list = kzalloc(num_connectors * sizeof(*connector_list),
				 GFP_KERNEL);
	if (!connector_list)
		return -ENOMEM;
	get_connectors_for_crtc(crtc, connector_list, num_connectors);

	set.connectors = connector_list;
	set.num_connectors = num_connectors;

	/*
	 * set_config() adjusts crtc->primary->fb; however the DRM setplane
	 * code that called us expects to handle the framebuffer update and
	 * reference counting; save and restore the current fb before
	 * calling it.
	 *
	 * N.B., we call set_config() directly here rather than using
	 * drm_mode_set_config_internal.  We're reprogramming the same
	 * connectors that were already in use, so we shouldn't need the extra
	 * cross-CRTC fb refcounting to accomodate stealing connectors.
	 * drm_mode_setplane() already handles the basic refcounting for the
	 * framebuffers involved in this operation.
	 */
	tmpfb = plane->fb;
	ret = crtc->funcs->set_config(&set);
	plane->fb = tmpfb;

	kfree(connector_list);
	return ret;
}
EXPORT_SYMBOL(drm_primary_helper_update);

/**
 * drm_primary_helper_disable() - Helper for primary plane disable
 * @plane: plane to disable
 *
 * Provides a default plane disable handler for primary planes.  This is handler
 * is called in response to a userspace SetPlane operation on the plane with a
 * NULL framebuffer parameter.  It unconditionally fails the disable call with
 * -EINVAL the only way to disable the primary plane without driver support is
 * to disable the entier CRTC. Which does not match the plane ->disable hook.
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
int drm_primary_helper_disable(struct drm_plane *plane)
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
	plane->funcs->disable_plane(plane);
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

/**
 * drm_primary_helper_create_plane() - Create a generic primary plane
 * @dev: drm device
 * @formats: pixel formats supported, or NULL for a default safe list
 * @num_formats: size of @formats; ignored if @formats is NULL
 *
 * Allocates and initializes a primary plane that can be used with the primary
 * plane helpers.  Drivers that wish to use driver-specific plane structures or
 * provide custom handler functions may perform their own allocation and
 * initialization rather than calling this function.
 */
struct drm_plane *drm_primary_helper_create_plane(struct drm_device *dev,
						  const uint32_t *formats,
						  int num_formats)
{
	struct drm_plane *primary;
	int ret;

	primary = kzalloc(sizeof(*primary), GFP_KERNEL);
	if (primary == NULL) {
		DRM_DEBUG_KMS("Failed to allocate primary plane\n");
		return NULL;
	}

	if (formats == NULL) {
		formats = safe_modeset_formats;
		num_formats = ARRAY_SIZE(safe_modeset_formats);
	}

	/* possible_crtc's will be filled in later by crtc_init */
	ret = drm_plane_init(dev, primary, 0, &drm_primary_helper_funcs,
			     formats, num_formats,
			     DRM_PLANE_TYPE_PRIMARY);
	if (ret) {
		kfree(primary);
		primary = NULL;
	}

	return primary;
}
EXPORT_SYMBOL(drm_primary_helper_create_plane);

/**
 * drm_crtc_init - Legacy CRTC initialization function
 * @dev: DRM device
 * @crtc: CRTC object to init
 * @funcs: callbacks for the new CRTC
 *
 * Initialize a CRTC object with a default helper-provided primary plane and no
 * cursor plane.
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_crtc_init(struct drm_device *dev, struct drm_crtc *crtc,
		  const struct drm_crtc_funcs *funcs)
{
	struct drm_plane *primary;

	primary = drm_primary_helper_create_plane(dev, NULL, 0);
	return drm_crtc_init_with_planes(dev, crtc, primary, NULL, funcs);
}
EXPORT_SYMBOL(drm_crtc_init);
