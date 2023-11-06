// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <linux/prime_numbers.h>
#include <linux/sort.h>

#include <drm/drm_buddy.h>

#include "../i915_selftest.h"

#include "mock_drm.h"
#include "mock_gem_device.h"
#include "mock_region.h"

#include "gem/i915_gem_context.h"
#include "gem/i915_gem_lmem.h"
#include "gem/i915_gem_region.h"
#include "gem/i915_gem_ttm.h"
#include "gem/selftests/igt_gem_utils.h"
#include "gem/selftests/mock_context.h"
#include "gt/intel_engine_pm.h"
#include "gt/intel_engine_user.h"
#include "gt/intel_gt.h"
#include "gt/intel_migrate.h"
#include "i915_memcpy.h"
#include "i915_ttm_buddy_manager.h"
#include "selftests/igt_flush_test.h"
#include "selftests/i915_random.h"

static void close_objects(struct intel_memory_region *mem,
			  struct list_head *objects)
{
	struct drm_i915_private *i915 = mem->i915;
	struct drm_i915_gem_object *obj, *on;

	list_for_each_entry_safe(obj, on, objects, st_link) {
		i915_gem_object_lock(obj, NULL);
		if (i915_gem_object_has_pinned_pages(obj))
			i915_gem_object_unpin_pages(obj);
		/* No polluting the memory region between tests */
		__i915_gem_object_put_pages(obj);
		i915_gem_object_unlock(obj);
		list_del(&obj->st_link);
		i915_gem_object_put(obj);
	}

	cond_resched();

	i915_gem_drain_freed_objects(i915);
}

static int igt_mock_fill(void *arg)
{
	struct intel_memory_region *mem = arg;
	resource_size_t total = resource_size(&mem->region);
	resource_size_t page_size;
	resource_size_t rem;
	unsigned long max_pages;
	unsigned long page_num;
	LIST_HEAD(objects);
	int err = 0;

	page_size = PAGE_SIZE;
	max_pages = div64_u64(total, page_size);
	rem = total;

	for_each_prime_number_from(page_num, 1, max_pages) {
		resource_size_t size = page_num * page_size;
		struct drm_i915_gem_object *obj;

		obj = i915_gem_object_create_region(mem, size, 0, 0);
		if (IS_ERR(obj)) {
			err = PTR_ERR(obj);
			break;
		}

		err = i915_gem_object_pin_pages_unlocked(obj);
		if (err) {
			i915_gem_object_put(obj);
			break;
		}

		list_add(&obj->st_link, &objects);
		rem -= size;
	}

	if (err == -ENOMEM)
		err = 0;
	if (err == -ENXIO) {
		if (page_num * page_size <= rem) {
			pr_err("%s failed, space still left in region\n",
			       __func__);
			err = -EINVAL;
		} else {
			err = 0;
		}
	}

	close_objects(mem, &objects);

	return err;
}

static struct drm_i915_gem_object *
igt_object_create(struct intel_memory_region *mem,
		  struct list_head *objects,
		  u64 size,
		  unsigned int flags)
{
	struct drm_i915_gem_object *obj;
	int err;

	obj = i915_gem_object_create_region(mem, size, 0, flags);
	if (IS_ERR(obj))
		return obj;

	err = i915_gem_object_pin_pages_unlocked(obj);
	if (err)
		goto put;

	list_add(&obj->st_link, objects);
	return obj;

put:
	i915_gem_object_put(obj);
	return ERR_PTR(err);
}

static void igt_object_release(struct drm_i915_gem_object *obj)
{
	i915_gem_object_lock(obj, NULL);
	i915_gem_object_unpin_pages(obj);
	__i915_gem_object_put_pages(obj);
	i915_gem_object_unlock(obj);
	list_del(&obj->st_link);
	i915_gem_object_put(obj);
}

static bool is_contiguous(struct drm_i915_gem_object *obj)
{
	struct scatterlist *sg;
	dma_addr_t addr = -1;

	for (sg = obj->mm.pages->sgl; sg; sg = sg_next(sg)) {
		if (addr != -1 && sg_dma_address(sg) != addr)
			return false;

		addr = sg_dma_address(sg) + sg_dma_len(sg);
	}

	return true;
}

