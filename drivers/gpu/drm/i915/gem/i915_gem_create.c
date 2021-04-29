// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include "gem/i915_gem_ioctls.h"
#include "gem/i915_gem_region.h"

#include "i915_drv.h"
#include "i915_trace.h"

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
i915_gem_setup(struct drm_i915_gem_object *obj,
	       struct intel_memory_region *mr,
	       u64 size)
{
	int ret;

	GEM_BUG_ON(!is_power_of_2(mr->min_page_size));
	size = round_up(size, mr->min_page_size);
	if (size == 0)
		return -EINVAL;

	/* For most of the ABI (e.g. mmap) we think in system pages */
	GEM_BUG_ON(!IS_ALIGNED(size, PAGE_SIZE));

	if (i915_gem_object_size_2big(size))
		return -E2BIG;

	ret = mr->ops->init_object(mr, obj, size, 0);
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

	ret = i915_gem_setup(obj,
			     intel_memory_region_by_type(to_i915(dev),
							 mem_type),
			     args->size);
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
	int ret;

	i915_gem_flush_free_objects(i915);

	obj = i915_gem_object_alloc();
	if (!obj)
		return -ENOMEM;

	ret = i915_gem_setup(obj,
			     intel_memory_region_by_type(i915,
							 INTEL_MEMORY_SYSTEM),
			     args->size);
	if (ret)
		goto object_free;

	return i915_gem_publish(obj, file, &args->size, &args->handle);

object_free:
	i915_gem_object_free(obj);
	return ret;
}
