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
#include <drm/drm_property.h>

#include "drm_crtc_internal.h"

/**
 * DOC: overview
 *
 * Properties as represented by &drm_property are used to extend the modeset
 * interface exposed to userspace. For the atomic modeset IOCTL properties are
 * even the only way to transport metadata about the desired new modeset
 * configuration from userspace to the kernel. Properties have a well-defined
 * value range, which is enforced by the drm core. See the documentation of the
 * flags member of &struct drm_property for an overview of the different
 * property types and ranges.
 *
 * Properties don't store the current value directly, but need to be
 * instatiated by attaching them to a &drm_mode_object with
 * drm_object_attach_property().
 *
 * Property values are only 64bit. To support bigger piles of data (like gamma
 * tables, color correction matrices or large structures) a property can instead
 * point at a &drm_property_blob with that additional data.
 *
 * Properties are defined by their symbolic name, userspace must keep a
 * per-object mapping from those names to the property ID used in the atomic
 * IOCTL and in the get/set property IOCTL.
 */

static bool drm_property_type_valid(struct drm_property *property)
{
	if (property->flags & DRM_MODE_PROP_EXTENDED_TYPE)
		return !(property->flags & DRM_MODE_PROP_LEGACY_TYPE);
	return !!(property->flags & DRM_MODE_PROP_LEGACY_TYPE);
}

/**
 * drm_property_create - create a new property type
 * @dev: drm device
 * @flags: flags specifying the property type
 * @name: name of the property
 * @num_values: number of pre-defined values
 *
 * This creates a new generic drm property which can then be attached to a drm
 * object with drm_object_attach_property(). The returned property object must
 * be freed with drm_property_destroy(), which is done automatically when
 * calling drm_mode_config_cleanup().
 *
 * Returns:
 * A pointer to the newly created property on success, NULL on failure.
 */
struct drm_property *drm_property_create(struct drm_device *dev, int flags,
					 const char *name, int num_values)
{
	struct drm_property *property = NULL;
	int ret;

	property = kzalloc(sizeof(struct drm_property), GFP_KERNEL);
	if (!property)
		return NULL;

	property->dev = dev;

	if (num_values) {
		property->values = kcalloc(num_values, sizeof(uint64_t),
					   GFP_KERNEL);
		if (!property->values)
			goto fail;
	}

	ret = drm_mode_object_add(dev, &property->base, DRM_MODE_OBJECT_PROPERTY);
	if (ret)
		goto fail;

	property->flags = flags;
	property->num_values = num_values;
	INIT_LIST_HEAD(&property->enum_list);

	if (name) {
		strncpy(property->name, name, DRM_PROP_NAME_LEN);
		property->name[DRM_PROP_NAME_LEN-1] = '\0';
	}

	list_add_tail(&property->head, &dev->mode_config.property_list);

	WARN_ON(!drm_property_type_valid(property));

	return property;
fail:
	kfree(property->values);
	kfree(property);
	return NULL;
}
EXPORT_SYMBOL(drm_property_create);

/**
 * drm_property_create_enum - create a new enumeration property type
 * @dev: drm device
 * @flags: flags specifying the property type
 * @name: name of the property
 * @props: enumeration lists with property values
 * @num_values: number of pre-defined values
 *
 * This creates a new generic drm property which can then be attached to a drm
 * object with drm_object_attach_property(). The returned property object must
 * be freed with drm_property_destroy(), which is done automatically when
 * calling drm_mode_config_cleanup().
 *
 * Userspace is only allowed to set one of the predefined values for enumeration
 * properties.
 *
 * Returns:
 * A pointer to the newly created property on success, NULL on failure.
 */
