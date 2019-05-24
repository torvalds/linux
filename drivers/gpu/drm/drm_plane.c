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

#include <linux/slab.h>
#include <linux/uaccess.h>

#include <drm/drm_plane.h>
#include <drm/drm_drv.h>
#include <drm/drm_print.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_file.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_vblank.h>

#include "drm_crtc_internal.h"

/**
 * DOC: overview
 *
 * A plane represents an image source that can be blended with or overlayed on
 * top of a CRTC during the scanout process. Planes take their input data from a
 * &drm_framebuffer object. The plane itself specifies the cropping and scaling
 * of that image, and where it is placed on the visible are of a display
 * pipeline, represented by &drm_crtc. A plane can also have additional
 * properties that specify how the pixels are positioned and blended, like
 * rotation or Z-position. All these properties are stored in &drm_plane_state.
 *
 * To create a plane, a KMS drivers allocates and zeroes an instances of
 * &struct drm_plane (possibly as part of a larger structure) and registers it
 * with a call to drm_universal_plane_init().
 *
 * Cursor and overlay planes are optional. All drivers should provide one
 * primary plane per CRTC to avoid surprising userspace too much. See enum
 * drm_plane_type for a more in-depth discussion of these special uapi-relevant
 * plane types. Special planes are associated with their CRTC by calling
 * drm_crtc_init_with_planes().
 *
 * The type of a plane is exposed in the immutable "type" enumeration property,
 * which has one of the following values: "Overlay", "Primary", "Cursor".
 */

static unsigned int drm_num_planes(struct drm_device *dev)
{
	unsigned int num = 0;
	struct drm_plane *tmp;

	drm_for_each_plane(tmp, dev) {
		num++;
	}

	return num;
}

static inline u32 *
formats_ptr(struct drm_format_modifier_blob *blob)
{
	return (u32 *)(((char *)blob) + blob->formats_offset);
}

static inline struct drm_format_modifier *
modifiers_ptr(struct drm_format_modifier_blob *blob)
{
	return (struct drm_format_modifier *)(((char *)blob) + blob->modifiers_offset);
}

static int create_in_format_blob(struct drm_device *dev, struct drm_plane *plane)
{
	const struct drm_mode_config *config = &dev->mode_config;
	struct drm_property_blob *blob;
	struct drm_format_modifier *mod;
	size_t blob_size, formats_size, modifiers_size;
	struct drm_format_modifier_blob *blob_data;
	unsigned int i, j;

	formats_size = sizeof(__u32) * plane->format_count;
	if (WARN_ON(!formats_size)) {
		/* 0 formats are never expected */
		return 0;
	}

	modifiers_size =
		sizeof(struct drm_format_modifier) * plane->modifier_count;

	blob_size = sizeof(struct drm_format_modifier_blob);
	/* Modifiers offset is a pointer to a struct with a 64 bit field so it
	 * should be naturally aligned to 8B.
	 */
	BUILD_BUG_ON(sizeof(struct drm_format_modifier_blob) % 8);
	blob_size += ALIGN(formats_size, 8);
	blob_size += modifiers_size;

	blob = drm_property_create_blob(dev, blob_size, NULL);
	if (IS_ERR(blob))
		return -1;

	blob_data = blob->data;
	blob_data->version = FORMAT_BLOB_CURRENT;
	blob_data->count_formats = plane->format_count;
	blob_data->formats_offset = sizeof(struct drm_format_modifier_blob);
	blob_data->count_modifiers = plane->modifier_count;

	blob_data->modifiers_offset =
		ALIGN(blob_data->formats_offset + formats_size, 8);

	memcpy(formats_ptr(blob_data), plane->format_types, formats_size);

	/* If we can't determine support, just bail */
	if (!plane->funcs->format_mod_supported)
		goto done;

	mod = modifiers_ptr(blob_data);
	for (i = 0; i < plane->modifier_count; i++) {
		for (j = 0; j < plane->format_count; j++) {
			if (plane->funcs->format_mod_supported(plane,
							       plane->format_types[j],
							       plane->modifiers[i])) {

				mod->formats |= 1ULL << j;
			}
		}

		mod->modifier = plane->modifiers[i];
		mod->offset = 0;
		mod->pad = 0;
		mod++;
	}

done:
	drm_object_attach_property(&plane->base, config->modifiers_property,
				   blob->base.id);

	return 0;
}

