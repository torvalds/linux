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

#include <linux/export.h>
#include <drm/drmP.h>
#include <drm/drm_mode_object.h>
#include <drm/drm_atomic.h>

#include "drm_crtc_internal.h"

/*
 * Internal function to assign a slot in the object idr and optionally
 * register the object into the idr.
 */
int __drm_mode_object_add(struct drm_device *dev, struct drm_mode_object *obj,
			  uint32_t obj_type, bool register_obj,
			  void (*obj_free_cb)(struct kref *kref))
{
	int ret;

	mutex_lock(&dev->mode_config.idr_mutex);
	ret = idr_alloc(&dev->mode_config.crtc_idr, register_obj ? obj : NULL, 1, 0, GFP_KERNEL);
	if (ret >= 0) {
		/*
		 * Set up the object linking under the protection of the idr
		 * lock so that other users can't see inconsistent state.
		 */
		obj->id = ret;
		obj->type = obj_type;
		if (obj_free_cb) {
			obj->free_cb = obj_free_cb;
			kref_init(&obj->refcount);
		}
	}
	mutex_unlock(&dev->mode_config.idr_mutex);

	return ret < 0 ? ret : 0;
}

/**
 * drm_mode_object_add - allocate a new modeset identifier
 * @dev: DRM device
 * @obj: object pointer, used to generate unique ID
 * @obj_type: object type
 *
 * Create a unique identifier based on @ptr in @dev's identifier space.  Used
 * for tracking modes, CRTCs and connectors.
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_mode_object_add(struct drm_device *dev,
			struct drm_mode_object *obj, uint32_t obj_type)
{
	return __drm_mode_object_add(dev, obj, obj_type, true, NULL);
}

void drm_mode_object_register(struct drm_device *dev,
			      struct drm_mode_object *obj)
{
	mutex_lock(&dev->mode_config.idr_mutex);
	idr_replace(&dev->mode_config.crtc_idr, obj, obj->id);
	mutex_unlock(&dev->mode_config.idr_mutex);
}

/**
 * drm_mode_object_unregister - free a modeset identifer
 * @dev: DRM device
 * @object: object to free
 *
 * Free @id from @dev's unique identifier pool.
 * This function can be called multiple times, and guards against
 * multiple removals.
 * These modeset identifiers are _not_ reference counted. Hence don't use this
 * for reference counted modeset objects like framebuffers.
 */
void drm_mode_object_unregister(struct drm_device *dev,
				struct drm_mode_object *object)
{
	mutex_lock(&dev->mode_config.idr_mutex);
	if (object->id) {
		idr_remove(&dev->mode_config.crtc_idr, object->id);
		object->id = 0;
	}
	mutex_unlock(&dev->mode_config.idr_mutex);
}

struct drm_mode_object *__drm_mode_object_find(struct drm_device *dev,
					       uint32_t id, uint32_t type)
{
	struct drm_mode_object *obj = NULL;

	mutex_lock(&dev->mode_config.idr_mutex);
	obj = idr_find(&dev->mode_config.crtc_idr, id);
	if (obj && type != DRM_MODE_OBJECT_ANY && obj->type != type)
		obj = NULL;
	if (obj && obj->id != id)
		obj = NULL;

	if (obj && obj->free_cb) {
		if (!kref_get_unless_zero(&obj->refcount))
			obj = NULL;
	}
	mutex_unlock(&dev->mode_config.idr_mutex);

	return obj;
}

/**
 * drm_mode_object_find - look up a drm object with static lifetime
 * @dev: drm device
 * @id: id of the mode object
 * @type: type of the mode object
 *
 * This function is used to look up a modeset object. It will acquire a
 * reference for reference counted objects. This reference must be dropped again
 * by callind drm_mode_object_put().
 */
struct drm_mode_object *drm_mode_object_find(struct drm_device *dev,
		uint32_t id, uint32_t type)
{
	struct drm_mode_object *obj = NULL;

	obj = __drm_mode_object_find(dev, id, type);
	return obj;
}
EXPORT_SYMBOL(drm_mode_object_find);

/**
 * drm_mode_object_put - release a mode object reference
 * @obj: DRM mode object
 *
 * This function decrements the object's refcount if it is a refcounted modeset
 * object. It is a no-op on any other object. This is used to drop references
 * acquired with drm_mode_object_get().
 */
void drm_mode_object_put(struct drm_mode_object *obj)
{
	if (obj->free_cb) {
		DRM_DEBUG("OBJ ID: %d (%d)\n", obj->id, kref_read(&obj->refcount));
		kref_put(&obj->refcount, obj->free_cb);
	}
}
EXPORT_SYMBOL(drm_mode_object_put);

