/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include "gem/i915_gem_pm.h"
#include "gem/selftests/igt_gem_utils.h"
#include "gem/selftests/mock_context.h"
#include "gt/intel_gt.h"

#include "i915_selftest.h"

#include "igt_flush_test.h"
#include "lib_sw_fence.h"
#include "mock_drm.h"
#include "mock_gem_device.h"

static void quirk_add(struct drm_i915_gem_object *obj,
		      struct list_head *objects)
{
	/* quirk is only for live tiled objects, use it to declare ownership */
	GEM_BUG_ON(obj->mm.quirked);
	obj->mm.quirked = true;
	list_add(&obj->st_link, objects);
}

static int populate_ggtt(struct i915_ggtt *ggtt, struct list_head *objects)
{
	struct drm_i915_gem_object *obj;
	unsigned long count;

	count = 0;
	do {
		struct i915_vma *vma;

		obj = i915_gem_object_create_internal(ggtt->vm.i915,
						      I915_GTT_PAGE_SIZE);
		if (IS_ERR(obj))
			return PTR_ERR(obj);

		vma = i915_gem_object_ggtt_pin(obj, NULL, 0, 0, 0);
		if (IS_ERR(vma)) {
			i915_gem_object_put(obj);
			if (vma == ERR_PTR(-ENOSPC))
				break;

			return PTR_ERR(vma);
		}

		quirk_add(obj, objects);
		count++;
	} while (1);
	pr_debug("Filled GGTT with %lu pages [%llu total]\n",
		 count, ggtt->vm.total / PAGE_SIZE);

	if (list_empty(&ggtt->vm.bound_list)) {
		pr_err("No objects on the GGTT inactive list!\n");
		return -EINVAL;
	}

	return 0;
}

static void unpin_ggtt(struct i915_ggtt *ggtt)
{
	struct i915_vma *vma;

	list_for_each_entry(vma, &ggtt->vm.bound_list, vm_link)
		if (vma->obj->mm.quirked)
			i915_vma_unpin(vma);
}

static void cleanup_objects(struct i915_ggtt *ggtt, struct list_head *list)
{
	struct drm_i915_gem_object *obj, *on;

	list_for_each_entry_safe(obj, on, list, st_link) {
		GEM_BUG_ON(!obj->mm.quirked);
		obj->mm.quirked = false;
		i915_gem_object_put(obj);
	}

	i915_gem_drain_freed_objects(ggtt->vm.i915);
}

static int igt_evict_something(void *arg)
{
	struct intel_gt *gt = arg;
	struct i915_ggtt *ggtt = gt->ggtt;
	LIST_HEAD(objects);
	int err;

	/* Fill the GGTT with pinned objects and try to evict one. */

	err = populate_ggtt(ggtt, &objects);
	if (err)
		goto cleanup;

	/* Everything is pinned, nothing should happen */
	mutex_lock(&ggtt->vm.mutex);
	err = i915_gem_evict_something(&ggtt->vm,
				       I915_GTT_PAGE_SIZE, 0, 0,
				       0, U64_MAX,
				       0);
	mutex_unlock(&ggtt->vm.mutex);
	if (err != -ENOSPC) {
		pr_err("i915_gem_evict_something failed on a full GGTT with err=%d\n",
		       err);
		goto cleanup;
	}

	unpin_ggtt(ggtt);

	/* Everything is unpinned, we should be able to evict something */
	mutex_lock(&ggtt->vm.mutex);
	err = i915_gem_evict_something(&ggtt->vm,
				       I915_GTT_PAGE_SIZE, 0, 0,
				       0, U64_MAX,
				       0);
	mutex_unlock(&ggtt->vm.mutex);
	if (err) {
		pr_err("i915_gem_evict_something failed on a full GGTT with err=%d\n",
		       err);
		goto cleanup;
	}

cleanup:
	cleanup_objects(ggtt, &objects);
	return err;
}

