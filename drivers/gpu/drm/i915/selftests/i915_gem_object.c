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

#include "mock_gem_device.h"

static int igt_gem_object(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj;
	int err = -ENOMEM;

	/* Basic test to ensure we can create an object */

	obj = i915_gem_object_create(i915, PAGE_SIZE);
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

static int igt_phys_object(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj;
	int err;

	/* Create an object and bind it to a contiguous set of physical pages,
	 * i.e. exercise the i915_gem_object_phys API.
	 */

	obj = i915_gem_object_create(i915, PAGE_SIZE);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		pr_err("i915_gem_object_create failed, err=%d\n", err);
		goto out;
	}

	mutex_lock(&i915->drm.struct_mutex);
	err = i915_gem_object_attach_phys(obj, PAGE_SIZE);
	mutex_unlock(&i915->drm.struct_mutex);
	if (err) {
		pr_err("i915_gem_object_attach_phys failed, err=%d\n", err);
		goto out_obj;
	}

	if (obj->ops != &i915_gem_phys_ops) {
		pr_err("i915_gem_object_attach_phys did not create a phys object\n");
		err = -EINVAL;
		goto out_obj;
	}

	if (!atomic_read(&obj->mm.pages_pin_count)) {
		pr_err("i915_gem_object_attach_phys did not pin its phys pages\n");
		err = -EINVAL;
		goto out_obj;
	}

	/* Make the object dirty so that put_pages must do copy back the data */
	mutex_lock(&i915->drm.struct_mutex);
	err = i915_gem_object_set_to_gtt_domain(obj, true);
	mutex_unlock(&i915->drm.struct_mutex);
	if (err) {
		pr_err("i915_gem_object_set_to_gtt_domain failed with err=%d\n",
		       err);
		goto out_obj;
	}

out_obj:
	i915_gem_object_put(obj);
out:
	return err;
}

int i915_gem_object_mock_selftests(void)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_gem_object),
		SUBTEST(igt_phys_object),
	};
	struct drm_i915_private *i915;
	int err;

	i915 = mock_gem_device();
	if (!i915)
		return -ENOMEM;

	err = i915_subtests(tests, i915);

	drm_dev_unref(&i915->drm);
	return err;
}
