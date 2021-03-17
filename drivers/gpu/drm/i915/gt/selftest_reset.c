// SPDX-License-Identifier: MIT
/*
 * Copyright © 2018 Intel Corporation
 */

#include <linux/crc32.h>

#include "gem/i915_gem_stolen.h"

#include "i915_memcpy.h"
#include "i915_selftest.h"
#include "selftests/igt_reset.h"
#include "selftests/igt_atomic.h"
#include "selftests/igt_spinner.h"

static int
__igt_reset_stolen(struct intel_gt *gt,
		   intel_engine_mask_t mask,
		   const char *msg)
{
	struct i915_ggtt *ggtt = &gt->i915->ggtt;
	const struct resource *dsm = &gt->i915->dsm;
	resource_size_t num_pages, page;
	struct intel_engine_cs *engine;
	intel_wakeref_t wakeref;
	enum intel_engine_id id;
	struct igt_spinner spin;
	long max, count;
	void *tmp;
	u32 *crc;
	int err;

	if (!drm_mm_node_allocated(&ggtt->error_capture))
		return 0;

	num_pages = resource_size(dsm) >> PAGE_SHIFT;
	if (!num_pages)
		return 0;

	crc = kmalloc_array(num_pages, sizeof(u32), GFP_KERNEL);
	if (!crc)
		return -ENOMEM;

	tmp = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp) {
		err = -ENOMEM;
		goto err_crc;
	}

	igt_global_reset_lock(gt);
	wakeref = intel_runtime_pm_get(gt->uncore->rpm);

	err = igt_spinner_init(&spin, gt);
	if (err)
		goto err_lock;

	for_each_engine(engine, gt, id) {
		struct intel_context *ce;
		struct i915_request *rq;

		if (!(mask & engine->mask))
			continue;

		if (!intel_engine_can_store_dword(engine))
			continue;

		ce = intel_context_create(engine);
		if (IS_ERR(ce)) {
			err = PTR_ERR(ce);
			goto err_spin;
		}
		rq = igt_spinner_create_request(&spin, ce, MI_ARB_CHECK);
		intel_context_put(ce);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto err_spin;
		}
		i915_request_add(rq);
	}

	for (page = 0; page < num_pages; page++) {
		dma_addr_t dma = (dma_addr_t)dsm->start + (page << PAGE_SHIFT);
		void __iomem *s;
		void *in;

		ggtt->vm.insert_page(&ggtt->vm, dma,
				     ggtt->error_capture.start,
				     I915_CACHE_NONE, 0);
		mb();

		s = io_mapping_map_wc(&ggtt->iomap,
				      ggtt->error_capture.start,
				      PAGE_SIZE);

		if (!__drm_mm_interval_first(&gt->i915->mm.stolen,
					     page << PAGE_SHIFT,
					     ((page + 1) << PAGE_SHIFT) - 1))
			memset32(s, STACK_MAGIC, PAGE_SIZE / sizeof(u32));

		in = s;
		if (i915_memcpy_from_wc(tmp, s, PAGE_SIZE))
			in = tmp;
		crc[page] = crc32_le(0, in, PAGE_SIZE);

		io_mapping_unmap(s);
	}
	mb();
	ggtt->vm.clear_range(&ggtt->vm, ggtt->error_capture.start, PAGE_SIZE);

	if (mask == ALL_ENGINES) {
		intel_gt_reset(gt, mask, NULL);
	} else {
		for_each_engine(engine, gt, id) {
			if (mask & engine->mask)
				intel_engine_reset(engine, NULL);
		}
	}

	max = -1;
	count = 0;
	for (page = 0; page < num_pages; page++) {
		dma_addr_t dma = (dma_addr_t)dsm->start + (page << PAGE_SHIFT);
		void __iomem *s;
		void *in;
		u32 x;

		ggtt->vm.insert_page(&ggtt->vm, dma,
				     ggtt->error_capture.start,
				     I915_CACHE_NONE, 0);
		mb();

		s = io_mapping_map_wc(&ggtt->iomap,
				      ggtt->error_capture.start,
				      PAGE_SIZE);

		in = s;
		if (i915_memcpy_from_wc(tmp, s, PAGE_SIZE))
			in = tmp;
		x = crc32_le(0, in, PAGE_SIZE);

		if (x != crc[page] &&
		    !__drm_mm_interval_first(&gt->i915->mm.stolen,
					     page << PAGE_SHIFT,
					     ((page + 1) << PAGE_SHIFT) - 1)) {
			pr_debug("unused stolen page %pa modified by GPU reset\n",
				 &page);
			if (count++ == 0)
				igt_hexdump(in, PAGE_SIZE);
			max = page;
		}

		io_mapping_unmap(s);
	}
	mb();
	ggtt->vm.clear_range(&ggtt->vm, ggtt->error_capture.start, PAGE_SIZE);

	if (count > 0) {
		pr_info("%s reset clobbered %ld pages of stolen, last clobber at page %ld\n",
			msg, count, max);
	}
	if (max >= I915_GEM_STOLEN_BIAS >> PAGE_SHIFT) {
		pr_err("%s reset clobbered unreserved area [above %x] of stolen; may cause severe faults\n",
		       msg, I915_GEM_STOLEN_BIAS);
		err = -EINVAL;
	}

err_spin:
	igt_spinner_fini(&spin);