static int igt_mock_reserve(void *arg)
{
	struct intel_memory_region *mem = arg;
	struct drm_i915_private *i915 = mem->i915;
	resource_size_t avail = resource_size(&mem->region);
	struct drm_i915_gem_object *obj;
	const u32 chunk_size = SZ_32M;
	u32 i, offset, count, *order;
	u64 allocated, cur_avail;
	I915_RND_STATE(prng);
	LIST_HEAD(objects);
	int err = 0;

	count = avail / chunk_size;
	order = i915_random_order(count, &prng);
	if (!order)
		return 0;

	mem = mock_region_create(i915, 0, SZ_2G, I915_GTT_PAGE_SIZE_4K, 0, 0);
	if (IS_ERR(mem)) {
		pr_err("failed to create memory region\n");
		err = PTR_ERR(mem);
		goto out_free_order;
	}

	/* Reserve a bunch of ranges within the region */
	for (i = 0; i < count; ++i) {
		u64 start = order[i] * chunk_size;
		u64 size = i915_prandom_u32_max_state(chunk_size, &prng);

		/* Allow for some really big holes */
		if (!size)
			continue;

		size = round_up(size, PAGE_SIZE);
		offset = igt_random_offset(&prng, 0, chunk_size, size,
					   PAGE_SIZE);

		err = intel_memory_region_reserve(mem, start + offset, size);
		if (err) {
			pr_err("%s failed to reserve range", __func__);
			goto out_close;
		}

		/* XXX: maybe sanity check the block range here? */
		avail -= size;
	}

	/* Try to see if we can allocate from the remaining space */
	allocated = 0;
	cur_avail = avail;
	do {
		u32 size = i915_prandom_u32_max_state(cur_avail, &prng);

		size = max_t(u32, round_up(size, PAGE_SIZE), PAGE_SIZE);
		obj = igt_object_create(mem, &objects, size, 0);
		if (IS_ERR(obj)) {
			if (PTR_ERR(obj) == -ENXIO)
				break;

			err = PTR_ERR(obj);
			goto out_close;
		}
		cur_avail -= size;
		allocated += size;
	} while (1);

	if (allocated != avail) {
		pr_err("%s mismatch between allocation and free space", __func__);
		err = -EINVAL;
	}

out_close:
	close_objects(mem, &objects);
	intel_memory_region_destroy(mem);
out_free_order:
	kfree(order);
	return err;
}

static int igt_mock_contiguous(void *arg)
{
	struct intel_memory_region *mem = arg;
	struct drm_i915_gem_object *obj;
	unsigned long n_objects;
	LIST_HEAD(objects);
	LIST_HEAD(holes);
	I915_RND_STATE(prng);
	resource_size_t total;
	resource_size_t min;
	u64 target;
	int err = 0;

	total = resource_size(&mem->region);

	/* Min size */
	obj = igt_object_create(mem, &objects, PAGE_SIZE,
				I915_BO_ALLOC_CONTIGUOUS);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	if (!is_contiguous(obj)) {
		pr_err("%s min object spans disjoint sg entries\n", __func__);
		err = -EINVAL;
		goto err_close_objects;
	}

	igt_object_release(obj);

	/* Max size */
	obj = igt_object_create(mem, &objects, total, I915_BO_ALLOC_CONTIGUOUS);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	if (!is_contiguous(obj)) {
		pr_err("%s max object spans disjoint sg entries\n", __func__);
		err = -EINVAL;
		goto err_close_objects;
	}

	igt_object_release(obj);

	/* Internal fragmentation should not bleed into the object size */
	target = i915_prandom_u64_state(&prng);
	div64_u64_rem(target, total, &target);
	target = round_up(target, PAGE_SIZE);
	target = max_t(u64, PAGE_SIZE, target);

	obj = igt_object_create(mem, &objects, target,
				I915_BO_ALLOC_CONTIGUOUS);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	if (obj->base.size != target) {
		pr_err("%s obj->base.size(%zx) != target(%llx)\n", __func__,
		       obj->base.size, target);
		err = -EINVAL;
		goto err_close_objects;
	}

	if (!is_contiguous(obj)) {
		pr_err("%s object spans disjoint sg entries\n", __func__);
		err = -EINVAL;
		goto err_close_objects;
	}

	igt_object_release(obj);

	/*
	 * Try to fragment the address space, such that half of it is free, but
	 * the max contiguous block size is SZ_64K.
	 */

	target = SZ_64K;
	n_objects = div64_u64(total, target);

	while (n_objects--) {
		struct list_head *list;

		if (n_objects % 2)
			list = &holes;
		else
			list = &objects;

		obj = igt_object_create(mem, list, target,
					I915_BO_ALLOC_CONTIGUOUS);
		if (IS_ERR(obj)) {
			err = PTR_ERR(obj);
			goto err_close_objects;
		}
	}

	close_objects(mem, &holes);

	min = target;
	target = total >> 1;

	/* Make sure we can still allocate all the fragmented space */
	obj = igt_object_create(mem, &objects, target, 0);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		goto err_close_objects;
	}

	igt_object_release(obj);

	/*
	 * Even though we have enough free space, we don't have a big enough
	 * contiguous block. Make sure that holds true.
	 */

	do {
		bool should_fail = target > min;

		obj = igt_object_create(mem, &objects, target,
					I915_BO_ALLOC_CONTIGUOUS);
		if (should_fail != IS_ERR(obj)) {
			pr_err("%s target allocation(%llx) mismatch\n",
			       __func__, target);
			err = -EINVAL;
			goto err_close_objects;
		}

		target >>= 1;
	} while (target >= PAGE_SIZE);

err_close_objects:
	list_splice_tail(&holes, &objects);
	close_objects(mem, &objects);
	return err;
}

