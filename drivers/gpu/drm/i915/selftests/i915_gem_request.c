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

#include "mock_context.h"
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

static int igt_request_rewind(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_request *request, *vip;
	struct i915_gem_context *ctx[2];
	int err = -EINVAL;

	mutex_lock(&i915->drm.struct_mutex);
	ctx[0] = mock_context(i915, "A");
	request = mock_request(i915->engine[RCS], ctx[0], 2 * HZ);
	if (!request) {
		err = -ENOMEM;
		goto err_context_0;
	}

	i915_gem_request_get(request);
	i915_add_request(request);

	ctx[1] = mock_context(i915, "B");
	vip = mock_request(i915->engine[RCS], ctx[1], 0);
	if (!vip) {
		err = -ENOMEM;
		goto err_context_1;
	}

	/* Simulate preemption by manual reordering */
	if (!mock_cancel_request(request)) {
		pr_err("failed to cancel request (already executed)!\n");
		i915_add_request(vip);
		goto err_context_1;
	}
	i915_gem_request_get(vip);
	i915_add_request(vip);
	rcu_read_lock();
	request->engine->submit_request(request);
	rcu_read_unlock();

	mutex_unlock(&i915->drm.struct_mutex);

	if (i915_wait_request(vip, 0, HZ) == -ETIME) {
		pr_err("timed out waiting for high priority request, vip.seqno=%d, current seqno=%d\n",
		       vip->global_seqno, intel_engine_get_seqno(i915->engine[RCS]));
		goto err;
	}

	if (i915_gem_request_completed(request)) {
		pr_err("low priority request already completed\n");
		goto err;
	}

	err = 0;
err:
	i915_gem_request_put(vip);
	mutex_lock(&i915->drm.struct_mutex);
err_context_1:
	mock_context_close(ctx[1]);
	i915_gem_request_put(request);
err_context_0:
	mock_context_close(ctx[0]);
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
		SUBTEST(igt_request_rewind),
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

	i915->gpu_error.missed_irq_rings = 0;
	t->reset_count = i915_reset_count(&i915->gpu_error);

	return 0;
}

