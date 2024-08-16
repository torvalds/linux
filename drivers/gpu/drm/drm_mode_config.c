/*
 * Copyright (c) 2016 Intel Corporation
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
 */

#include <linux/uaccess.h>

#include <drm/drm_drv.h>
#include <drm/drm_encoder.h>
#include <drm/drm_file.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_managed.h>
#include <drm/drm_mode_config.h>
#include <drm/drm_print.h>
#include <linux/dma-resv.h>

#include "drm_crtc_internal.h"
#include "drm_internal.h"

int drm_modeset_register_all(struct drm_device *dev)
{
	int ret;

	ret = drm_plane_register_all(dev);
	if (ret)
		goto err_plane;

	ret = drm_crtc_register_all(dev);
	if  (ret)
		goto err_crtc;

	ret = drm_encoder_register_all(dev);
	if (ret)
		goto err_encoder;

	ret = drm_connector_register_all(dev);
	if (ret)
		goto err_connector;

	return 0;

err_connector:
	drm_encoder_unregister_all(dev);
err_encoder:
	drm_crtc_unregister_all(dev);
err_crtc:
	drm_plane_unregister_all(dev);
err_plane:
	return ret;
}

void drm_modeset_unregister_all(struct drm_device *dev)
{
	drm_connector_unregister_all(dev);
	drm_encoder_unregister_all(dev);
	drm_crtc_unregister_all(dev);
	drm_plane_unregister_all(dev);
}

/**
 * drm_mode_getresources - get graphics configuration
 * @dev: drm device for the ioctl
 * @data: data pointer for the ioctl
 * @file_priv: drm file for the ioctl call
 *
 * Construct a set of configuration description structures and return
 * them to the user, including CRTC, connector and framebuffer configuration.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_getresources(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	struct drm_mode_card_res *card_res = data;
	struct drm_framebuffer *fb;
	struct drm_connector *connector;
	struct drm_crtc *crtc;
	struct drm_encoder *encoder;
	int count, ret = 0;
	uint32_t __user *fb_id;
	uint32_t __user *crtc_id;
	uint32_t __user *connector_id;
	uint32_t __user *encoder_id;
	struct drm_connector_list_iter conn_iter;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;

	mutex_lock(&file_priv->fbs_lock);
	count = 0;
	fb_id = u64_to_user_ptr(card_res->fb_id_ptr);
	list_for_each_entry(fb, &file_priv->fbs, filp_head) {
		if (count < card_res->count_fbs &&
		    put_user(fb->base.id, fb_id + count)) {
			mutex_unlock(&file_priv->fbs_lock);
			return -EFAULT;
		}
		count++;
	}
	card_res->count_fbs = count;
	mutex_unlock(&file_priv->fbs_lock);

	card_res->max_height = dev->mode_config.max_height;
	card_res->min_height = dev->mode_config.min_height;
	card_res->max_width = dev->mode_config.max_width;
	card_res->min_width = dev->mode_config.min_width;

	count = 0;
	crtc_id = u64_to_user_ptr(card_res->crtc_id_ptr);
	drm_for_each_crtc(crtc, dev) {
		if (drm_lease_held(file_priv, crtc->base.id)) {
			if (count < card_res->count_crtcs &&
			    put_user(crtc->base.id, crtc_id + count))
				return -EFAULT;
			count++;
		}
	}
	card_res->count_crtcs = count;

	count = 0;
	encoder_id = u64_to_user_ptr(card_res->encoder_id_ptr);
	drm_for_each_encoder(encoder, dev) {
		if (count < card_res->count_encoders &&
		    put_user(encoder->base.id, encoder_id + count))
			return -EFAULT;
		count++;
	}
	card_res->count_encoders = count;

	drm_connector_list_iter_begin(dev, &conn_iter);
	count = 0;
	connector_id = u64_to_user_ptr(card_res->connector_id_ptr);
	drm_for_each_connector_iter(connector, &conn_iter) {
		/* only expose writeback connectors if userspace understands them */
		if (!file_priv->writeback_connectors &&
		    (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK))
			continue;

		if (drm_lease_held(file_priv, connector->base.id)) {
			if (count < card_res->count_connectors &&
			    put_user(connector->base.id, connector_id + count)) {
				drm_connector_list_iter_end(&conn_iter);
				return -EFAULT;
			}
			count++;
		}
	}
	card_res->count_connectors = count;
	drm_connector_list_iter_end(&conn_iter);

	return ret;
}