struct drm_property *drm_property_create_enum(struct drm_device *dev, int flags,
					 const char *name,
					 const struct drm_prop_enum_list *props,
					 int num_values)
{
	struct drm_property *property;
	int i, ret;

	flags |= DRM_MODE_PROP_ENUM;

	property = drm_property_create(dev, flags, name, num_values);
	if (!property)
		return NULL;

	for (i = 0; i < num_values; i++) {
		ret = drm_property_add_enum(property, i,
				      props[i].type,
				      props[i].name);
		if (ret) {
			drm_property_destroy(dev, property);
			return NULL;
		}
	}

	return property;
}
EXPORT_SYMBOL(drm_property_create_enum);

/**
 * drm_property_create_bitmask - create a new bitmask property type
 * @dev: drm device
 * @flags: flags specifying the property type
 * @name: name of the property
 * @props: enumeration lists with property bitflags
 * @num_props: size of the @props array
 * @supported_bits: bitmask of all supported enumeration values
 *
 * This creates a new bitmask drm property which can then be attached to a drm
 * object with drm_object_attach_property(). The returned property object must
 * be freed with drm_property_destroy(), which is done automatically when
 * calling drm_mode_config_cleanup().
 *
 * Compared to plain enumeration properties userspace is allowed to set any
 * or'ed together combination of the predefined property bitflag values
 *
 * Returns:
 * A pointer to the newly created property on success, NULL on failure.
 */
struct drm_property *drm_property_create_bitmask(struct drm_device *dev,
					 int flags, const char *name,
					 const struct drm_prop_enum_list *props,
					 int num_props,
					 uint64_t supported_bits)
{
	struct drm_property *property;
	int i, ret, index = 0;
	int num_values = hweight64(supported_bits);

	flags |= DRM_MODE_PROP_BITMASK;

	property = drm_property_create(dev, flags, name, num_values);
	if (!property)
		return NULL;
	for (i = 0; i < num_props; i++) {
		if (!(supported_bits & (1ULL << props[i].type)))
			continue;

		if (WARN_ON(index >= num_values)) {
			drm_property_destroy(dev, property);
			return NULL;
		}

		ret = drm_property_add_enum(property, index++,
				      props[i].type,
				      props[i].name);
		if (ret) {
			drm_property_destroy(dev, property);
			return NULL;
		}
	}

	return property;
}
EXPORT_SYMBOL(drm_property_create_bitmask);

static struct drm_property *property_create_range(struct drm_device *dev,
					 int flags, const char *name,
					 uint64_t min, uint64_t max)
{
	struct drm_property *property;

	property = drm_property_create(dev, flags, name, 2);
	if (!property)
		return NULL;

	property->values[0] = min;
	property->values[1] = max;

	return property;
}

/**
 * drm_property_create_range - create a new unsigned ranged property type
 * @dev: drm device
 * @flags: flags specifying the property type
 * @name: name of the property
 * @min: minimum value of the property
 * @max: maximum value of the property
 *
 * This creates a new generic drm property which can then be attached to a drm
 * object with drm_object_attach_property(). The returned property object must
 * be freed with drm_property_destroy(), which is done automatically when
 * calling drm_mode_config_cleanup().
 *
 * Userspace is allowed to set any unsigned integer value in the (min, max)
 * range inclusive.
 *
 * Returns:
 * A pointer to the newly created property on success, NULL on failure.
 */
struct drm_property *drm_property_create_range(struct drm_device *dev, int flags,
					 const char *name,
					 uint64_t min, uint64_t max)
{
	return property_create_range(dev, DRM_MODE_PROP_RANGE | flags,
			name, min, max);
}
EXPORT_SYMBOL(drm_property_create_range);

/**
 * drm_property_create_signed_range - create a new signed ranged property type
 * @dev: drm device
 * @flags: flags specifying the property type
 * @name: name of the property
 * @min: minimum value of the property
 * @max: maximum value of the property
 *
 * This creates a new generic drm property which can then be attached to a drm
 * object with drm_object_attach_property(). The returned property object must
 * be freed with drm_property_destroy(), which is done automatically when
 * calling drm_mode_config_cleanup().
 *
 * Userspace is allowed to set any signed integer value in the (min, max)
 * range inclusive.
 *
 * Returns:
 * A pointer to the newly created property on success, NULL on failure.
 */
