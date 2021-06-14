// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "intel_memory_region.h"
#include "i915_drv.h"

static const struct {
	u16 class;
	u16 instance;
} intel_region_map[] = {
	[INTEL_REGION_SMEM] = {
		.class = INTEL_MEMORY_SYSTEM,
		.instance = 0,
	},
	[INTEL_REGION_LMEM] = {
		.class = INTEL_MEMORY_LOCAL,
		.instance = 0,
	},
	[INTEL_REGION_STOLEN_SMEM] = {
		.class = INTEL_MEMORY_STOLEN_SYSTEM,
		.instance = 0,
	},
	[INTEL_REGION_STOLEN_LMEM] = {
		.class = INTEL_MEMORY_STOLEN_LOCAL,
		.instance = 0,
	},
};

struct intel_region_reserve {
	struct list_head link;
	struct ttm_resource *res;
};

struct intel_memory_region *
intel_memory_region_lookup(struct drm_i915_private *i915,
			   u16 class, u16 instance)
{
	struct intel_memory_region *mr;
	int id;

	/* XXX: consider maybe converting to an rb tree at some point */
	for_each_memory_region(mr, i915, id) {
		if (mr->type == class && mr->instance == instance)
			return mr;
	}

	return NULL;
}

struct intel_memory_region *
intel_memory_region_by_type(struct drm_i915_private *i915,
			    enum intel_memory_type mem_type)
{
	struct intel_memory_region *mr;
	int id;

	for_each_memory_region(mr, i915, id)
		if (mr->type == mem_type)
			return mr;

	return NULL;
}

/**
 * intel_memory_region_unreserve - Unreserve all previously reserved
 * ranges
 * @mem: The region containing the reserved ranges.
 */
void intel_memory_region_unreserve(struct intel_memory_region *mem)
{
	struct intel_region_reserve *reserve, *next;

	if (!mem->priv_ops || !mem->priv_ops->free)
		return;

	mutex_lock(&mem->mm_lock);
	list_for_each_entry_safe(reserve, next, &mem->reserved, link) {
		list_del(&reserve->link);
		mem->priv_ops->free(mem, reserve->res);
		kfree(reserve);
	}
	mutex_unlock(&mem->mm_lock);
}

/**
 * intel_memory_region_reserve - Reserve a memory range
 * @mem: The region for which we want to reserve a range.
 * @offset: Start of the range to reserve.
 * @size: The size of the range to reserve.
 *
 * Return: 0 on success, negative error code on failure.
 */
int intel_memory_region_reserve(struct intel_memory_region *mem,
				resource_size_t offset,
				resource_size_t size)
{
	int ret;
	struct intel_region_reserve *reserve;

	if (!mem->priv_ops || !mem->priv_ops->reserve)
		return -EINVAL;

	reserve = kzalloc(sizeof(*reserve), GFP_KERNEL);
	if (!reserve)
		return -ENOMEM;

	reserve->res = mem->priv_ops->reserve(mem, offset, size);
	if (IS_ERR(reserve->res)) {
		ret = PTR_ERR(reserve->res);
		kfree(reserve);
		return ret;
	}

	mutex_lock(&mem->mm_lock);
	list_add_tail(&reserve->link, &mem->reserved);
	mutex_unlock(&mem->mm_lock);

	return 0;
}

struct intel_memory_region *
intel_memory_region_create(struct drm_i915_private *i915,
			   resource_size_t start,
			   resource_size_t size,
			   resource_size_t min_page_size,
			   resource_size_t io_start,
			   u16 type,
			   u16 instance,
			   const struct intel_memory_region_ops *ops)
{
	struct intel_memory_region *mem;
	int err;

	mem = kzalloc(sizeof(*mem), GFP_KERNEL);
	if (!mem)
		return ERR_PTR(-ENOMEM);

	mem->i915 = i915;
	mem->region = (struct resource)DEFINE_RES_MEM(start, size);
	mem->io_start = io_start;
	mem->min_page_size = min_page_size;
	mem->ops = ops;
	mem->total = size;
	mem->avail = mem->total;
	mem->type = type;
	mem->instance = instance;

	mutex_init(&mem->objects.lock);
	INIT_LIST_HEAD(&mem->objects.list);
	INIT_LIST_HEAD(&mem->objects.purgeable);
	INIT_LIST_HEAD(&mem->reserved);

	mutex_init(&mem->mm_lock);

	if (ops->init) {
		err = ops->init(mem);
		if (err)
			goto err_free;
	}

	kref_init(&mem->kref);
	return mem;

err_free:
	kfree(mem);
	return ERR_PTR(err);
}

void intel_memory_region_set_name(struct intel_memory_region *mem,
				  const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(mem->name, sizeof(mem->name), fmt, ap);
	va_end(ap);
}

static void __intel_memory_region_destroy(struct kref *kref)
{
	struct intel_memory_region *mem =
		container_of(kref, typeof(*mem), kref);

	intel_memory_region_unreserve(mem);
	if (mem->ops->release)
		mem->ops->release(mem);

	mutex_destroy(&mem->mm_lock);
	mutex_destroy(&mem->objects.lock);
	kfree(mem);
}

struct intel_memory_region *
intel_memory_region_get(struct intel_memory_region *mem)
{
	kref_get(&mem->kref);
	return mem;
}

void intel_memory_region_put(struct intel_memory_region *mem)
{
	kref_put(&mem->kref, __intel_memory_region_destroy);
}

/* Global memory region registration -- only slight layer inversions! */

int intel_memory_regions_hw_probe(struct drm_i915_private *i915)
{
	int err, i;

	for (i = 0; i < ARRAY_SIZE(i915->mm.regions); i++) {
		struct intel_memory_region *mem = ERR_PTR(-ENODEV);
		u16 type, instance;

		if (!HAS_REGION(i915, BIT(i)))
			continue;

		type = intel_region_map[i].class;
		instance = intel_region_map[i].instance;
		switch (type) {
		case INTEL_MEMORY_SYSTEM:
			mem = i915_gem_shmem_setup(i915, type, instance);
			break;
		case INTEL_MEMORY_STOLEN_LOCAL:
			mem = i915_gem_stolen_lmem_setup(i915, type, instance);
			if (!IS_ERR(mem))
				i915->mm.stolen_region = mem;
			break;
		case INTEL_MEMORY_STOLEN_SYSTEM:
			mem = i915_gem_stolen_smem_setup(i915, type, instance);
			if (!IS_ERR(mem))
				i915->mm.stolen_region = mem;
			break;
		default:
			continue;
		}

		if (IS_ERR(mem)) {
			err = PTR_ERR(mem);
			drm_err(&i915->drm,
				"Failed to setup region(%d) type=%d\n",
				err, type);
			goto out_cleanup;
		}

		mem->id = i;
		i915->mm.regions[i] = mem;
	}

	return 0;

out_cleanup:
	intel_memory_regions_driver_release(i915);
	return err;
}

void intel_memory_regions_driver_release(struct drm_i915_private *i915)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(i915->mm.regions); i++) {
		struct intel_memory_region *region =
			fetch_and_zero(&i915->mm.regions[i]);

		if (region)
			intel_memory_region_put(region);
	}
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/intel_memory_region.c"
#include "selftests/mock_region.c"
#endif
