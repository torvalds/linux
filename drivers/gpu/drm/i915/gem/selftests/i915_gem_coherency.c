/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2017 Intel Corporation
 */

#include <linux/prime_numbers.h>

#include "gt/intel_engine_pm.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_pm.h"
#include "gt/intel_ring.h"

#include "i915_selftest.h"
#include "selftests/i915_random.h"

struct context {
	struct drm_i915_gem_object *obj;
	struct intel_engine_cs *engine;
};

static int cpu_set(struct context *ctx, unsigned long offset, u32 v)
{
	unsigned int needs_clflush;
	struct page *page;
	void *map;
	u32 *cpu;
	int err;

	err = i915_gem_object_prepare_write(ctx->obj, &needs_clflush);
	if (err)
		return err;

	page = i915_gem_object_get_page(ctx->obj, offset >> PAGE_SHIFT);
	map = kmap_atomic(page);
	cpu = map + offset_in_page(offset);

	if (needs_clflush & CLFLUSH_BEFORE)
		drm_clflush_virt_range(cpu, sizeof(*cpu));

	*cpu = v;

	if (needs_clflush & CLFLUSH_AFTER)
		drm_clflush_virt_range(cpu, sizeof(*cpu));

	kunmap_atomic(map);
	i915_gem_object_finish_access(ctx->obj);

	return 0;
}

static int cpu_get(struct context *ctx, unsigned long offset, u32 *v)
{
	unsigned int needs_clflush;
	struct page *page;
	void *map;
	u32 *cpu;
	int err;

	err = i915_gem_object_prepare_read(ctx->obj, &needs_clflush);
	if (err)
		return err;

	page = i915_gem_object_get_page(ctx->obj, offset >> PAGE_SHIFT);
	map = kmap_atomic(page);
	cpu = map + offset_in_page(offset);

	if (needs_clflush & CLFLUSH_BEFORE)
		drm_clflush_virt_range(cpu, sizeof(*cpu));

	*v = *cpu;

	kunmap_atomic(map);
	i915_gem_object_finish_access(ctx->obj);

	return 0;
}

static int gtt_set(struct context *ctx, unsigned long offset, u32 v)
{
	struct i915_vma *vma;
	u32 __iomem *map;
	int err = 0;

	i915_gem_object_lock(ctx->obj);
	err = i915_gem_object_set_to_gtt_domain(ctx->obj, true);
	i915_gem_object_unlock(ctx->obj);
	if (err)
		return err;

	vma = i915_gem_object_ggtt_pin(ctx->obj, NULL, 0, 0, PIN_MAPPABLE);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	intel_gt_pm_get(vma->vm->gt);

	map = i915_vma_pin_iomap(vma);
	i915_vma_unpin(vma);
	if (IS_ERR(map)) {
		err = PTR_ERR(map);
		goto out_rpm;
	}

	iowrite32(v, &map[offset / sizeof(*map)]);
	i915_vma_unpin_iomap(vma);

out_rpm:
	intel_gt_pm_put(vma->vm->gt);
	return err;
}

static int gtt_get(struct context *ctx, unsigned long offset, u32 *v)
{
	struct i915_vma *vma;
	u32 __iomem *map;
	int err = 0;

	i915_gem_object_lock(ctx->obj);
	err = i915_gem_object_set_to_gtt_domain(ctx->obj, false);
	i915_gem_object_unlock(ctx->obj);
	if (err)
		return err;

	vma = i915_gem_object_ggtt_pin(ctx->obj, NULL, 0, 0, PIN_MAPPABLE);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	intel_gt_pm_get(vma->vm->gt);

	map = i915_vma_pin_iomap(vma);
	i915_vma_unpin(vma);
	if (IS_ERR(map)) {
		err = PTR_ERR(map);
		goto out_rpm;
	}

	*v = ioread32(&map[offset / sizeof(*map)]);
	i915_vma_unpin_iomap(vma);

out_rpm:
	intel_gt_pm_put(vma->vm->gt);
	return err;
}

static int wc_set(struct context *ctx, unsigned long offset, u32 v)
{
	u32 *map;
	int err;

	i915_gem_object_lock(ctx->obj);
	err = i915_gem_object_set_to_wc_domain(ctx->obj, true);
	i915_gem_object_unlock(ctx->obj);
	if (err)
		return err;

	map = i915_gem_object_pin_map(ctx->obj, I915_MAP_WC);
	if (IS_ERR(map))
		return PTR_ERR(map);

	map[offset / sizeof(*map)] = v;
	i915_gem_object_unpin_map(ctx->obj);

	return 0;
}

static int wc_get(struct context *ctx, unsigned long offset, u32 *v)
{
	u32 *map;
	int err;

	i915_gem_object_lock(ctx->obj);
	err = i915_gem_object_set_to_wc_domain(ctx->obj, false);
	i915_gem_object_unlock(ctx->obj);
	if (err)
		return err;

	map = i915_gem_object_pin_map(ctx->obj, I915_MAP_WC);
	if (IS_ERR(map))
		return PTR_ERR(map);

	*v = map[offset / sizeof(*map)];
	i915_gem_object_unpin_map(ctx->obj);

	return 0;
}