/**
 * drm_universal_plane_init - Initialize a new universal plane object
 * @dev: DRM device
 * @plane: plane object to init
 * @possible_crtcs: bitmask of possible CRTCs
 * @funcs: callbacks for the new plane
 * @formats: array of supported formats (DRM_FORMAT\_\*)
 * @format_count: number of elements in @formats
 * @format_modifiers: array of struct drm_format modifiers terminated by
 *                    DRM_FORMAT_MOD_INVALID
 * @type: type of plane (overlay, primary, cursor)
 * @name: printf style format string for the plane name, or NULL for default name
 *
 * Initializes a plane object of type @type.
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_universal_plane_init(struct drm_device *dev, struct drm_plane *plane,
			     uint32_t possible_crtcs,
			     const struct drm_plane_funcs *funcs,
			     const uint32_t *formats, unsigned int format_count,
			     const uint64_t *format_modifiers,
			     enum drm_plane_type type,
			     const char *name, ...)
{
	struct drm_mode_config *config = &dev->mode_config;
	unsigned int format_modifier_count = 0;
	int ret;

	/* plane index is used with 32bit bitmasks */
	if (WARN_ON(config->num_total_plane >= 32))
		return -EINVAL;

	WARN_ON(drm_drv_uses_atomic_modeset(dev) &&
		(!funcs->atomic_destroy_state ||
		 !funcs->atomic_duplicate_state));

	ret = drm_mode_object_add(dev, &plane->base, DRM_MODE_OBJECT_PLANE);
	if (ret)
		return ret;

	drm_modeset_lock_init(&plane->mutex);

	plane->base.properties = &plane->properties;
	plane->dev = dev;
	plane->funcs = funcs;
	plane->format_types = kmalloc_array(format_count, sizeof(uint32_t),
					    GFP_KERNEL);
	if (!plane->format_types) {
		DRM_DEBUG_KMS("out of memory when allocating plane\n");
		drm_mode_object_unregister(dev, &plane->base);
		return -ENOMEM;
	}

	/*
	 * First driver to need more than 64 formats needs to fix this. Each
	 * format is encoded as a bit and the current code only supports a u64.
	 */
	if (WARN_ON(format_count > 64))
		return -EINVAL;

	if (format_modifiers) {
		const uint64_t *temp_modifiers = format_modifiers;
		while (*temp_modifiers++ != DRM_FORMAT_MOD_INVALID)
			format_modifier_count++;
	}

	if (format_modifier_count)
		config->allow_fb_modifiers = true;

	plane->modifier_count = format_modifier_count;
	plane->modifiers = kmalloc_array(format_modifier_count,
					 sizeof(format_modifiers[0]),
					 GFP_KERNEL);

	if (format_modifier_count && !plane->modifiers) {
		DRM_DEBUG_KMS("out of memory when allocating plane\n");
		kfree(plane->format_types);
		drm_mode_object_unregister(dev, &plane->base);
		return -ENOMEM;
	}

	if (name) {
		va_list ap;

		va_start(ap, name);
		plane->name = kvasprintf(GFP_KERNEL, name, ap);
		va_end(ap);
	} else {
		plane->name = kasprintf(GFP_KERNEL, "plane-%d",
					drm_num_planes(dev));
	}
	if (!plane->name) {
		kfree(plane->format_types);
		kfree(plane->modifiers);
		drm_mode_object_unregister(dev, &plane->base);
		return -ENOMEM;
	}

	memcpy(plane->format_types, formats, format_count * sizeof(uint32_t));
	plane->format_count = format_count;
	memcpy(plane->modifiers, format_modifiers,
	       format_modifier_count * sizeof(format_modifiers[0]));
	plane->possible_crtcs = possible_crtcs;
	plane->type = type;

	list_add_tail(&plane->head, &config->plane_list);
	plane->index = config->num_total_plane++;

	drm_object_attach_property(&plane->base,
				   config->plane_type_property,
				   plane->type);

	if (drm_core_check_feature(dev, DRIVER_ATOMIC)) {
		drm_object_attach_property(&plane->base, config->prop_fb_id, 0);
		drm_object_attach_property(&plane->base, config->prop_in_fence_fd, -1);
		drm_object_attach_property(&plane->base, config->prop_crtc_id, 0);
		drm_object_attach_property(&plane->base, config->prop_crtc_x, 0);
		drm_object_attach_property(&plane->base, config->prop_crtc_y, 0);
		drm_object_attach_property(&plane->base, config->prop_crtc_w, 0);
		drm_object_attach_property(&plane->base, config->prop_crtc_h, 0);
		drm_object_attach_property(&plane->base, config->prop_src_x, 0);
		drm_object_attach_property(&plane->base, config->prop_src_y, 0);
		drm_object_attach_property(&plane->base, config->prop_src_w, 0);
		drm_object_attach_property(&plane->base, config->prop_src_h, 0);
	}

	if (config->allow_fb_modifiers)
		create_in_format_blob(dev, plane);

	return 0;
}
EXPORT_SYMBOL(drm_universal_plane_init);

int drm_plane_register_all(struct drm_device *dev)
{
	struct drm_plane *plane;
	int ret = 0;

	drm_for_each_plane(plane, dev) {
		if (plane->funcs->late_register)
			ret = plane->funcs->late_register(plane);
		if (ret)
			return ret;
	}

	return 0;
}