/**
 * drm_mode_config_reset - call ->reset callbacks
 * @dev: drm device
 *
 * This functions calls all the crtc's, encoder's and connector's ->reset
 * callback. Drivers can use this in e.g. their driver load or resume code to
 * reset hardware and software state.
 */
void drm_mode_config_reset(struct drm_device *dev)
{
	struct drm_crtc *crtc;
	struct drm_plane *plane;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;

	drm_for_each_plane(plane, dev)
		if (plane->funcs->reset)
			plane->funcs->reset(plane);

	drm_for_each_crtc(crtc, dev)
		if (crtc->funcs->reset)
			crtc->funcs->reset(crtc);

	drm_for_each_encoder(encoder, dev)
		if (encoder->funcs && encoder->funcs->reset)
			encoder->funcs->reset(encoder);

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter)
		if (connector->funcs->reset)
			connector->funcs->reset(connector);
	drm_connector_list_iter_end(&conn_iter);
}
EXPORT_SYMBOL(drm_mode_config_reset);

/*
 * Global properties
 */
static const struct drm_prop_enum_list drm_plane_type_enum_list[] = {
	{ DRM_PLANE_TYPE_OVERLAY, "Overlay" },
	{ DRM_PLANE_TYPE_PRIMARY, "Primary" },
	{ DRM_PLANE_TYPE_CURSOR, "Cursor" },
};