static int gpu_set(struct context *ctx, unsigned long offset, u32 v)
{
	struct i915_request *rq;
	struct i915_vma *vma;
	u32 *cs;
	int err;

	i915_gem_object_lock(ctx->obj);
	err = i915_gem_object_set_to_gtt_domain(ctx->obj, true);
	i915_gem_object_unlock(ctx->obj);
	if (err)
		return err;

	vma = i915_gem_object_ggtt_pin(ctx->obj, NULL, 0, 0, 0);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	rq = intel_engine_create_kernel_request(ctx->engine);
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

	if (INTEL_GEN(ctx->engine->i915) >= 8) {
		*cs++ = MI_STORE_DWORD_IMM_GEN4 | 1 << 22;
		*cs++ = lower_32_bits(i915_ggtt_offset(vma) + offset);
		*cs++ = upper_32_bits(i915_ggtt_offset(vma) + offset);
		*cs++ = v;
	} else if (INTEL_GEN(ctx->engine->i915) >= 4) {
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
	err = i915_request_await_object(rq, vma->obj, true);
	if (err == 0)
		err = i915_vma_move_to_active(vma, rq, EXEC_OBJECT_WRITE);
	i915_vma_unlock(vma);
	i915_vma_unpin(vma);

	i915_request_add(rq);

	return err;
}

static bool always_valid(struct context *ctx)
{
	return true;
}

static bool needs_fence_registers(struct context *ctx)
{
	struct intel_gt *gt = ctx->engine->gt;

	if (intel_gt_is_wedged(gt))
		return false;

	return gt->ggtt->num_fences;
}

static bool needs_mi_store_dword(struct context *ctx)
{
	if (intel_gt_is_wedged(ctx->engine->gt))
		return false;

	return intel_engine_can_store_dword(ctx->engine);
}

static const struct igt_coherency_mode {
	const char *name;
	int (*set)(struct context *ctx, unsigned long offset, u32 v);
	int (*get)(struct context *ctx, unsigned long offset, u32 *v);
	bool (*valid)(struct context *ctx);
} igt_coherency_mode[] = {
	{ "cpu", cpu_set, cpu_get, always_valid },
	{ "gtt", gtt_set, gtt_get, needs_fence_registers },
	{ "wc", wc_set, wc_get, always_valid },
	{ "gpu", gpu_set, NULL, needs_mi_store_dword },
	{ },
};

static struct intel_engine_cs *
random_engine(struct drm_i915_private *i915, struct rnd_state *prng)
{
	struct intel_engine_cs *engine;
	unsigned int count;

	count = 0;
	for_each_uabi_engine(engine, i915)
		count++;

	count = i915_prandom_u32_max_state(count, prng);
	for_each_uabi_engine(engine, i915)
		if (count-- == 0)
			return engine;

	return NULL;
}

static int igt_gem_coherency(void *arg)
{
	const unsigned int ncachelines = PAGE_SIZE/64;
	struct drm_i915_private *i915 = arg;
	const struct igt_coherency_mode *read, *write, *over;
	unsigned long count, n;
	u32 *offsets, *values;
	I915_RND_STATE(prng);
	struct context ctx;
	int err = 0;

	/*
	 * We repeatedly write, overwrite and read from a sequence of
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

	ctx.engine = random_engine(i915, &prng);
	if (!ctx.engine) {
		err = -ENODEV;
		goto out_free;
	}
	pr_info("%s: using %s\n", __func__, ctx.engine->name);
	intel_engine_pm_get(ctx.engine);

	for (over = igt_coherency_mode; over->name; over++) {
		if (!over->set)
			continue;

		if (!over->valid(&ctx))
			continue;

		for (write = igt_coherency_mode; write->name; write++) {
			if (!write->set)
				continue;

			if (!write->valid(&ctx))
				continue;

			for (read = igt_coherency_mode; read->name; read++) {
				if (!read->get)
					continue;

				if (!read->valid(&ctx))
					continue;

				for_each_prime_number_from(count, 1, ncachelines) {
					ctx.obj = i915_gem_object_create_internal(i915, PAGE_SIZE);
					if (IS_ERR(ctx.obj)) {
						err = PTR_ERR(ctx.obj);
						goto out_pm;
					}

					i915_random_reorder(offsets, ncachelines, &prng);
					for (n = 0; n < count; n++)
						values[n] = prandom_u32_state(&prng);

					for (n = 0; n < count; n++) {
						err = over->set(&ctx, offsets[n], ~values[n]);
						if (err) {
							pr_err("Failed to set stale value[%ld/%ld] in object using %s, err=%d\n",
							       n, count, over->name, err);
							goto put_object;
						}
					}

					for (n = 0; n < count; n++) {
						err = write->set(&ctx, offsets[n], values[n]);
						if (err) {
							pr_err("Failed to set value[%ld/%ld] in object using %s, err=%d\n",
							       n, count, write->name, err);
							goto put_object;
						}
					}

					for (n = 0; n < count; n++) {
						u32 found;

						err = read->get(&ctx, offsets[n], &found);
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

					i915_gem_object_put(ctx.obj);
				}
			}
		}
	}
out_pm:
	intel_engine_pm_put(ctx.engine);
out_free:
	kfree(offsets);
	return err;

put_object:
	i915_gem_object_put(ctx.obj);
	goto out_pm;
}

int i915_gem_coherency_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_gem_coherency),
	};

	return i915_subtests(tests, i915);
}
