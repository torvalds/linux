/*
 * Copyright (c) 2006-2008 Intel Corporation
 * Copyright (c) 2007 Dave Airlie <airlied@linux.ie>
 * Copyright (c) 2008 Red Hat Inc.
 *
 * DRM core CRTC related functions
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 *
 * Authors:
 *      Keith Packard
 *	Eric Anholt <eric@anholt.net>
 *      Dave Airlie <airlied@linux.ie>
 *      Jesse Barnes <jesse.barnes@intel.com>
 */
#include <linux/ctype.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/dma-fence.h>
#include <linux/uaccess.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_modeset_lock.h>
#include <drm/drm_atomic.h>
#include <drm/drm_auth.h>
#include <drm/drm_debugfs_crc.h>
#include <drm/drm_drv.h>
#include <drm/drm_print.h>
#include <drm/drm_file.h>

#include "drm_crtc_internal.h"
#include "drm_internal.h"

/**
 * DOC: overview
 *
 * A CRTC represents the overall display pipeline. It receives pixel data from
 * &drm_plane and blends them together. The &drm_display_mode is also attached
 * to the CRTC, specifying display timings. On the output side the data is fed
 * to one or more &drm_encoder, which are then each connected to one
 * &drm_connector.
 *
 * To create a CRTC, a KMS drivers allocates and zeroes an instances of
 * &struct drm_crtc (possibly as part of a larger structure) and registers it
 * with a call to drm_crtc_init_with_planes().
 *
 * The CRTC is also the entry point for legacy modeset operations, see
 * &drm_crtc_funcs.set_config, legacy plane operations, see
 * &drm_crtc_funcs.page_flip and &drm_crtc_funcs.cursor_set2, and other legacy
 * operations like &drm_crtc_funcs.gamma_set. For atomic drivers all these
 * features are controlled through &drm_property and
 * &drm_mode_config_funcs.atomic_check and &drm_mode_config_funcs.atomic_check.
 */

/**
 * drm_crtc_from_index - find the registered CRTC at an index
 * @dev: DRM device
 * @idx: index of registered CRTC to find for
 *
 * Given a CRTC index, return the registered CRTC from DRM device's
 * list of CRTCs with matching index. This is the inverse of drm_crtc_index().
 * It's useful in the vblank callbacks (like &drm_driver.enable_vblank or
 * &drm_driver.disable_vblank), since that still deals with indices instead
 * of pointers to &struct drm_crtc."
 */
struct drm_crtc *drm_crtc_from_index(struct drm_device *dev, int idx)
{
	struct drm_crtc *crtc;

	drm_for_each_crtc(crtc, dev)
		if (idx == crtc->index)
			return crtc;

	return NULL;
}
EXPORT_SYMBOL(drm_crtc_from_index);

int drm_crtc_force_disable(struct drm_crtc *crtc)
{
	struct drm_mode_set set = {
		.crtc = crtc,
	};

	WARN_ON(drm_drv_uses_atomic_modeset(crtc->dev));

	return drm_mode_set_config_internal(&set);
}

static unsigned int drm_num_crtcs(struct drm_device *dev)
{
	unsigned int num = 0;
	struct drm_crtc *tmp;

	drm_for_each_crtc(tmp, dev) {
		num++;
	}

	return num;
}

int drm_crtc_register_all(struct drm_device *dev)
{
	struct drm_crtc *crtc;
	int ret = 0;

	drm_for_each_crtc(crtc, dev) {
		drm_debugfs_crtc_add(crtc);

		if (crtc->funcs->late_register)
			ret = crtc->funcs->late_register(crtc);
		if (ret)
			return ret;
	}

	return 0;
}

void drm_crtc_unregister_all(struct drm_device *dev)
{
	struct drm_crtc *crtc;

	drm_for_each_crtc(crtc, dev) {
		if (crtc->funcs->early_unregister)
			crtc->funcs->early_unregister(crtc);
		drm_debugfs_crtc_remove(crtc);
	}
}

static int drm_crtc_crc_init(struct drm_crtc *crtc)
{
#ifdef CONFIG_DEBUG_FS
	spin_lock_init(&crtc->crc.lock);
	init_waitqueue_head(&crtc->crc.wq);
	crtc->crc.source = kstrdup("auto", GFP_KERNEL);
	if (!crtc->crc.source)
		return -ENOMEM;
#endif
	return 0;
}

static void drm_crtc_crc_fini(struct drm_crtc *crtc)
{
#ifdef CONFIG_DEBUG_FS
	kfree(crtc->crc.source);
#endif
}

static const struct dma_fence_ops drm_crtc_fence_ops;