static int igt_overcommit(void *arg)
{
	struct intel_gt *gt = arg;
	struct i915_ggtt *ggtt = gt->ggtt;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	LIST_HEAD(objects);
	int err;

	/* Fill the GGTT with pinned objects and then try to pin one more.
	 * We expect it to fail.
	 */

	err = populate_ggtt(ggtt, &objects);
	if (err)
		goto cleanup;

	obj = i915_gem_object_create_internal(gt->i915, I915_GTT_PAGE_SIZE);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		goto cleanup;
	}

	quirk_add(obj, &objects);

	vma = i915_gem_object_ggtt_pin(obj, NULL, 0, 0, 0);
	if (vma != ERR_PTR(-ENOSPC)) {
		pr_err("Failed to evict+insert, i915_gem_object_ggtt_pin returned err=%d\n", (int)PTR_ERR_OR_ZERO(vma));
		err = -EINVAL;
		goto cleanup;
	}

cleanup:
	cleanup_objects(ggtt, &objects);
	return err;
}

static int igt_evict_for_vma(void *arg)
{
	struct intel_gt *gt = arg;
	struct i915_ggtt *ggtt = gt->ggtt;
	struct drm_mm_node target = {
		.start = 0,
		.size = 4096,
	};
	LIST_HEAD(objects);
	int err;

	/* Fill the GGTT with pinned objects and try to evict a range. */

	err = populate_ggtt(ggtt, &objects);
	if (err)
		goto cleanup;

	/* Everything is pinned, nothing should happen */
	mutex_lock(&ggtt->vm.mutex);
	err = i915_gem_evict_for_node(&ggtt->vm, &target, 0);
	mutex_unlock(&ggtt->vm.mutex);
	if (err != -ENOSPC) {
		pr_err("i915_gem_evict_for_node on a full GGTT returned err=%d\n",
		       err);
		goto cleanup;
	}

	unpin_ggtt(ggtt);

	/* Everything is unpinned, we should be able to evict the node */
	mutex_lock(&ggtt->vm.mutex);
	err = i915_gem_evict_for_node(&ggtt->vm, &target, 0);
	mutex_unlock(&ggtt->vm.mutex);
	if (err) {
		pr_err("i915_gem_evict_for_node returned err=%d\n",
		       err);
		goto cleanup;
	}

cleanup:
	cleanup_objects(ggtt, &objects);
	return err;
}

static void mock_color_adjust(const struct drm_mm_node *node,
			      unsigned long color,
			      u64 *start,
			      u64 *end)
{
}

static int igt_evict_for_cache_color(void *arg)
{
	struct intel_gt *gt = arg;
	struct i915_ggtt *ggtt = gt->ggtt;
	const unsigned long flags = PIN_OFFSET_FIXED;
	struct drm_mm_node target = {
		.start = I915_GTT_PAGE_SIZE * 2,
		.size = I915_GTT_PAGE_SIZE,
		.color = I915_CACHE_LLC,
	};
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	LIST_HEAD(objects);
	int err;

	/*
	 * Currently the use of color_adjust for the GGTT is limited to cache
	 * coloring and guard pages, and so the presence of mm.color_adjust for
	 * the GGTT is assumed to be i915_ggtt_color_adjust, hence using a mock
	 * color adjust will work just fine for our purposes.
	 */
	ggtt->vm.mm.color_adjust = mock_color_adjust;
	GEM_BUG_ON(!i915_vm_has_cache_coloring(&ggtt->vm));

	obj = i915_gem_object_create_internal(gt->i915, I915_GTT_PAGE_SIZE);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		goto cleanup;
	}
	i915_gem_object_set_cache_coherency(obj, I915_CACHE_LLC);
	quirk_add(obj, &objects);

	vma = i915_gem_object_ggtt_pin(obj, NULL, 0, 0,
				       I915_GTT_PAGE_SIZE | flags);
	if (IS_ERR(vma)) {
		pr_err("[0]i915_gem_object_ggtt_pin failed\n");
		err = PTR_ERR(vma);
		goto cleanup;
	}

	obj = i915_gem_object_create_internal(gt->i915, I915_GTT_PAGE_SIZE);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		goto cleanup;
	}
	i915_gem_object_set_cache_coherency(obj, I915_CACHE_LLC);
	quirk_add(obj, &objects);

	/* Neighbouring; same colour - should fit */
	vma = i915_gem_object_ggtt_pin(obj, NULL, 0, 0,
				       (I915_GTT_PAGE_SIZE * 2) | flags);
	if (IS_ERR(vma)) {
		pr_err("[1]i915_gem_object_ggtt_pin failed\n");
		err = PTR_ERR(vma);
		goto cleanup;
	}

	i915_vma_unpin(vma);

	/* Remove just the second vma */
	mutex_lock(&ggtt->vm.mutex);
	err = i915_gem_evict_for_node(&ggtt->vm, &target, 0);
	mutex_unlock(&ggtt->vm.mutex);
	if (err) {
		pr_err("[0]i915_gem_evict_for_node returned err=%d\n", err);
		goto cleanup;
	}

	/* Attempt to remove the first *pinned* vma, by removing the (empty)
	 * neighbour -- this should fail.
	 */
	target.color = I915_CACHE_L3_LLC;

	mutex_lock(&ggtt->vm.mutex);
	err = i915_gem_evict_for_node(&ggtt->vm, &target, 0);
	mutex_unlock(&ggtt->vm.mutex);
	if (!err) {
		pr_err("[1]i915_gem_evict_for_node returned err=%d\n", err);
		err = -EINVAL;
		goto cleanup;
	}

	err = 0;

