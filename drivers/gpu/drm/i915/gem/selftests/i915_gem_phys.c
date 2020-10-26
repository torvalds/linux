/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2016 Intel Corporation
 */

#include "i915_selftest.h"

#include "selftests/mock_gem_device.h"

static int mock_phys_object(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_object *obj;
	int err;

	/* Create an object and bind it to a contiguous set of physical pages,
	 * i.e. exercise the i915_gem_object_phys API.
	 */

	obj = i915_gem_object_create_shmem(i915, PAGE_SIZE);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		pr_err("i915_gem_object_create failed, err=%d\n", err);
		goto out;
	}

	err = i915_gem_object_attach_phys(obj, PAGE_SIZE);
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
	i915_gem_object_lock(obj, NULL);
	err = i915_gem_object_set_to_gtt_domain(obj, true);
	i915_gem_object_unlock(obj);
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

int i915_gem_phys_mock_selftests(void)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(mock_phys_object),
	};
	struct drm_i915_private *i915;
	int err;

	i915 = mock_gem_device();
	if (!i915)
		return -ENOMEM;

	err = i915_subtests(tests, i915);

	mock_destroy_device(i915);
	return err;
}