static struct drm_crtc *fence_to_crtc(struct dma_fence *fence)
{
	BUG_ON(fence->ops != &drm_crtc_fence_ops);
	return container_of(fence->lock, struct drm_crtc, fence_lock);
}

static const char *drm_crtc_fence_get_driver_name(struct dma_fence *fence)
{
	struct drm_crtc *crtc = fence_to_crtc(fence);

	return crtc->dev->driver->name;
}

static const char *drm_crtc_fence_get_timeline_name(struct dma_fence *fence)
{
	struct drm_crtc *crtc = fence_to_crtc(fence);

	return crtc->timeline_name;
}

static const struct dma_fence_ops drm_crtc_fence_ops = {
	.get_driver_name = drm_crtc_fence_get_driver_name,
	.get_timeline_name = drm_crtc_fence_get_timeline_name,
};

struct dma_fence *drm_crtc_create_fence(struct drm_crtc *crtc)
{
	struct dma_fence *fence;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return NULL;

	dma_fence_init(fence, &drm_crtc_fence_ops, &crtc->fence_lock,
		       crtc->fence_context, ++crtc->fence_seqno);

	return fence;
}

/**
 * drm_crtc_init_with_planes - Initialise a new CRTC object with
 *    specified primary and cursor planes.
 * @dev: DRM device
 * @crtc: CRTC object to init
 * @primary: Primary plane for CRTC
 * @cursor: Cursor plane for CRTC
 * @funcs: callbacks for the new CRTC
 * @name: printf style format string for the CRTC name, or NULL for default name
 *
 * Inits a new object created as base part of a driver crtc object. Drivers
 * should use this function instead of drm_crtc_init(), which is only provided
 * for backwards compatibility with drivers which do not yet support universal
 * planes). For really simple hardware which has only 1 plane look at
 * drm_simple_display_pipe_init() instead.
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_crtc_init_with_planes(struct drm_device *dev, struct drm_crtc *crtc,
			      struct drm_plane *primary,
			      struct drm_plane *cursor,
			      const struct drm_crtc_funcs *funcs,
			      const char *name, ...)
{
	struct drm_mode_config *config = &dev->mode_config;
	int ret;

	WARN_ON(primary && primary->type != DRM_PLANE_TYPE_PRIMARY);
	WARN_ON(cursor && cursor->type != DRM_PLANE_TYPE_CURSOR);

	/* crtc index is used with 32bit bitmasks */
	if (WARN_ON(config->num_crtc >= 32))
		return -EINVAL;

	WARN_ON(drm_drv_uses_atomic_modeset(dev) &&
		(!funcs->atomic_destroy_state ||
		 !funcs->atomic_duplicate_state));

	crtc->dev = dev;
	crtc->funcs = funcs;

	INIT_LIST_HEAD(&crtc->commit_list);
	spin_lock_init(&crtc->commit_lock);

	drm_modeset_lock_init(&crtc->mutex);
	ret = drm_mode_object_add(dev, &crtc->base, DRM_MODE_OBJECT_CRTC);
	if (ret)
		return ret;

	if (name) {
		va_list ap;

		va_start(ap, name);
		crtc->name = kvasprintf(GFP_KERNEL, name, ap);
		va_end(ap);
	} else {
		crtc->name = kasprintf(GFP_KERNEL, "crtc-%d",
				       drm_num_crtcs(dev));
	}
	if (!crtc->name) {
		drm_mode_object_unregister(dev, &crtc->base);
		return -ENOMEM;
	}

	crtc->fence_context = dma_fence_context_alloc(1);
	spin_lock_init(&crtc->fence_lock);
	snprintf(crtc->timeline_name, sizeof(crtc->timeline_name),
		 "CRTC:%d-%s", crtc->base.id, crtc->name);

	crtc->base.properties = &crtc->properties;

	list_add_tail(&crtc->head, &config->crtc_list);
	crtc->index = config->num_crtc++;

	crtc->primary = primary;
	crtc->cursor = cursor;
	if (primary && !primary->possible_crtcs)
		primary->possible_crtcs = drm_crtc_mask(crtc);
	if (cursor && !cursor->possible_crtcs)
		cursor->possible_crtcs = drm_crtc_mask(crtc);

	ret = drm_crtc_crc_init(crtc);
	if (ret) {
		drm_mode_object_unregister(dev, &crtc->base);
		return ret;
	}

	if (drm_core_check_feature(dev, DRIVER_ATOMIC)) {
		drm_object_attach_property(&crtc->base, config->prop_active, 0);
		drm_object_attach_property(&crtc->base, config->prop_mode_id, 0);
		drm_object_attach_property(&crtc->base,
					   config->prop_out_fence_ptr, 0);
		drm_object_attach_property(&crtc->base,
					   config->prop_vrr_enabled, 0);
	}

	return 0;
}
EXPORT_SYMBOL(drm_crtc_init_with_planes);

