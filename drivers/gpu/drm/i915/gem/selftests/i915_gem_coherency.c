/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2017 Intel Corporation
 */

#include <linux/prime_numbers.h>

#include "gt/intel_gt.h"

#include "i915_selftest.h"
#include "selftests/i915_random.h"

static int cpu_set(struct drm_i915_gem_object *obj,
		   unsigned long offset,
		   u32 v)
{
	unsigned int needs_clflush;
	struct page *page;
	void *map;
	u32 *cpu;
	int err;

	err = i915_gem_object_prepare_write(obj, &needs_clflush);
	if (err)
		return err;

	page = i915_gem_object_get_page(obj, offset >> PAGE_SHIFT);
	map = kmap_atomic(page);
	cpu = map + offset_in_page(offset);

	if (needs_clflush & CLFLUSH_BEFORE)
		drm_clflush_virt_range(cpu, sizeof(*cpu));

	*cpu = v;

	if (needs_clflush & CLFLUSH_AFTER)
		drm_clflush_virt_range(cpu, sizeof(*cpu));

	kunmap_atomic(map);
	i915_gem_object_finish_access(obj);

	return 0;
}

static int cpu_get(struct drm_i915_gem_object *obj,
		   unsigned long offset,
		   u32 *v)
{
	unsigned int needs_clflush;
	struct page *page;
	void *map;
	u32 *cpu;
	int err;

	err = i915_gem_object_prepare_read(obj, &needs_clflush);
	if (err)
		return err;

	page = i915_gem_object_get_page(obj, offset >> PAGE_SHIFT);
	map = kmap_atomic(page);
	cpu = map + offset_in_page(offset);

	if (needs_clflush & CLFLUSH_BEFORE)
		drm_clflush_virt_range(cpu, sizeof(*cpu));

	*v = *cpu;

	kunmap_atomic(map);
	i915_gem_object_finish_access(obj);

	return 0;
}

static int gtt_set(struct drm_i915_gem_object *obj,
		   unsigned long offset,
		   u32 v)
{
	struct i915_vma *vma;
	u32 __iomem *map;
	int err;

	i915_gem_object_lock(obj);
	err = i915_gem_object_set_to_gtt_domain(obj, true);
	i915_gem_object_unlock(obj);
	if (err)
		return err;

	vma = i915_gem_object_ggtt_pin(obj, NULL, 0, 0, PIN_MAPPABLE);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	map = i915_vma_pin_iomap(vma);
	i915_vma_unpin(vma);
	if (IS_ERR(map))
		return PTR_ERR(map);

	iowrite32(v, &map[offset / sizeof(*map)]);
	i915_vma_unpin_iomap(vma);

	return 0;
}

static int gtt_get(struct drm_i915_gem_object *obj,
		   unsigned long offset,
		   u32 *v)
{
	struct i915_vma *vma;
	u32 __iomem *map;
	int err;

	i915_gem_object_lock(obj);
	err = i915_gem_object_set_to_gtt_domain(obj, false);
	i915_gem_object_unlock(obj);
	if (err)
		return err;

	vma = i915_gem_object_ggtt_pin(obj, NULL, 0, 0, PIN_MAPPABLE);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	map = i915_vma_pin_iomap(vma);
	i915_vma_unpin(vma);
	if (IS_ERR(map))
		return PTR_ERR(map);

	*v = ioread32(&map[offset / sizeof(*map)]);
	i915_vma_unpin_iomap(vma);

	return 0;
}

static int wc_set(struct drm_i915_gem_object *obj,
		  unsigned long offset,
		  u32 v)
{
	u32 *map;
	int err;

	i915_gem_object_lock(obj);
	err = i915_gem_object_set_to_wc_domain(obj, true);
	i915_gem_object_unlock(obj);
	if (err)
		return err;

	map = i915_gem_object_pin_map(obj, I915_MAP_WC);
	if (IS_ERR(map))
		return PTR_ERR(map);

	map[offset / sizeof(*map)] = v;
	i915_gem_object_unpin_map(obj);

	return 0;
}

static int wc_get(struct drm_i915_gem_object *obj,
		  unsigned long offset,
		  u32 *v)
{
	u32 *map;
	int err;

	i915_gem_object_lock(obj);
	err = i915_gem_object_set_to_wc_domain(obj, false);
	i915_gem_object_unlock(obj);
	if (err)
		return err;

	map = i915_gem_object_pin_map(obj, I915_MAP_WC);
	if (IS_ERR(map))
		return PTR_ERR(map);

	*v = map[offset / sizeof(*map)];
	i915_gem_object_unpin_map(obj);

	return 0;
}