struct drm_property *drm_property_create_signed_range(struct drm_device *dev,
					 int flags, const char *name,
					 int64_t min, int64_t max)
{
	return property_create_range(dev, DRM_MODE_PROP_SIGNED_RANGE | flags,
			name, I642U64(min), I642U64(max));
}
EXPORT_SYMBOL(drm_property_create_signed_range);

/**
 * drm_property_create_object - create a new object property type
 * @dev: drm device
 * @flags: flags specifying the property type
 * @name: name of the property
 * @type: object type from DRM_MODE_OBJECT_* defines
 *
 * This creates a new generic drm property which can then be attached to a drm
 * object with drm_object_attach_property(). The returned property object must
 * be freed with drm_property_destroy(), which is done automatically when
 * calling drm_mode_config_cleanup().
 *
 * Userspace is only allowed to set this to any property value of the given
 * @type. Only useful for atomic properties, which is enforced.
 *
 * Returns:
 * A pointer to the newly created property on success, NULL on failure.
 */
struct drm_property *drm_property_create_object(struct drm_device *dev,
						int flags, const char *name,
						uint32_t type)
{
	struct drm_property *property;

	flags |= DRM_MODE_PROP_OBJECT;

	if (WARN_ON(!(flags & DRM_MODE_PROP_ATOMIC)))
		return NULL;

	property = drm_property_create(dev, flags, name, 1);
	if (!property)
		return NULL;

	property->values[0] = type;

	return property;
}
EXPORT_SYMBOL(drm_property_create_object);

/**
 * drm_property_create_bool - create a new boolean property type
 * @dev: drm device
 * @flags: flags specifying the property type
 * @name: name of the property
 *
 * This creates a new generic drm property which can then be attached to a drm
 * object with drm_object_attach_property(). The returned property object must
 * be freed with drm_property_destroy(), which is done automatically when
 * calling drm_mode_config_cleanup().
 *
 * This is implemented as a ranged property with only {0, 1} as valid values.
 *
 * Returns:
 * A pointer to the newly created property on success, NULL on failure.
 */
struct drm_property *drm_property_create_bool(struct drm_device *dev, int flags,
					      const char *name)
{
	return drm_property_create_range(dev, flags, name, 0, 1);
}
EXPORT_SYMBOL(drm_property_create_bool);

/**
 * drm_property_add_enum - add a possible value to an enumeration property
 * @property: enumeration property to change
 * @index: index of the new enumeration
 * @value: value of the new enumeration
 * @name: symbolic name of the new enumeration
 *
 * This functions adds enumerations to a property.
 *
 * It's use is deprecated, drivers should use one of the more specific helpers
 * to directly create the property with all enumerations already attached.
 *
 * Returns:
 * Zero on success, error code on failure.
 */
int drm_property_add_enum(struct drm_property *property, int index,
			  uint64_t value, const char *name)
{
	struct drm_property_enum *prop_enum;

	if (!(drm_property_type_is(property, DRM_MODE_PROP_ENUM) ||
			drm_property_type_is(property, DRM_MODE_PROP_BITMASK)))
		return -EINVAL;

	/*
	 * Bitmask enum properties have the additional constraint of values
	 * from 0 to 63
	 */
	if (drm_property_type_is(property, DRM_MODE_PROP_BITMASK) &&
			(value > 63))
		return -EINVAL;

	if (!list_empty(&property->enum_list)) {
		list_for_each_entry(prop_enum, &property->enum_list, head) {
			if (prop_enum->value == value) {
				strncpy(prop_enum->name, name, DRM_PROP_NAME_LEN);
				prop_enum->name[DRM_PROP_NAME_LEN-1] = '\0';
				return 0;
			}
		}
	}

	prop_enum = kzalloc(sizeof(struct drm_property_enum), GFP_KERNEL);
	if (!prop_enum)
		return -ENOMEM;

	strncpy(prop_enum->name, name, DRM_PROP_NAME_LEN);
	prop_enum->name[DRM_PROP_NAME_LEN-1] = '\0';
	prop_enum->value = value;

	property->values[index] = value;
	list_add_tail(&prop_enum->head, &property->enum_list);
	return 0;
}
EXPORT_SYMBOL(drm_property_add_enum);