/**
 * drm_crtc_cleanup - Clean up the core crtc usage
 * @crtc: CRTC to cleanup
 *
 * This function cleans up @crtc and removes it from the DRM mode setting
 * core. Note that the function does *not* free the crtc structure itself,
 * this is the responsibility of the caller.
 */
void drm_crtc_cleanup(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;

	/* Note that the crtc_list is considered to be static; should we
	 * remove the drm_crtc at runtime we would have to decrement all
	 * the indices on the drm_crtc after us in the crtc_list.
	 */

	drm_crtc_crc_fini(crtc);

	kfree(crtc->gamma_store);
	crtc->gamma_store = NULL;

	drm_modeset_lock_fini(&crtc->mutex);

	drm_mode_object_unregister(dev, &crtc->base);
	list_del(&crtc->head);
	dev->mode_config.num_crtc--;

	WARN_ON(crtc->state && !crtc->funcs->atomic_destroy_state);
	if (crtc->state && crtc->funcs->atomic_destroy_state)
		crtc->funcs->atomic_destroy_state(crtc, crtc->state);

	kfree(crtc->name);

	memset(crtc, 0, sizeof(*crtc));
}
EXPORT_SYMBOL(drm_crtc_cleanup);

/**
 * drm_mode_getcrtc - get CRTC configuration
 * @dev: drm device for the ioctl
 * @data: data pointer for the ioctl
 * @file_priv: drm file for the ioctl call
 *
 * Construct a CRTC configuration structure to return to the user.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_getcrtc(struct drm_device *dev,
		     void *data, struct drm_file *file_priv)
{
	struct drm_mode_crtc *crtc_resp = data;
	struct drm_crtc *crtc;
	struct drm_plane *plane;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;

	crtc = drm_crtc_find(dev, file_priv, crtc_resp->crtc_id);
	if (!crtc)
		return -ENOENT;

	plane = crtc->primary;

	crtc_resp->gamma_size = crtc->gamma_size;

	drm_modeset_lock(&plane->mutex, NULL);
	if (plane->state && plane->state->fb)
		crtc_resp->fb_id = plane->state->fb->base.id;
	else if (!plane->state && plane->fb)
		crtc_resp->fb_id = plane->fb->base.id;
	else
		crtc_resp->fb_id = 0;

	if (plane->state) {
		crtc_resp->x = plane->state->src_x >> 16;
		crtc_resp->y = plane->state->src_y >> 16;
	}
	drm_modeset_unlock(&plane->mutex);

	drm_modeset_lock(&crtc->mutex, NULL);
	if (crtc->state) {
		if (crtc->state->enable) {
			drm_mode_convert_to_umode(&crtc_resp->mode, &crtc->state->mode);
			crtc_resp->mode_valid = 1;
		} else {
			crtc_resp->mode_valid = 0;
		}
	} else {
		crtc_resp->x = crtc->x;
		crtc_resp->y = crtc->y;

		if (crtc->enabled) {
			drm_mode_convert_to_umode(&crtc_resp->mode, &crtc->mode);
			crtc_resp->mode_valid = 1;

		} else {
			crtc_resp->mode_valid = 0;
		}
	}
	if (!file_priv->aspect_ratio_allowed)
		crtc_resp->mode.flags &= ~DRM_MODE_FLAG_PIC_AR_MASK;
	drm_modeset_unlock(&crtc->mutex);

	return 0;
}

static int __drm_mode_set_config_internal(struct drm_mode_set *set,
					  struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_crtc *crtc = set->crtc;
	struct drm_framebuffer *fb;
	struct drm_crtc *tmp;
	int ret;

	WARN_ON(drm_drv_uses_atomic_modeset(crtc->dev));

	/*
	 * NOTE: ->set_config can also disable other crtcs (if we steal all
	 * connectors from it), hence we need to refcount the fbs across all
	 * crtcs. Atomic modeset will have saner semantics ...
	 */
	drm_for_each_crtc(tmp, crtc->dev) {
		struct drm_plane *plane = tmp->primary;

		plane->old_fb = plane->fb;
	}

	fb = set->fb;

	ret = crtc->funcs->set_config(set, ctx);
	if (ret == 0) {
		struct drm_plane *plane = crtc->primary;

		plane->crtc = fb ? crtc : NULL;
		plane->fb = fb;
	}

	drm_for_each_crtc(tmp, crtc->dev) {
		struct drm_plane *plane = tmp->primary;

		if (plane->fb)
			drm_framebuffer_get(plane->fb);
		if (plane->old_fb)
			drm_framebuffer_put(plane->old_fb);
		plane->old_fb = NULL;
	}

	return ret;
}