static int gpu_set(struct drm_i915_gem_object *obj,
		   unsigned long offset,
		   u32 v)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct i915_request *rq;
	struct i915_vma *vma;
	u32 *cs;
	int err;

	i915_gem_object_lock(obj);
	err = i915_gem_object_set_to_gtt_domain(obj, true);
	i915_gem_object_unlock(obj);
	if (err)
		return err;

	vma = i915_gem_object_ggtt_pin(obj, NULL, 0, 0, 0);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	rq = i915_request_create(i915->engine[RCS0]->kernel_context);
	if (IS_ERR(rq)) {
		i915_vma_unpin(vma);
		return PTR_ERR(rq);
	}

	cs = intel_ring_begin(rq, 4);
	if (IS_ERR(cs)) {
		i915_request_add(rq);
		i915_vma_unpin(vma);
		return PTR_ERR(cs);
	}

	if (INTEL_GEN(i915) >= 8) {
		*cs++ = MI_STORE_DWORD_IMM_GEN4 | 1 << 22;
		*cs++ = lower_32_bits(i915_ggtt_offset(vma) + offset);
		*cs++ = upper_32_bits(i915_ggtt_offset(vma) + offset);
		*cs++ = v;
	} else if (INTEL_GEN(i915) >= 4) {
		*cs++ = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT;
		*cs++ = 0;
		*cs++ = i915_ggtt_offset(vma) + offset;
		*cs++ = v;
	} else {
		*cs++ = MI_STORE_DWORD_IMM | MI_MEM_VIRTUAL;
		*cs++ = i915_ggtt_offset(vma) + offset;
		*cs++ = v;
		*cs++ = MI_NOOP;
	}
	intel_ring_advance(rq, cs);

	i915_vma_lock(vma);
	err = i915_vma_move_to_active(vma, rq, EXEC_OBJECT_WRITE);
	i915_vma_unlock(vma);
	i915_vma_unpin(vma);

	i915_request_add(rq);

	return err;
}

static bool always_valid(struct drm_i915_private *i915)
{
	return true;
}

static bool needs_fence_registers(struct drm_i915_private *i915)
{
	return !intel_gt_is_wedged(&i915->gt);
}

static bool needs_mi_store_dword(struct drm_i915_private *i915)
{
	if (intel_gt_is_wedged(&i915->gt))
		return false;

	if (!HAS_ENGINE(i915, RCS0))
		return false;

	return intel_engine_can_store_dword(i915->engine[RCS0]);
}

static const struct igt_coherency_mode {
	const char *name;
	int (*set)(struct drm_i915_gem_object *, unsigned long offset, u32 v);
	int (*get)(struct drm_i915_gem_object *, unsigned long offset, u32 *v);
	bool (*valid)(struct drm_i915_private *i915);
} igt_coherency_mode[] = {
	{ "cpu", cpu_set, cpu_get, always_valid },
	{ "gtt", gtt_set, gtt_get, needs_fence_registers },
	{ "wc", wc_set, wc_get, always_valid },
	{ "gpu", gpu_set, NULL, needs_mi_store_dword },
	{ },
};

static int igt_gem_coherency(void *arg)
{
	const unsigned int ncachelines = PAGE_SIZE/64;
	I915_RND_STATE(prng);
	struct drm_i915_private *i915 = arg;
	const struct igt_coherency_mode *read, *write, *over;
	struct drm_i915_gem_object *obj;
	intel_wakeref_t wakeref;
	unsigned long count, n;
	u32 *offsets, *values;
	int err = 0;

	/* We repeatedly write, overwrite and read from a sequence of
	 * cachelines in order to try and detect incoherency (unflushed writes
	 * from either the CPU or GPU). Each setter/getter uses our cache
	 * domain API which should prevent incoherency.
	 */

	offsets = kmalloc_array(ncachelines, 2*sizeof(u32), GFP_KERNEL);
	if (!offsets)
		return -ENOMEM;
	for (count = 0; count < ncachelines; count++)
		offsets[count] = count * 64 + 4 * (count % 16);

	values = offsets + ncachelines;

	mutex_lock(&i915->drm.struct_mutex);
	wakeref = intel_runtime_pm_get(&i915->runtime_pm);
	for (over = igt_coherency_mode; over->name; over++) {
		if (!over->set)
			continue;

		if (!over->valid(i915))
			continue;

		for (write = igt_coherency_mode; write->name; write++) {
			if (!write->set)
				continue;

			if (!write->valid(i915))
				continue;

			for (read = igt_coherency_mode; read->name; read++) {
				if (!read->get)
					continue;

				if (!read->valid(i915))
					continue;

				for_each_prime_number_from(count, 1, ncachelines) {
					obj = i915_gem_object_create_internal(i915, PAGE_SIZE);
					if (IS_ERR(obj)) {
						err = PTR_ERR(obj);
						goto unlock;
					}

					i915_random_reorder(offsets, ncachelines, &prng);
					for (n = 0; n < count; n++)
						values[n] = prandom_u32_state(&prng);

					for (n = 0; n < count; n++) {
						err = over->set(obj, offsets[n], ~values[n]);
						if (err) {
							pr_err("Failed to set stale value[%ld/%ld] in object using %s, err=%d\n",
							       n, count, over->name, err);
							goto put_object;
						}
					}

					for (n = 0; n < count; n++) {
						err = write->set(obj, offsets[n], values[n]);
						if (err) {
							pr_err("Failed to set value[%ld/%ld] in object using %s, err=%d\n",
							       n, count, write->name, err);
							goto put_object;
						}
					}

					for (n = 0; n < count; n++) {
						u32 found;

						err = read->get(obj, offsets[n], &found);
						if (err) {
							pr_err("Failed to get value[%ld/%ld] in object using %s, err=%d\n",
							       n, count, read->name, err);
							goto put_object;
						}

						if (found != values[n]) {
							pr_err("Value[%ld/%ld] mismatch, (overwrite with %s) wrote [%s] %x read [%s] %x (inverse %x), at offset %x\n",
							       n, count, over->name,
							       write->name, values[n],
							       read->name, found,
							       ~values[n], offsets[n]);
							err = -EINVAL;
							goto put_object;
						}
					}

					i915_gem_object_put(obj);
				}
			}
		}
	}
unlock:
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
	mutex_unlock(&i915->drm.struct_mutex);
	kfree(offsets);
	return err;

put_object:
	i915_gem_object_put(obj);
	goto unlock;
}

int i915_gem_coherency_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_gem_coherency),
	};

	return i915_subtests(tests, i915);
}