/**
 * drm_mode_object_get - acquire a mode object reference
 * @obj: DRM mode object
 *
 * This function increments the object's refcount if it is a refcounted modeset
 * object. It is a no-op on any other object. References should be dropped again
 * by calling drm_mode_object_put().
 */
void drm_mode_object_get(struct drm_mode_object *obj)
{
	if (obj->free_cb) {
		DRM_DEBUG("OBJ ID: %d (%d)\n", obj->id, kref_read(&obj->refcount));
		kref_get(&obj->refcount);
	}
}
EXPORT_SYMBOL(drm_mode_object_get);

/**
 * drm_object_attach_property - attach a property to a modeset object
 * @obj: drm modeset object
 * @property: property to attach
 * @init_val: initial value of the property
 *
 * This attaches the given property to the modeset object with the given initial
 * value. Currently this function cannot fail since the properties are stored in
 * a statically sized array.
 */
void drm_object_attach_property(struct drm_mode_object *obj,
				struct drm_property *property,
				uint64_t init_val)
{
	int count = obj->properties->count;

	if (count == DRM_OBJECT_MAX_PROPERTY) {
		WARN(1, "Failed to attach object property (type: 0x%x). Please "
			"increase DRM_OBJECT_MAX_PROPERTY by 1 for each time "
			"you see this message on the same object type.\n",
			obj->type);
		return;
	}

	obj->properties->properties[count] = property;
	obj->properties->values[count] = init_val;
	obj->properties->count++;
}
EXPORT_SYMBOL(drm_object_attach_property);

/**
 * drm_object_property_set_value - set the value of a property
 * @obj: drm mode object to set property value for
 * @property: property to set
 * @val: value the property should be set to
 *
 * This function sets a given property on a given object. This function only
 * changes the software state of the property, it does not call into the
 * driver's ->set_property callback.
 *
 * Note that atomic drivers should not have any need to call this, the core will
 * ensure consistency of values reported back to userspace through the
 * appropriate ->atomic_get_property callback. Only legacy drivers should call
 * this function to update the tracked value (after clamping and other
 * restrictions have been applied).
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_object_property_set_value(struct drm_mode_object *obj,
				  struct drm_property *property, uint64_t val)
{
	int i;

	WARN_ON(drm_drv_uses_atomic_modeset(property->dev) &&
		!(property->flags & DRM_MODE_PROP_IMMUTABLE));

	for (i = 0; i < obj->properties->count; i++) {
		if (obj->properties->properties[i] == property) {
			obj->properties->values[i] = val;
			return 0;
		}
	}

	return -EINVAL;
}
EXPORT_SYMBOL(drm_object_property_set_value);

int __drm_object_property_get_value(struct drm_mode_object *obj,
				  struct drm_property *property, uint64_t *val)
{
	int i;

	/* read-only properties bypass atomic mechanism and still store
	 * their value in obj->properties->values[].. mostly to avoid
	 * having to deal w/ EDID and similar props in atomic paths:
	 */
	if (drm_drv_uses_atomic_modeset(property->dev) &&
			!(property->flags & DRM_MODE_PROP_IMMUTABLE))
		return drm_atomic_get_property(obj, property, val);

	for (i = 0; i < obj->properties->count; i++) {
		if (obj->properties->properties[i] == property) {
			*val = obj->properties->values[i];
			return 0;
		}

	}

	return -EINVAL;
}

/**
 * drm_object_property_get_value - retrieve the value of a property
 * @obj: drm mode object to get property value from
 * @property: property to retrieve
 * @val: storage for the property value
 *
 * This function retrieves the softare state of the given property for the given
 * property. Since there is no driver callback to retrieve the current property
 * value this might be out of sync with the hardware, depending upon the driver
 * and property.
 *
 * Atomic drivers should never call this function directly, the core will read
 * out property values through the various ->atomic_get_property callbacks.
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_object_property_get_value(struct drm_mode_object *obj,
				  struct drm_property *property, uint64_t *val)
{
	WARN_ON(drm_drv_uses_atomic_modeset(property->dev));

	return __drm_object_property_get_value(obj, property, val);
}
EXPORT_SYMBOL(drm_object_property_get_value);

/* helper for getconnector and getproperties ioctls */
int drm_mode_object_get_properties(struct drm_mode_object *obj, bool atomic,
				   uint32_t __user *prop_ptr,
				   uint64_t __user *prop_values,
				   uint32_t *arg_count_props)
{
	int i, ret, count;

	for (i = 0, count = 0; i < obj->properties->count; i++) {
		struct drm_property *prop = obj->properties->properties[i];
		uint64_t val;

		if ((prop->flags & DRM_MODE_PROP_ATOMIC) && !atomic)
			continue;

		if (*arg_count_props > count) {
			ret = __drm_object_property_get_value(obj, prop, &val);
			if (ret)
				return ret;

			if (put_user(prop->base.id, prop_ptr + count))
				return -EFAULT;

			if (put_user(val, prop_values + count))
				return -EFAULT;
		}

		count++;
	}
	*arg_count_props = count;

	return 0;
}