/**
 * drm_mode_set_config_internal - helper to call &drm_mode_config_funcs.set_config
 * @set: modeset config to set
 *
 * This is a little helper to wrap internal calls to the
 * &drm_mode_config_funcs.set_config driver interface. The only thing it adds is
 * correct refcounting dance.
 *
 * This should only be used by non-atomic legacy drivers.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_set_config_internal(struct drm_mode_set *set)
{
	WARN_ON(drm_drv_uses_atomic_modeset(set->crtc->dev));

	return __drm_mode_set_config_internal(set, NULL);
}
EXPORT_SYMBOL(drm_mode_set_config_internal);

/**
 * drm_crtc_check_viewport - Checks that a framebuffer is big enough for the
 *     CRTC viewport
 * @crtc: CRTC that framebuffer will be displayed on
 * @x: x panning
 * @y: y panning
 * @mode: mode that framebuffer will be displayed under
 * @fb: framebuffer to check size of
 */
int drm_crtc_check_viewport(const struct drm_crtc *crtc,
			    int x, int y,
			    const struct drm_display_mode *mode,
			    const struct drm_framebuffer *fb)

{
	int hdisplay, vdisplay;

	drm_mode_get_hv_timing(mode, &hdisplay, &vdisplay);

	if (crtc->state &&
	    drm_rotation_90_or_270(crtc->primary->state->rotation))
		swap(hdisplay, vdisplay);

	return drm_framebuffer_check_src_coords(x << 16, y << 16,
						hdisplay << 16, vdisplay << 16,
						fb);
}
EXPORT_SYMBOL(drm_crtc_check_viewport);