cleanup:
	unpin_ggtt(ggtt);
	cleanup_objects(ggtt, &objects);
	ggtt->vm.mm.color_adjust = NULL;
	return err;
}

static int igt_evict_vm(void *arg)
{
	struct intel_gt *gt = arg;
	struct i915_ggtt *ggtt = gt->ggtt;
	LIST_HEAD(objects);
	int err;

	/* Fill the GGTT with pinned objects and try to evict everything. */

	err = populate_ggtt(ggtt, &objects);
	if (err)
		goto cleanup;

	/* Everything is pinned, nothing should happen */
	mutex_lock(&ggtt->vm.mutex);
	err = i915_gem_evict_vm(&ggtt->vm);
	mutex_unlock(&ggtt->vm.mutex);
	if (err) {
		pr_err("i915_gem_evict_vm on a full GGTT returned err=%d]\n",
		       err);
		goto cleanup;
	}

	unpin_ggtt(ggtt);

	mutex_lock(&ggtt->vm.mutex);
	err = i915_gem_evict_vm(&ggtt->vm);
	mutex_unlock(&ggtt->vm.mutex);
	if (err) {
		pr_err("i915_gem_evict_vm on a full GGTT returned err=%d]\n",
		       err);
		goto cleanup;
	}

cleanup:
	cleanup_objects(ggtt, &objects);
	return err;
}

