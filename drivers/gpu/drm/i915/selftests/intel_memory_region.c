// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <linux/prime_numbers.h>

#include "../i915_selftest.h"

#include "mock_drm.h"
#include "mock_gem_device.h"
#include "mock_region.h"

#include "gem/i915_gem_region.h"
#include "gem/selftests/mock_context.h"
#include "selftests/i915_random.h"

static void close_objects(struct intel_memory_region *mem,
			  struct list_head *objects)
{
	struct drm_i915_private *i915 = mem->i915;
	struct drm_i915_gem_object *obj, *on;

	list_for_each_entry_safe(obj, on, objects, st_link) {
		if (i915_gem_object_has_pinned_pages(obj))
			i915_gem_object_unpin_pages(obj);
		/* No polluting the memory region between tests */
		__i915_gem_object_put_pages(obj, I915_MM_NORMAL);
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

	page_size = mem->mm.chunk_size;
	max_pages = div64_u64(total, page_size);
	rem = total;

	for_each_prime_number_from(page_num, 1, max_pages) {
		resource_size_t size = page_num * page_size;
		struct drm_i915_gem_object *obj;

		obj = i915_gem_object_create_region(mem, size, 0);
		if (IS_ERR(obj)) {
			err = PTR_ERR(obj);
			break;
		}

		err = i915_gem_object_pin_pages(obj);
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

	obj = i915_gem_object_create_region(mem, size, flags);
	if (IS_ERR(obj))
		return obj;

	err = i915_gem_object_pin_pages(obj);
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
	i915_gem_object_unpin_pages(obj);
	__i915_gem_object_put_pages(obj, I915_MM_NORMAL);
	list_del(&obj->st_link);
	i915_gem_object_put(obj);
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
	obj = igt_object_create(mem, &objects, mem->mm.chunk_size,
				I915_BO_ALLOC_CONTIGUOUS);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	if (obj->mm.pages->nents != 1) {
		pr_err("%s min object spans multiple sg entries\n", __func__);
		err = -EINVAL;
		goto err_close_objects;
	}

	igt_object_release(obj);

	/* Max size */
	obj = igt_object_create(mem, &objects, total, I915_BO_ALLOC_CONTIGUOUS);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	if (obj->mm.pages->nents != 1) {
		pr_err("%s max object spans multiple sg entries\n", __func__);
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

	if (obj->mm.pages->nents != 1) {
		pr_err("%s object spans multiple sg entries\n", __func__);
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
	} while (target >= mem->mm.chunk_size);

err_close_objects:
	list_splice_tail(&holes, &objects);
	close_objects(mem, &objects);
	return err;
}

int intel_memory_region_mock_selftests(void)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_mock_fill),
		SUBTEST(igt_mock_contiguous),
	};
	struct intel_memory_region *mem;
	struct drm_i915_private *i915;
	int err;

	i915 = mock_gem_device();
	if (!i915)
		return -ENOMEM;

	mem = mock_region_create(i915, 0, SZ_2G, I915_GTT_PAGE_SIZE_4K, 0);
	if (IS_ERR(mem)) {
		pr_err("failed to create memory region\n");
		err = PTR_ERR(mem);
		goto out_unref;
	}

	err = i915_subtests(tests, mem);

	intel_memory_region_put(mem);
out_unref:
	drm_dev_put(&i915->drm);
	return err;
}
