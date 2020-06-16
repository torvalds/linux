// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include "i915_selftest.h"

#include "gt/intel_engine_pm.h"
#include "selftests/igt_flush_test.h"

static u64 read_reloc(const u32 *map, int x, const u64 mask)
{
	u64 reloc;

	memcpy(&reloc, &map[x], sizeof(reloc));
	return reloc & mask;
}

static int __igt_gpu_reloc(struct i915_execbuffer *eb,
			   struct drm_i915_gem_object *obj)
{
	const unsigned int offsets[] = { 8, 3, 0 };
	const u64 mask =
		GENMASK_ULL(eb->reloc_cache.use_64bit_reloc ? 63 : 31, 0);
	const u32 *map = page_mask_bits(obj->mm.mapping);
	struct i915_request *rq;
	struct i915_vma *vma;
	int err;
	int i;

	vma = i915_vma_instance(obj, eb->context->vm, NULL);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	err = i915_vma_pin(vma, 0, 0, PIN_USER | PIN_HIGH);
	if (err)
		return err;

	/* 8-Byte aligned */
	if (!__reloc_entry_gpu(eb, vma,
			       offsets[0] * sizeof(u32),
			       0)) {
		err = -EIO;
		goto unpin_vma;
	}

	/* !8-Byte aligned */
	if (!__reloc_entry_gpu(eb, vma,
			       offsets[1] * sizeof(u32),
			       1)) {
		err = -EIO;
		goto unpin_vma;
	}

	/* Skip to the end of the cmd page */
	i = PAGE_SIZE / sizeof(u32) - RELOC_TAIL - 1;
	i -= eb->reloc_cache.rq_size;
	memset32(eb->reloc_cache.rq_cmd + eb->reloc_cache.rq_size,
		 MI_NOOP, i);
	eb->reloc_cache.rq_size += i;

	/* Force batch chaining */
	if (!__reloc_entry_gpu(eb, vma,
			       offsets[2] * sizeof(u32),
			       2)) {
		err = -EIO;
		goto unpin_vma;
	}

	GEM_BUG_ON(!eb->reloc_cache.rq);
	rq = i915_request_get(eb->reloc_cache.rq);
	err = reloc_gpu_flush(&eb->reloc_cache);
	if (err)
		goto put_rq;
	GEM_BUG_ON(eb->reloc_cache.rq);

	err = i915_gem_object_wait(obj, I915_WAIT_INTERRUPTIBLE, HZ / 2);
	if (err) {
		intel_gt_set_wedged(eb->engine->gt);
		goto put_rq;
	}

	if (!i915_request_completed(rq)) {
		pr_err("%s: did not wait for relocations!\n", eb->engine->name);
		err = -EINVAL;
		goto put_rq;
	}

	for (i = 0; i < ARRAY_SIZE(offsets); i++) {
		u64 reloc = read_reloc(map, offsets[i], mask);

		if (reloc != i) {
			pr_err("%s[%d]: map[%d] %llx != %x\n",
			       eb->engine->name, i, offsets[i], reloc, i);
			err = -EINVAL;
		}
	}
	if (err)
		igt_hexdump(map, 4096);

put_rq:
	i915_request_put(rq);
unpin_vma:
	i915_vma_unpin(vma);
	return err;
}

static int igt_gpu_reloc(void *arg)
{
	struct i915_execbuffer eb;
	struct drm_i915_gem_object *scratch;
	int err = 0;
	u32 *map;

	eb.i915 = arg;

	scratch = i915_gem_object_create_internal(eb.i915, 4096);
	if (IS_ERR(scratch))
		return PTR_ERR(scratch);

	map = i915_gem_object_pin_map(scratch, I915_MAP_WC);
	if (IS_ERR(map)) {
		err = PTR_ERR(map);
		goto err_scratch;
	}

	for_each_uabi_engine(eb.engine, eb.i915) {
		reloc_cache_init(&eb.reloc_cache, eb.i915);
		memset(map, POISON_INUSE, 4096);

		intel_engine_pm_get(eb.engine);
		eb.context = intel_context_create(eb.engine);
		if (IS_ERR(eb.context)) {
			err = PTR_ERR(eb.context);
			goto err_pm;
		}

		err = intel_context_pin(eb.context);
		if (err)
			goto err_put;

		err = __igt_gpu_reloc(&eb, scratch);

		intel_context_unpin(eb.context);
err_put:
		intel_context_put(eb.context);
err_pm:
		intel_engine_pm_put(eb.engine);
		if (err)
			break;
	}

	if (igt_flush_test(eb.i915))
		err = -EIO;

err_scratch:
	i915_gem_object_put(scratch);
	return err;
}

int i915_gem_execbuffer_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_gpu_reloc),
	};

	if (intel_gt_is_wedged(&i915->gt))
		return 0;

	return i915_live_subtests(tests, i915);
}