static int drm_mode_create_standard_properties(struct drm_device *dev)
{
	struct drm_property *prop;
	int ret;

	ret = drm_connector_create_standard_properties(dev);
	if (ret)
		return ret;

	prop = drm_property_create_enum(dev, DRM_MODE_PROP_IMMUTABLE,
					"type", drm_plane_type_enum_list,
					ARRAY_SIZE(drm_plane_type_enum_list));
	if (!prop)
		return -ENOMEM;
	dev->mode_config.plane_type_property = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
			"SRC_X", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_src_x = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
			"SRC_Y", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_src_y = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
			"SRC_W", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_src_w = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
			"SRC_H", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_src_h = prop;

	prop = drm_property_create_signed_range(dev, DRM_MODE_PROP_ATOMIC,
			"CRTC_X", INT_MIN, INT_MAX);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_crtc_x = prop;

	prop = drm_property_create_signed_range(dev, DRM_MODE_PROP_ATOMIC,
			"CRTC_Y", INT_MIN, INT_MAX);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_crtc_y = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
			"CRTC_W", 0, INT_MAX);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_crtc_w = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
			"CRTC_H", 0, INT_MAX);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_crtc_h = prop;

	prop = drm_property_create_object(dev, DRM_MODE_PROP_ATOMIC,
			"FB_ID", DRM_MODE_OBJECT_FB);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_fb_id = prop;

	prop = drm_property_create_signed_range(dev, DRM_MODE_PROP_ATOMIC,
			"IN_FENCE_FD", -1, INT_MAX);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_in_fence_fd = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
			"OUT_FENCE_PTR", 0, U64_MAX);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_out_fence_ptr = prop;

	prop = drm_property_create_object(dev, DRM_MODE_PROP_ATOMIC,
			"CRTC_ID", DRM_MODE_OBJECT_CRTC);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_crtc_id = prop;

	prop = drm_property_create(dev,
			DRM_MODE_PROP_ATOMIC | DRM_MODE_PROP_BLOB,
			"FB_DAMAGE_CLIPS", 0);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_fb_damage_clips = prop;

	prop = drm_property_create_bool(dev, DRM_MODE_PROP_ATOMIC,
			"ACTIVE");
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_active = prop;

	prop = drm_property_create(dev,
			DRM_MODE_PROP_ATOMIC | DRM_MODE_PROP_BLOB,
			"MODE_ID", 0);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_mode_id = prop;

	prop = drm_property_create_bool(dev, 0,
			"VRR_ENABLED");
	if (!prop)
		return -ENOMEM;
	dev->mode_config.prop_vrr_enabled = prop;

	prop = drm_property_create(dev,
			DRM_MODE_PROP_BLOB,
			"DEGAMMA_LUT", 0);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.degamma_lut_property = prop;

	prop = drm_property_create_range(dev,
			DRM_MODE_PROP_IMMUTABLE,
			"DEGAMMA_LUT_SIZE", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.degamma_lut_size_property = prop;

	prop = drm_property_create(dev,
			DRM_MODE_PROP_BLOB,
			"CTM", 0);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.ctm_property = prop;

	prop = drm_property_create(dev,
			DRM_MODE_PROP_BLOB,
			"GAMMA_LUT", 0);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.gamma_lut_property = prop;

	prop = drm_property_create_range(dev,
			DRM_MODE_PROP_IMMUTABLE,
			"GAMMA_LUT_SIZE", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.gamma_lut_size_property = prop;

	prop = drm_property_create(dev,
				   DRM_MODE_PROP_IMMUTABLE | DRM_MODE_PROP_BLOB,
				   "IN_FORMATS", 0);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.modifiers_property = prop;

	prop = drm_property_create(dev,
				   DRM_MODE_PROP_IMMUTABLE | DRM_MODE_PROP_BLOB,
				   "SIZE_HINTS", 0);
	if (!prop)
		return -ENOMEM;
	dev->mode_config.size_hints_property = prop;

	return 0;
}

static void drm_mode_config_init_release(struct drm_device *dev, void *ptr)
{
	drm_mode_config_cleanup(dev);
}

/**
 * drmm_mode_config_init - managed DRM mode_configuration structure
 * 	initialization
 * @dev: DRM device
 *
 * Initialize @dev's mode_config structure, used for tracking the graphics
 * configuration of @dev.
 *
 * Since this initializes the modeset locks, no locking is possible. Which is no
 * problem, since this should happen single threaded at init time. It is the
 * driver's problem to ensure this guarantee.
 *
 * Cleanup is automatically handled through registering drm_mode_config_cleanup
 * with drmm_add_action().
 *
 * Returns: 0 on success, negative error value on failure.
 */
int drmm_mode_config_init(struct drm_device *dev)
{
	int ret;

	mutex_init(&dev->mode_config.mutex);
	drm_modeset_lock_init(&dev->mode_config.connection_mutex);
	mutex_init(&dev->mode_config.idr_mutex);
	mutex_init(&dev->mode_config.fb_lock);
	mutex_init(&dev->mode_config.blob_lock);
	INIT_LIST_HEAD(&dev->mode_config.fb_list);
	INIT_LIST_HEAD(&dev->mode_config.crtc_list);
	INIT_LIST_HEAD(&dev->mode_config.connector_list);
	INIT_LIST_HEAD(&dev->mode_config.encoder_list);
	INIT_LIST_HEAD(&dev->mode_config.property_list);
	INIT_LIST_HEAD(&dev->mode_config.property_blob_list);
	INIT_LIST_HEAD(&dev->mode_config.plane_list);
	INIT_LIST_HEAD(&dev->mode_config.privobj_list);
	idr_init_base(&dev->mode_config.object_idr, 1);
	idr_init_base(&dev->mode_config.tile_idr, 1);
	ida_init(&dev->mode_config.connector_ida);
	spin_lock_init(&dev->mode_config.connector_list_lock);

	init_llist_head(&dev->mode_config.connector_free_list);
	INIT_WORK(&dev->mode_config.connector_free_work, drm_connector_free_work_fn);

	ret = drm_mode_create_standard_properties(dev);
	if (ret) {
		drm_mode_config_cleanup(dev);
		return ret;
	}

	/* Just to be sure */
	dev->mode_config.num_fb = 0;
	dev->mode_config.num_connector = 0;
	dev->mode_config.num_crtc = 0;
	dev->mode_config.num_encoder = 0;
	dev->mode_config.num_total_plane = 0;

	if (IS_ENABLED(CONFIG_LOCKDEP)) {
		struct drm_modeset_acquire_ctx modeset_ctx;
		struct ww_acquire_ctx resv_ctx;
		struct dma_resv resv;
		int ret;

		dma_resv_init(&resv);

		drm_modeset_acquire_init(&modeset_ctx, 0);
		ret = drm_modeset_lock(&dev->mode_config.connection_mutex,
				       &modeset_ctx);
		if (ret == -EDEADLK)
			ret = drm_modeset_backoff(&modeset_ctx);

		might_fault();

		ww_acquire_init(&resv_ctx, &reservation_ww_class);
		ret = dma_resv_lock(&resv, &resv_ctx);
		if (ret == -EDEADLK)
			dma_resv_lock_slow(&resv, &resv_ctx);

		dma_resv_unlock(&resv);
		ww_acquire_fini(&resv_ctx);

		drm_modeset_drop_locks(&modeset_ctx);
		drm_modeset_acquire_fini(&modeset_ctx);
		dma_resv_fini(&resv);
	}

	return drmm_add_action_or_reset(dev, drm_mode_config_init_release,
					NULL);
}
EXPORT_SYMBOL(drmm_mode_config_init);

/**
 * drm_mode_config_cleanup - free up DRM mode_config info
 * @dev: DRM device
 *
 * Free up all the connectors and CRTCs associated with this DRM device, then
 * free up the framebuffers and associated buffer objects.
 *
 * Note that since this /should/ happen single-threaded at driver/device
 * teardown time, no locking is required. It's the driver's job to ensure that
 * this guarantee actually holds true.
 *
 * FIXME: With the managed drmm_mode_config_init() it is no longer necessary for
 * drivers to explicitly call this function.
 */
void drm_mode_config_cleanup(struct drm_device *dev)
{
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct drm_crtc *crtc, *ct;
	struct drm_encoder *encoder, *enct;
	struct drm_framebuffer *fb, *fbt;
	struct drm_property *property, *pt;
	struct drm_property_blob *blob, *bt;
	struct drm_plane *plane, *plt;

	list_for_each_entry_safe(encoder, enct, &dev->mode_config.encoder_list,
				 head) {
		encoder->funcs->destroy(encoder);
	}

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		/* drm_connector_list_iter holds an full reference to the
		 * current connector itself, which means it is inherently safe
		 * against unreferencing the current connector - but not against
		 * deleting it right away. */
		drm_connector_put(connector);
	}
	drm_connector_list_iter_end(&conn_iter);
	/* connector_iter drops references in a work item. */
	flush_work(&dev->mode_config.connector_free_work);
	if (WARN_ON(!list_empty(&dev->mode_config.connector_list))) {
		drm_connector_list_iter_begin(dev, &conn_iter);
		drm_for_each_connector_iter(connector, &conn_iter)
			DRM_ERROR("connector %s leaked!\n", connector->name);
		drm_connector_list_iter_end(&conn_iter);
	}

	list_for_each_entry_safe(property, pt, &dev->mode_config.property_list,
				 head) {
		drm_property_destroy(dev, property);
	}

	list_for_each_entry_safe(plane, plt, &dev->mode_config.plane_list,
				 head) {
		plane->funcs->destroy(plane);
	}

	list_for_each_entry_safe(crtc, ct, &dev->mode_config.crtc_list, head) {
		crtc->funcs->destroy(crtc);
	}

	list_for_each_entry_safe(blob, bt, &dev->mode_config.property_blob_list,
				 head_global) {
		drm_property_blob_put(blob);
	}

	/*
	 * Single-threaded teardown context, so it's not required to grab the
	 * fb_lock to protect against concurrent fb_list access. Contrary, it
	 * would actually deadlock with the drm_framebuffer_cleanup function.
	 *
	 * Also, if there are any framebuffers left, that's a driver leak now,
	 * so politely WARN about this.
	 */
	WARN_ON(!list_empty(&dev->mode_config.fb_list));
	list_for_each_entry_safe(fb, fbt, &dev->mode_config.fb_list, head) {
		struct drm_printer p = drm_dbg_printer(dev, DRM_UT_KMS, "[leaked fb]");

		drm_printf(&p, "framebuffer[%u]:\n", fb->base.id);
		drm_framebuffer_print_info(&p, 1, fb);
		drm_framebuffer_free(&fb->base.refcount);
	}

	ida_destroy(&dev->mode_config.connector_ida);
	idr_destroy(&dev->mode_config.tile_idr);
	idr_destroy(&dev->mode_config.object_idr);
	drm_modeset_lock_fini(&dev->mode_config.connection_mutex);
}
EXPORT_SYMBOL(drm_mode_config_cleanup);

