// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include "gem/i915_gem_ioctls.h"
#include "gem/i915_gem_lmem.h"
#include "gem/i915_gem_region.h"

#include "i915_drv.h"
#include "i915_trace.h"
#include "i915_user_extensions.h"

static u32 object_max_page_size(struct intel_memory_region **placements,
				unsigned int n_placements)
{
	u32 max_page_size = 0;
	int i;

	for (i = 0; i < n_placements; i++) {
		struct intel_memory_region *mr = placements[i];

		GEM_BUG_ON(!is_power_of_2(mr->min_page_size));
		max_page_size = max_t(u32, max_page_size, mr->min_page_size);
	}

	GEM_BUG_ON(!max_page_size);
	return max_page_size;
}

static int object_set_placements(struct drm_i915_gem_object *obj,
				 struct intel_memory_region **placements,
				 unsigned int n_placements)
{
	struct intel_memory_region **arr;
	unsigned int i;

	GEM_BUG_ON(!n_placements);

	/*
	 * For the common case of one memory region, skip storing an
	 * allocated array and just point at the region directly.
	 */
	if (n_placements == 1) {
		struct intel_memory_region *mr = placements[0];
		struct drm_i915_private *i915 = mr->i915;

		obj->mm.placements = &i915->mm.regions[mr->id];
		obj->mm.n_placements = 1;
	} else {
		arr = kmalloc_array(n_placements,
				    sizeof(struct intel_memory_region *),
				    GFP_KERNEL);
		if (!arr)
			return -ENOMEM;

		for (i = 0; i < n_placements; i++)
			arr[i] = placements[i];

		obj->mm.placements = arr;
		obj->mm.n_placements = n_placements;
	}

	return 0;
}

static int i915_gem_publish(struct drm_i915_gem_object *obj,
			    struct drm_file *file,
			    u64 *size_p,
			    u32 *handle_p)
{
	u64 size = obj->base.size;
	int ret;

	ret = drm_gem_handle_create(file, &obj->base, handle_p);
	/* drop reference from allocate - handle holds it now */
	i915_gem_object_put(obj);
	if (ret)
		return ret;

	*size_p = size;
	return 0;
}

/**
 * Creates a new object using the same path as DRM_I915_GEM_CREATE_EXT
 * @i915: i915 private
 * @size: size of the buffer, in bytes
 * @placements: possible placement regions, in priority order
 * @n_placements: number of possible placement regions
 *
 * This function is exposed primarily for selftests and does very little
 * error checking.  It is assumed that the set of placement regions has
 * already been verified to be valid.
 */
struct drm_i915_gem_object *
__i915_gem_object_create_user(struct drm_i915_private *i915, u64 size,
			      struct intel_memory_region **placements,
			      unsigned int n_placements)
{
	struct intel_memory_region *mr = placements[0];
	struct drm_i915_gem_object *obj;
	unsigned int flags;
	int ret;

	i915_gem_flush_free_objects(i915);

	size = round_up(size, object_max_page_size(placements, n_placements));
	if (size == 0)
		return ERR_PTR(-EINVAL);

	/* For most of the ABI (e.g. mmap) we think in system pages */
	GEM_BUG_ON(!IS_ALIGNED(size, PAGE_SIZE));

	if (i915_gem_object_size_2big(size))
		return ERR_PTR(-E2BIG);

	obj = i915_gem_object_alloc();
	if (!obj)
		return ERR_PTR(-ENOMEM);

	ret = object_set_placements(obj, placements, n_placements);
	if (ret)
		goto object_free;

	/*
	 * I915_BO_ALLOC_USER will make sure the object is cleared before
	 * any user access.
	 */
	flags = I915_BO_ALLOC_USER;

	ret = mr->ops->init_object(mr, obj, size, 0, flags);
	if (ret)
		goto object_free;

	GEM_BUG_ON(size != obj->base.size);

	trace_i915_gem_object_create(obj);
	return obj;

object_free:
	if (obj->mm.n_placements > 1)
		kfree(obj->mm.placements);
	i915_gem_object_free(obj);
	return ERR_PTR(ret);
}

int
i915_gem_dumb_create(struct drm_file *file,
		     struct drm_device *dev,
		     struct drm_mode_create_dumb *args)
{
	struct drm_i915_gem_object *obj;
	struct intel_memory_region *mr;
	enum intel_memory_type mem_type;
	int cpp = DIV_ROUND_UP(args->bpp, 8);
	u32 format;

	switch (cpp) {
	case 1:
		format = DRM_FORMAT_C8;
		break;
	case 2:
		format = DRM_FORMAT_RGB565;
		break;
	case 4:
		format = DRM_FORMAT_XRGB8888;
		break;
	default:
		return -EINVAL;
	}

	/* have to work out size/pitch and return them */
	args->pitch = ALIGN(args->width * cpp, 64);

	/* align stride to page size so that we can remap */
	if (args->pitch > intel_plane_fb_max_stride(to_i915(dev), format,
						    DRM_FORMAT_MOD_LINEAR))
		args->pitch = ALIGN(args->pitch, 4096);

	if (args->pitch < args->width)
		return -EINVAL;

	args->size = mul_u32_u32(args->pitch, args->height);

	mem_type = INTEL_MEMORY_SYSTEM;
	if (HAS_LMEM(to_i915(dev)))
		mem_type = INTEL_MEMORY_LOCAL;

	mr = intel_memory_region_by_type(to_i915(dev), mem_type);

	obj = __i915_gem_object_create_user(to_i915(dev), args->size, &mr, 1);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	return i915_gem_publish(obj, file, &args->size, &args->handle);
}

/**
 * Creates a new mm object and returns a handle to it.
 * @dev: drm device pointer
 * @data: ioctl data blob
 * @file: drm file pointer
 */