static int igt_mock_splintered_region(void *arg)
{
	struct intel_memory_region *mem = arg;
	struct drm_i915_private *i915 = mem->i915;
	struct i915_ttm_buddy_resource *res;
	struct drm_i915_gem_object *obj;
	struct drm_buddy *mm;
	unsigned int expected_order;
	LIST_HEAD(objects);
	u64 size;
	int err = 0;

	/*
	 * Sanity check we can still allocate everything even if the
	 * mm.max_order != mm.size. i.e our starting address space size is not a
	 * power-of-two.
	 */

	size = (SZ_4G - 1) & PAGE_MASK;
	mem = mock_region_create(i915, 0, size, PAGE_SIZE, 0, 0);
	if (IS_ERR(mem))
		return PTR_ERR(mem);

	obj = igt_object_create(mem, &objects, size, 0);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		goto out_close;
	}

	res = to_ttm_buddy_resource(obj->mm.res);
	mm = res->mm;
	if (mm->size != size) {
		pr_err("%s size mismatch(%llu != %llu)\n",
		       __func__, mm->size, size);
		err = -EINVAL;
		goto out_put;
	}

	expected_order = get_order(rounddown_pow_of_two(size));
	if (mm->max_order != expected_order) {
		pr_err("%s order mismatch(%u != %u)\n",
		       __func__, mm->max_order, expected_order);
		err = -EINVAL;
		goto out_put;
	}

	close_objects(mem, &objects);

	/*
	 * While we should be able allocate everything without any flag
	 * restrictions, if we consider I915_BO_ALLOC_CONTIGUOUS then we are
	 * actually limited to the largest power-of-two for the region size i.e
	 * max_order, due to the inner workings of the buddy allocator. So make
	 * sure that does indeed hold true.
	 */

	obj = igt_object_create(mem, &objects, size, I915_BO_ALLOC_CONTIGUOUS);
	if (!IS_ERR(obj)) {
		pr_err("%s too large contiguous allocation was not rejected\n",
		       __func__);
		err = -EINVAL;
		goto out_close;
	}

	obj = igt_object_create(mem, &objects, rounddown_pow_of_two(size),
				I915_BO_ALLOC_CONTIGUOUS);
	if (IS_ERR(obj)) {
		pr_err("%s largest possible contiguous allocation failed\n",
		       __func__);
		err = PTR_ERR(obj);
		goto out_close;
	}

out_close:
	close_objects(mem, &objects);
out_put:
	intel_memory_region_destroy(mem);
	return err;
}

#ifndef SZ_8G
#define SZ_8G BIT_ULL(33)
#endif

static int igt_mock_max_segment(void *arg)
{
	struct intel_memory_region *mem = arg;
	struct drm_i915_private *i915 = mem->i915;
	struct i915_ttm_buddy_resource *res;
	struct drm_i915_gem_object *obj;
	struct drm_buddy_block *block;
	struct drm_buddy *mm;
	struct list_head *blocks;
	struct scatterlist *sg;
	I915_RND_STATE(prng);
	LIST_HEAD(objects);
	unsigned int max_segment;
	unsigned int ps;
	u64 size;
	int err = 0;

	/*
	 * While we may create very large contiguous blocks, we may need
	 * to break those down for consumption elsewhere. In particular,
	 * dma-mapping with scatterlist elements have an implicit limit of
	 * UINT_MAX on each element.
	 */

	size = SZ_8G;
	ps = PAGE_SIZE;
	if (i915_prandom_u64_state(&prng) & 1)
		ps = SZ_64K; /* For something like DG2 */

	max_segment = round_down(UINT_MAX, ps);

	mem = mock_region_create(i915, 0, size, ps, 0, 0);
	if (IS_ERR(mem))
		return PTR_ERR(mem);

	obj = igt_object_create(mem, &objects, size, 0);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		goto out_put;
	}

	res = to_ttm_buddy_resource(obj->mm.res);
	blocks = &res->blocks;
	mm = res->mm;
	size = 0;
	list_for_each_entry(block, blocks, link) {
		if (drm_buddy_block_size(mm, block) > size)
			size = drm_buddy_block_size(mm, block);
	}
	if (size < max_segment) {
		pr_err("%s: Failed to create a huge contiguous block [> %u], largest block %lld\n",
		       __func__, max_segment, size);
		err = -EINVAL;
		goto out_close;
	}

	for (sg = obj->mm.pages->sgl; sg; sg = sg_next(sg)) {
		dma_addr_t daddr = sg_dma_address(sg);

		if (sg->length > max_segment) {
			pr_err("%s: Created an oversized scatterlist entry, %u > %u\n",
			       __func__, sg->length, max_segment);
			err = -EINVAL;
			goto out_close;
		}

		if (!IS_ALIGNED(daddr, ps)) {
			pr_err("%s: Created an unaligned scatterlist entry, addr=%pa, ps=%u\n",
			       __func__,  &daddr, ps);
			err = -EINVAL;
			goto out_close;
		}
	}

out_close:
	close_objects(mem, &objects);
out_put:
	intel_memory_region_destroy(mem);
	return err;
}