void drm_plane_unregister_all(struct drm_device *dev)
{
	struct drm_plane *plane;

	drm_for_each_plane(plane, dev) {
		if (plane->funcs->early_unregister)
			plane->funcs->early_unregister(plane);
	}
}

/**
 * drm_plane_init - Initialize a legacy plane
 * @dev: DRM device
 * @plane: plane object to init
 * @possible_crtcs: bitmask of possible CRTCs
 * @funcs: callbacks for the new plane
 * @formats: array of supported formats (DRM_FORMAT\_\*)
 * @format_count: number of elements in @formats
 * @is_primary: plane type (primary vs overlay)
 *
 * Legacy API to initialize a DRM plane.
 *
 * New drivers should call drm_universal_plane_init() instead.
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_plane_init(struct drm_device *dev, struct drm_plane *plane,
		   uint32_t possible_crtcs,
		   const struct drm_plane_funcs *funcs,
		   const uint32_t *formats, unsigned int format_count,
		   bool is_primary)
{
	enum drm_plane_type type;

	type = is_primary ? DRM_PLANE_TYPE_PRIMARY : DRM_PLANE_TYPE_OVERLAY;
	return drm_universal_plane_init(dev, plane, possible_crtcs, funcs,
					formats, format_count,
					NULL, type, NULL);
}
EXPORT_SYMBOL(drm_plane_init);

/**
 * drm_plane_cleanup - Clean up the core plane usage
 * @plane: plane to cleanup
 *
 * This function cleans up @plane and removes it from the DRM mode setting
 * core. Note that the function does *not* free the plane structure itself,
 * this is the responsibility of the caller.
 */
void drm_plane_cleanup(struct drm_plane *plane)
{
	struct drm_device *dev = plane->dev;

	drm_modeset_lock_fini(&plane->mutex);

	kfree(plane->format_types);
	kfree(plane->modifiers);
	drm_mode_object_unregister(dev, &plane->base);

	BUG_ON(list_empty(&plane->head));

	/* Note that the plane_list is considered to be static; should we
	 * remove the drm_plane at runtime we would have to decrement all
	 * the indices on the drm_plane after us in the plane_list.
	 */

	list_del(&plane->head);
	dev->mode_config.num_total_plane--;

	WARN_ON(plane->state && !plane->funcs->atomic_destroy_state);
	if (plane->state && plane->funcs->atomic_destroy_state)
		plane->funcs->atomic_destroy_state(plane, plane->state);

	kfree(plane->name);

	memset(plane, 0, sizeof(*plane));
}
EXPORT_SYMBOL(drm_plane_cleanup);

/**
 * drm_plane_from_index - find the registered plane at an index
 * @dev: DRM device
 * @idx: index of registered plane to find for
 *
 * Given a plane index, return the registered plane from DRM device's
 * list of planes with matching index. This is the inverse of drm_plane_index().
 */
struct drm_plane *
drm_plane_from_index(struct drm_device *dev, int idx)
{
	struct drm_plane *plane;

	drm_for_each_plane(plane, dev)
		if (idx == plane->index)
			return plane;

	return NULL;
}
EXPORT_SYMBOL(drm_plane_from_index);

/**
 * drm_plane_force_disable - Forcibly disable a plane
 * @plane: plane to disable
 *
 * Forces the plane to be disabled.
 *
 * Used when the plane's current framebuffer is destroyed,
 * and when restoring fbdev mode.
 *
 * Note that this function is not suitable for atomic drivers, since it doesn't
 * wire through the lock acquisition context properly and hence can't handle
 * retries or driver private locks. You probably want to use
 * drm_atomic_helper_disable_plane() or
 * drm_atomic_helper_disable_planes_on_crtc() instead.
 */
void drm_plane_force_disable(struct drm_plane *plane)
{
	int ret;

	if (!plane->fb)
		return;

	WARN_ON(drm_drv_uses_atomic_modeset(plane->dev));

	plane->old_fb = plane->fb;
	ret = plane->funcs->disable_plane(plane, NULL);
	if (ret) {
		DRM_ERROR("failed to disable plane with busy fb\n");
		plane->old_fb = NULL;
		return;
	}
	/* disconnect the plane from the fb and crtc: */
	drm_framebuffer_put(plane->old_fb);
	plane->old_fb = NULL;
	plane->fb = NULL;
	plane->crtc = NULL;
}
EXPORT_SYMBOL(drm_plane_force_disable);

