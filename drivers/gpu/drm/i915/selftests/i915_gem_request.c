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

static int igt_add_request(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_request *request;
	int err = -ENOMEM;

	/* Basic preliminary test to create a request and let it loose! */

	mutex_lock(&i915->drm.struct_mutex);
	request = mock_request(i915->engine[RCS],
			       i915->kernel_context,
			       HZ / 10);
	if (!request)
		goto out_unlock;

	i915_add_request(request);

	err = 0;
out_unlock:
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

static int igt_wait_request(void *arg)
{
	const long T = HZ / 4;
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_request *request;
	int err = -EINVAL;

	/* Submit a request, then wait upon it */

	mutex_lock(&i915->drm.struct_mutex);
	request = mock_request(i915->engine[RCS], i915->kernel_context, T);
	if (!request) {
		err = -ENOMEM;
		goto out_unlock;
	}

	if (i915_wait_request(request, I915_WAIT_LOCKED, 0) != -ETIME) {
		pr_err("request wait (busy query) succeeded (expected timeout before submit!)\n");
		goto out_unlock;
	}

	if (i915_wait_request(request, I915_WAIT_LOCKED, T) != -ETIME) {
		pr_err("request wait succeeded (expected timeout before submit!)\n");
		goto out_unlock;
	}

	if (i915_gem_request_completed(request)) {
		pr_err("request completed before submit!!\n");
		goto out_unlock;
	}

	i915_add_request(request);

	if (i915_wait_request(request, I915_WAIT_LOCKED, 0) != -ETIME) {
		pr_err("request wait (busy query) succeeded (expected timeout after submit!)\n");
		goto out_unlock;
	}

	if (i915_gem_request_completed(request)) {
		pr_err("request completed immediately!\n");
		goto out_unlock;
	}

	if (i915_wait_request(request, I915_WAIT_LOCKED, T / 2) != -ETIME) {
		pr_err("request wait succeeded (expected timeout!)\n");
		goto out_unlock;
	}

	if (i915_wait_request(request, I915_WAIT_LOCKED, T) == -ETIME) {
		pr_err("request wait timed out!\n");
		goto out_unlock;
	}

	if (!i915_gem_request_completed(request)) {
		pr_err("request not complete after waiting!\n");
		goto out_unlock;
	}

	if (i915_wait_request(request, I915_WAIT_LOCKED, T) == -ETIME) {
		pr_err("request wait timed out when already complete!\n");
		goto out_unlock;
	}

	err = 0;
out_unlock:
	mock_device_flush(i915);
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

int i915_gem_request_mock_selftests(void)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_add_request),
		SUBTEST(igt_wait_request),
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
