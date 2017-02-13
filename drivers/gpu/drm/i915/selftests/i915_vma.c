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

#include <linux/prime_numbers.h>

#include "../i915_selftest.h"

#include "mock_gem_device.h"
#include "mock_context.h"

static bool assert_vma(struct i915_vma *vma,
		       struct drm_i915_gem_object *obj,
		       struct i915_gem_context *ctx)
{
	bool ok = true;

	if (vma->vm != &ctx->ppgtt->base) {
		pr_err("VMA created with wrong VM\n");
		ok = false;
	}

	if (vma->size != obj->base.size) {
		pr_err("VMA created with wrong size, found %llu, expected %zu\n",
		       vma->size, obj->base.size);
		ok = false;
	}

	if (vma->ggtt_view.type != I915_GGTT_VIEW_NORMAL) {
		pr_err("VMA created with wrong type [%d]\n",
		       vma->ggtt_view.type);
		ok = false;
	}

	return ok;
}

static struct i915_vma *
checked_vma_instance(struct drm_i915_gem_object *obj,
		     struct i915_address_space *vm,
		     struct i915_ggtt_view *view)
{
	struct i915_vma *vma;
	bool ok = true;

	vma = i915_vma_instance(obj, vm, view);
	if (IS_ERR(vma))
		return vma;

	/* Manual checks, will be reinforced by i915_vma_compare! */
	if (vma->vm != vm) {
		pr_err("VMA's vm [%p] does not match request [%p]\n",
		       vma->vm, vm);
		ok = false;
	}

	if (i915_is_ggtt(vm) != i915_vma_is_ggtt(vma)) {
		pr_err("VMA ggtt status [%d] does not match parent [%d]\n",
		       i915_vma_is_ggtt(vma), i915_is_ggtt(vm));
		ok = false;
	}

	if (i915_vma_compare(vma, vm, view)) {
		pr_err("i915_vma_compare failed with create parmaters!\n");
		return ERR_PTR(-EINVAL);
	}

	if (i915_vma_compare(vma, vma->vm,
			     i915_vma_is_ggtt(vma) ? &vma->ggtt_view : NULL)) {
		pr_err("i915_vma_compare failed with itself\n");
		return ERR_PTR(-EINVAL);
	}

	if (!ok) {
		pr_err("i915_vma_compare failed to detect the difference!\n");
		return ERR_PTR(-EINVAL);
	}

	return vma;
}

static int create_vmas(struct drm_i915_private *i915,
		       struct list_head *objects,
		       struct list_head *contexts)
{
	struct drm_i915_gem_object *obj;
	struct i915_gem_context *ctx;
	int pinned;

	list_for_each_entry(obj, objects, st_link) {
		for (pinned = 0; pinned <= 1; pinned++) {
			list_for_each_entry(ctx, contexts, link) {
				struct i915_address_space *vm =
					&ctx->ppgtt->base;
				struct i915_vma *vma;
				int err;

				vma = checked_vma_instance(obj, vm, NULL);
				if (IS_ERR(vma))
					return PTR_ERR(vma);

				if (!assert_vma(vma, obj, ctx)) {
					pr_err("VMA lookup/create failed\n");
					return -EINVAL;
				}

				if (!pinned) {
					err = i915_vma_pin(vma, 0, 0, PIN_USER);
					if (err) {
						pr_err("Failed to pin VMA\n");
						return err;
					}
				} else {
					i915_vma_unpin(vma);
				}
			}
		}
	}

	return 0;
}