err_lock:
	intel_runtime_pm_put(gt->uncore->rpm, wakeref);
	igt_global_reset_unlock(gt);

	kfree(tmp);
err_crc:
	kfree(crc);
	return err;
}

static int igt_reset_device_stolen(void *arg)
{
	return __igt_reset_stolen(arg, ALL_ENGINES, "device");
}

static int igt_reset_engines_stolen(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err;

	if (!intel_has_reset_engine(gt))
		return 0;

	for_each_engine(engine, gt, id) {
		err = __igt_reset_stolen(gt, engine->mask, engine->name);
		if (err)
			return err;
	}

	return 0;
}

static int igt_global_reset(void *arg)
{
	struct intel_gt *gt = arg;
	unsigned int reset_count;
	intel_wakeref_t wakeref;
	int err = 0;

	/* Check that we can issue a global GPU reset */

	igt_global_reset_lock(gt);
	wakeref = intel_runtime_pm_get(gt->uncore->rpm);

	reset_count = i915_reset_count(&gt->i915->gpu_error);

	intel_gt_reset(gt, ALL_ENGINES, NULL);

	if (i915_reset_count(&gt->i915->gpu_error) == reset_count) {
		pr_err("No GPU reset recorded!\n");
		err = -EINVAL;
	}

	intel_runtime_pm_put(gt->uncore->rpm, wakeref);
	igt_global_reset_unlock(gt);

	if (intel_gt_is_wedged(gt))
		err = -EIO;

	return err;
}

static int igt_wedged_reset(void *arg)
{
	struct intel_gt *gt = arg;
	intel_wakeref_t wakeref;

	/* Check that we can recover a wedged device with a GPU reset */

	igt_global_reset_lock(gt);
	wakeref = intel_runtime_pm_get(gt->uncore->rpm);

	intel_gt_set_wedged(gt);

	GEM_BUG_ON(!intel_gt_is_wedged(gt));
	intel_gt_reset(gt, ALL_ENGINES, NULL);

	intel_runtime_pm_put(gt->uncore->rpm, wakeref);
	igt_global_reset_unlock(gt);

	return intel_gt_is_wedged(gt) ? -EIO : 0;
}

static int igt_atomic_reset(void *arg)
{
	struct intel_gt *gt = arg;
	const typeof(*igt_atomic_phases) *p;
	int err = 0;

	/* Check that the resets are usable from atomic context */

	intel_gt_pm_get(gt);
	igt_global_reset_lock(gt);

	/* Flush any requests before we get started and check basics */
	if (!igt_force_reset(gt))
		goto unlock;

	for (p = igt_atomic_phases; p->name; p++) {
		intel_engine_mask_t awake;

		GEM_TRACE("__intel_gt_reset under %s\n", p->name);

		awake = reset_prepare(gt);
		p->critical_section_begin();

		err = __intel_gt_reset(gt, ALL_ENGINES);

		p->critical_section_end();
		reset_finish(gt, awake);

		if (err) {
			pr_err("__intel_gt_reset failed under %s\n", p->name);
			break;
		}
	}

	/* As we poke around the guts, do a full reset before continuing. */
	igt_force_reset(gt);

unlock:
	igt_global_reset_unlock(gt);
	intel_gt_pm_put(gt);

	return err;
}

static int igt_atomic_engine_reset(void *arg)
{
	struct intel_gt *gt = arg;
	const typeof(*igt_atomic_phases) *p;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = 0;

	/* Check that the resets are usable from atomic context */

	if (!intel_has_reset_engine(gt))
		return 0;

	if (intel_uc_uses_guc_submission(&gt->uc))
		return 0;

	intel_gt_pm_get(gt);
	igt_global_reset_lock(gt);

	/* Flush any requests before we get started and check basics */
	if (!igt_force_reset(gt))
		goto out_unlock;

	for_each_engine(engine, gt, id) {
		tasklet_disable(&engine->execlists.tasklet);
		intel_engine_pm_get(engine);

		for (p = igt_atomic_phases; p->name; p++) {
			GEM_TRACE("intel_engine_reset(%s) under %s\n",
				  engine->name, p->name);

			p->critical_section_begin();
			err = intel_engine_reset(engine, NULL);
			p->critical_section_end();

			if (err) {
				pr_err("intel_engine_reset(%s) failed under %s\n",
				       engine->name, p->name);
				break;
			}
		}

		intel_engine_pm_put(engine);
		tasklet_enable(&engine->execlists.tasklet);
		if (err)
			break;
	}

	/* As we poke around the guts, do a full reset before continuing. */
	igt_force_reset(gt);

out_unlock:
	igt_global_reset_unlock(gt);
	intel_gt_pm_put(gt);

	return err;
}

int intel_reset_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_global_reset), /* attempt to recover GPU first */
		SUBTEST(igt_reset_device_stolen),
		SUBTEST(igt_reset_engines_stolen),
		SUBTEST(igt_wedged_reset),
		SUBTEST(igt_atomic_reset),
		SUBTEST(igt_atomic_engine_reset),
	};
	struct intel_gt *gt = &i915->gt;

	if (!intel_has_gpu_reset(gt))
		return 0;

	if (intel_gt_is_wedged(gt))
		return -EIO; /* we're long past hope of a successful reset */

	return intel_gt_live_subtests(tests, gt);
}
