// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "gt/intel_gt.h"

#include "i915_selftest.h"

#include "selftests/igt_flush_test.h"
#include "selftests/mock_drm.h"
#include "huge_gem_object.h"
#include "mock_context.h"

static int igt_fill_blt(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_context *ce = i915->engine[BCS0]->kernel_context;
	struct drm_i915_gem_object *obj;
	struct rnd_state prng;
	IGT_TIMEOUT(end);
	u32 *vaddr;
	int err = 0;

	prandom_seed_state(&prng, i915_selftest.random_seed);

	/*
	 * XXX: needs some threads to scale all these tests, also maybe throw
	 * in submission from higher priority context to see if we are
	 * preempted for very large objects...
	 */

	do {
		const u32 max_block_size = S16_MAX * PAGE_SIZE;
		u32 sz = min_t(u64, ce->vm->total >> 4, prandom_u32_state(&prng));
		u32 phys_sz = sz % (max_block_size + 1);
		u32 val = prandom_u32_state(&prng);
		u32 i;

		sz = round_up(sz, PAGE_SIZE);
		phys_sz = round_up(phys_sz, PAGE_SIZE);

		pr_debug("%s with phys_sz= %x, sz=%x, val=%x\n", __func__,
			 phys_sz, sz, val);

		obj = huge_gem_object(i915, phys_sz, sz);
		if (IS_ERR(obj)) {
			err = PTR_ERR(obj);
			goto err_flush;
		}

		vaddr = i915_gem_object_pin_map(obj, I915_MAP_WB);
		if (IS_ERR(vaddr)) {
			err = PTR_ERR(vaddr);
			goto err_put;
		}

		/*
		 * Make sure the potentially async clflush does its job, if
		 * required.
		 */
		memset32(vaddr, val ^ 0xdeadbeaf,
			 huge_gem_object_phys_size(obj) / sizeof(u32));

		if (!(obj->cache_coherent & I915_BO_CACHE_COHERENT_FOR_WRITE))
			obj->cache_dirty = true;

		mutex_lock(&i915->drm.struct_mutex);
		err = i915_gem_object_fill_blt(obj, ce, val);
		mutex_unlock(&i915->drm.struct_mutex);
		if (err)
			goto err_unpin;

		i915_gem_object_lock(obj);
		err = i915_gem_object_set_to_cpu_domain(obj, false);
		i915_gem_object_unlock(obj);
		if (err)
			goto err_unpin;

		for (i = 0; i < huge_gem_object_phys_size(obj) / sizeof(u32); ++i) {
			if (vaddr[i] != val) {
				pr_err("vaddr[%u]=%x, expected=%x\n", i,
				       vaddr[i], val);
				err = -EINVAL;
				goto err_unpin;
			}
		}

		i915_gem_object_unpin_map(obj);
		i915_gem_object_put(obj);
	} while (!time_after(jiffies, end));

	goto err_flush;

err_unpin:
	i915_gem_object_unpin_map(obj);
err_put:
	i915_gem_object_put(obj);
err_flush:
	if (err == -ENOMEM)
		err = 0;

	return err;
}

static int igt_copy_blt(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_context *ce = i915->engine[BCS0]->kernel_context;
	struct drm_i915_gem_object *src, *dst;
	struct rnd_state prng;
	IGT_TIMEOUT(end);
	u32 *vaddr;
	int err = 0;

	prandom_seed_state(&prng, i915_selftest.random_seed);

	do {
		const u32 max_block_size = S16_MAX * PAGE_SIZE;
		u32 sz = min_t(u64, ce->vm->total >> 4, prandom_u32_state(&prng));
		u32 phys_sz = sz % (max_block_size + 1);
		u32 val = prandom_u32_state(&prng);
		u32 i;

		sz = round_up(sz, PAGE_SIZE);
		phys_sz = round_up(phys_sz, PAGE_SIZE);

		pr_debug("%s with phys_sz= %x, sz=%x, val=%x\n", __func__,
			 phys_sz, sz, val);

		src = huge_gem_object(i915, phys_sz, sz);
		if (IS_ERR(src)) {
			err = PTR_ERR(src);
			goto err_flush;
		}

		vaddr = i915_gem_object_pin_map(src, I915_MAP_WB);
		if (IS_ERR(vaddr)) {
			err = PTR_ERR(vaddr);
			goto err_put_src;
		}

		memset32(vaddr, val,
			 huge_gem_object_phys_size(src) / sizeof(u32));

		i915_gem_object_unpin_map(src);

		if (!(src->cache_coherent & I915_BO_CACHE_COHERENT_FOR_READ))
			src->cache_dirty = true;

		dst = huge_gem_object(i915, phys_sz, sz);
		if (IS_ERR(dst)) {
			err = PTR_ERR(dst);
			goto err_put_src;
		}

		vaddr = i915_gem_object_pin_map(dst, I915_MAP_WB);
		if (IS_ERR(vaddr)) {
			err = PTR_ERR(vaddr);
			goto err_put_dst;
		}

		memset32(vaddr, val ^ 0xdeadbeaf,
			 huge_gem_object_phys_size(dst) / sizeof(u32));

		if (!(dst->cache_coherent & I915_BO_CACHE_COHERENT_FOR_WRITE))
			dst->cache_dirty = true;

		mutex_lock(&i915->drm.struct_mutex);
		err = i915_gem_object_copy_blt(src, dst, ce);
		mutex_unlock(&i915->drm.struct_mutex);
		if (err)
			goto err_unpin;

		i915_gem_object_lock(dst);
		err = i915_gem_object_set_to_cpu_domain(dst, false);
		i915_gem_object_unlock(dst);
		if (err)
			goto err_unpin;

		for (i = 0; i < huge_gem_object_phys_size(dst) / sizeof(u32); ++i) {
			if (vaddr[i] != val) {
				pr_err("vaddr[%u]=%x, expected=%x\n", i,
				       vaddr[i], val);
				err = -EINVAL;
				goto err_unpin;
			}
		}

		i915_gem_object_unpin_map(dst);

		i915_gem_object_put(src);
		i915_gem_object_put(dst);
	} while (!time_after(jiffies, end));

	goto err_flush;

err_unpin:
	i915_gem_object_unpin_map(dst);
err_put_dst:
	i915_gem_object_put(dst);
err_put_src:
	i915_gem_object_put(src);
err_flush:
	if (err == -ENOMEM)
		err = 0;

	return err;
}

int i915_gem_object_blt_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_fill_blt),
		SUBTEST(igt_copy_blt),
	};

	if (intel_gt_is_wedged(&i915->gt))
		return 0;

	if (!HAS_ENGINE(i915, BCS0))
		return 0;

	return i915_live_subtests(tests, i915);
}
