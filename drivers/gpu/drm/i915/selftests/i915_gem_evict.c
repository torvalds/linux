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

#include "../i915_selftest.h"

#include "lib_sw_fence.h"
#include "mock_context.h"
#include "mock_drm.h"
#include "mock_gem_device.h"

static int populate_ggtt(struct drm_i915_private *i915)
{
	struct drm_i915_gem_object *obj;
	u64 size;

	for (size = 0;
	     size + I915_GTT_PAGE_SIZE <= i915->ggtt.vm.total;
	     size += I915_GTT_PAGE_SIZE) {
		struct i915_vma *vma;

		obj = i915_gem_object_create_internal(i915, I915_GTT_PAGE_SIZE);
		if (IS_ERR(obj))
			return PTR_ERR(obj);

		vma = i915_gem_object_ggtt_pin(obj, NULL, 0, 0, 0);
		if (IS_ERR(vma))
			return PTR_ERR(vma);
	}

	if (!list_empty(&i915->mm.unbound_list)) {
		size = 0;
		list_for_each_entry(obj, &i915->mm.unbound_list, mm.link)
			size++;

		pr_err("Found %lld objects unbound!\n", size);
		return -EINVAL;
	}

	if (list_empty(&i915->ggtt.vm.inactive_list)) {
		pr_err("No objects on the GGTT inactive list!\n");
		return -EINVAL;
	}

	return 0;
}

static void unpin_ggtt(struct drm_i915_private *i915)
{
	struct i915_vma *vma;

	list_for_each_entry(vma, &i915->ggtt.vm.inactive_list, vm_link)
		i915_vma_unpin(vma);
}

static void cleanup_objects(struct drm_i915_private *i915)
{
	struct drm_i915_gem_object *obj, *on;

	list_for_each_entry_safe(obj, on, &i915->mm.unbound_list, mm.link)
		i915_gem_object_put(obj);

	list_for_each_entry_safe(obj, on, &i915->mm.bound_list, mm.link)
		i915_gem_object_put(obj);

	mutex_unlock(&i915->drm.struct_mutex);

	i915_gem_drain_freed_objects(i915);

	mutex_lock(&i915->drm.struct_mutex);
}

static int igt_evict_something(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct i915_ggtt *ggtt = &i915->ggtt;
	int err;

	/* Fill the GGTT with pinned objects and try to evict one. */

	err = populate_ggtt(i915);
	if (err)
		goto cleanup;

	/* Everything is pinned, nothing should happen */
	err = i915_gem_evict_something(&ggtt->vm,
				       I915_GTT_PAGE_SIZE, 0, 0,
				       0, U64_MAX,
				       0);
	if (err != -ENOSPC) {
		pr_err("i915_gem_evict_something failed on a full GGTT with err=%d\n",
		       err);
		goto cleanup;
	}

	unpin_ggtt(i915);

	/* Everything is unpinned, we should be able to evict something */
	err = i915_gem_evict_something(&ggtt->vm,
				       I915_GTT_PAGE_SIZE, 0, 0,
				       0, U64_MAX,
				       0);
	if (err) {
		pr_err("i915_gem_evict_something failed on a full GGTT with err=%d\n",
		       err);
		goto cleanup;
	}

cleanup:
	cleanup_objects(i915);
	return err;
}

static int igt_overcommit(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	int err;

	/* Fill the GGTT with pinned objects and then try to pin one more.
	 * We expect it to fail.
	 */

	err = populate_ggtt(i915);
	if (err)
		goto cleanup;

	obj = i915_gem_object_create_internal(i915, I915_GTT_PAGE_SIZE);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		goto cleanup;
	}

	vma = i915_gem_object_ggtt_pin(obj, NULL, 0, 0, 0);
	if (!IS_ERR(vma) || PTR_ERR(vma) != -ENOSPC) {
		pr_err("Failed to evict+insert, i915_gem_object_ggtt_pin returned err=%d\n", (int)PTR_ERR(vma));
		err = -EINVAL;
		goto cleanup;
	}