static u64 igt_object_mappable_total(struct drm_i915_gem_object *obj)
{
	struct intel_memory_region *mr = obj->mm.region;
	struct i915_ttm_buddy_resource *bman_res =
		to_ttm_buddy_resource(obj->mm.res);
	struct drm_buddy *mm = bman_res->mm;
	struct drm_buddy_block *block;
	u64 total;

	total = 0;
	list_for_each_entry(block, &bman_res->blocks, link) {
		u64 start = drm_buddy_block_offset(block);
		u64 end = start + drm_buddy_block_size(mm, block);

		if (start < mr->io_size)
			total += min_t(u64, end, mr->io_size) - start;
	}

	return total;
}

static int igt_mock_io_size(void *arg)
{
	struct intel_memory_region *mr = arg;
	struct drm_i915_private *i915 = mr->i915;
	struct drm_i915_gem_object *obj;
	u64 mappable_theft_total;
	u64 io_size;
	u64 total;
	u64 ps;
	u64 rem;
	u64 size;
	I915_RND_STATE(prng);
	LIST_HEAD(objects);
	int err = 0;

	ps = SZ_4K;
	if (i915_prandom_u64_state(&prng) & 1)
		ps = SZ_64K; /* For something like DG2 */

	div64_u64_rem(i915_prandom_u64_state(&prng), SZ_8G, &total);
	total = round_down(total, ps);
	total = max_t(u64, total, SZ_1G);

	div64_u64_rem(i915_prandom_u64_state(&prng), total - ps, &io_size);
	io_size = round_down(io_size, ps);
	io_size = max_t(u64, io_size, SZ_256M); /* 256M seems to be the common lower limit */

	pr_info("%s with ps=%llx, io_size=%llx, total=%llx\n",
		__func__, ps, io_size, total);

	mr = mock_region_create(i915, 0, total, ps, 0, io_size);
	if (IS_ERR(mr)) {
		err = PTR_ERR(mr);
		goto out_err;
	}

	mappable_theft_total = 0;
	rem = total - io_size;
	do {
		div64_u64_rem(i915_prandom_u64_state(&prng), rem, &size);
		size = round_down(size, ps);
		size = max(size, ps);

		obj = igt_object_create(mr, &objects, size,
					I915_BO_ALLOC_GPU_ONLY);
		if (IS_ERR(obj)) {
			pr_err("%s TOPDOWN failed with rem=%llx, size=%llx\n",
			       __func__, rem, size);
			err = PTR_ERR(obj);
			goto out_close;
		}

		mappable_theft_total += igt_object_mappable_total(obj);
		rem -= size;
	} while (rem);

	pr_info("%s mappable theft=(%lluMiB/%lluMiB), total=%lluMiB\n",
		__func__,
		(u64)mappable_theft_total >> 20,
		(u64)io_size >> 20,
		(u64)total >> 20);

	/*
	 * Even if we allocate all of the non-mappable portion, we should still
	 * be able to dip into the mappable portion.
	 */
	obj = igt_object_create(mr, &objects, io_size,
				I915_BO_ALLOC_GPU_ONLY);
	if (IS_ERR(obj)) {
		pr_err("%s allocation unexpectedly failed\n", __func__);
		err = PTR_ERR(obj);
		goto out_close;
	}

	close_objects(mr, &objects);

	rem = io_size;
	do {
		div64_u64_rem(i915_prandom_u64_state(&prng), rem, &size);
		size = round_down(size, ps);
		size = max(size, ps);

		obj = igt_object_create(mr, &objects, size, 0);
		if (IS_ERR(obj)) {
			pr_err("%s MAPPABLE failed with rem=%llx, size=%llx\n",
			       __func__, rem, size);
			err = PTR_ERR(obj);
			goto out_close;
		}

		if (igt_object_mappable_total(obj) != size) {
			pr_err("%s allocation is not mappable(size=%llx)\n",
			       __func__, size);
			err = -EINVAL;
			goto out_close;
		}
		rem -= size;
	} while (rem);

	/*
	 * We assume CPU access is required by default, which should result in a
	 * failure here, even though the non-mappable portion is free.
	 */
	obj = igt_object_create(mr, &objects, ps, 0);
	if (!IS_ERR(obj)) {
		pr_err("%s allocation unexpectedly succeeded\n", __func__);
		err = -EINVAL;
		goto out_close;
	}

out_close:
	close_objects(mr, &objects);
	intel_memory_region_destroy(mr);
out_err:
	if (err == -ENOMEM)
		err = 0;

	return err;
}

static int igt_gpu_write_dw(struct intel_context *ce,
			    struct i915_vma *vma,
			    u32 dword,
			    u32 value)
{
	return igt_gpu_fill_dw(ce, vma, dword * sizeof(u32),
			       vma->size >> PAGE_SHIFT, value);
}

