// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <linux/prandom.h>

#include <uapi/drm/i915_drm.h>

#include "intel_memory_region.h"
#include "i915_drv.h"
#include "i915_ttm_buddy_manager.h"

static const struct {
	u16 class;
	u16 instance;
} intel_region_map[] = {
	[INTEL_REGION_SMEM] = {
		.class = INTEL_MEMORY_SYSTEM,
		.instance = 0,
	},
	[INTEL_REGION_LMEM_0] = {
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

static int __iopagetest(struct intel_memory_region *mem,
			u8 __iomem *va, int pagesize,
			u8 value, resource_size_t offset,
			const void *caller)
{
	int byte = get_random_u32_below(pagesize);
	u8 result[3];

	memset_io(va, value, pagesize); /* or GPF! */
	wmb();

	result[0] = ioread8(va);
	result[1] = ioread8(va + byte);
	result[2] = ioread8(va + pagesize - 1);
	if (memchr_inv(result, value, sizeof(result))) {
		dev_err(mem->i915->drm.dev,
			"Failed to read back from memory region:%pR at [%pa + %pa] for %ps; wrote %x, read (%x, %x, %x)\n",
			&mem->region, &mem->io_start, &offset, caller,
			value, result[0], result[1], result[2]);
		return -EINVAL;
	}

	return 0;
}

static int iopagetest(struct intel_memory_region *mem,
		      resource_size_t offset,
		      const void *caller)
{
	const u8 val[] = { 0x0, 0xa5, 0xc3, 0xf0 };
	void __iomem *va;
	int err;
	int i;

	va = ioremap_wc(mem->io_start + offset, PAGE_SIZE);
	if (!va) {
		dev_err(mem->i915->drm.dev,
			"Failed to ioremap memory region [%pa + %pa] for %ps\n",
			&mem->io_start, &offset, caller);
		return -EFAULT;
	}

	for (i = 0; i < ARRAY_SIZE(val); i++) {
		err = __iopagetest(mem, va, PAGE_SIZE, val[i], offset, caller);
		if (err)
			break;

		err = __iopagetest(mem, va, PAGE_SIZE, ~val[i], offset, caller);
		if (err)
			break;
	}

	iounmap(va);
	return err;
}

static resource_size_t random_page(resource_size_t last)
{
	/* Limited to low 44b (16TiB), but should suffice for a spot check */
	return get_random_u32_below(last >> PAGE_SHIFT) << PAGE_SHIFT;
}

static int iomemtest(struct intel_memory_region *mem,
		     bool test_all,
		     const void *caller)
{
	resource_size_t last, page;
	int err;

	if (mem->io_size < PAGE_SIZE)
		return 0;

	last = mem->io_size - PAGE_SIZE;

	/*
	 * Quick test to check read/write access to the iomap (backing store).
	 *
	 * Write a byte, read it back. If the iomapping fails, we expect
	 * a GPF preventing further execution. If the backing store does not
	 * exist, the read back will return garbage. We check a couple of pages,
	 * the first and last of the specified region to confirm the backing
	 * store + iomap does cover the entire memory region; and we check
	 * a random offset within as a quick spot check for bad memory.
	 */

	if (test_all) {
		for (page = 0; page <= last; page += PAGE_SIZE) {
			err = iopagetest(mem, page, caller);
			if (err)
				return err;
		}
	} else {
		err = iopagetest(mem, 0, caller);
		if (err)
			return err;

		err = iopagetest(mem, last, caller);
		if (err)
			return err;

		err = iopagetest(mem, random_page(last), caller);
		if (err)
			return err;
	}

	return 0;
}

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
	struct ttm_resource_manager *man = mem->region_private;

	GEM_BUG_ON(mem->is_range_manager);

	return i915_ttm_buddy_man_reserve(man, offset, size);
}

void intel_memory_region_debug(struct intel_memory_region *mr,
			       struct drm_printer *printer)
{
	drm_printf(printer, "%s: ", mr->name);

	if (mr->region_private)
		ttm_resource_manager_debug(mr->region_private, printer);
	else
		drm_printf(printer, "total:%pa bytes\n", &mr->total);
}

static int intel_memory_region_memtest(struct intel_memory_region *mem,
				       void *caller)
{
	struct drm_i915_private *i915 = mem->i915;
	int err = 0;

	if (!mem->io_start)
		return 0;

	if (IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM) || i915->params.memtest)
		err = iomemtest(mem, i915->params.memtest, caller);

	return err;
}

static const char *region_type_str(u16 type)
{
	switch (type) {
	case INTEL_MEMORY_SYSTEM:
		return "system";
	case INTEL_MEMORY_LOCAL:
		return "local";
	case INTEL_MEMORY_STOLEN_LOCAL:
		return "stolen-local";
	case INTEL_MEMORY_STOLEN_SYSTEM:
		return "stolen-system";
	default:
		return "unknown";
	}
}

struct intel_memory_region *
intel_memory_region_create(struct drm_i915_private *i915,
			   resource_size_t start,
			   resource_size_t size,
			   resource_size_t min_page_size,
			   resource_size_t io_start,
			   resource_size_t io_size,
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
	mem->region = DEFINE_RES_MEM(start, size);
	mem->io_start = io_start;
	mem->io_size = io_size;
	mem->min_page_size = min_page_size;
	mem->ops = ops;
	mem->total = size;
	mem->type = type;
	mem->instance = instance;

	snprintf(mem->uabi_name, sizeof(mem->uabi_name), "%s%u",
		 region_type_str(type), instance);

	mutex_init(&mem->objects.lock);
	INIT_LIST_HEAD(&mem->objects.list);

	if (ops->init) {
		err = ops->init(mem);
		if (err)
			goto err_free;
	}

	err = intel_memory_region_memtest(mem, (void *)_RET_IP_);
	if (err)
		goto err_release;

	return mem;

err_release:
	if (mem->ops->release)
		mem->ops->release(mem);
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

void intel_memory_region_avail(struct intel_memory_region *mr,
			       u64 *avail, u64 *visible_avail)
{
	if (mr->type == INTEL_MEMORY_LOCAL) {
		i915_ttm_buddy_man_avail(mr->region_private,
					 avail, visible_avail);
		*avail <<= PAGE_SHIFT;
		*visible_avail <<= PAGE_SHIFT;
	} else {
		*avail = mr->total;
		*visible_avail = mr->total;
	}
}

void intel_memory_region_destroy(struct intel_memory_region *mem)
{
	int ret = 0;

	if (mem->ops->release)
		ret = mem->ops->release(mem);

	GEM_WARN_ON(!list_empty_careful(&mem->objects.list));
	mutex_destroy(&mem->objects.lock);
	if (!ret)
		kfree(mem);
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
			if (IS_DGFX(i915))
				mem = i915_gem_ttm_system_setup(i915, type,
								instance);
			else
				mem = i915_gem_shmem_setup(i915, type,
							   instance);
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
			intel_memory_region_destroy(region);
	}
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/intel_memory_region.c"
#include "selftests/mock_region.c"
#endif