static u32 full_encoder_mask(struct drm_device *dev)
{
	struct drm_encoder *encoder;
	u32 encoder_mask = 0;

	drm_for_each_encoder(encoder, dev)
		encoder_mask |= drm_encoder_mask(encoder);

	return encoder_mask;
}

/*
 * For some reason we want the encoder itself included in
 * possible_clones. Make life easy for drivers by allowing them
 * to leave possible_clones unset if no cloning is possible.
 */
static void fixup_encoder_possible_clones(struct drm_encoder *encoder)
{
	if (encoder->possible_clones == 0)
		encoder->possible_clones = drm_encoder_mask(encoder);
}

static void validate_encoder_possible_clones(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	u32 encoder_mask = full_encoder_mask(dev);
	struct drm_encoder *other;

	drm_for_each_encoder(other, dev) {
		WARN(!!(encoder->possible_clones & drm_encoder_mask(other)) !=
		     !!(other->possible_clones & drm_encoder_mask(encoder)),
		     "possible_clones mismatch: "
		     "[ENCODER:%d:%s] mask=0x%x possible_clones=0x%x vs. "
		     "[ENCODER:%d:%s] mask=0x%x possible_clones=0x%x\n",
		     encoder->base.id, encoder->name,
		     drm_encoder_mask(encoder), encoder->possible_clones,
		     other->base.id, other->name,
		     drm_encoder_mask(other), other->possible_clones);
	}

	WARN((encoder->possible_clones & drm_encoder_mask(encoder)) == 0 ||
	     (encoder->possible_clones & ~encoder_mask) != 0,
	     "Bogus possible_clones: "
	     "[ENCODER:%d:%s] possible_clones=0x%x (full encoder mask=0x%x)\n",
	     encoder->base.id, encoder->name,
	     encoder->possible_clones, encoder_mask);
}