/**
 * drm_mode_plane_set_obj_prop - set the value of a property
 * @plane: drm plane object to set property value for
 * @property: property to set
 * @value: value the property should be set to
 *
 * This functions sets a given property on a given plane object. This function
 * calls the driver's ->set_property callback and changes the software state of
 * the property if the callback succeeds.
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_mode_plane_set_obj_prop(struct drm_plane *plane,
				struct drm_property *property,
				uint64_t value)
{
	int ret = -EINVAL;
	struct drm_mode_object *obj = &plane->base;

	if (plane->funcs->set_property)
		ret = plane->funcs->set_property(plane, property, value);
	if (!ret)
		drm_object_property_set_value(obj, property, value);

	return ret;
}
EXPORT_SYMBOL(drm_mode_plane_set_obj_prop);

int drm_mode_getplane_res(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	struct drm_mode_get_plane_res *plane_resp = data;
	struct drm_plane *plane;
	uint32_t __user *plane_ptr;
	int count = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;

	plane_ptr = u64_to_user_ptr(plane_resp->plane_id_ptr);

	/*
	 * This ioctl is called twice, once to determine how much space is
	 * needed, and the 2nd time to fill it.
	 */
	drm_for_each_plane(plane, dev) {
		/*
		 * Unless userspace set the 'universal planes'
		 * capability bit, only advertise overlays.
		 */
		if (plane->type != DRM_PLANE_TYPE_OVERLAY &&
		    !file_priv->universal_planes)
			continue;

		if (drm_lease_held(file_priv, plane->base.id)) {
			if (count < plane_resp->count_planes &&
			    put_user(plane->base.id, plane_ptr + count))
				return -EFAULT;
			count++;
		}
	}
	plane_resp->count_planes = count;

	return 0;
}

int drm_mode_getplane(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct drm_mode_get_plane *plane_resp = data;
	struct drm_plane *plane;
	uint32_t __user *format_ptr;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;

	plane = drm_plane_find(dev, file_priv, plane_resp->plane_id);
	if (!plane)
		return -ENOENT;

	drm_modeset_lock(&plane->mutex, NULL);
	if (plane->state && plane->state->crtc && drm_lease_held(file_priv, plane->state->crtc->base.id))
		plane_resp->crtc_id = plane->state->crtc->base.id;
	else if (!plane->state && plane->crtc && drm_lease_held(file_priv, plane->crtc->base.id))
		plane_resp->crtc_id = plane->crtc->base.id;
	else
		plane_resp->crtc_id = 0;

	if (plane->state && plane->state->fb)
		plane_resp->fb_id = plane->state->fb->base.id;
	else if (!plane->state && plane->fb)
		plane_resp->fb_id = plane->fb->base.id;
	else
		plane_resp->fb_id = 0;
	drm_modeset_unlock(&plane->mutex);

	plane_resp->plane_id = plane->base.id;
	plane_resp->possible_crtcs = drm_lease_filter_crtcs(file_priv,
							    plane->possible_crtcs);

	plane_resp->gamma_size = 0;

	/*
	 * This ioctl is called twice, once to determine how much space is
	 * needed, and the 2nd time to fill it.
	 */
	if (plane->format_count &&
	    (plane_resp->count_format_types >= plane->format_count)) {
		format_ptr = (uint32_t __user *)(unsigned long)plane_resp->format_type_ptr;
		if (copy_to_user(format_ptr,
				 plane->format_types,
				 sizeof(uint32_t) * plane->format_count)) {
			return -EFAULT;
		}
	}
	plane_resp->count_format_types = plane->format_count;

	return 0;
}

int drm_plane_check_pixel_format(struct drm_plane *plane,
				 u32 format, u64 modifier)
{
	unsigned int i;

	for (i = 0; i < plane->format_count; i++) {
		if (format == plane->format_types[i])
			break;
	}
	if (i == plane->format_count)
		return -EINVAL;

	if (plane->funcs->format_mod_supported) {
		if (!plane->funcs->format_mod_supported(plane, format, modifier))
			return -EINVAL;
	} else {
		if (!plane->modifier_count)
			return 0;

		for (i = 0; i < plane->modifier_count; i++) {
			if (modifier == plane->modifiers[i])
				break;
		}
		if (i == plane->modifier_count)
			return -EINVAL;
	}

	return 0;
}

static int __setplane_check(struct drm_plane *plane,
			    struct drm_crtc *crtc,
			    struct drm_framebuffer *fb,
			    int32_t crtc_x, int32_t crtc_y,
			    uint32_t crtc_w, uint32_t crtc_h,
			    uint32_t src_x, uint32_t src_y,
			    uint32_t src_w, uint32_t src_h)
{
	int ret;

	/* Check whether this plane is usable on this CRTC */
	if (!(plane->possible_crtcs & drm_crtc_mask(crtc))) {
		DRM_DEBUG_KMS("Invalid crtc for plane\n");
		return -EINVAL;
	}

	/* Check whether this plane supports the fb pixel format. */
	ret = drm_plane_check_pixel_format(plane, fb->format->format,
					   fb->modifier);
	if (ret) {
		struct drm_format_name_buf format_name;

		DRM_DEBUG_KMS("Invalid pixel format %s, modifier 0x%llx\n",
			      drm_get_format_name(fb->format->format,
						  &format_name),
			      fb->modifier);
		return ret;
	}

	/* Give drivers some help against integer overflows */
	if (crtc_w > INT_MAX ||
	    crtc_x > INT_MAX - (int32_t) crtc_w ||
	    crtc_h > INT_MAX ||
	    crtc_y > INT_MAX - (int32_t) crtc_h) {
		DRM_DEBUG_KMS("Invalid CRTC coordinates %ux%u+%d+%d\n",
			      crtc_w, crtc_h, crtc_x, crtc_y);
		return -ERANGE;
	}

	ret = drm_framebuffer_check_src_coords(src_x, src_y, src_w, src_h, fb);
	if (ret)
		return ret;

	return 0;
}

