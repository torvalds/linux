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

#include "igt_flush_test.h"
#include "mock_gem_device.h"
#include "huge_gem_object.h"

static int igt_gem_object(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj;
	int err = -ENOMEM;

	/* Basic test to ensure we can create an object */

	obj = i915_gem_object_create_shmem(i915, PAGE_SIZE);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		pr_err("i915_gem_object_create failed, err=%d\n", err);
		goto out;
	}

	err = 0;
	i915_gem_object_put(obj);
out:
	return err;
}

static int igt_gem_huge(void *arg)
{
	const unsigned int nreal = 509; /* just to be awkward */
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj;
	unsigned int n;
	int err;

	/* Basic sanitycheck of our huge fake object allocation */

	obj = huge_gem_object(i915,
			      nreal * PAGE_SIZE,
			      i915->ggtt.vm.total + PAGE_SIZE);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	err = i915_gem_object_pin_pages(obj);
	if (err) {
		pr_err("Failed to allocate %u pages (%lu total), err=%d\n",
		       nreal, obj->base.size / PAGE_SIZE, err);
		goto out;
	}

	for (n = 0; n < obj->base.size / PAGE_SIZE; n++) {
		if (i915_gem_object_get_page(obj, n) !=
		    i915_gem_object_get_page(obj, n % nreal)) {
			pr_err("Page lookup mismatch at index %u [%u]\n",
			       n, n % nreal);
			err = -EINVAL;
			goto out_unpin;
		}
	}

out_unpin:
	i915_gem_object_unpin_pages(obj);
out:
	i915_gem_object_put(obj);
	return err;
}

int i915_gem_object_mock_selftests(void)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_gem_object),
	};
	struct drm_i915_private *i915;
	int err;

	i915 = mock_gem_device();
	if (!i915)
		return -ENOMEM;

	err = i915_subtests(tests, i915);

	drm_dev_put(&i915->drm);
	return err;
}

int i915_gem_object_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_gem_huge),
	};

	return i915_subtests(tests, i915);
}