/**
 * drm_property_destroy - destroy a drm property
 * @dev: drm device
 * @property: property to destry
 *
 * This function frees a property including any attached resources like
 * enumeration values.
 */
void drm_property_destroy(struct drm_device *dev, struct drm_property *property)
{
	struct drm_property_enum *prop_enum, *pt;

	list_for_each_entry_safe(prop_enum, pt, &property->enum_list, head) {
		list_del(&prop_enum->head);
		kfree(prop_enum);
	}

	if (property->num_values)
		kfree(property->values);
	drm_mode_object_unregister(dev, &property->base);
	list_del(&property->head);
	kfree(property);
}
EXPORT_SYMBOL(drm_property_destroy);

int drm_mode_getproperty_ioctl(struct drm_device *dev,
			       void *data, struct drm_file *file_priv)
{
	struct drm_mode_get_property *out_resp = data;
	struct drm_property *property;
	int enum_count = 0;
	int value_count = 0;
	int i, copied;
	struct drm_property_enum *prop_enum;
	struct drm_mode_property_enum __user *enum_ptr;
	uint64_t __user *values_ptr;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	property = drm_property_find(dev, out_resp->prop_id);
	if (!property)
		return -ENOENT;

	strncpy(out_resp->name, property->name, DRM_PROP_NAME_LEN);
	out_resp->name[DRM_PROP_NAME_LEN-1] = 0;
	out_resp->flags = property->flags;

	value_count = property->num_values;
	values_ptr = u64_to_user_ptr(out_resp->values_ptr);

	for (i = 0; i < value_count; i++) {
		if (i < out_resp->count_values &&
		    put_user(property->values[i], values_ptr + i)) {
			return -EFAULT;
		}
	}
	out_resp->count_values = value_count;

	copied = 0;
	enum_ptr = u64_to_user_ptr(out_resp->enum_blob_ptr);

	if (drm_property_type_is(property, DRM_MODE_PROP_ENUM) ||
	    drm_property_type_is(property, DRM_MODE_PROP_BITMASK)) {
		list_for_each_entry(prop_enum, &property->enum_list, head) {
			enum_count++;
			if (out_resp->count_enum_blobs < enum_count)
				continue;

			if (copy_to_user(&enum_ptr[copied].value,
					 &prop_enum->value, sizeof(uint64_t)))
				return -EFAULT;

			if (copy_to_user(&enum_ptr[copied].name,
					 &prop_enum->name, DRM_PROP_NAME_LEN))
				return -EFAULT;
			copied++;
		}
		out_resp->count_enum_blobs = enum_count;
	}

	/*
	 * NOTE: The idea seems to have been to use this to read all the blob
	 * property values. But nothing ever added them to the corresponding
	 * list, userspace always used the special-purpose get_blob ioctl to
	 * read the value for a blob property. It also doesn't make a lot of
	 * sense to return values here when everything else is just metadata for
	 * the property itself.
	 */
	if (drm_property_type_is(property, DRM_MODE_PROP_BLOB))
		out_resp->count_enum_blobs = 0;

	return 0;
}

static void drm_property_free_blob(struct kref *kref)
{
	struct drm_property_blob *blob =
		container_of(kref, struct drm_property_blob, base.refcount);

	mutex_lock(&blob->dev->mode_config.blob_lock);
	list_del(&blob->head_global);
	mutex_unlock(&blob->dev->mode_config.blob_lock);

	drm_mode_object_unregister(blob->dev, &blob->base);

	kfree(blob);
}

