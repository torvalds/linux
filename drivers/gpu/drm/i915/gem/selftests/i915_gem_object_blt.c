// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_selftest.h"

#include "selftests/igt_flush_test.h"
#include "selftests/mock_drm.h"
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

	do {
		u32 sz = prandom_u32_state(&prng) % SZ_32M;
		u32 val = prandom_u32_state(&prng);
		u32 i;

		sz = round_up(sz, PAGE_SIZE);

		pr_debug("%s with sz=%x, val=%x\n", __func__, sz, val);

		obj = i915_gem_object_create_internal(i915, sz);
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
		memset32(vaddr, val ^ 0xdeadbeaf, obj->base.size / sizeof(u32));

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

		for (i = 0; i < obj->base.size / sizeof(u32); ++i) {
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

int i915_gem_object_blt_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_fill_blt),
	};

	if (i915_terminally_wedged(i915))
		return 0;

	if (!HAS_ENGINE(i915, BCS0))
		return 0;

	return i915_live_subtests(tests, i915);
}