/**
 * drm_any_plane_has_format - Check whether any plane supports this format and modifier combination
 * @dev: DRM device
 * @format: pixel format (DRM_FORMAT_*)
 * @modifier: data layout modifier
 *
 * Returns:
 * Whether at least one plane supports the specified format and modifier combination.
 */
bool drm_any_plane_has_format(struct drm_device *dev,
			      u32 format, u64 modifier)
{
	struct drm_plane *plane;

	drm_for_each_plane(plane, dev) {
		if (drm_plane_check_pixel_format(plane, format, modifier) == 0)
			return true;
	}

	return false;
}
EXPORT_SYMBOL(drm_any_plane_has_format);

/*
 * __setplane_internal - setplane handler for internal callers
 *
 * This function will take a reference on the new fb for the plane
 * on success.
 *
 * src_{x,y,w,h} are provided in 16.16 fixed point format
 */
static int __setplane_internal(struct drm_plane *plane,
			       struct drm_crtc *crtc,
			       struct drm_framebuffer *fb,
			       int32_t crtc_x, int32_t crtc_y,
			       uint32_t crtc_w, uint32_t crtc_h,
			       /* src_{x,y,w,h} values are 16.16 fixed point */
			       uint32_t src_x, uint32_t src_y,
			       uint32_t src_w, uint32_t src_h,
			       struct drm_modeset_acquire_ctx *ctx)
{
	int ret = 0;

	WARN_ON(drm_drv_uses_atomic_modeset(plane->dev));

	/* No fb means shut it down */
	if (!fb) {
		plane->old_fb = plane->fb;
		ret = plane->funcs->disable_plane(plane, ctx);
		if (!ret) {
			plane->crtc = NULL;
			plane->fb = NULL;
		} else {
			plane->old_fb = NULL;
		}
		goto out;
	}

	ret = __setplane_check(plane, crtc, fb,
			       crtc_x, crtc_y, crtc_w, crtc_h,
			       src_x, src_y, src_w, src_h);
	if (ret)
		goto out;

	plane->old_fb = plane->fb;
	ret = plane->funcs->update_plane(plane, crtc, fb,
					 crtc_x, crtc_y, crtc_w, crtc_h,
					 src_x, src_y, src_w, src_h, ctx);
	if (!ret) {
		plane->crtc = crtc;
		plane->fb = fb;
		drm_framebuffer_get(plane->fb);
	} else {
		plane->old_fb = NULL;
	}

out:
	if (plane->old_fb)
		drm_framebuffer_put(plane->old_fb);
	plane->old_fb = NULL;

	return ret;
}

static int __setplane_atomic(struct drm_plane *plane,
			     struct drm_crtc *crtc,
			     struct drm_framebuffer *fb,
			     int32_t crtc_x, int32_t crtc_y,
			     uint32_t crtc_w, uint32_t crtc_h,
			     uint32_t src_x, uint32_t src_y,
			     uint32_t src_w, uint32_t src_h,
			     struct drm_modeset_acquire_ctx *ctx)
{
	int ret;

	WARN_ON(!drm_drv_uses_atomic_modeset(plane->dev));

	/* No fb means shut it down */
	if (!fb)
		return plane->funcs->disable_plane(plane, ctx);

	/*
	 * FIXME: This is redundant with drm_atomic_plane_check(),
	 * but the legacy cursor/"async" .update_plane() tricks
	 * don't call that so we still need this here. Should remove
	 * this when all .update_plane() implementations have been
	 * fixed to call drm_atomic_plane_check().
	 */
	ret = __setplane_check(plane, crtc, fb,
			       crtc_x, crtc_y, crtc_w, crtc_h,
			       src_x, src_y, src_w, src_h);
	if (ret)
		return ret;

	return plane->funcs->update_plane(plane, crtc, fb,
					  crtc_x, crtc_y, crtc_w, crtc_h,
					  src_x, src_y, src_w, src_h, ctx);
}