static int igt_vma_create(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj, *on;
	struct i915_gem_context *ctx, *cn;
	unsigned long num_obj, num_ctx;
	unsigned long no, nc;
	IGT_TIMEOUT(end_time);
	LIST_HEAD(contexts);
	LIST_HEAD(objects);
	int err;

	/* Exercise creating many vma amonst many objections, checking the
	 * vma creation and lookup routines.
	 */

	no = 0;
	for_each_prime_number(num_obj, ULONG_MAX - 1) {
		for (; no < num_obj; no++) {
			obj = i915_gem_object_create_internal(i915, PAGE_SIZE);
			if (IS_ERR(obj))
				goto out;

			list_add(&obj->st_link, &objects);
		}

		nc = 0;
		for_each_prime_number(num_ctx, MAX_CONTEXT_HW_ID) {
			for (; nc < num_ctx; nc++) {
				ctx = mock_context(i915, "mock");
				if (!ctx)
					goto out;

				list_move(&ctx->link, &contexts);
			}

			err = create_vmas(i915, &objects, &contexts);
			if (err)
				goto out;

			if (igt_timeout(end_time,
					"%s timed out: after %lu objects in %lu contexts\n",
					__func__, no, nc))
				goto end;
		}

		list_for_each_entry_safe(ctx, cn, &contexts, link)
			mock_context_close(ctx);
	}

end:
	/* Final pass to lookup all created contexts */
	err = create_vmas(i915, &objects, &contexts);
out:
	list_for_each_entry_safe(ctx, cn, &contexts, link)
		mock_context_close(ctx);

	list_for_each_entry_safe(obj, on, &objects, st_link)
		i915_gem_object_put(obj);
	return err;
}

struct pin_mode {
	u64 size;
	u64 flags;
	bool (*assert)(const struct i915_vma *,
		       const struct pin_mode *mode,
		       int result);
	const char *string;
};

static bool assert_pin_valid(const struct i915_vma *vma,
			     const struct pin_mode *mode,
			     int result)
{
	if (result)
		return false;

	if (i915_vma_misplaced(vma, mode->size, 0, mode->flags))
		return false;

	return true;
}

__maybe_unused
static bool assert_pin_e2big(const struct i915_vma *vma,
			     const struct pin_mode *mode,
			     int result)
{
	return result == -E2BIG;
}

__maybe_unused
static bool assert_pin_enospc(const struct i915_vma *vma,
			      const struct pin_mode *mode,
			      int result)
{
	return result == -ENOSPC;
}

__maybe_unused
static bool assert_pin_einval(const struct i915_vma *vma,
			      const struct pin_mode *mode,
			      int result)
{
	return result == -EINVAL;
}