/**
 * drm_property_create_blob - Create new blob property
 * @dev: DRM device to create property for
 * @length: Length to allocate for blob data
 * @data: If specified, copies data into blob
 *
 * Creates a new blob property for a specified DRM device, optionally
 * copying data. Note that blob properties are meant to be invariant, hence the
 * data must be filled out before the blob is used as the value of any property.
 *
 * Returns:
 * New blob property with a single reference on success, or an ERR_PTR
 * value on failure.
 */
struct drm_property_blob *
drm_property_create_blob(struct drm_device *dev, size_t length,
			 const void *data)
{
	struct drm_property_blob *blob;
	int ret;

	if (!length || length > ULONG_MAX - sizeof(struct drm_property_blob))
		return ERR_PTR(-EINVAL);

	blob = kzalloc(sizeof(struct drm_property_blob)+length, GFP_KERNEL);
	if (!blob)
		return ERR_PTR(-ENOMEM);

	/* This must be explicitly initialised, so we can safely call list_del
	 * on it in the removal handler, even if it isn't in a file list. */
	INIT_LIST_HEAD(&blob->head_file);
	blob->length = length;
	blob->dev = dev;

	if (data)
		memcpy(blob->data, data, length);

	ret = __drm_mode_object_add(dev, &blob->base, DRM_MODE_OBJECT_BLOB,
				    true, drm_property_free_blob);
	if (ret) {
		kfree(blob);
		return ERR_PTR(-EINVAL);
	}

	mutex_lock(&dev->mode_config.blob_lock);
	list_add_tail(&blob->head_global,
	              &dev->mode_config.property_blob_list);
	mutex_unlock(&dev->mode_config.blob_lock);

	return blob;
}
EXPORT_SYMBOL(drm_property_create_blob);

/**
 * drm_property_blob_put - release a blob property reference
 * @blob: DRM blob property
 *
 * Releases a reference to a blob property. May free the object.
 */
void drm_property_blob_put(struct drm_property_blob *blob)
{
	if (!blob)
		return;

	drm_mode_object_put(&blob->base);
}
EXPORT_SYMBOL(drm_property_blob_put);

void drm_property_destroy_user_blobs(struct drm_device *dev,
				     struct drm_file *file_priv)
{
	struct drm_property_blob *blob, *bt;

	/*
	 * When the file gets released that means no one else can access the
	 * blob list any more, so no need to grab dev->blob_lock.
	 */
	list_for_each_entry_safe(blob, bt, &file_priv->blobs, head_file) {
		list_del_init(&blob->head_file);
		drm_property_blob_put(blob);
	}
}

/**
 * drm_property_blob_get - acquire blob property reference
 * @blob: DRM blob property
 *
 * Acquires a reference to an existing blob property. Returns @blob, which
 * allows this to be used as a shorthand in assignments.
 */
struct drm_property_blob *drm_property_blob_get(struct drm_property_blob *blob)
{
	drm_mode_object_get(&blob->base);
	return blob;
}
EXPORT_SYMBOL(drm_property_blob_get);

/**
 * drm_property_lookup_blob - look up a blob property and take a reference
 * @dev: drm device
 * @id: id of the blob property
 *
 * If successful, this takes an additional reference to the blob property.
 * callers need to make sure to eventually unreference the returned property
 * again, using drm_property_blob_put().
 *
 * Return:
 * NULL on failure, pointer to the blob on success.
 */
struct drm_property_blob *drm_property_lookup_blob(struct drm_device *dev,
					           uint32_t id)
{
	struct drm_mode_object *obj;
	struct drm_property_blob *blob = NULL;

	obj = __drm_mode_object_find(dev, id, DRM_MODE_OBJECT_BLOB);
	if (obj)
		blob = obj_to_blob(obj);
	return blob;
}
EXPORT_SYMBOL(drm_property_lookup_blob);

