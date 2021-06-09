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
};

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

static u64
intel_memory_region_free_pages(struct intel_memory_region *mem,
			       struct list_head *blocks)
{
	struct i915_buddy_block *block, *on;
	u64 size = 0;

	list_for_each_entry_safe(block, on, blocks, link) {
		size += i915_buddy_block_size(&mem->mm, block);
		i915_buddy_free(&mem->mm, block);
	}
	INIT_LIST_HEAD(blocks);

	return size;
}

void
__intel_memory_region_put_pages_buddy(struct intel_memory_region *mem,
				      struct list_head *blocks)
{
	mutex_lock(&mem->mm_lock);
	mem->avail += intel_memory_region_free_pages(mem, blocks);
	mutex_unlock(&mem->mm_lock);
}

void
__intel_memory_region_put_block_buddy(struct i915_buddy_block *block)
{
	struct list_head blocks;

	INIT_LIST_HEAD(&blocks);
	list_add(&block->link, &blocks);
	__intel_memory_region_put_pages_buddy(block->private, &blocks);
}

int
__intel_memory_region_get_pages_buddy(struct intel_memory_region *mem,
				      resource_size_t size,
				      unsigned int flags,
				      struct list_head *blocks)
{
	unsigned int min_order = 0;
	unsigned long n_pages;

	GEM_BUG_ON(!IS_ALIGNED(size, mem->mm.chunk_size));
	GEM_BUG_ON(!list_empty(blocks));

	if (flags & I915_ALLOC_MIN_PAGE_SIZE) {
		min_order = ilog2(mem->min_page_size) -
			    ilog2(mem->mm.chunk_size);
	}

	if (flags & I915_ALLOC_CONTIGUOUS) {
		size = roundup_pow_of_two(size);
		min_order = ilog2(size) - ilog2(mem->mm.chunk_size);
	}

	if (size > mem->mm.size)
		return -E2BIG;

	n_pages = size >> ilog2(mem->mm.chunk_size);

	mutex_lock(&mem->mm_lock);

	do {
		struct i915_buddy_block *block;
		unsigned int order;

		order = fls(n_pages) - 1;
		GEM_BUG_ON(order > mem->mm.max_order);
		GEM_BUG_ON(order < min_order);

		do {
			block = i915_buddy_alloc(&mem->mm, order);
			if (!IS_ERR(block))
				break;

			if (order-- == min_order)
				goto err_free_blocks;
		} while (1);

		n_pages -= BIT(order);

		block->private = mem;
		list_add_tail(&block->link, blocks);

		if (!n_pages)
			break;
	} while (1);

	mem->avail -= size;
	mutex_unlock(&mem->mm_lock);
	return 0;

err_free_blocks:
	intel_memory_region_free_pages(mem, blocks);
	mutex_unlock(&mem->mm_lock);
	return -ENXIO;
}

struct i915_buddy_block *
__intel_memory_region_get_block_buddy(struct intel_memory_region *mem,
				      resource_size_t size,
				      unsigned int flags)
{
	struct i915_buddy_block *block;
	LIST_HEAD(blocks);
	int ret;

	ret = __intel_memory_region_get_pages_buddy(mem, size, flags, &blocks);
	if (ret)
		return ERR_PTR(ret);

	block = list_first_entry(&blocks, typeof(*block), link);
	list_del_init(&block->link);
	return block;
}

int intel_memory_region_init_buddy(struct intel_memory_region *mem)
{
	return i915_buddy_init(&mem->mm, resource_size(&mem->region),
			       PAGE_SIZE);
}

void intel_memory_region_release_buddy(struct intel_memory_region *mem)
{
	i915_buddy_free_list(&mem->mm, &mem->reserved);
	i915_buddy_fini(&mem->mm);
}

int intel_memory_region_reserve(struct intel_memory_region *mem,
				u64 offset, u64 size)
{
	int ret;

	mutex_lock(&mem->mm_lock);
	ret = i915_buddy_alloc_range(&mem->mm, &mem->reserved, offset, size);
	mutex_unlock(&mem->mm_lock);

	return ret;
}

struct intel_memory_region *
intel_memory_region_create(struct drm_i915_private *i915,
			   resource_size_t start,
			   resource_size_t size,
			   resource_size_t min_page_size,
			   resource_size_t io_start,
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
			mem = i915_gem_shmem_setup(i915);
			break;
		case INTEL_MEMORY_STOLEN_SYSTEM:
			mem = i915_gem_stolen_setup(i915);
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
		mem->type = type;
		mem->instance = instance;

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