static int igt_cpu_check(struct drm_i915_gem_object *obj, u32 dword, u32 val)
{
	unsigned long n = obj->base.size >> PAGE_SHIFT;
	u32 *ptr;
	int err;

	err = i915_gem_object_wait(obj, 0, MAX_SCHEDULE_TIMEOUT);
	if (err)
		return err;

	ptr = i915_gem_object_pin_map(obj, I915_MAP_WC);
	if (IS_ERR(ptr))
		return PTR_ERR(ptr);

	ptr += dword;
	while (n--) {
		if (*ptr != val) {
			pr_err("base[%u]=%08x, val=%08x\n",
			       dword, *ptr, val);
			err = -EINVAL;
			break;
		}

		ptr += PAGE_SIZE / sizeof(*ptr);
	}

	i915_gem_object_unpin_map(obj);
	return err;
}

static int igt_gpu_write(struct i915_gem_context *ctx,
			 struct drm_i915_gem_object *obj)
{
	struct i915_gem_engines *engines;
	struct i915_gem_engines_iter it;
	struct i915_address_space *vm;
	struct intel_context *ce;
	I915_RND_STATE(prng);
	IGT_TIMEOUT(end_time);
	unsigned int count;
	struct i915_vma *vma;
	int *order;
	int i, n;
	int err = 0;

	GEM_BUG_ON(!i915_gem_object_has_pinned_pages(obj));

	n = 0;
	count = 0;
	for_each_gem_engine(ce, i915_gem_context_lock_engines(ctx), it) {
		count++;
		if (!intel_engine_can_store_dword(ce->engine))
			continue;

		vm = ce->vm;
		n++;
	}
	i915_gem_context_unlock_engines(ctx);
	if (!n)
		return 0;

	order = i915_random_order(count * count, &prng);
	if (!order)
		return -ENOMEM;

	vma = i915_vma_instance(obj, vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto out_free;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_USER);
	if (err)
		goto out_free;

	i = 0;
	engines = i915_gem_context_lock_engines(ctx);
	do {
		u32 rng = prandom_u32_state(&prng);
		u32 dword = offset_in_page(rng) / 4;

		ce = engines->engines[order[i] % engines->num_engines];
		i = (i + 1) % (count * count);
		if (!ce || !intel_engine_can_store_dword(ce->engine))
			continue;

		err = igt_gpu_write_dw(ce, vma, dword, rng);
		if (err)
			break;

		i915_gem_object_lock(obj, NULL);
		err = igt_cpu_check(obj, dword, rng);
		i915_gem_object_unlock(obj);
		if (err)
			break;
	} while (!__igt_timeout(end_time, NULL));
	i915_gem_context_unlock_engines(ctx);

out_free:
	kfree(order);

	if (err == -ENOMEM)
		err = 0;

	return err;
}

static int igt_lmem_create(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj;
	int err = 0;

	obj = i915_gem_object_create_lmem(i915, PAGE_SIZE, 0);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	err = i915_gem_object_pin_pages_unlocked(obj);
	if (err)
		goto out_put;

	i915_gem_object_unpin_pages(obj);
out_put:
	i915_gem_object_put(obj);

	return err;
}

static int igt_lmem_create_with_ps(void *arg)
{
	struct drm_i915_private *i915 = arg;
	int err = 0;
	u32 ps;

	for (ps = PAGE_SIZE; ps <= SZ_1G; ps <<= 1) {
		struct drm_i915_gem_object *obj;
		dma_addr_t daddr;

		obj = __i915_gem_object_create_lmem_with_ps(i915, ps, ps, 0);
		if (IS_ERR(obj)) {
			err = PTR_ERR(obj);
			if (err == -ENXIO || err == -E2BIG) {
				pr_info("%s not enough lmem for ps(%u) err=%d\n",
					__func__, ps, err);
				err = 0;
			}

			break;
		}

		if (obj->base.size != ps) {
			pr_err("%s size(%zu) != ps(%u)\n",
			       __func__, obj->base.size, ps);
			err = -EINVAL;
			goto out_put;
		}

		i915_gem_object_lock(obj, NULL);
		err = i915_gem_object_pin_pages(obj);
		if (err) {
			if (err == -ENXIO || err == -E2BIG || err == -ENOMEM) {
				pr_info("%s not enough lmem for ps(%u) err=%d\n",
					__func__, ps, err);
				err = 0;
			}
			goto out_put;
		}

		daddr = i915_gem_object_get_dma_address(obj, 0);
		if (!IS_ALIGNED(daddr, ps)) {
			pr_err("%s daddr(%pa) not aligned with ps(%u)\n",
			       __func__, &daddr, ps);
			err = -EINVAL;
			goto out_unpin;
		}

out_unpin:
		i915_gem_object_unpin_pages(obj);
		__i915_gem_object_put_pages(obj);
out_put:
		i915_gem_object_unlock(obj);
		i915_gem_object_put(obj);

		if (err)
			break;
	}

	return err;
}