cleanup:
	cleanup_objects(i915);
	return err;
}

static int igt_evict_for_vma(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct i915_ggtt *ggtt = &i915->ggtt;
	struct drm_mm_node target = {
		.start = 0,
		.size = 4096,
	};
	int err;

	/* Fill the GGTT with pinned objects and try to evict a range. */

	err = populate_ggtt(i915);
	if (err)
		goto cleanup;

	/* Everything is pinned, nothing should happen */
	err = i915_gem_evict_for_node(&ggtt->vm, &target, 0);
	if (err != -ENOSPC) {
		pr_err("i915_gem_evict_for_node on a full GGTT returned err=%d\n",
		       err);
		goto cleanup;
	}

	unpin_ggtt(i915);

	/* Everything is unpinned, we should be able to evict the node */
	err = i915_gem_evict_for_node(&ggtt->vm, &target, 0);
	if (err) {
		pr_err("i915_gem_evict_for_node returned err=%d\n",
		       err);
		goto cleanup;
	}

cleanup:
	cleanup_objects(i915);
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
	struct drm_i915_private *i915 = arg;
	struct i915_ggtt *ggtt = &i915->ggtt;
	const unsigned long flags = PIN_OFFSET_FIXED;
	struct drm_mm_node target = {
		.start = I915_GTT_PAGE_SIZE * 2,
		.size = I915_GTT_PAGE_SIZE,
		.color = I915_CACHE_LLC,
	};
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	int err;

	/* Currently the use of color_adjust is limited to cache domains within
	 * the ggtt, and so the presence of mm.color_adjust is assumed to be
	 * i915_gtt_color_adjust throughout our driver, so using a mock color
	 * adjust will work just fine for our purposes.
	 */
	ggtt->vm.mm.color_adjust = mock_color_adjust;

	obj = i915_gem_object_create_internal(i915, I915_GTT_PAGE_SIZE);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		goto cleanup;
	}
	i915_gem_object_set_cache_level(obj, I915_CACHE_LLC);

	vma = i915_gem_object_ggtt_pin(obj, NULL, 0, 0,
				       I915_GTT_PAGE_SIZE | flags);
	if (IS_ERR(vma)) {
		pr_err("[0]i915_gem_object_ggtt_pin failed\n");
		err = PTR_ERR(vma);
		goto cleanup;
	}

	obj = i915_gem_object_create_internal(i915, I915_GTT_PAGE_SIZE);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		goto cleanup;
	}
	i915_gem_object_set_cache_level(obj, I915_CACHE_LLC);

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
	err = i915_gem_evict_for_node(&ggtt->vm, &target, 0);
	if (err) {
		pr_err("[0]i915_gem_evict_for_node returned err=%d\n", err);
		goto cleanup;
	}

	/* Attempt to remove the first *pinned* vma, by removing the (empty)
	 * neighbour -- this should fail.
	 */
	target.color = I915_CACHE_L3_LLC;

	err = i915_gem_evict_for_node(&ggtt->vm, &target, 0);
	if (!err) {
		pr_err("[1]i915_gem_evict_for_node returned err=%d\n", err);
		err = -EINVAL;
		goto cleanup;
	}

	err = 0;

cleanup:
	unpin_ggtt(i915);
	cleanup_objects(i915);
	ggtt->vm.mm.color_adjust = NULL;
	return err;
}

static int igt_evict_vm(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct i915_ggtt *ggtt = &i915->ggtt;
	int err;

	/* Fill the GGTT with pinned objects and try to evict everything. */

	err = populate_ggtt(i915);
	if (err)
		goto cleanup;

	/* Everything is pinned, nothing should happen */
	err = i915_gem_evict_vm(&ggtt->vm);
	if (err) {
		pr_err("i915_gem_evict_vm on a full GGTT returned err=%d]\n",
		       err);
		goto cleanup;
	}

	unpin_ggtt(i915);

	err = i915_gem_evict_vm(&ggtt->vm);
	if (err) {
		pr_err("i915_gem_evict_vm on a full GGTT returned err=%d]\n",
		       err);
		goto cleanup;
	}

cleanup:
	cleanup_objects(i915);
	return err;
}