static int setplane_internal(struct drm_plane *plane,
			     struct drm_crtc *crtc,
			     struct drm_framebuffer *fb,
			     int32_t crtc_x, int32_t crtc_y,
			     uint32_t crtc_w, uint32_t crtc_h,
			     /* src_{x,y,w,h} values are 16.16 fixed point */
			     uint32_t src_x, uint32_t src_y,
			     uint32_t src_w, uint32_t src_h)
{
	struct drm_modeset_acquire_ctx ctx;
	int ret;

	DRM_MODESET_LOCK_ALL_BEGIN(plane->dev, ctx,
				   DRM_MODESET_ACQUIRE_INTERRUPTIBLE, ret);

	if (drm_drv_uses_atomic_modeset(plane->dev))
		ret = __setplane_atomic(plane, crtc, fb,
					crtc_x, crtc_y, crtc_w, crtc_h,
					src_x, src_y, src_w, src_h, &ctx);
	else
		ret = __setplane_internal(plane, crtc, fb,
					  crtc_x, crtc_y, crtc_w, crtc_h,
					  src_x, src_y, src_w, src_h, &ctx);

	DRM_MODESET_LOCK_ALL_END(ctx, ret);

	return ret;
}

int drm_mode_setplane(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct drm_mode_set_plane *plane_req = data;
	struct drm_plane *plane;
	struct drm_crtc *crtc = NULL;
	struct drm_framebuffer *fb = NULL;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;

	/*
	 * First, find the plane, crtc, and fb objects.  If not available,
	 * we don't bother to call the driver.
	 */
	plane = drm_plane_find(dev, file_priv, plane_req->plane_id);
	if (!plane) {
		DRM_DEBUG_KMS("Unknown plane ID %d\n",
			      plane_req->plane_id);
		return -ENOENT;
	}

	if (plane_req->fb_id) {
		fb = drm_framebuffer_lookup(dev, file_priv, plane_req->fb_id);
		if (!fb) {
			DRM_DEBUG_KMS("Unknown framebuffer ID %d\n",
				      plane_req->fb_id);
			return -ENOENT;
		}

		crtc = drm_crtc_find(dev, file_priv, plane_req->crtc_id);
		if (!crtc) {
			drm_framebuffer_put(fb);
			DRM_DEBUG_KMS("Unknown crtc ID %d\n",
				      plane_req->crtc_id);
			return -ENOENT;
		}
	}

	ret = setplane_internal(plane, crtc, fb,
				plane_req->crtc_x, plane_req->crtc_y,
				plane_req->crtc_w, plane_req->crtc_h,
				plane_req->src_x, plane_req->src_y,
				plane_req->src_w, plane_req->src_h);

	if (fb)
		drm_framebuffer_put(fb);

	return ret;
}

static int drm_mode_cursor_universal(struct drm_crtc *crtc,
				     struct drm_mode_cursor2 *req,
				     struct drm_file *file_priv,
				     struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_device *dev = crtc->dev;
	struct drm_plane *plane = crtc->cursor;
	struct drm_framebuffer *fb = NULL;
	struct drm_mode_fb_cmd2 fbreq = {
		.width = req->width,
		.height = req->height,
		.pixel_format = DRM_FORMAT_ARGB8888,
		.pitches = { req->width * 4 },
		.handles = { req->handle },
	};
	int32_t crtc_x, crtc_y;
	uint32_t crtc_w = 0, crtc_h = 0;
	uint32_t src_w = 0, src_h = 0;
	int ret = 0;

	BUG_ON(!plane);
	WARN_ON(plane->crtc != crtc && plane->crtc != NULL);

	/*
	 * Obtain fb we'll be using (either new or existing) and take an extra
	 * reference to it if fb != null.  setplane will take care of dropping
	 * the reference if the plane update fails.
	 */
	if (req->flags & DRM_MODE_CURSOR_BO) {
		if (req->handle) {
			fb = drm_internal_framebuffer_create(dev, &fbreq, file_priv);
			if (IS_ERR(fb)) {
				DRM_DEBUG_KMS("failed to wrap cursor buffer in drm framebuffer\n");
				return PTR_ERR(fb);
			}

			fb->hot_x = req->hot_x;
			fb->hot_y = req->hot_y;
		} else {
			fb = NULL;
		}
	} else {
		if (plane->state)
			fb = plane->state->fb;
		else
			fb = plane->fb;

		if (fb)
			drm_framebuffer_get(fb);
	}

	if (req->flags & DRM_MODE_CURSOR_MOVE) {
		crtc_x = req->x;
		crtc_y = req->y;
	} else {
		crtc_x = crtc->cursor_x;
		crtc_y = crtc->cursor_y;
	}

	if (fb) {
		crtc_w = fb->width;
		crtc_h = fb->height;
		src_w = fb->width << 16;
		src_h = fb->height << 16;
	}

	if (drm_drv_uses_atomic_modeset(dev))
		ret = __setplane_atomic(plane, crtc, fb,
					crtc_x, crtc_y, crtc_w, crtc_h,
					0, 0, src_w, src_h, ctx);
	else
		ret = __setplane_internal(plane, crtc, fb,
					  crtc_x, crtc_y, crtc_w, crtc_h,
					  0, 0, src_w, src_h, ctx);

	if (fb)
		drm_framebuffer_put(fb);

	/* Update successful; save new cursor position, if necessary */
	if (ret == 0 && req->flags & DRM_MODE_CURSOR_MOVE) {
		crtc->cursor_x = req->x;
		crtc->cursor_y = req->y;
	}

	return ret;
}

