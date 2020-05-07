// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_selftest.h"

#include "gt/intel_engine_user.h"
#include "gt/intel_gt.h"

#include "selftests/igt_flush_test.h"
#include "selftests/mock_drm.h"
#include "huge_gem_object.h"
#include "mock_context.h"

static int __igt_client_fill(struct intel_engine_cs *engine)
{
	struct intel_context *ce = engine->kernel_context;
	struct drm_i915_gem_object *obj;
	struct rnd_state prng;
	IGT_TIMEOUT(end);
	u32 *vaddr;
	int err = 0;

	prandom_seed_state(&prng, i915_selftest.random_seed);

	intel_engine_pm_get(engine);
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

		obj = huge_gem_object(engine->i915, phys_sz, sz);
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
		 * XXX: The goal is move this to get_pages, so try to dirty the
		 * CPU cache first to check that we do the required clflush
		 * before scheduling the blt for !llc platforms. This matches
		 * some version of reality where at get_pages the pages
		 * themselves may not yet be coherent with the GPU(swap-in). If
		 * we are missing the flush then we should see the stale cache
		 * values after we do the set_to_cpu_domain and pick it up as a
		 * test failure.
		 */
		memset32(vaddr, val ^ 0xdeadbeaf,
			 huge_gem_object_phys_size(obj) / sizeof(u32));

		if (!(obj->cache_coherent & I915_BO_CACHE_COHERENT_FOR_WRITE))
			obj->cache_dirty = true;

		err = i915_gem_schedule_fill_pages_blt(obj, ce, obj->mm.pages,
						       &obj->mm.page_sizes,
						       val);
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
	intel_engine_pm_put(engine);

	return err;
}

static int igt_client_fill(void *arg)
{
	int inst = 0;

	do {
		struct intel_engine_cs *engine;
		int err;

		engine = intel_engine_lookup_user(arg,
						  I915_ENGINE_CLASS_COPY,
						  inst++);
		if (!engine)
			return 0;

		err = __igt_client_fill(engine);
		if (err == -ENOMEM)
			err = 0;
		if (err)
			return err;
	} while (1);
}

int i915_gem_client_blt_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_client_fill),
	};

	if (intel_gt_is_wedged(&i915->gt))
		return 0;

	if (!HAS_ENGINE(i915, BCS0))
		return 0;

	return i915_live_subtests(tests, i915);
}