static int igt_lmem_create_cleared_cpu(void *arg)
{
	struct drm_i915_private *i915 = arg;
	I915_RND_STATE(prng);
	IGT_TIMEOUT(end_time);
	u32 size, i;
	int err;

	i915_gem_drain_freed_objects(i915);

	size = max_t(u32, PAGE_SIZE, i915_prandom_u32_max_state(SZ_32M, &prng));
	size = round_up(size, PAGE_SIZE);
	i = 0;

	do {
		struct drm_i915_gem_object *obj;
		unsigned int flags;
		u32 dword, val;
		void *vaddr;

		/*
		 * Alternate between cleared and uncleared allocations, while
		 * also dirtying the pages each time to check that the pages are
		 * always cleared if requested, since we should get some overlap
		 * of the underlying pages, if not all, since we are the only
		 * user.
		 */

		flags = I915_BO_ALLOC_CPU_CLEAR;
		if (i & 1)
			flags = 0;

		obj = i915_gem_object_create_lmem(i915, size, flags);
		if (IS_ERR(obj))
			return PTR_ERR(obj);

		i915_gem_object_lock(obj, NULL);
		err = i915_gem_object_pin_pages(obj);
		if (err)
			goto out_put;

		dword = i915_prandom_u32_max_state(PAGE_SIZE / sizeof(u32),
						   &prng);

		if (flags & I915_BO_ALLOC_CPU_CLEAR) {
			err = igt_cpu_check(obj, dword, 0);
			if (err) {
				pr_err("%s failed with size=%u, flags=%u\n",
				       __func__, size, flags);
				goto out_unpin;
			}
		}

		vaddr = i915_gem_object_pin_map(obj, I915_MAP_WC);
		if (IS_ERR(vaddr)) {
			err = PTR_ERR(vaddr);
			goto out_unpin;
		}

		val = prandom_u32_state(&prng);

		memset32(vaddr, val, obj->base.size / sizeof(u32));

		i915_gem_object_flush_map(obj);
		i915_gem_object_unpin_map(obj);
out_unpin:
		i915_gem_object_unpin_pages(obj);
		__i915_gem_object_put_pages(obj);
out_put:
		i915_gem_object_unlock(obj);
		i915_gem_object_put(obj);

		if (err)
			break;
		++i;
	} while (!__igt_timeout(end_time, NULL));

	pr_info("%s completed (%u) iterations\n", __func__, i);

	return err;
}

static int igt_lmem_write_gpu(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj;
	struct i915_gem_context *ctx;
	struct file *file;
	I915_RND_STATE(prng);
	u32 sz;
	int err;

	file = mock_file(i915);
	if (IS_ERR(file))
		return PTR_ERR(file);

	ctx = live_context(i915, file);
	if (IS_ERR(ctx)) {
		err = PTR_ERR(ctx);
		goto out_file;
	}

	sz = round_up(prandom_u32_state(&prng) % SZ_32M, PAGE_SIZE);

	obj = i915_gem_object_create_lmem(i915, sz, 0);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		goto out_file;
	}

	err = i915_gem_object_pin_pages_unlocked(obj);
	if (err)
		goto out_put;

	err = igt_gpu_write(ctx, obj);
	if (err)
		pr_err("igt_gpu_write failed(%d)\n", err);

	i915_gem_object_unpin_pages(obj);
out_put:
	i915_gem_object_put(obj);
out_file:
	fput(file);
	return err;
}

static struct intel_engine_cs *
random_engine_class(struct drm_i915_private *i915,
		    unsigned int class,
		    struct rnd_state *prng)
{
	struct intel_engine_cs *engine;
	unsigned int count;

	count = 0;
	for (engine = intel_engine_lookup_user(i915, class, 0);
	     engine && engine->uabi_class == class;
	     engine = rb_entry_safe(rb_next(&engine->uabi_node),
				    typeof(*engine), uabi_node))
		count++;

	count = i915_prandom_u32_max_state(count, prng);
	return intel_engine_lookup_user(i915, class, count);
}