/**
 * drm_mode_obj_get_properties_ioctl - get the current value of a object's property
 * @dev: DRM device
 * @data: ioctl data
 * @file_priv: DRM file info
 *
 * This function retrieves the current value for an object's property. Compared
 * to the connector specific ioctl this one is extended to also work on crtc and
 * plane objects.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_obj_get_properties_ioctl(struct drm_device *dev, void *data,
				      struct drm_file *file_priv)
{
	struct drm_mode_obj_get_properties *arg = data;
	struct drm_mode_object *obj;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	drm_modeset_lock_all(dev);

	obj = drm_mode_object_find(dev, arg->obj_id, arg->obj_type);
	if (!obj) {
		ret = -ENOENT;
		goto out;
	}
	if (!obj->properties) {
		ret = -EINVAL;
		goto out_unref;
	}

	ret = drm_mode_object_get_properties(obj, file_priv->atomic,
			(uint32_t __user *)(unsigned long)(arg->props_ptr),
			(uint64_t __user *)(unsigned long)(arg->prop_values_ptr),
			&arg->count_props);

out_unref:
	drm_mode_object_put(obj);
out:
	drm_modeset_unlock_all(dev);
	return ret;
}

struct drm_property *drm_mode_obj_find_prop_id(struct drm_mode_object *obj,
					       uint32_t prop_id)
{
	int i;

	for (i = 0; i < obj->properties->count; i++)
		if (obj->properties->properties[i]->base.id == prop_id)
			return obj->properties->properties[i];

	return NULL;
}

static int set_property_legacy(struct drm_mode_object *obj,
			       struct drm_property *prop,
			       uint64_t prop_value)
{
	struct drm_device *dev = prop->dev;
	struct drm_mode_object *ref;
	int ret = -EINVAL;

	if (!drm_property_change_valid_get(prop, prop_value, &ref))
		return -EINVAL;

	drm_modeset_lock_all(dev);
	switch (obj->type) {
	case DRM_MODE_OBJECT_CONNECTOR:
		ret = drm_mode_connector_set_obj_prop(obj, prop,
						      prop_value);
		break;
	case DRM_MODE_OBJECT_CRTC:
		ret = drm_mode_crtc_set_obj_prop(obj, prop, prop_value);
		break;
	case DRM_MODE_OBJECT_PLANE:
		ret = drm_mode_plane_set_obj_prop(obj_to_plane(obj),
						  prop, prop_value);
		break;
	}
	drm_property_change_valid_put(prop, ref);
	drm_modeset_unlock_all(dev);

	return ret;
}

static int set_property_atomic(struct drm_mode_object *obj,
			       struct drm_property *prop,
			       uint64_t prop_value)
{
	struct drm_device *dev = prop->dev;
	struct drm_atomic_state *state;
	struct drm_modeset_acquire_ctx ctx;
	int ret;

	drm_modeset_acquire_init(&ctx, 0);

	state = drm_atomic_state_alloc(dev);
	if (!state)
		return -ENOMEM;
	state->acquire_ctx = &ctx;
retry:
	if (prop == state->dev->mode_config.dpms_property) {
		if (obj->type != DRM_MODE_OBJECT_CONNECTOR) {
			ret = -EINVAL;
			goto out;
		}

		ret = drm_atomic_connector_commit_dpms(state,
						       obj_to_connector(obj),
						       prop_value);
	} else {
		ret = drm_atomic_set_property(state, obj, prop, prop_value);
		if (ret)
			goto out;
		ret = drm_atomic_commit(state);
	}
out:
	if (ret == -EDEADLK) {
		drm_atomic_state_clear(state);
		drm_modeset_backoff(&ctx);
		goto retry;
	}

	drm_atomic_state_put(state);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	return ret;
}

int drm_mode_obj_set_property_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv)
{
	struct drm_mode_obj_set_property *arg = data;
	struct drm_mode_object *arg_obj;
	struct drm_property *property;
	int ret = -EINVAL;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	arg_obj = drm_mode_object_find(dev, arg->obj_id, arg->obj_type);
	if (!arg_obj)
		return -ENOENT;

	if (!arg_obj->properties)
		goto out_unref;

	property = drm_mode_obj_find_prop_id(arg_obj, arg->prop_id);
	if (!property)
		goto out_unref;

	if (drm_drv_uses_atomic_modeset(property->dev))
		ret = set_property_atomic(arg_obj, property, arg->value);
	else
		ret = set_property_legacy(arg_obj, property, arg->value);

out_unref:
	drm_mode_object_put(arg_obj);
	return ret;
}