int
i915_gem_create_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file)
{
	struct drm_i915_private *i915 = to_i915(dev);
	struct drm_i915_gem_create *args = data;
	struct drm_i915_gem_object *obj;
	struct intel_memory_region *mr;

	mr = intel_memory_region_by_type(i915, INTEL_MEMORY_SYSTEM);

	obj = __i915_gem_object_create_user(i915, args->size, &mr, 1);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	return i915_gem_publish(obj, file, &args->size, &args->handle);
}

struct create_ext {
	struct drm_i915_private *i915;
	struct intel_memory_region *placements[INTEL_REGION_UNKNOWN];
	unsigned int n_placements;
};

static void repr_placements(char *buf, size_t size,
			    struct intel_memory_region **placements,
			    int n_placements)
{
	int i;

	buf[0] = '\0';

	for (i = 0; i < n_placements; i++) {
		struct intel_memory_region *mr = placements[i];
		int r;

		r = snprintf(buf, size, "\n  %s -> { class: %d, inst: %d }",
			     mr->name, mr->type, mr->instance);
		if (r >= size)
			return;

		buf += r;
		size -= r;
	}
}

static int set_placements(struct drm_i915_gem_create_ext_memory_regions *args,
			  struct create_ext *ext_data)
{
	struct drm_i915_private *i915 = ext_data->i915;
	struct drm_i915_gem_memory_class_instance __user *uregions =
		u64_to_user_ptr(args->regions);
	struct intel_memory_region *placements[INTEL_REGION_UNKNOWN];
	u32 mask;
	int i, ret = 0;

	if (args->pad) {
		drm_dbg(&i915->drm, "pad should be zero\n");
		ret = -EINVAL;
	}

	if (!args->num_regions) {
		drm_dbg(&i915->drm, "num_regions is zero\n");
		ret = -EINVAL;
	}

	BUILD_BUG_ON(ARRAY_SIZE(i915->mm.regions) != ARRAY_SIZE(placements));
	BUILD_BUG_ON(ARRAY_SIZE(ext_data->placements) != ARRAY_SIZE(placements));
	if (args->num_regions > ARRAY_SIZE(i915->mm.regions)) {
		drm_dbg(&i915->drm, "num_regions is too large\n");
		ret = -EINVAL;
	}

	if (ret)
		return ret;

	mask = 0;
	for (i = 0; i < args->num_regions; i++) {
		struct drm_i915_gem_memory_class_instance region;
		struct intel_memory_region *mr;

		if (copy_from_user(&region, uregions, sizeof(region)))
			return -EFAULT;

		mr = intel_memory_region_lookup(i915,
						region.memory_class,
						region.memory_instance);
		if (!mr || mr->private) {
			drm_dbg(&i915->drm, "Device is missing region { class: %d, inst: %d } at index = %d\n",
				region.memory_class, region.memory_instance, i);
			ret = -EINVAL;
			goto out_dump;
		}

		if (mask & BIT(mr->id)) {
			drm_dbg(&i915->drm, "Found duplicate placement %s -> { class: %d, inst: %d } at index = %d\n",
				mr->name, region.memory_class,
				region.memory_instance, i);
			ret = -EINVAL;
			goto out_dump;
		}

		placements[i] = mr;
		mask |= BIT(mr->id);

		++uregions;
	}

	if (ext_data->n_placements) {
		ret = -EINVAL;
		goto out_dump;
	}

	ext_data->n_placements = args->num_regions;
	for (i = 0; i < args->num_regions; i++)
		ext_data->placements[i] = placements[i];

	return 0;

out_dump:
	if (1) {
		char buf[256];

		if (ext_data->n_placements) {
			repr_placements(buf,
					sizeof(buf),
					ext_data->placements,
					ext_data->n_placements);
			drm_dbg(&i915->drm,
				"Placements were already set in previous EXT. Existing placements: %s\n",
				buf);
		}

		repr_placements(buf, sizeof(buf), placements, i);
		drm_dbg(&i915->drm, "New placements(so far validated): %s\n", buf);
	}

	return ret;
}

static int ext_set_placements(struct i915_user_extension __user *base,
			      void *data)
{
	struct drm_i915_gem_create_ext_memory_regions ext;

	if (!IS_ENABLED(CONFIG_DRM_I915_UNSTABLE_FAKE_LMEM))
		return -ENODEV;

	if (copy_from_user(&ext, base, sizeof(ext)))
		return -EFAULT;

	return set_placements(&ext, data);
}

static const i915_user_extension_fn create_extensions[] = {
	[I915_GEM_CREATE_EXT_MEMORY_REGIONS] = ext_set_placements,
};

/**
 * Creates a new mm object and returns a handle to it.
 * @dev: drm device pointer
 * @data: ioctl data blob
 * @file: drm file pointer
 */
int
i915_gem_create_ext_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file)
{
	struct drm_i915_private *i915 = to_i915(dev);
	struct drm_i915_gem_create_ext *args = data;
	struct create_ext ext_data = { .i915 = i915 };
	struct drm_i915_gem_object *obj;
	int ret;

	if (args->flags)
		return -EINVAL;

	ret = i915_user_extensions(u64_to_user_ptr(args->extensions),
				   create_extensions,
				   ARRAY_SIZE(create_extensions),
				   &ext_data);
	if (ret)
		return ret;

	if (!ext_data.n_placements) {
		ext_data.placements[0] =
			intel_memory_region_by_type(i915, INTEL_MEMORY_SYSTEM);
		ext_data.n_placements = 1;
	}

	obj = __i915_gem_object_create_user(i915, args->size,
					    ext_data.placements,
					    ext_data.n_placements);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	return i915_gem_publish(obj, file, &args->size, &args->handle);
}