static int drm_mode_cursor_common(struct drm_device *dev,
				  struct drm_mode_cursor2 *req,
				  struct drm_file *file_priv)
{
	struct drm_crtc *crtc;
	struct drm_modeset_acquire_ctx ctx;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;

	if (!req->flags || (~DRM_MODE_CURSOR_FLAGS & req->flags))
		return -EINVAL;

	crtc = drm_crtc_find(dev, file_priv, req->crtc_id);
	if (!crtc) {
		DRM_DEBUG_KMS("Unknown CRTC ID %d\n", req->crtc_id);
		return -ENOENT;
	}

	drm_modeset_acquire_init(&ctx, DRM_MODESET_ACQUIRE_INTERRUPTIBLE);
retry:
	ret = drm_modeset_lock(&crtc->mutex, &ctx);
	if (ret)
		goto out;
	/*
	 * If this crtc has a universal cursor plane, call that plane's update
	 * handler rather than using legacy cursor handlers.
	 */
	if (crtc->cursor) {
		ret = drm_modeset_lock(&crtc->cursor->mutex, &ctx);
		if (ret)
			goto out;

		if (!drm_lease_held(file_priv, crtc->cursor->base.id)) {
			ret = -EACCES;
			goto out;
		}

		ret = drm_mode_cursor_universal(crtc, req, file_priv, &ctx);
		goto out;
	}

	if (req->flags & DRM_MODE_CURSOR_BO) {
		if (!crtc->funcs->cursor_set && !crtc->funcs->cursor_set2) {
			ret = -ENXIO;
			goto out;
		}
		/* Turns off the cursor if handle is 0 */
		if (crtc->funcs->cursor_set2)
			ret = crtc->funcs->cursor_set2(crtc, file_priv, req->handle,
						      req->width, req->height, req->hot_x, req->hot_y);
		else
			ret = crtc->funcs->cursor_set(crtc, file_priv, req->handle,
						      req->width, req->height);
	}

	if (req->flags & DRM_MODE_CURSOR_MOVE) {
		if (crtc->funcs->cursor_move) {
			ret = crtc->funcs->cursor_move(crtc, req->x, req->y);
		} else {
			ret = -EFAULT;
			goto out;
		}
	}
out:
	if (ret == -EDEADLK) {
		ret = drm_modeset_backoff(&ctx);
		if (!ret)
			goto retry;
	}

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	return ret;

}


int drm_mode_cursor_ioctl(struct drm_device *dev,
			  void *data, struct drm_file *file_priv)
{
	struct drm_mode_cursor *req = data;
	struct drm_mode_cursor2 new_req;

	memcpy(&new_req, req, sizeof(struct drm_mode_cursor));
	new_req.hot_x = new_req.hot_y = 0;

	return drm_mode_cursor_common(dev, &new_req, file_priv);
}

/*
 * Set the cursor configuration based on user request. This implements the 2nd
 * version of the cursor ioctl, which allows userspace to additionally specify
 * the hotspot of the pointer.
 */
int drm_mode_cursor2_ioctl(struct drm_device *dev,
			   void *data, struct drm_file *file_priv)
{
	struct drm_mode_cursor2 *req = data;

	return drm_mode_cursor_common(dev, req, file_priv);
}