static int igt_evict_contexts(void *arg)
{
	const u64 PRETEND_GGTT_SIZE = 16ull << 20;
	struct intel_gt *gt = arg;
	struct i915_ggtt *ggtt = gt->ggtt;
	struct drm_i915_private *i915 = gt->i915;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct reserved {
		struct drm_mm_node node;
		struct reserved *next;
	} *reserved = NULL;
	intel_wakeref_t wakeref;
	struct drm_mm_node hole;
	unsigned long count;
	int err;

	/*
	 * The purpose of this test is to verify that we will trigger an
	 * eviction in the GGTT when constructing a request that requires
	 * additional space in the GGTT for pinning the context. This space
	 * is not directly tied to the request so reclaiming it requires
	 * extra work.
	 *
	 * As such this test is only meaningful for full-ppgtt environments
	 * where the GTT space of the request is separate from the GGTT
	 * allocation required to build the request.
	 */
	if (!HAS_FULL_PPGTT(i915))
		return 0;

	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	/* Reserve a block so that we know we have enough to fit a few rq */
	memset(&hole, 0, sizeof(hole));
	mutex_lock(&ggtt->vm.mutex);
	err = i915_gem_gtt_insert(&ggtt->vm, &hole,
				  PRETEND_GGTT_SIZE, 0, I915_COLOR_UNEVICTABLE,
				  0, ggtt->vm.total,
				  PIN_NOEVICT);
	if (err)
		goto out_locked;

	/* Make the GGTT appear small by filling it with unevictable nodes */
	count = 0;
	do {
		struct reserved *r;

		mutex_unlock(&ggtt->vm.mutex);
		r = kcalloc(1, sizeof(*r), GFP_KERNEL);
		mutex_lock(&ggtt->vm.mutex);
		if (!r) {
			err = -ENOMEM;
			goto out_locked;
		}

		if (i915_gem_gtt_insert(&ggtt->vm, &r->node,
					1ul << 20, 0, I915_COLOR_UNEVICTABLE,
					0, ggtt->vm.total,
					PIN_NOEVICT)) {
			kfree(r);
			break;
		}

		r->next = reserved;
		reserved = r;

		count++;
	} while (1);
	drm_mm_remove_node(&hole);
	mutex_unlock(&ggtt->vm.mutex);
	pr_info("Filled GGTT with %lu 1MiB nodes\n", count);

	/* Overfill the GGTT with context objects and so try to evict one. */
	for_each_engine(engine, gt, id) {
		struct i915_sw_fence fence;
		struct file *file;

		file = mock_file(i915);
		if (IS_ERR(file)) {
			err = PTR_ERR(file);
			break;
		}

		count = 0;
		onstack_fence_init(&fence);
		do {
			struct i915_request *rq;
			struct i915_gem_context *ctx;

			ctx = live_context(i915, file);
			if (IS_ERR(ctx))
				break;

			/* We will need some GGTT space for the rq's context */
			igt_evict_ctl.fail_if_busy = true;
			rq = igt_request_alloc(ctx, engine);
			igt_evict_ctl.fail_if_busy = false;

			if (IS_ERR(rq)) {
				/* When full, fail_if_busy will trigger EBUSY */
				if (PTR_ERR(rq) != -EBUSY) {
					pr_err("Unexpected error from request alloc (on %s): %d\n",
					       engine->name,
					       (int)PTR_ERR(rq));
					err = PTR_ERR(rq);
				}
				break;
			}

			/* Keep every request/ctx pinned until we are full */
			err = i915_sw_fence_await_sw_fence_gfp(&rq->submit,
							       &fence,
							       GFP_KERNEL);
			if (err < 0)
				break;

			i915_request_add(rq);
			count++;
			err = 0;
		} while(1);
		onstack_fence_fini(&fence);
		pr_info("Submitted %lu contexts/requests on %s\n",
			count, engine->name);

		fput(file);
		if (err)
			break;
	}

	mutex_lock(&ggtt->vm.mutex);
out_locked:
	if (igt_flush_test(i915))
		err = -EIO;
	while (reserved) {
		struct reserved *next = reserved->next;

		drm_mm_remove_node(&reserved->node);
		kfree(reserved);

		reserved = next;
	}
	if (drm_mm_node_allocated(&hole))
		drm_mm_remove_node(&hole);
	mutex_unlock(&ggtt->vm.mutex);
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);

	return err;
}

int i915_gem_evict_mock_selftests(void)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_evict_something),
		SUBTEST(igt_evict_for_vma),
		SUBTEST(igt_evict_for_cache_color),
		SUBTEST(igt_evict_vm),
		SUBTEST(igt_overcommit),
	};
	struct drm_i915_private *i915;
	intel_wakeref_t wakeref;
	int err = 0;

	i915 = mock_gem_device();
	if (!i915)
		return -ENOMEM;

	with_intel_runtime_pm(&i915->runtime_pm, wakeref)
		err = i915_subtests(tests, &i915->gt);

	drm_dev_put(&i915->drm);
	return err;
}

int i915_gem_evict_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_evict_contexts),
	};

	if (intel_gt_is_wedged(&i915->gt))
		return 0;

	return intel_gt_live_subtests(tests, &i915->gt);
}