static u32 full_crtc_mask(struct drm_device *dev)
{
	struct drm_crtc *crtc;
	u32 crtc_mask = 0;

	drm_for_each_crtc(crtc, dev)
		crtc_mask |= drm_crtc_mask(crtc);

	return crtc_mask;
}

static void validate_encoder_possible_crtcs(struct drm_encoder *encoder)
{
	u32 crtc_mask = full_crtc_mask(encoder->dev);

	WARN((encoder->possible_crtcs & crtc_mask) == 0 ||
	     (encoder->possible_crtcs & ~crtc_mask) != 0,
	     "Bogus possible_crtcs: "
	     "[ENCODER:%d:%s] possible_crtcs=0x%x (full crtc mask=0x%x)\n",
	     encoder->base.id, encoder->name,
	     encoder->possible_crtcs, crtc_mask);
}

void drm_mode_config_validate(struct drm_device *dev)
{
	struct drm_encoder *encoder;
	struct drm_crtc *crtc;
	struct drm_plane *plane;
	u32 primary_with_crtc = 0, cursor_with_crtc = 0;
	unsigned int num_primary = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return;

	drm_for_each_encoder(encoder, dev)
		fixup_encoder_possible_clones(encoder);

	drm_for_each_encoder(encoder, dev) {
		validate_encoder_possible_clones(encoder);
		validate_encoder_possible_crtcs(encoder);
	}

	drm_for_each_crtc(crtc, dev) {
		WARN(!crtc->primary, "Missing primary plane on [CRTC:%d:%s]\n",
		     crtc->base.id, crtc->name);

		WARN(crtc->cursor && crtc->funcs->cursor_set,
		     "[CRTC:%d:%s] must not have both a cursor plane and a cursor_set func",
		     crtc->base.id, crtc->name);
		WARN(crtc->cursor && crtc->funcs->cursor_set2,
		     "[CRTC:%d:%s] must not have both a cursor plane and a cursor_set2 func",
		     crtc->base.id, crtc->name);
		WARN(crtc->cursor && crtc->funcs->cursor_move,
		     "[CRTC:%d:%s] must not have both a cursor plane and a cursor_move func",
		     crtc->base.id, crtc->name);

		if (crtc->primary) {
			WARN(!(crtc->primary->possible_crtcs & drm_crtc_mask(crtc)),
			     "Bogus primary plane possible_crtcs: [PLANE:%d:%s] must be compatible with [CRTC:%d:%s]\n",
			     crtc->primary->base.id, crtc->primary->name,
			     crtc->base.id, crtc->name);
			WARN(primary_with_crtc & drm_plane_mask(crtc->primary),
			     "Primary plane [PLANE:%d:%s] used for multiple CRTCs",
			     crtc->primary->base.id, crtc->primary->name);
			primary_with_crtc |= drm_plane_mask(crtc->primary);
		}
		if (crtc->cursor) {
			WARN(!(crtc->cursor->possible_crtcs & drm_crtc_mask(crtc)),
			     "Bogus cursor plane possible_crtcs: [PLANE:%d:%s] must be compatible with [CRTC:%d:%s]\n",
			     crtc->cursor->base.id, crtc->cursor->name,
			     crtc->base.id, crtc->name);
			WARN(cursor_with_crtc & drm_plane_mask(crtc->cursor),
			     "Cursor plane [PLANE:%d:%s] used for multiple CRTCs",
			     crtc->cursor->base.id, crtc->cursor->name);
			cursor_with_crtc |= drm_plane_mask(crtc->cursor);
		}
	}

	drm_for_each_plane(plane, dev) {
		if (plane->type == DRM_PLANE_TYPE_PRIMARY)
			num_primary++;
	}

	WARN(num_primary != dev->mode_config.num_crtc,
	     "Must have as many primary planes as there are CRTCs, but have %u primary planes and %u CRTCs",
	     num_primary, dev->mode_config.num_crtc);
}