static int end_live_test(struct live_test *t)
{
	struct drm_i915_private *i915 = t->i915;

	i915_gem_retire_requests(i915);

	if (wait_for(intel_engines_are_idle(i915), 10)) {
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
	int err = -ENODEV;

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

static struct i915_vma *empty_batch(struct drm_i915_private *i915)
{
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	u32 *cmd;
	int err;

	obj = i915_gem_object_create_internal(i915, PAGE_SIZE);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	cmd = i915_gem_object_pin_map(obj, I915_MAP_WB);
	if (IS_ERR(cmd)) {
		err = PTR_ERR(cmd);
		goto err;
	}

	*cmd = MI_BATCH_BUFFER_END;
	i915_gem_chipset_flush(i915);

	i915_gem_object_unpin_map(obj);

	err = i915_gem_object_set_to_gtt_domain(obj, false);
	if (err)
		goto err;

	vma = i915_vma_instance(obj, &i915->ggtt.base, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto err;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_USER | PIN_GLOBAL);
	if (err)
		goto err;

	return vma;

err:
	i915_gem_object_put(obj);
	return ERR_PTR(err);
}

static struct drm_i915_gem_request *
empty_request(struct intel_engine_cs *engine,
	      struct i915_vma *batch)
{
	struct drm_i915_gem_request *request;
	int err;

	request = i915_gem_request_alloc(engine,
					 engine->i915->kernel_context);
	if (IS_ERR(request))
		return request;

	err = engine->emit_bb_start(request,
				    batch->node.start,
				    batch->node.size,
				    I915_DISPATCH_SECURE);
	if (err)
		goto out_request;

out_request:
	__i915_add_request(request, err == 0);
	return err ? ERR_PTR(err) : request;
}

static int live_empty_request(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_engine_cs *engine;
	struct live_test t;
	struct i915_vma *batch;
	unsigned int id;
	int err = 0;

	/* Submit various sized batches of empty requests, to each engine
	 * (individually), and wait for the batch to complete. We can check
	 * the overhead of submitting requests to the hardware.
	 */

	mutex_lock(&i915->drm.struct_mutex);

	batch = empty_batch(i915);
	if (IS_ERR(batch)) {
		err = PTR_ERR(batch);
		goto out_unlock;
	}

	for_each_engine(engine, i915, id) {
		IGT_TIMEOUT(end_time);
		struct drm_i915_gem_request *request;
		unsigned long n, prime;
		ktime_t times[2] = {};

		err = begin_live_test(&t, i915, __func__, engine->name);
		if (err)
			goto out_batch;

		/* Warmup / preload */
		request = empty_request(engine, batch);
		if (IS_ERR(request)) {
			err = PTR_ERR(request);
			goto out_batch;
		}
		i915_wait_request(request,
				  I915_WAIT_LOCKED,
				  MAX_SCHEDULE_TIMEOUT);

		for_each_prime_number_from(prime, 1, 8192) {
			times[1] = ktime_get_raw();

			for (n = 0; n < prime; n++) {
				request = empty_request(engine, batch);
				if (IS_ERR(request)) {
					err = PTR_ERR(request);
					goto out_batch;
				}
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
			goto out_batch;

		pr_info("Batch latencies on %s: 1 = %lluns, %lu = %lluns\n",
			engine->name,
			ktime_to_ns(times[0]),
			prime, div64_u64(ktime_to_ns(times[1]), prime));
	}

out_batch:
	i915_vma_unpin(batch);
	i915_vma_put(batch);
out_unlock:
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

static struct i915_vma *recursive_batch(struct drm_i915_private *i915)
{
	struct i915_gem_context *ctx = i915->kernel_context;
	struct i915_address_space *vm = ctx->ppgtt ? &ctx->ppgtt->base : &i915->ggtt.base;
	struct drm_i915_gem_object *obj;
	const int gen = INTEL_GEN(i915);
	struct i915_vma *vma;
	u32 *cmd;
	int err;

	obj = i915_gem_object_create_internal(i915, PAGE_SIZE);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	vma = i915_vma_instance(obj, vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto err;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_USER);
	if (err)
		goto err;

	err = i915_gem_object_set_to_wc_domain(obj, true);
	if (err)
		goto err;

	cmd = i915_gem_object_pin_map(obj, I915_MAP_WC);
	if (IS_ERR(cmd)) {
		err = PTR_ERR(cmd);
		goto err;
	}

	if (gen >= 8) {
		*cmd++ = MI_BATCH_BUFFER_START | 1 << 8 | 1;
		*cmd++ = lower_32_bits(vma->node.start);
		*cmd++ = upper_32_bits(vma->node.start);
	} else if (gen >= 6) {
		*cmd++ = MI_BATCH_BUFFER_START | 1 << 8;
		*cmd++ = lower_32_bits(vma->node.start);
	} else if (gen >= 4) {
		*cmd++ = MI_BATCH_BUFFER_START | MI_BATCH_GTT;
		*cmd++ = lower_32_bits(vma->node.start);
	} else {
		*cmd++ = MI_BATCH_BUFFER_START | MI_BATCH_GTT | 1;
		*cmd++ = lower_32_bits(vma->node.start);
	}
	*cmd++ = MI_BATCH_BUFFER_END; /* terminate early in case of error */
	i915_gem_chipset_flush(i915);

	i915_gem_object_unpin_map(obj);

	return vma;

err:
	i915_gem_object_put(obj);
	return ERR_PTR(err);
}

static int recursive_batch_resolve(struct i915_vma *batch)
{
	u32 *cmd;

	cmd = i915_gem_object_pin_map(batch->obj, I915_MAP_WC);
	if (IS_ERR(cmd))
		return PTR_ERR(cmd);

	*cmd = MI_BATCH_BUFFER_END;
	i915_gem_chipset_flush(batch->vm->i915);

	i915_gem_object_unpin_map(batch->obj);

	return 0;
}

static int live_all_engines(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_engine_cs *engine;
	struct drm_i915_gem_request *request[I915_NUM_ENGINES];
	struct i915_vma *batch;
	struct live_test t;
	unsigned int id;
	int err;

	/* Check we can submit requests to all engines simultaneously. We
	 * send a recursive batch to each engine - checking that we don't
	 * block doing so, and that they don't complete too soon.
	 */

	mutex_lock(&i915->drm.struct_mutex);

	err = begin_live_test(&t, i915, __func__, "");
	if (err)
		goto out_unlock;

	batch = recursive_batch(i915);
	if (IS_ERR(batch)) {
		err = PTR_ERR(batch);
		pr_err("%s: Unable to create batch, err=%d\n", __func__, err);
		goto out_unlock;
	}

	for_each_engine(engine, i915, id) {
		request[id] = i915_gem_request_alloc(engine,
						     i915->kernel_context);
		if (IS_ERR(request[id])) {
			err = PTR_ERR(request[id]);
			pr_err("%s: Request allocation failed with err=%d\n",
			       __func__, err);
			goto out_request;
		}

		err = engine->emit_bb_start(request[id],
					    batch->node.start,
					    batch->node.size,
					    0);
		GEM_BUG_ON(err);
		request[id]->batch = batch;

		if (!i915_gem_object_has_active_reference(batch->obj)) {
			i915_gem_object_get(batch->obj);
			i915_gem_object_set_active_reference(batch->obj);
		}

		i915_vma_move_to_active(batch, request[id], 0);
		i915_gem_request_get(request[id]);
		i915_add_request(request[id]);
	}

	for_each_engine(engine, i915, id) {
		if (i915_gem_request_completed(request[id])) {
			pr_err("%s(%s): request completed too early!\n",
			       __func__, engine->name);
			err = -EINVAL;
			goto out_request;
		}
	}

	err = recursive_batch_resolve(batch);
	if (err) {
		pr_err("%s: failed to resolve batch, err=%d\n", __func__, err);
		goto out_request;
	}

	for_each_engine(engine, i915, id) {
		long timeout;

		timeout = i915_wait_request(request[id],
					    I915_WAIT_LOCKED,
					    MAX_SCHEDULE_TIMEOUT);
		if (timeout < 0) {
			err = timeout;
			pr_err("%s: error waiting for request on %s, err=%d\n",
			       __func__, engine->name, err);
			goto out_request;
		}

		GEM_BUG_ON(!i915_gem_request_completed(request[id]));
		i915_gem_request_put(request[id]);
		request[id] = NULL;
	}

	err = end_live_test(&t);

out_request:
	for_each_engine(engine, i915, id)
		if (request[id])
			i915_gem_request_put(request[id]);
	i915_vma_unpin(batch);
	i915_vma_put(batch);
out_unlock:
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

static int live_sequential_engines(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct drm_i915_gem_request *request[I915_NUM_ENGINES] = {};
	struct drm_i915_gem_request *prev = NULL;
	struct intel_engine_cs *engine;
	struct live_test t;
	unsigned int id;
	int err;

	/* Check we can submit requests to all engines sequentially, such
	 * that each successive request waits for the earlier ones. This
	 * tests that we don't execute requests out of order, even though
	 * they are running on independent engines.
	 */

	mutex_lock(&i915->drm.struct_mutex);

	err = begin_live_test(&t, i915, __func__, "");
	if (err)
		goto out_unlock;

	for_each_engine(engine, i915, id) {
		struct i915_vma *batch;

		batch = recursive_batch(i915);
		if (IS_ERR(batch)) {
			err = PTR_ERR(batch);
			pr_err("%s: Unable to create batch for %s, err=%d\n",
			       __func__, engine->name, err);
			goto out_unlock;
		}

		request[id] = i915_gem_request_alloc(engine,
						     i915->kernel_context);
		if (IS_ERR(request[id])) {
			err = PTR_ERR(request[id]);
			pr_err("%s: Request allocation failed for %s with err=%d\n",
			       __func__, engine->name, err);
			goto out_request;
		}

		if (prev) {
			err = i915_gem_request_await_dma_fence(request[id],
							       &prev->fence);
			if (err) {
				i915_add_request(request[id]);
				pr_err("%s: Request await failed for %s with err=%d\n",
				       __func__, engine->name, err);
				goto out_request;
			}
		}

		err = engine->emit_bb_start(request[id],
					    batch->node.start,
					    batch->node.size,
					    0);
		GEM_BUG_ON(err);
		request[id]->batch = batch;

		i915_vma_move_to_active(batch, request[id], 0);
		i915_gem_object_set_active_reference(batch->obj);
		i915_vma_get(batch);

		i915_gem_request_get(request[id]);
		i915_add_request(request[id]);

		prev = request[id];
	}

	for_each_engine(engine, i915, id) {
		long timeout;

		if (i915_gem_request_completed(request[id])) {
			pr_err("%s(%s): request completed too early!\n",
			       __func__, engine->name);
			err = -EINVAL;
			goto out_request;
		}

		err = recursive_batch_resolve(request[id]->batch);
		if (err) {
			pr_err("%s: failed to resolve batch, err=%d\n",
			       __func__, err);
			goto out_request;
		}

		timeout = i915_wait_request(request[id],
					    I915_WAIT_LOCKED,
					    MAX_SCHEDULE_TIMEOUT);
		if (timeout < 0) {
			err = timeout;
			pr_err("%s: error waiting for request on %s, err=%d\n",
			       __func__, engine->name, err);
			goto out_request;
		}

		GEM_BUG_ON(!i915_gem_request_completed(request[id]));
	}

	err = end_live_test(&t);

out_request:
	for_each_engine(engine, i915, id) {
		u32 *cmd;

		if (!request[id])
			break;

		cmd = i915_gem_object_pin_map(request[id]->batch->obj,
					      I915_MAP_WC);
		if (!IS_ERR(cmd)) {
			*cmd = MI_BATCH_BUFFER_END;
			i915_gem_chipset_flush(i915);

			i915_gem_object_unpin_map(request[id]->batch->obj);
		}

		i915_vma_put(request[id]->batch);
		i915_gem_request_put(request[id]);
	}
out_unlock:
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

int i915_gem_request_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_nop_request),
		SUBTEST(live_all_engines),
		SUBTEST(live_sequential_engines),
		SUBTEST(live_empty_request),
	};
	return i915_subtests(tests, i915);
}