/**
 * drm_property_replace_global_blob - replace existing blob property
 * @dev: drm device
 * @replace: location of blob property pointer to be replaced
 * @length: length of data for new blob, or 0 for no data
 * @data: content for new blob, or NULL for no data
 * @obj_holds_id: optional object for property holding blob ID
 * @prop_holds_id: optional property holding blob ID
 * @return 0 on success or error on failure
 *
 * This function will replace a global property in the blob list, optionally
 * updating a property which holds the ID of that property.
 *
 * If length is 0 or data is NULL, no new blob will be created, and the holding
 * property, if specified, will be set to 0.
 *
 * Access to the replace pointer is assumed to be protected by the caller, e.g.
 * by holding the relevant modesetting object lock for its parent.
 *
 * For example, a drm_connector has a 'PATH' property, which contains the ID
 * of a blob property with the value of the MST path information. Calling this
 * function with replace pointing to the connector's path_blob_ptr, length and
 * data set for the new path information, obj_holds_id set to the connector's
 * base object, and prop_holds_id set to the path property name, will perform
 * a completely atomic update. The access to path_blob_ptr is protected by the
 * caller holding a lock on the connector.
 */
int drm_property_replace_global_blob(struct drm_device *dev,
				     struct drm_property_blob **replace,
				     size_t length,
				     const void *data,
				     struct drm_mode_object *obj_holds_id,
				     struct drm_property *prop_holds_id)
{
	struct drm_property_blob *new_blob = NULL;
	struct drm_property_blob *old_blob = NULL;
	int ret;

	WARN_ON(replace == NULL);

	old_blob = *replace;

	if (length && data) {
		new_blob = drm_property_create_blob(dev, length, data);
		if (IS_ERR(new_blob))
			return PTR_ERR(new_blob);
	}

	if (obj_holds_id) {
		ret = drm_object_property_set_value(obj_holds_id,
						    prop_holds_id,
						    new_blob ?
						        new_blob->base.id : 0);
		if (ret != 0)
			goto err_created;
	}

	drm_property_blob_put(old_blob);
	*replace = new_blob;

	return 0;

err_created:
	drm_property_blob_put(new_blob);
	return ret;
}
EXPORT_SYMBOL(drm_property_replace_global_blob);

int drm_mode_getblob_ioctl(struct drm_device *dev,
			   void *data, struct drm_file *file_priv)
{
	struct drm_mode_get_blob *out_resp = data;
	struct drm_property_blob *blob;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	blob = drm_property_lookup_blob(dev, out_resp->blob_id);
	if (!blob)
		return -ENOENT;

	if (out_resp->length == blob->length) {
		if (copy_to_user(u64_to_user_ptr(out_resp->data),
				 blob->data,
				 blob->length)) {
			ret = -EFAULT;
			goto unref;
		}
	}
	out_resp->length = blob->length;
unref:
	drm_property_blob_put(blob);

	return ret;
}

int drm_mode_createblob_ioctl(struct drm_device *dev,
			      void *data, struct drm_file *file_priv)
{
	struct drm_mode_create_blob *out_resp = data;
	struct drm_property_blob *blob;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	blob = drm_property_create_blob(dev, out_resp->length, NULL);
	if (IS_ERR(blob))
		return PTR_ERR(blob);

	if (copy_from_user(blob->data,
			   u64_to_user_ptr(out_resp->data),
			   out_resp->length)) {
		ret = -EFAULT;
		goto out_blob;
	}

	/* Dropping the lock between create_blob and our access here is safe
	 * as only the same file_priv can remove the blob; at this point, it is
	 * not associated with any file_priv. */
	mutex_lock(&dev->mode_config.blob_lock);
	out_resp->blob_id = blob->base.id;
	list_add_tail(&blob->head_file, &file_priv->blobs);
	mutex_unlock(&dev->mode_config.blob_lock);

	return 0;

out_blob:
	drm_property_blob_put(blob);
	return ret;
}