static int igt_evict_contexts(void *arg)
{
	const u64 PRETEND_GGTT_SIZE = 16ull << 20;
	struct drm_i915_private *i915 = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct reserved {
		struct drm_mm_node node;
		struct reserved *next;
	} *reserved = NULL;
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

	mutex_lock(&i915->drm.struct_mutex);
	intel_runtime_pm_get(i915);

	/* Reserve a block so that we know we have enough to fit a few rq */
	memset(&hole, 0, sizeof(hole));
	err = i915_gem_gtt_insert(&i915->ggtt.vm, &hole,
				  PRETEND_GGTT_SIZE, 0, I915_COLOR_UNEVICTABLE,
				  0, i915->ggtt.vm.total,
				  PIN_NOEVICT);
	if (err)
		goto out_locked;

	/* Make the GGTT appear small by filling it with unevictable nodes */
	count = 0;
	do {
		struct reserved *r;

		r = kcalloc(1, sizeof(*r), GFP_KERNEL);
		if (!r) {
			err = -ENOMEM;
			goto out_locked;
		}

		if (i915_gem_gtt_insert(&i915->ggtt.vm, &r->node,
					1ul << 20, 0, I915_COLOR_UNEVICTABLE,
					0, i915->ggtt.vm.total,
					PIN_NOEVICT)) {
			kfree(r);
			break;
		}

		r->next = reserved;
		reserved = r;

		count++;
	} while (1);
	drm_mm_remove_node(&hole);
	mutex_unlock(&i915->drm.struct_mutex);
	pr_info("Filled GGTT with %lu 1MiB nodes\n", count);

	/* Overfill the GGTT with context objects and so try to evict one. */
	for_each_engine(engine, i915, id) {
		struct i915_sw_fence fence;
		struct drm_file *file;

		file = mock_file(i915);
		if (IS_ERR(file))
			return PTR_ERR(file);

		count = 0;
		mutex_lock(&i915->drm.struct_mutex);
		onstack_fence_init(&fence);
		do {
			struct i915_request *rq;
			struct i915_gem_context *ctx;

			ctx = live_context(i915, file);
			if (!ctx)
				break;

			/* We will need some GGTT space for the rq's context */
			igt_evict_ctl.fail_if_busy = true;
			rq = i915_request_alloc(engine, ctx);
			igt_evict_ctl.fail_if_busy = false;

			if (IS_ERR(rq)) {
				/* When full, fail_if_busy will trigger EBUSY */
				if (PTR_ERR(rq) != -EBUSY) {
					pr_err("Unexpected error from request alloc (ctx hw id %u, on %s): %d\n",
					       ctx->hw_id, engine->name,
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
		mutex_unlock(&i915->drm.struct_mutex);

		onstack_fence_fini(&fence);
		pr_info("Submitted %lu contexts/requests on %s\n",
			count, engine->name);

		mock_file_free(i915, file);
		if (err)
			break;
	}

	mutex_lock(&i915->drm.struct_mutex);
out_locked:
	while (reserved) {
		struct reserved *next = reserved->next;

		drm_mm_remove_node(&reserved->node);
		kfree(reserved);

		reserved = next;
	}
	if (drm_mm_node_allocated(&hole))
		drm_mm_remove_node(&hole);
	intel_runtime_pm_put(i915);
	mutex_unlock(&i915->drm.struct_mutex);

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
	int err;

	i915 = mock_gem_device();
	if (!i915)
		return -ENOMEM;

	mutex_lock(&i915->drm.struct_mutex);
	err = i915_subtests(tests, i915);
	mutex_unlock(&i915->drm.struct_mutex);

	drm_dev_put(&i915->drm);
	return err;
}

int i915_gem_evict_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_evict_contexts),
	};

	if (i915_terminally_wedged(&i915->gpu_error))
		return 0;

	return i915_subtests(tests, i915);
}
