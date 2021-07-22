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

static u32 object_max_page_size(struct drm_i915_gem_object *obj)
{
	u32 max_page_size = 0;
	int i;

	for (i = 0; i < obj->mm.n_placements; i++) {
		struct intel_memory_region *mr = obj->mm.placements[i];

		GEM_BUG_ON(!is_power_of_2(mr->min_page_size));
		max_page_size = max_t(u32, max_page_size, mr->min_page_size);
	}

	GEM_BUG_ON(!max_page_size);
	return max_page_size;
}

static void object_set_placements(struct drm_i915_gem_object *obj,
				  struct intel_memory_region **placements,
				  unsigned int n_placements)
{
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
		obj->mm.placements = placements;
		obj->mm.n_placements = n_placements;
	}
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

static int
i915_gem_setup(struct drm_i915_gem_object *obj, u64 size)
{
	struct intel_memory_region *mr = obj->mm.placements[0];
	unsigned int flags;
	int ret;

	size = round_up(size, object_max_page_size(obj));
	if (size == 0)
		return -EINVAL;

	/* For most of the ABI (e.g. mmap) we think in system pages */
	GEM_BUG_ON(!IS_ALIGNED(size, PAGE_SIZE));

	if (i915_gem_object_size_2big(size))
		return -E2BIG;

	/*
	 * For now resort to CPU based clearing for device local-memory, in the
	 * near future this will use the blitter engine for accelerated, GPU
	 * based clearing.
	 */
	flags = 0;
	if (mr->type == INTEL_MEMORY_LOCAL)
		flags = I915_BO_ALLOC_CPU_CLEAR;

	ret = mr->ops->init_object(mr, obj, size, flags);
	if (ret)
		return ret;

	GEM_BUG_ON(size != obj->base.size);

	trace_i915_gem_object_create(obj);
	return 0;
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
	int ret;

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

	obj = i915_gem_object_alloc();
	if (!obj)
		return -ENOMEM;

	mr = intel_memory_region_by_type(to_i915(dev), mem_type);
	object_set_placements(obj, &mr, 1);

	ret = i915_gem_setup(obj, args->size);
	if (ret)
		goto object_free;

	return i915_gem_publish(obj, file, &args->size, &args->handle);

object_free:
	i915_gem_object_free(obj);
	return ret;
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
	int ret;

	i915_gem_flush_free_objects(i915);

	obj = i915_gem_object_alloc();
	if (!obj)
		return -ENOMEM;

	mr = intel_memory_region_by_type(i915, INTEL_MEMORY_SYSTEM);
	object_set_placements(obj, &mr, 1);

	ret = i915_gem_setup(obj, args->size);
	if (ret)
		goto object_free;

	return i915_gem_publish(obj, file, &args->size, &args->handle);

object_free:
	i915_gem_object_free(obj);
	return ret;
}

struct create_ext {
	struct drm_i915_private *i915;
	struct drm_i915_gem_object *vanilla_object;
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
	struct drm_i915_gem_object *obj = ext_data->vanilla_object;
	struct intel_memory_region **placements;
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

	if (args->num_regions > ARRAY_SIZE(i915->mm.regions)) {
		drm_dbg(&i915->drm, "num_regions is too large\n");
		ret = -EINVAL;
	}

	if (ret)
		return ret;

	placements = kmalloc_array(args->num_regions,
				   sizeof(struct intel_memory_region *),
				   GFP_KERNEL);
	if (!placements)
		return -ENOMEM;

	mask = 0;
	for (i = 0; i < args->num_regions; i++) {
		struct drm_i915_gem_memory_class_instance region;
		struct intel_memory_region *mr;

		if (copy_from_user(&region, uregions, sizeof(region))) {
			ret = -EFAULT;
			goto out_free;
		}

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

	if (obj->mm.placements) {
		ret = -EINVAL;
		goto out_dump;
	}

	object_set_placements(obj, placements, args->num_regions);
	if (args->num_regions == 1)
		kfree(placements);

	return 0;

out_dump:
	if (1) {
		char buf[256];

		if (obj->mm.placements) {
			repr_placements(buf,
					sizeof(buf),
					obj->mm.placements,
					obj->mm.n_placements);
			drm_dbg(&i915->drm,
				"Placements were already set in previous EXT. Existing placements: %s\n",
				buf);
		}

		repr_placements(buf, sizeof(buf), placements, i);
		drm_dbg(&i915->drm, "New placements(so far validated): %s\n", buf);
	}

out_free:
	kfree(placements);
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
	struct intel_memory_region **placements_ext;
	struct drm_i915_gem_object *obj;
	int ret;

	if (args->flags)
		return -EINVAL;

	i915_gem_flush_free_objects(i915);

	obj = i915_gem_object_alloc();
	if (!obj)
		return -ENOMEM;

	ext_data.vanilla_object = obj;
	ret = i915_user_extensions(u64_to_user_ptr(args->extensions),
				   create_extensions,
				   ARRAY_SIZE(create_extensions),
				   &ext_data);
	placements_ext = obj->mm.placements;
	if (ret)
		goto object_free;

	if (!placements_ext) {
		struct intel_memory_region *mr =
			intel_memory_region_by_type(i915, INTEL_MEMORY_SYSTEM);

		object_set_placements(obj, &mr, 1);
	}

	ret = i915_gem_setup(obj, args->size);
	if (ret)
		goto object_free;

	return i915_gem_publish(obj, file, &args->size, &args->handle);

object_free:
	if (obj->mm.n_placements > 1)
		kfree(placements_ext);
	i915_gem_object_free(obj);
	return ret;
}
