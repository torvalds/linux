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

static int igt_fence_wait(void *arg)
{
	const long T = HZ / 4;
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_request *request;
	int err = -EINVAL;

	/* Submit a request, treat it as a fence and wait upon it */

	mutex_lock(&i915->drm.struct_mutex);
	request = mock_request(i915->engine[RCS], i915->kernel_context, T);
	if (!request) {
		err = -ENOMEM;
		goto out_locked;
	}
	mutex_unlock(&i915->drm.struct_mutex); /* safe as we are single user */

	if (dma_fence_wait_timeout(&request->fence, false, T) != -ETIME) {
		pr_err("fence wait success before submit (expected timeout)!\n");
		goto out_device;
	}

	mutex_lock(&i915->drm.struct_mutex);
	i915_add_request(request);
	mutex_unlock(&i915->drm.struct_mutex);

	if (dma_fence_is_signaled(&request->fence)) {
		pr_err("fence signaled immediately!\n");
		goto out_device;
	}

	if (dma_fence_wait_timeout(&request->fence, false, T / 2) != -ETIME) {
		pr_err("fence wait success after submit (expected timeout)!\n");
		goto out_device;
	}

	if (dma_fence_wait_timeout(&request->fence, false, T) <= 0) {
		pr_err("fence wait timed out (expected success)!\n");
		goto out_device;
	}

	if (!dma_fence_is_signaled(&request->fence)) {
		pr_err("fence unsignaled after waiting!\n");
		goto out_device;
	}

	if (dma_fence_wait_timeout(&request->fence, false, T) <= 0) {
		pr_err("fence wait timed out when complete (expected success)!\n");
		goto out_device;
	}

	err = 0;
out_device:
	mutex_lock(&i915->drm.struct_mutex);
out_locked:
	mock_device_flush(i915);
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

int i915_gem_request_mock_selftests(void)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_add_request),
		SUBTEST(igt_wait_request),
		SUBTEST(igt_fence_wait),
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

struct live_test {
	struct drm_i915_private *i915;
	const char *func;
	const char *name;

	unsigned int reset_count;
};

static int begin_live_test(struct live_test *t,
			   struct drm_i915_private *i915,
			   const char *func,
			   const char *name)
{
	int err;

	t->i915 = i915;
	t->func = func;
	t->name = name;

	err = i915_gem_wait_for_idle(i915, I915_WAIT_LOCKED);
	if (err) {
		pr_err("%s(%s): failed to idle before, with err=%d!",
		       func, name, err);
		return err;
	}

	i915_gem_retire_requests(i915);

	i915->gpu_error.missed_irq_rings = 0;
	t->reset_count = i915_reset_count(&i915->gpu_error);

	return 0;
}

static int end_live_test(struct live_test *t)
{
	struct drm_i915_private *i915 = t->i915;

	if (wait_for(intel_execlists_idle(i915), 1)) {
		pr_err("%s(%s): GPU not idle\n", t->func, t->name);
		return -EIO;
	}

	if (t->reset_count != i915_reset_count(&i915->gpu_error)) {
		pr_err("%s(%s): GPU was reset %d times!\n",
		       t->func, t->name,
		       i915_reset_count(&i915->gpu_error) - t->reset_count);
		return -EIO;
	}

	if (i915->gpu_error.missed_irq_rings) {
		pr_err("%s(%s): Missed interrupts on engines %lx\n",
		       t->func, t->name, i915->gpu_error.missed_irq_rings);
		return -EIO;
	}

	return 0;
}

static int live_nop_request(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_engine_cs *engine;
	struct live_test t;
	unsigned int id;
	int err;

	/* Submit various sized batches of empty requests, to each engine
	 * (individually), and wait for the batch to complete. We can check
	 * the overhead of submitting requests to the hardware.
	 */

	mutex_lock(&i915->drm.struct_mutex);

	for_each_engine(engine, i915, id) {
		IGT_TIMEOUT(end_time);
		struct drm_i915_gem_request *request;
		unsigned long n, prime;
		ktime_t times[2] = {};

		err = begin_live_test(&t, i915, __func__, engine->name);
		if (err)
			goto out_unlock;

		for_each_prime_number_from(prime, 1, 8192) {
			times[1] = ktime_get_raw();

			for (n = 0; n < prime; n++) {
				request = i915_gem_request_alloc(engine,
								 i915->kernel_context);
				if (IS_ERR(request)) {
					err = PTR_ERR(request);
					goto out_unlock;
				}

				/* This space is left intentionally blank.
				 *
				 * We do not actually want to perform any
				 * action with this request, we just want
				 * to measure the latency in allocation
				 * and submission of our breadcrumbs -
				 * ensuring that the bare request is sufficient
				 * for the system to work (i.e. proper HEAD
				 * tracking of the rings, interrupt handling,
				 * etc). It also gives us the lowest bounds
				 * for latency.
				 */

				i915_add_request(request);
			}
			i915_wait_request(request,
					  I915_WAIT_LOCKED,
					  MAX_SCHEDULE_TIMEOUT);

			times[1] = ktime_sub(ktime_get_raw(), times[1]);
			if (prime == 1)
				times[0] = times[1];

			if (__igt_timeout(end_time, NULL))
				break;
		}

		err = end_live_test(&t);
		if (err)
			goto out_unlock;

		pr_info("Request latencies on %s: 1 = %lluns, %lu = %lluns\n",
			engine->name,
			ktime_to_ns(times[0]),
			prime, div64_u64(ktime_to_ns(times[1]), prime));
	}

out_unlock:
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

int i915_gem_request_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_nop_request),
	};
	return i915_subtests(tests, i915);
}