static int igt_vma_pin1(void *arg)
{
	struct drm_i915_private *i915 = arg;
	const struct pin_mode modes[] = {
#define VALID(sz, fl) { .size = (sz), .flags = (fl), .assert = assert_pin_valid, .string = #sz ", " #fl ", (valid) " }
#define __INVALID(sz, fl, check, eval) { .size = (sz), .flags = (fl), .assert = (check), .string = #sz ", " #fl ", (invalid " #eval ")" }
#define INVALID(sz, fl) __INVALID(sz, fl, assert_pin_einval, EINVAL)
#define TOOBIG(sz, fl) __INVALID(sz, fl, assert_pin_e2big, E2BIG)
#define NOSPACE(sz, fl) __INVALID(sz, fl, assert_pin_enospc, ENOSPC)
		VALID(0, PIN_GLOBAL),
		VALID(0, PIN_GLOBAL | PIN_MAPPABLE),

		VALID(0, PIN_GLOBAL | PIN_OFFSET_BIAS | 4096),
		VALID(0, PIN_GLOBAL | PIN_OFFSET_BIAS | 8192),
		VALID(0, PIN_GLOBAL | PIN_OFFSET_BIAS | (i915->ggtt.mappable_end - 4096)),
		VALID(0, PIN_GLOBAL | PIN_MAPPABLE | PIN_OFFSET_BIAS | (i915->ggtt.mappable_end - 4096)),
		VALID(0, PIN_GLOBAL | PIN_OFFSET_BIAS | (i915->ggtt.base.total - 4096)),

		VALID(0, PIN_GLOBAL | PIN_MAPPABLE | PIN_OFFSET_FIXED | (i915->ggtt.mappable_end - 4096)),
		INVALID(0, PIN_GLOBAL | PIN_MAPPABLE | PIN_OFFSET_FIXED | i915->ggtt.mappable_end),
		VALID(0, PIN_GLOBAL | PIN_OFFSET_FIXED | (i915->ggtt.base.total - 4096)),
		INVALID(0, PIN_GLOBAL | PIN_OFFSET_FIXED | i915->ggtt.base.total),
		INVALID(0, PIN_GLOBAL | PIN_OFFSET_FIXED | round_down(U64_MAX, PAGE_SIZE)),

		VALID(4096, PIN_GLOBAL),
		VALID(8192, PIN_GLOBAL),
		VALID(i915->ggtt.mappable_end - 4096, PIN_GLOBAL | PIN_MAPPABLE),
		VALID(i915->ggtt.mappable_end, PIN_GLOBAL | PIN_MAPPABLE),
		TOOBIG(i915->ggtt.mappable_end + 4096, PIN_GLOBAL | PIN_MAPPABLE),
		VALID(i915->ggtt.base.total - 4096, PIN_GLOBAL),
		VALID(i915->ggtt.base.total, PIN_GLOBAL),
		TOOBIG(i915->ggtt.base.total + 4096, PIN_GLOBAL),
		TOOBIG(round_down(U64_MAX, PAGE_SIZE), PIN_GLOBAL),
		INVALID(8192, PIN_GLOBAL | PIN_MAPPABLE | PIN_OFFSET_FIXED | (i915->ggtt.mappable_end - 4096)),
		INVALID(8192, PIN_GLOBAL | PIN_OFFSET_FIXED | (i915->ggtt.base.total - 4096)),
		INVALID(8192, PIN_GLOBAL | PIN_OFFSET_FIXED | (round_down(U64_MAX, PAGE_SIZE) - 4096)),

		VALID(8192, PIN_GLOBAL | PIN_OFFSET_BIAS | (i915->ggtt.mappable_end - 4096)),

#if !IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM)
		/* Misusing BIAS is a programming error (it is not controllable
		 * from userspace) so when debugging is enabled, it explodes.
		 * However, the tests are still quite interesting for checking
		 * variable start, end and size.
		 */
		NOSPACE(0, PIN_GLOBAL | PIN_MAPPABLE | PIN_OFFSET_BIAS | i915->ggtt.mappable_end),
		NOSPACE(0, PIN_GLOBAL | PIN_OFFSET_BIAS | i915->ggtt.base.total),
		NOSPACE(8192, PIN_GLOBAL | PIN_MAPPABLE | PIN_OFFSET_BIAS | (i915->ggtt.mappable_end - 4096)),
		NOSPACE(8192, PIN_GLOBAL | PIN_OFFSET_BIAS | (i915->ggtt.base.total - 4096)),
#endif
		{ },
#undef NOSPACE
#undef TOOBIG
#undef INVALID
#undef __INVALID
#undef VALID
	}, *m;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	int err = -EINVAL;

	/* Exercise all the weird and wonderful i915_vma_pin requests,
	 * focusing on error handling of boundary conditions.
	 */

	GEM_BUG_ON(!drm_mm_clean(&i915->ggtt.base.mm));

	obj = i915_gem_object_create_internal(i915, PAGE_SIZE);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	vma = checked_vma_instance(obj, &i915->ggtt.base, NULL);
	if (IS_ERR(vma))
		goto out;

	for (m = modes; m->assert; m++) {
		err = i915_vma_pin(vma, m->size, 0, m->flags);
		if (!m->assert(vma, m, err)) {
			pr_err("%s to pin single page into GGTT with mode[%d:%s]: size=%llx flags=%llx, err=%d\n",
			       m->assert == assert_pin_valid ? "Failed" : "Unexpectedly succeeded",
			       (int)(m - modes), m->string, m->size, m->flags,
			       err);
			if (!err)
				i915_vma_unpin(vma);
			err = -EINVAL;
			goto out;
		}

		if (!err) {
			i915_vma_unpin(vma);
			err = i915_vma_unbind(vma);
			if (err) {
				pr_err("Failed to unbind single page from GGTT, err=%d\n", err);
				goto out;
			}
		}
	}

	err = 0;
out:
	i915_gem_object_put(obj);
	return err;
}

int i915_vma_mock_selftests(void)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_vma_create),
		SUBTEST(igt_vma_pin1),
	};
	struct drm_i915_private *i915;
	int err;

	i915 = mock_gem_device();
	if (!i915)
		return -ENOMEM;

	mutex_lock(&i915->drm.struct_mutex);
	err = i915_subtests(tests, i915);
	mutex_unlock(&i915->drm.struct_mutex);

	drm_dev_unref(&i915->drm);
	return err;
}