int drm_mode_destroyblob_ioctl(struct drm_device *dev,
			       void *data, struct drm_file *file_priv)
{
	struct drm_mode_destroy_blob *out_resp = data;
	struct drm_property_blob *blob = NULL, *bt;
	bool found = false;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	blob = drm_property_lookup_blob(dev, out_resp->blob_id);
	if (!blob)
		return -ENOENT;

	mutex_lock(&dev->mode_config.blob_lock);
	/* Ensure the property was actually created by this user. */
	list_for_each_entry(bt, &file_priv->blobs, head_file) {
		if (bt == blob) {
			found = true;
			break;
		}
	}

	if (!found) {
		ret = -EPERM;
		goto err;
	}

	/* We must drop head_file here, because we may not be the last
	 * reference on the blob. */
	list_del_init(&blob->head_file);
	mutex_unlock(&dev->mode_config.blob_lock);

	/* One reference from lookup, and one from the filp. */
	drm_property_blob_put(blob);
	drm_property_blob_put(blob);

	return 0;

err:
	mutex_unlock(&dev->mode_config.blob_lock);
	drm_property_blob_put(blob);

	return ret;
}

/* Some properties could refer to dynamic refcnt'd objects, or things that
 * need special locking to handle lifetime issues (ie. to ensure the prop
 * value doesn't become invalid part way through the property update due to
 * race).  The value returned by reference via 'obj' should be passed back
 * to drm_property_change_valid_put() after the property is set (and the
 * object to which the property is attached has a chance to take it's own
 * reference).
 */
bool drm_property_change_valid_get(struct drm_property *property,
				   uint64_t value, struct drm_mode_object **ref)
{
	int i;

	if (property->flags & DRM_MODE_PROP_IMMUTABLE)
		return false;

	*ref = NULL;

	if (drm_property_type_is(property, DRM_MODE_PROP_RANGE)) {
		if (value < property->values[0] || value > property->values[1])
			return false;
		return true;
	} else if (drm_property_type_is(property, DRM_MODE_PROP_SIGNED_RANGE)) {
		int64_t svalue = U642I64(value);

		if (svalue < U642I64(property->values[0]) ||
				svalue > U642I64(property->values[1]))
			return false;
		return true;
	} else if (drm_property_type_is(property, DRM_MODE_PROP_BITMASK)) {
		uint64_t valid_mask = 0;

		for (i = 0; i < property->num_values; i++)
			valid_mask |= (1ULL << property->values[i]);
		return !(value & ~valid_mask);
	} else if (drm_property_type_is(property, DRM_MODE_PROP_BLOB)) {
		struct drm_property_blob *blob;

		if (value == 0)
			return true;

		blob = drm_property_lookup_blob(property->dev, value);
		if (blob) {
			*ref = &blob->base;
			return true;
		} else {
			return false;
		}
	} else if (drm_property_type_is(property, DRM_MODE_PROP_OBJECT)) {
		/* a zero value for an object property translates to null: */
		if (value == 0)
			return true;

		*ref = __drm_mode_object_find(property->dev, value,
					      property->values[0]);
		return *ref != NULL;
	}

	for (i = 0; i < property->num_values; i++)
		if (property->values[i] == value)
			return true;
	return false;
}

void drm_property_change_valid_put(struct drm_property *property,
		struct drm_mode_object *ref)
{
	if (!ref)
		return;

	if (drm_property_type_is(property, DRM_MODE_PROP_OBJECT)) {
		drm_mode_object_put(ref);
	} else if (drm_property_type_is(property, DRM_MODE_PROP_BLOB))
		drm_property_blob_put(obj_to_blob(ref));
}