static int igt_lmem_write_cpu(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj;
	I915_RND_STATE(prng);
	IGT_TIMEOUT(end_time);
	u32 bytes[] = {
		0, /* rng placeholder */
		sizeof(u32),
		sizeof(u64),
		64, /* cl */
		PAGE_SIZE,
		PAGE_SIZE - sizeof(u32),
		PAGE_SIZE - sizeof(u64),
		PAGE_SIZE - 64,
	};
	struct intel_engine_cs *engine;
	struct i915_request *rq;
	u32 *vaddr;
	u32 sz;
	u32 i;
	int *order;
	int count;
	int err;

	engine = random_engine_class(i915, I915_ENGINE_CLASS_COPY, &prng);
	if (!engine)
		return 0;

	pr_info("%s: using %s\n", __func__, engine->name);

	sz = round_up(prandom_u32_state(&prng) % SZ_32M, PAGE_SIZE);
	sz = max_t(u32, 2 * PAGE_SIZE, sz);

	obj = i915_gem_object_create_lmem(i915, sz, I915_BO_ALLOC_CONTIGUOUS);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	vaddr = i915_gem_object_pin_map_unlocked(obj, I915_MAP_WC);
	if (IS_ERR(vaddr)) {
		err = PTR_ERR(vaddr);
		goto out_put;
	}

	i915_gem_object_lock(obj, NULL);

	err = dma_resv_reserve_fences(obj->base.resv, 1);
	if (err) {
		i915_gem_object_unlock(obj);
		goto out_put;
	}

	/* Put the pages into a known state -- from the gpu for added fun */
	intel_engine_pm_get(engine);
	err = intel_context_migrate_clear(engine->gt->migrate.context, NULL,
					  obj->mm.pages->sgl,
					  i915_gem_get_pat_index(i915,
								 I915_CACHE_NONE),
					  true, 0xdeadbeaf, &rq);
	if (rq) {
		dma_resv_add_fence(obj->base.resv, &rq->fence,
				   DMA_RESV_USAGE_WRITE);
		i915_request_put(rq);
	}

	intel_engine_pm_put(engine);
	if (!err)
		err = i915_gem_object_set_to_wc_domain(obj, true);
	i915_gem_object_unlock(obj);
	if (err)
		goto out_unpin;

	count = ARRAY_SIZE(bytes);
	order = i915_random_order(count * count, &prng);
	if (!order) {
		err = -ENOMEM;
		goto out_unpin;
	}

	/* A random multiple of u32, picked between [64, PAGE_SIZE - 64] */
	bytes[0] = igt_random_offset(&prng, 64, PAGE_SIZE - 64, 0, sizeof(u32));
	GEM_BUG_ON(!IS_ALIGNED(bytes[0], sizeof(u32)));

	i = 0;
	do {
		u32 offset;
		u32 align;
		u32 dword;
		u32 size;
		u32 val;

		size = bytes[order[i] % count];
		i = (i + 1) % (count * count);

		align = bytes[order[i] % count];
		i = (i + 1) % (count * count);

		align = max_t(u32, sizeof(u32), rounddown_pow_of_two(align));

		offset = igt_random_offset(&prng, 0, obj->base.size,
					   size, align);

		val = prandom_u32_state(&prng);
		memset32(vaddr + offset / sizeof(u32), val ^ 0xdeadbeaf,
			 size / sizeof(u32));

		/*
		 * Sample random dw -- don't waste precious time reading every
		 * single dw.
		 */
		dword = igt_random_offset(&prng, offset,
					  offset + size,
					  sizeof(u32), sizeof(u32));
		dword /= sizeof(u32);
		if (vaddr[dword] != (val ^ 0xdeadbeaf)) {
			pr_err("%s vaddr[%u]=%u, val=%u, size=%u, align=%u, offset=%u\n",
			       __func__, dword, vaddr[dword], val ^ 0xdeadbeaf,
			       size, align, offset);
			err = -EINVAL;
			break;
		}
	} while (!__igt_timeout(end_time, NULL));

out_unpin:
	i915_gem_object_unpin_map(obj);
out_put:
	i915_gem_object_put(obj);

	return err;
}

static const char *repr_type(u32 type)
{
	switch (type) {
	case I915_MAP_WB:
		return "WB";
	case I915_MAP_WC:
		return "WC";
	}

	return "";
}

static struct drm_i915_gem_object *
create_region_for_mapping(struct intel_memory_region *mr, u64 size, u32 type,
			  void **out_addr)
{
	struct drm_i915_gem_object *obj;
	void *addr;

	obj = i915_gem_object_create_region(mr, size, 0, 0);
	if (IS_ERR(obj)) {
		if (PTR_ERR(obj) == -ENOSPC) /* Stolen memory */
			return ERR_PTR(-ENODEV);
		return obj;
	}

	addr = i915_gem_object_pin_map_unlocked(obj, type);
	if (IS_ERR(addr)) {
		i915_gem_object_put(obj);
		if (PTR_ERR(addr) == -ENXIO)
			return ERR_PTR(-ENODEV);
		return addr;
	}

	*out_addr = addr;
	return obj;
}

static int wrap_ktime_compare(const void *A, const void *B)
{
	const ktime_t *a = A, *b = B;

	return ktime_compare(*a, *b);
}

static void igt_memcpy_long(void *dst, const void *src, size_t size)
{
	unsigned long *tmp = dst;
	const unsigned long *s = src;

	size = size / sizeof(unsigned long);
	while (size--)
		*tmp++ = *s++;
}

static inline void igt_memcpy(void *dst, const void *src, size_t size)
{
	memcpy(dst, src, size);
}

static inline void igt_memcpy_from_wc(void *dst, const void *src, size_t size)
{
	i915_memcpy_from_wc(dst, src, size);
}