int drm_mode_page_flip_ioctl(struct drm_device *dev,
			     void *data, struct drm_file *file_priv)
{
	struct drm_mode_crtc_page_flip_target *page_flip = data;
	struct drm_crtc *crtc;
	struct drm_plane *plane;
	struct drm_framebuffer *fb = NULL, *old_fb;
	struct drm_pending_vblank_event *e = NULL;
	u32 target_vblank = page_flip->sequence;
	struct drm_modeset_acquire_ctx ctx;
	int ret = -EINVAL;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;

	if (page_flip->flags & ~DRM_MODE_PAGE_FLIP_FLAGS)
		return -EINVAL;

	if (page_flip->sequence != 0 && !(page_flip->flags & DRM_MODE_PAGE_FLIP_TARGET))
		return -EINVAL;

	/* Only one of the DRM_MODE_PAGE_FLIP_TARGET_ABSOLUTE/RELATIVE flags
	 * can be specified
	 */
	if ((page_flip->flags & DRM_MODE_PAGE_FLIP_TARGET) == DRM_MODE_PAGE_FLIP_TARGET)
		return -EINVAL;

	if ((page_flip->flags & DRM_MODE_PAGE_FLIP_ASYNC) && !dev->mode_config.async_page_flip)
		return -EINVAL;

	crtc = drm_crtc_find(dev, file_priv, page_flip->crtc_id);
	if (!crtc)
		return -ENOENT;

	plane = crtc->primary;

	if (!drm_lease_held(file_priv, plane->base.id))
		return -EACCES;

	if (crtc->funcs->page_flip_target) {
		u32 current_vblank;
		int r;

		r = drm_crtc_vblank_get(crtc);
		if (r)
			return r;

		current_vblank = (u32)drm_crtc_vblank_count(crtc);

		switch (page_flip->flags & DRM_MODE_PAGE_FLIP_TARGET) {
		case DRM_MODE_PAGE_FLIP_TARGET_ABSOLUTE:
			if ((int)(target_vblank - current_vblank) > 1) {
				DRM_DEBUG("Invalid absolute flip target %u, "
					  "must be <= %u\n", target_vblank,
					  current_vblank + 1);
				drm_crtc_vblank_put(crtc);
				return -EINVAL;
			}
			break;
		case DRM_MODE_PAGE_FLIP_TARGET_RELATIVE:
			if (target_vblank != 0 && target_vblank != 1) {
				DRM_DEBUG("Invalid relative flip target %u, "
					  "must be 0 or 1\n", target_vblank);
				drm_crtc_vblank_put(crtc);
				return -EINVAL;
			}
			target_vblank += current_vblank;
			break;
		default:
			target_vblank = current_vblank +
				!(page_flip->flags & DRM_MODE_PAGE_FLIP_ASYNC);
			break;
		}
	} else if (crtc->funcs->page_flip == NULL ||
		   (page_flip->flags & DRM_MODE_PAGE_FLIP_TARGET)) {
		return -EINVAL;
	}

	drm_modeset_acquire_init(&ctx, DRM_MODESET_ACQUIRE_INTERRUPTIBLE);
retry:
	ret = drm_modeset_lock(&crtc->mutex, &ctx);
	if (ret)
		goto out;
	ret = drm_modeset_lock(&plane->mutex, &ctx);
	if (ret)
		goto out;

	if (plane->state)
		old_fb = plane->state->fb;
	else
		old_fb = plane->fb;

	if (old_fb == NULL) {
		/* The framebuffer is currently unbound, presumably
		 * due to a hotplug event, that userspace has not
		 * yet discovered.
		 */
		ret = -EBUSY;
		goto out;
	}

	fb = drm_framebuffer_lookup(dev, file_priv, page_flip->fb_id);
	if (!fb) {
		ret = -ENOENT;
		goto out;
	}

	if (plane->state) {
		const struct drm_plane_state *state = plane->state;

		ret = drm_framebuffer_check_src_coords(state->src_x,
						       state->src_y,
						       state->src_w,
						       state->src_h,
						       fb);
	} else {
		ret = drm_crtc_check_viewport(crtc, crtc->x, crtc->y,
					      &crtc->mode, fb);
	}
	if (ret)
		goto out;

	if (old_fb->format != fb->format) {
		DRM_DEBUG_KMS("Page flip is not allowed to change frame buffer format.\n");
		ret = -EINVAL;
		goto out;
	}

	if (page_flip->flags & DRM_MODE_PAGE_FLIP_EVENT) {
		e = kzalloc(sizeof *e, GFP_KERNEL);
		if (!e) {
			ret = -ENOMEM;
			goto out;
		}

		e->event.base.type = DRM_EVENT_FLIP_COMPLETE;
		e->event.base.length = sizeof(e->event);
		e->event.vbl.user_data = page_flip->user_data;
		e->event.vbl.crtc_id = crtc->base.id;

		ret = drm_event_reserve_init(dev, file_priv, &e->base, &e->event.base);
		if (ret) {
			kfree(e);
			e = NULL;
			goto out;
		}
	}

	plane->old_fb = plane->fb;
	if (crtc->funcs->page_flip_target)
		ret = crtc->funcs->page_flip_target(crtc, fb, e,
						    page_flip->flags,
						    target_vblank,
						    &ctx);
	else
		ret = crtc->funcs->page_flip(crtc, fb, e, page_flip->flags,
					     &ctx);
	if (ret) {
		if (page_flip->flags & DRM_MODE_PAGE_FLIP_EVENT)
			drm_event_cancel_free(dev, &e->base);
		/* Keep the old fb, don't unref it. */
		plane->old_fb = NULL;
	} else {
		if (!plane->state) {
			plane->fb = fb;
			drm_framebuffer_get(fb);
		}
	}

out:
	if (fb)
		drm_framebuffer_put(fb);
	if (plane->old_fb)
		drm_framebuffer_put(plane->old_fb);
	plane->old_fb = NULL;

	if (ret == -EDEADLK) {
		ret = drm_modeset_backoff(&ctx);
		if (!ret)
			goto retry;
	}

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	if (ret && crtc->funcs->page_flip_target)
		drm_crtc_vblank_put(crtc);

	return ret;
}