/**
 * drm_mode_setcrtc - set CRTC configuration
 * @dev: drm device for the ioctl
 * @data: data pointer for the ioctl
 * @file_priv: drm file for the ioctl call
 *
 * Build a new CRTC configuration based on user request.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_setcrtc(struct drm_device *dev, void *data,
		     struct drm_file *file_priv)
{
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_mode_crtc *crtc_req = data;
	struct drm_crtc *crtc;
	struct drm_plane *plane;
	struct drm_connector **connector_set = NULL, *connector;
	struct drm_framebuffer *fb = NULL;
	struct drm_display_mode *mode = NULL;
	struct drm_mode_set set;
	uint32_t __user *set_connectors_ptr;
	struct drm_modeset_acquire_ctx ctx;
	int ret;
	int i;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;

	/*
	 * Universal plane src offsets are only 16.16, prevent havoc for
	 * drivers using universal plane code internally.
	 */
	if (crtc_req->x & 0xffff0000 || crtc_req->y & 0xffff0000)
		return -ERANGE;

	crtc = drm_crtc_find(dev, file_priv, crtc_req->crtc_id);
	if (!crtc) {
		DRM_DEBUG_KMS("Unknown CRTC ID %d\n", crtc_req->crtc_id);
		return -ENOENT;
	}
	DRM_DEBUG_KMS("[CRTC:%d:%s]\n", crtc->base.id, crtc->name);

	plane = crtc->primary;

	/* allow disabling with the primary plane leased */
	if (crtc_req->mode_valid && !drm_lease_held(file_priv, plane->base.id))
		return -EACCES;

	DRM_MODESET_LOCK_ALL_BEGIN(dev, ctx,
				   DRM_MODESET_ACQUIRE_INTERRUPTIBLE, ret);

	if (crtc_req->mode_valid) {
		/* If we have a mode we need a framebuffer. */
		/* If we pass -1, set the mode with the currently bound fb */
		if (crtc_req->fb_id == -1) {
			struct drm_framebuffer *old_fb;

			if (plane->state)
				old_fb = plane->state->fb;
			else
				old_fb = plane->fb;

			if (!old_fb) {
				DRM_DEBUG_KMS("CRTC doesn't have current FB\n");
				ret = -EINVAL;
				goto out;
			}

			fb = old_fb;
			/* Make refcounting symmetric with the lookup path. */
			drm_framebuffer_get(fb);
		} else {
			fb = drm_framebuffer_lookup(dev, file_priv, crtc_req->fb_id);
			if (!fb) {
				DRM_DEBUG_KMS("Unknown FB ID%d\n",
						crtc_req->fb_id);
				ret = -ENOENT;
				goto out;
			}
		}

		mode = drm_mode_create(dev);
		if (!mode) {
			ret = -ENOMEM;
			goto out;
		}
		if (!file_priv->aspect_ratio_allowed &&
		    (crtc_req->mode.flags & DRM_MODE_FLAG_PIC_AR_MASK) != DRM_MODE_FLAG_PIC_AR_NONE) {
			DRM_DEBUG_KMS("Unexpected aspect-ratio flag bits\n");
			ret = -EINVAL;
			goto out;
		}


		ret = drm_mode_convert_umode(dev, mode, &crtc_req->mode);
		if (ret) {
			DRM_DEBUG_KMS("Invalid mode (ret=%d, status=%s)\n",
				      ret, drm_get_mode_status_name(mode->status));
			drm_mode_debug_printmodeline(mode);
			goto out;
		}

		/*
		 * Check whether the primary plane supports the fb pixel format.
		 * Drivers not implementing the universal planes API use a
		 * default formats list provided by the DRM core which doesn't
		 * match real hardware capabilities. Skip the check in that
		 * case.
		 */
		if (!plane->format_default) {
			ret = drm_plane_check_pixel_format(plane,
							   fb->format->format,
							   fb->modifier);
			if (ret) {
				struct drm_format_name_buf format_name;
				DRM_DEBUG_KMS("Invalid pixel format %s, modifier 0x%llx\n",
					      drm_get_format_name(fb->format->format,
								  &format_name),
					      fb->modifier);
				goto out;
			}
		}

		ret = drm_crtc_check_viewport(crtc, crtc_req->x, crtc_req->y,
					      mode, fb);
		if (ret)
			goto out;

	}

	if (crtc_req->count_connectors == 0 && mode) {
		DRM_DEBUG_KMS("Count connectors is 0 but mode set\n");
		ret = -EINVAL;
		goto out;
	}

	if (crtc_req->count_connectors > 0 && (!mode || !fb)) {
		DRM_DEBUG_KMS("Count connectors is %d but no mode or fb set\n",
			  crtc_req->count_connectors);
		ret = -EINVAL;
		goto out;
	}

	if (crtc_req->count_connectors > 0) {
		u32 out_id;

		/* Avoid unbounded kernel memory allocation */
		if (crtc_req->count_connectors > config->num_connector) {
			ret = -EINVAL;
			goto out;
		}

		connector_set = kmalloc_array(crtc_req->count_connectors,
					      sizeof(struct drm_connector *),
					      GFP_KERNEL);
		if (!connector_set) {
			ret = -ENOMEM;
			goto out;
		}

		for (i = 0; i < crtc_req->count_connectors; i++) {
			connector_set[i] = NULL;
			set_connectors_ptr = (uint32_t __user *)(unsigned long)crtc_req->set_connectors_ptr;
			if (get_user(out_id, &set_connectors_ptr[i])) {
				ret = -EFAULT;
				goto out;
			}

			connector = drm_connector_lookup(dev, file_priv, out_id);
			if (!connector) {
				DRM_DEBUG_KMS("Connector id %d unknown\n",
						out_id);
				ret = -ENOENT;
				goto out;
			}
			DRM_DEBUG_KMS("[CONNECTOR:%d:%s]\n",
					connector->base.id,
					connector->name);

			connector_set[i] = connector;
		}
	}

	set.crtc = crtc;
	set.x = crtc_req->x;
	set.y = crtc_req->y;
	set.mode = mode;
	set.connectors = connector_set;
	set.num_connectors = crtc_req->count_connectors;
	set.fb = fb;

	if (drm_drv_uses_atomic_modeset(dev))
		ret = crtc->funcs->set_config(&set, &ctx);
	else
		ret = __drm_mode_set_config_internal(&set, &ctx);

out:
	if (fb)
		drm_framebuffer_put(fb);

	if (connector_set) {
		for (i = 0; i < crtc_req->count_connectors; i++) {
			if (connector_set[i])
				drm_connector_put(connector_set[i]);
		}
	}
	kfree(connector_set);
	drm_mode_destroy(dev, mode);

	/* In case we need to retry... */
	connector_set = NULL;
	fb = NULL;
	mode = NULL;

	DRM_MODESET_LOCK_ALL_END(dev, ctx, ret);

	return ret;
}

int drm_mode_crtc_set_obj_prop(struct drm_mode_object *obj,
			       struct drm_property *property,
			       uint64_t value)
{
	int ret = -EINVAL;
	struct drm_crtc *crtc = obj_to_crtc(obj);

	if (crtc->funcs->set_property)
		ret = crtc->funcs->set_property(crtc, property, value);
	if (!ret)
		drm_object_property_set_value(obj, property, value);

	return ret;
}