static int _perf_memcpy(struct intel_memory_region *src_mr,
			struct intel_memory_region *dst_mr,
			u64 size, u32 src_type, u32 dst_type)
{
	struct drm_i915_private *i915 = src_mr->i915;
	const struct {
		const char *name;
		void (*copy)(void *dst, const void *src, size_t size);
		bool skip;
	} tests[] = {
		{
			"memcpy",
			igt_memcpy,
		},
		{
			"memcpy_long",
			igt_memcpy_long,
		},
		{
			"memcpy_from_wc",
			igt_memcpy_from_wc,
			!i915_has_memcpy_from_wc(),
		},
	};
	struct drm_i915_gem_object *src, *dst;
	void *src_addr, *dst_addr;
	int ret = 0;
	int i;

	src = create_region_for_mapping(src_mr, size, src_type, &src_addr);
	if (IS_ERR(src)) {
		ret = PTR_ERR(src);
		goto out;
	}

	dst = create_region_for_mapping(dst_mr, size, dst_type, &dst_addr);
	if (IS_ERR(dst)) {
		ret = PTR_ERR(dst);
		goto out_unpin_src;
	}

	for (i = 0; i < ARRAY_SIZE(tests); ++i) {
		ktime_t t[5];
		int pass;

		if (tests[i].skip)
			continue;

		for (pass = 0; pass < ARRAY_SIZE(t); pass++) {
			ktime_t t0, t1;

			t0 = ktime_get();

			tests[i].copy(dst_addr, src_addr, size);

			t1 = ktime_get();
			t[pass] = ktime_sub(t1, t0);
		}

		sort(t, ARRAY_SIZE(t), sizeof(*t), wrap_ktime_compare, NULL);
		if (t[0] <= 0) {
			/* ignore the impossible to protect our sanity */
			pr_debug("Skipping %s src(%s, %s) -> dst(%s, %s) %14s %4lluKiB copy, unstable measurement [%lld, %lld]\n",
				 __func__,
				 src_mr->name, repr_type(src_type),
				 dst_mr->name, repr_type(dst_type),
				 tests[i].name, size >> 10,
				 t[0], t[4]);
			continue;
		}

		pr_info("%s src(%s, %s) -> dst(%s, %s) %14s %4llu KiB copy: %5lld MiB/s\n",
			__func__,
			src_mr->name, repr_type(src_type),
			dst_mr->name, repr_type(dst_type),
			tests[i].name, size >> 10,
			div64_u64(mul_u32_u32(4 * size,
					      1000 * 1000 * 1000),
				  t[1] + 2 * t[2] + t[3]) >> 20);

		cond_resched();
	}

	i915_gem_object_unpin_map(dst);
	i915_gem_object_put(dst);
out_unpin_src:
	i915_gem_object_unpin_map(src);
	i915_gem_object_put(src);

	i915_gem_drain_freed_objects(i915);
out:
	if (ret == -ENODEV)
		ret = 0;

	return ret;
}

static int perf_memcpy(void *arg)
{
	struct drm_i915_private *i915 = arg;
	static const u32 types[] = {
		I915_MAP_WB,
		I915_MAP_WC,
	};
	static const u32 sizes[] = {
		SZ_4K,
		SZ_64K,
		SZ_4M,
	};
	struct intel_memory_region *src_mr, *dst_mr;
	int src_id, dst_id;
	int i, j, k;
	int ret;

	for_each_memory_region(src_mr, i915, src_id) {
		for_each_memory_region(dst_mr, i915, dst_id) {
			for (i = 0; i < ARRAY_SIZE(sizes); ++i) {
				for (j = 0; j < ARRAY_SIZE(types); ++j) {
					for (k = 0; k < ARRAY_SIZE(types); ++k) {
						ret = _perf_memcpy(src_mr,
								   dst_mr,
								   sizes[i],
								   types[j],
								   types[k]);
						if (ret)
							return ret;
					}
				}
			}
		}
	}

	return 0;
}

int intel_memory_region_mock_selftests(void)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_mock_reserve),
		SUBTEST(igt_mock_fill),
		SUBTEST(igt_mock_contiguous),
		SUBTEST(igt_mock_splintered_region),
		SUBTEST(igt_mock_max_segment),
		SUBTEST(igt_mock_io_size),
	};
	struct intel_memory_region *mem;
	struct drm_i915_private *i915;
	int err;

	i915 = mock_gem_device();
	if (!i915)
		return -ENOMEM;

	mem = mock_region_create(i915, 0, SZ_2G, I915_GTT_PAGE_SIZE_4K, 0, 0);
	if (IS_ERR(mem)) {
		pr_err("failed to create memory region\n");
		err = PTR_ERR(mem);
		goto out_unref;
	}

	err = i915_subtests(tests, mem);

	intel_memory_region_destroy(mem);
out_unref:
	mock_destroy_device(i915);
	return err;
}

int intel_memory_region_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_lmem_create),
		SUBTEST(igt_lmem_create_with_ps),
		SUBTEST(igt_lmem_create_cleared_cpu),
		SUBTEST(igt_lmem_write_cpu),
		SUBTEST(igt_lmem_write_gpu),
	};

	if (!HAS_LMEM(i915)) {
		pr_info("device lacks LMEM support, skipping\n");
		return 0;
	}

	if (intel_gt_is_wedged(to_gt(i915)))
		return 0;

	return i915_live_subtests(tests, i915);
}

int intel_memory_region_perf_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(perf_memcpy),
	};

	if (intel_gt_is_wedged(to_gt(i915)))
		return 0;

	return i915_live_subtests(tests, i915);
}
