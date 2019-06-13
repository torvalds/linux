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

#include "gem/i915_gem_pm.h"
#include "gem/selftests/mock_context.h"

#include "i915_random.h"
#include "i915_selftest.h"
#include "igt_live_test.h"
#include "lib_sw_fence.h"

#include "mock_drm.h"
#include "mock_gem_device.h"

static int igt_add_request(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct i915_request *request;
	int err = -ENOMEM;

	/* Basic preliminary test to create a request and let it loose! */

	mutex_lock(&i915->drm.struct_mutex);
	request = mock_request(i915->engine[RCS0],
			       i915->kernel_context,
			       HZ / 10);
	if (!request)
		goto out_unlock;

	i915_request_add(request);

	err = 0;
out_unlock:
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

static int igt_wait_request(void *arg)
{
	const long T = HZ / 4;
	struct drm_i915_private *i915 = arg;
	struct i915_request *request;
	int err = -EINVAL;

	/* Submit a request, then wait upon it */

	mutex_lock(&i915->drm.struct_mutex);
	request = mock_request(i915->engine[RCS0], i915->kernel_context, T);
	if (!request) {
		err = -ENOMEM;
		goto out_unlock;
	}

	if (i915_request_wait(request, I915_WAIT_LOCKED, 0) != -ETIME) {
		pr_err("request wait (busy query) succeeded (expected timeout before submit!)\n");
		goto out_unlock;
	}

	if (i915_request_wait(request, I915_WAIT_LOCKED, T) != -ETIME) {
		pr_err("request wait succeeded (expected timeout before submit!)\n");
		goto out_unlock;
	}

	if (i915_request_completed(request)) {
		pr_err("request completed before submit!!\n");
		goto out_unlock;
	}

	i915_request_add(request);

	if (i915_request_wait(request, I915_WAIT_LOCKED, 0) != -ETIME) {
		pr_err("request wait (busy query) succeeded (expected timeout after submit!)\n");
		goto out_unlock;
	}

	if (i915_request_completed(request)) {
		pr_err("request completed immediately!\n");
		goto out_unlock;
	}

	if (i915_request_wait(request, I915_WAIT_LOCKED, T / 2) != -ETIME) {
		pr_err("request wait succeeded (expected timeout!)\n");
		goto out_unlock;
	}

	if (i915_request_wait(request, I915_WAIT_LOCKED, T) == -ETIME) {
		pr_err("request wait timed out!\n");
		goto out_unlock;
	}

	if (!i915_request_completed(request)) {
		pr_err("request not complete after waiting!\n");
		goto out_unlock;
	}

	if (i915_request_wait(request, I915_WAIT_LOCKED, T) == -ETIME) {
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
	struct i915_request *request;
	int err = -EINVAL;

	/* Submit a request, treat it as a fence and wait upon it */

	mutex_lock(&i915->drm.struct_mutex);
	request = mock_request(i915->engine[RCS0], i915->kernel_context, T);
	if (!request) {
		err = -ENOMEM;
		goto out_locked;
	}

	if (dma_fence_wait_timeout(&request->fence, false, T) != -ETIME) {
		pr_err("fence wait success before submit (expected timeout)!\n");
		goto out_locked;
	}

	i915_request_add(request);
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
	struct i915_request *request, *vip;
	struct i915_gem_context *ctx[2];
	int err = -EINVAL;

	mutex_lock(&i915->drm.struct_mutex);
	ctx[0] = mock_context(i915, "A");
	request = mock_request(i915->engine[RCS0], ctx[0], 2 * HZ);
	if (!request) {
		err = -ENOMEM;
		goto err_context_0;
	}

	i915_request_get(request);
	i915_request_add(request);

	ctx[1] = mock_context(i915, "B");
	vip = mock_request(i915->engine[RCS0], ctx[1], 0);
	if (!vip) {
		err = -ENOMEM;
		goto err_context_1;
	}

	/* Simulate preemption by manual reordering */
	if (!mock_cancel_request(request)) {
		pr_err("failed to cancel request (already executed)!\n");
		i915_request_add(vip);
		goto err_context_1;
	}
	i915_request_get(vip);
	i915_request_add(vip);
	rcu_read_lock();
	request->engine->submit_request(request);
	rcu_read_unlock();

	mutex_unlock(&i915->drm.struct_mutex);

	if (i915_request_wait(vip, 0, HZ) == -ETIME) {
		pr_err("timed out waiting for high priority request\n");
		goto err;
	}

	if (i915_request_completed(request)) {
		pr_err("low priority request already completed\n");
		goto err;
	}

	err = 0;
err:
	i915_request_put(vip);
	mutex_lock(&i915->drm.struct_mutex);
err_context_1:
	mock_context_close(ctx[1]);
	i915_request_put(request);
err_context_0:
	mock_context_close(ctx[0]);
	mock_device_flush(i915);
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

struct smoketest {
	struct intel_engine_cs *engine;
	struct i915_gem_context **contexts;
	atomic_long_t num_waits, num_fences;
	int ncontexts, max_batch;
	struct i915_request *(*request_alloc)(struct i915_gem_context *,
					      struct intel_engine_cs *);
};

static struct i915_request *
__mock_request_alloc(struct i915_gem_context *ctx,
		     struct intel_engine_cs *engine)
{
	return mock_request(engine, ctx, 0);
}

static struct i915_request *
__live_request_alloc(struct i915_gem_context *ctx,
		     struct intel_engine_cs *engine)
{
	return igt_request_alloc(ctx, engine);
}

static int __igt_breadcrumbs_smoketest(void *arg)
{
	struct smoketest *t = arg;
	struct mutex * const BKL = &t->engine->i915->drm.struct_mutex;
	const unsigned int max_batch = min(t->ncontexts, t->max_batch) - 1;
	const unsigned int total = 4 * t->ncontexts + 1;
	unsigned int num_waits = 0, num_fences = 0;
	struct i915_request **requests;
	I915_RND_STATE(prng);
	unsigned int *order;
	int err = 0;

	/*
	 * A very simple test to catch the most egregious of list handling bugs.
	 *
	 * At its heart, we simply create oodles of requests running across
	 * multiple kthreads and enable signaling on them, for the sole purpose
	 * of stressing our breadcrumb handling. The only inspection we do is
	 * that the fences were marked as signaled.
	 */

	requests = kmalloc_array(total, sizeof(*requests), GFP_KERNEL);
	if (!requests)
		return -ENOMEM;

	order = i915_random_order(total, &prng);
	if (!order) {
		err = -ENOMEM;
		goto out_requests;
	}

	while (!kthread_should_stop()) {
		struct i915_sw_fence *submit, *wait;
		unsigned int n, count;

		submit = heap_fence_create(GFP_KERNEL);
		if (!submit) {
			err = -ENOMEM;
			break;
		}

		wait = heap_fence_create(GFP_KERNEL);
		if (!wait) {
			i915_sw_fence_commit(submit);
			heap_fence_put(submit);
			err = ENOMEM;
			break;
		}

		i915_random_reorder(order, total, &prng);
		count = 1 + i915_prandom_u32_max_state(max_batch, &prng);

		for (n = 0; n < count; n++) {
			struct i915_gem_context *ctx =
				t->contexts[order[n] % t->ncontexts];
			struct i915_request *rq;

			mutex_lock(BKL);

			rq = t->request_alloc(ctx, t->engine);
			if (IS_ERR(rq)) {
				mutex_unlock(BKL);
				err = PTR_ERR(rq);
				count = n;
				break;
			}

			err = i915_sw_fence_await_sw_fence_gfp(&rq->submit,
							       submit,
							       GFP_KERNEL);

			requests[n] = i915_request_get(rq);
			i915_request_add(rq);

			mutex_unlock(BKL);

			if (err >= 0)
				err = i915_sw_fence_await_dma_fence(wait,
								    &rq->fence,
								    0,
								    GFP_KERNEL);

			if (err < 0) {
				i915_request_put(rq);
				count = n;
				break;
			}
		}

		i915_sw_fence_commit(submit);
		i915_sw_fence_commit(wait);

		if (!wait_event_timeout(wait->wait,
					i915_sw_fence_done(wait),
					HZ / 2)) {
			struct i915_request *rq = requests[count - 1];

			pr_err("waiting for %d fences (last %llx:%lld) on %s timed out!\n",
			       count,
			       rq->fence.context, rq->fence.seqno,
			       t->engine->name);
			i915_gem_set_wedged(t->engine->i915);
			GEM_BUG_ON(!i915_request_completed(rq));
			i915_sw_fence_wait(wait);
			err = -EIO;
		}

		for (n = 0; n < count; n++) {
			struct i915_request *rq = requests[n];

			if (!test_bit(DMA_FENCE_FLAG_SIGNALED_BIT,
				      &rq->fence.flags)) {
				pr_err("%llu:%llu was not signaled!\n",
				       rq->fence.context, rq->fence.seqno);
				err = -EINVAL;
			}

			i915_request_put(rq);
		}

		heap_fence_put(wait);
		heap_fence_put(submit);

		if (err < 0)
			break;

		num_fences += count;
		num_waits++;

		cond_resched();
	}

	atomic_long_add(num_fences, &t->num_fences);
	atomic_long_add(num_waits, &t->num_waits);

	kfree(order);
out_requests:
	kfree(requests);
	return err;
}

static int mock_breadcrumbs_smoketest(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct smoketest t = {
		.engine = i915->engine[RCS0],
		.ncontexts = 1024,
		.max_batch = 1024,
		.request_alloc = __mock_request_alloc
	};
	unsigned int ncpus = num_online_cpus();
	struct task_struct **threads;
	unsigned int n;
	int ret = 0;

	/*
	 * Smoketest our breadcrumb/signal handling for requests across multiple
	 * threads. A very simple test to only catch the most egregious of bugs.
	 * See __igt_breadcrumbs_smoketest();
	 */

	threads = kmalloc_array(ncpus, sizeof(*threads), GFP_KERNEL);
	if (!threads)
		return -ENOMEM;

	t.contexts =
		kmalloc_array(t.ncontexts, sizeof(*t.contexts), GFP_KERNEL);
	if (!t.contexts) {
		ret = -ENOMEM;
		goto out_threads;
	}

	mutex_lock(&t.engine->i915->drm.struct_mutex);
	for (n = 0; n < t.ncontexts; n++) {
		t.contexts[n] = mock_context(t.engine->i915, "mock");
		if (!t.contexts[n]) {
			ret = -ENOMEM;
			goto out_contexts;
		}
	}
	mutex_unlock(&t.engine->i915->drm.struct_mutex);

	for (n = 0; n < ncpus; n++) {
		threads[n] = kthread_run(__igt_breadcrumbs_smoketest,
					 &t, "igt/%d", n);
		if (IS_ERR(threads[n])) {
			ret = PTR_ERR(threads[n]);
			ncpus = n;
			break;
		}

		get_task_struct(threads[n]);
	}

	msleep(jiffies_to_msecs(i915_selftest.timeout_jiffies));

	for (n = 0; n < ncpus; n++) {
		int err;

		err = kthread_stop(threads[n]);
		if (err < 0 && !ret)
			ret = err;

		put_task_struct(threads[n]);
	}
	pr_info("Completed %lu waits for %lu fence across %d cpus\n",
		atomic_long_read(&t.num_waits),
		atomic_long_read(&t.num_fences),
		ncpus);

	mutex_lock(&t.engine->i915->drm.struct_mutex);
out_contexts:
	for (n = 0; n < t.ncontexts; n++) {
		if (!t.contexts[n])
			break;
		mock_context_close(t.contexts[n]);
	}
	mutex_unlock(&t.engine->i915->drm.struct_mutex);
	kfree(t.contexts);
out_threads:
	kfree(threads);

	return ret;
}

int i915_request_mock_selftests(void)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_add_request),
		SUBTEST(igt_wait_request),
		SUBTEST(igt_fence_wait),
		SUBTEST(igt_request_rewind),
		SUBTEST(mock_breadcrumbs_smoketest),
	};
	struct drm_i915_private *i915;
	intel_wakeref_t wakeref;
	int err = 0;

	i915 = mock_gem_device();
	if (!i915)
		return -ENOMEM;

	with_intel_runtime_pm(i915, wakeref)
		err = i915_subtests(tests, i915);

	drm_dev_put(&i915->drm);

	return err;
}

static int live_nop_request(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_engine_cs *engine;
	intel_wakeref_t wakeref;
	struct igt_live_test t;
	unsigned int id;
	int err = -ENODEV;

	/* Submit various sized batches of empty requests, to each engine
	 * (individually), and wait for the batch to complete. We can check
	 * the overhead of submitting requests to the hardware.
	 */

	mutex_lock(&i915->drm.struct_mutex);
	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	for_each_engine(engine, i915, id) {
		struct i915_request *request = NULL;
		unsigned long n, prime;
		IGT_TIMEOUT(end_time);
		ktime_t times[2] = {};

		err = igt_live_test_begin(&t, i915, __func__, engine->name);
		if (err)
			goto out_unlock;

		for_each_prime_number_from(prime, 1, 8192) {
			times[1] = ktime_get_raw();

			for (n = 0; n < prime; n++) {
				request = i915_request_create(engine->kernel_context);
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

				i915_request_add(request);
			}
			i915_request_wait(request,
					  I915_WAIT_LOCKED,
					  MAX_SCHEDULE_TIMEOUT);

			times[1] = ktime_sub(ktime_get_raw(), times[1]);
			if (prime == 1)
				times[0] = times[1];

			if (__igt_timeout(end_time, NULL))
				break;
		}

		err = igt_live_test_end(&t);
		if (err)
			goto out_unlock;

		pr_info("Request latencies on %s: 1 = %lluns, %lu = %lluns\n",
			engine->name,
			ktime_to_ns(times[0]),
			prime, div64_u64(ktime_to_ns(times[1]), prime));
	}

out_unlock:
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
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

	__i915_gem_object_flush_map(obj, 0, 64);
	i915_gem_object_unpin_map(obj);

	i915_gem_chipset_flush(i915);

	vma = i915_vma_instance(obj, &i915->ggtt.vm, NULL);
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

static struct i915_request *
empty_request(struct intel_engine_cs *engine,
	      struct i915_vma *batch)
{
	struct i915_request *request;
	int err;

	request = i915_request_create(engine->kernel_context);
	if (IS_ERR(request))
		return request;

	err = engine->emit_bb_start(request,
				    batch->node.start,
				    batch->node.size,
				    I915_DISPATCH_SECURE);
	if (err)
		goto out_request;

out_request:
	i915_request_add(request);
	return err ? ERR_PTR(err) : request;
}

static int live_empty_request(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_engine_cs *engine;
	intel_wakeref_t wakeref;
	struct igt_live_test t;
	struct i915_vma *batch;
	unsigned int id;
	int err = 0;

	/* Submit various sized batches of empty requests, to each engine
	 * (individually), and wait for the batch to complete. We can check
	 * the overhead of submitting requests to the hardware.
	 */

	mutex_lock(&i915->drm.struct_mutex);
	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	batch = empty_batch(i915);
	if (IS_ERR(batch)) {
		err = PTR_ERR(batch);
		goto out_unlock;
	}

	for_each_engine(engine, i915, id) {
		IGT_TIMEOUT(end_time);
		struct i915_request *request;
		unsigned long n, prime;
		ktime_t times[2] = {};

		err = igt_live_test_begin(&t, i915, __func__, engine->name);
		if (err)
			goto out_batch;

		/* Warmup / preload */
		request = empty_request(engine, batch);
		if (IS_ERR(request)) {
			err = PTR_ERR(request);
			goto out_batch;
		}
		i915_request_wait(request,
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
			i915_request_wait(request,
					  I915_WAIT_LOCKED,
					  MAX_SCHEDULE_TIMEOUT);

			times[1] = ktime_sub(ktime_get_raw(), times[1]);
			if (prime == 1)
				times[0] = times[1];

			if (__igt_timeout(end_time, NULL))
				break;
		}

		err = igt_live_test_end(&t);
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
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

static struct i915_vma *recursive_batch(struct drm_i915_private *i915)
{
	struct i915_gem_context *ctx = i915->kernel_context;
	struct i915_address_space *vm = ctx->vm ?: &i915->ggtt.vm;
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
	} else {
		*cmd++ = MI_BATCH_BUFFER_START | MI_BATCH_GTT;
		*cmd++ = lower_32_bits(vma->node.start);
	}
	*cmd++ = MI_BATCH_BUFFER_END; /* terminate early in case of error */

	__i915_gem_object_flush_map(obj, 0, 64);
	i915_gem_object_unpin_map(obj);

	i915_gem_chipset_flush(i915);

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
	struct i915_request *request[I915_NUM_ENGINES];
	intel_wakeref_t wakeref;
	struct igt_live_test t;
	struct i915_vma *batch;
	unsigned int id;
	int err;

	/* Check we can submit requests to all engines simultaneously. We
	 * send a recursive batch to each engine - checking that we don't
	 * block doing so, and that they don't complete too soon.
	 */

	mutex_lock(&i915->drm.struct_mutex);
	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	err = igt_live_test_begin(&t, i915, __func__, "");
	if (err)
		goto out_unlock;

	batch = recursive_batch(i915);
	if (IS_ERR(batch)) {
		err = PTR_ERR(batch);
		pr_err("%s: Unable to create batch, err=%d\n", __func__, err);
		goto out_unlock;
	}

	for_each_engine(engine, i915, id) {
		request[id] = i915_request_create(engine->kernel_context);
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

		i915_vma_lock(batch);
		err = i915_vma_move_to_active(batch, request[id], 0);
		i915_vma_unlock(batch);
		GEM_BUG_ON(err);

		i915_request_get(request[id]);
		i915_request_add(request[id]);
	}

	for_each_engine(engine, i915, id) {
		if (i915_request_completed(request[id])) {
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

		timeout = i915_request_wait(request[id],
					    I915_WAIT_LOCKED,
					    MAX_SCHEDULE_TIMEOUT);
		if (timeout < 0) {
			err = timeout;
			pr_err("%s: error waiting for request on %s, err=%d\n",
			       __func__, engine->name, err);
			goto out_request;
		}

		GEM_BUG_ON(!i915_request_completed(request[id]));
		i915_request_put(request[id]);
		request[id] = NULL;
	}

	err = igt_live_test_end(&t);

out_request:
	for_each_engine(engine, i915, id)
		if (request[id])
			i915_request_put(request[id]);
	i915_vma_unpin(batch);
	i915_vma_put(batch);
out_unlock:
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

static int live_sequential_engines(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct i915_request *request[I915_NUM_ENGINES] = {};
	struct i915_request *prev = NULL;
	struct intel_engine_cs *engine;
	intel_wakeref_t wakeref;
	struct igt_live_test t;
	unsigned int id;
	int err;

	/* Check we can submit requests to all engines sequentially, such
	 * that each successive request waits for the earlier ones. This
	 * tests that we don't execute requests out of order, even though
	 * they are running on independent engines.
	 */

	mutex_lock(&i915->drm.struct_mutex);
	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	err = igt_live_test_begin(&t, i915, __func__, "");
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

		request[id] = i915_request_create(engine->kernel_context);
		if (IS_ERR(request[id])) {
			err = PTR_ERR(request[id]);
			pr_err("%s: Request allocation failed for %s with err=%d\n",
			       __func__, engine->name, err);
			goto out_request;
		}

		if (prev) {
			err = i915_request_await_dma_fence(request[id],
							   &prev->fence);
			if (err) {
				i915_request_add(request[id]);
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

		i915_vma_lock(batch);
		err = i915_vma_move_to_active(batch, request[id], 0);
		i915_vma_unlock(batch);
		GEM_BUG_ON(err);

		i915_request_get(request[id]);
		i915_request_add(request[id]);

		prev = request[id];
	}

	for_each_engine(engine, i915, id) {
		long timeout;

		if (i915_request_completed(request[id])) {
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

		timeout = i915_request_wait(request[id],
					    I915_WAIT_LOCKED,
					    MAX_SCHEDULE_TIMEOUT);
		if (timeout < 0) {
			err = timeout;
			pr_err("%s: error waiting for request on %s, err=%d\n",
			       __func__, engine->name, err);
			goto out_request;
		}

		GEM_BUG_ON(!i915_request_completed(request[id]));
	}

	err = igt_live_test_end(&t);

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
		i915_request_put(request[id]);
	}
out_unlock:
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

static int
max_batches(struct i915_gem_context *ctx, struct intel_engine_cs *engine)
{
	struct i915_request *rq;
	int ret;

	/*
	 * Before execlists, all contexts share the same ringbuffer. With
	 * execlists, each context/engine has a separate ringbuffer and
	 * for the purposes of this test, inexhaustible.
	 *
	 * For the global ringbuffer though, we have to be very careful
	 * that we do not wrap while preventing the execution of requests
	 * with a unsignaled fence.
	 */
	if (HAS_EXECLISTS(ctx->i915))
		return INT_MAX;

	rq = igt_request_alloc(ctx, engine);
	if (IS_ERR(rq)) {
		ret = PTR_ERR(rq);
	} else {
		int sz;

		ret = rq->ring->size - rq->reserved_space;
		i915_request_add(rq);

		sz = rq->ring->emit - rq->head;
		if (sz < 0)
			sz += rq->ring->size;
		ret /= sz;
		ret /= 2; /* leave half spare, in case of emergency! */
	}

	return ret;
}

static int live_breadcrumbs_smoketest(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct smoketest t[I915_NUM_ENGINES];
	unsigned int ncpus = num_online_cpus();
	unsigned long num_waits, num_fences;
	struct intel_engine_cs *engine;
	struct task_struct **threads;
	struct igt_live_test live;
	enum intel_engine_id id;
	intel_wakeref_t wakeref;
	struct drm_file *file;
	unsigned int n;
	int ret = 0;

	/*
	 * Smoketest our breadcrumb/signal handling for requests across multiple
	 * threads. A very simple test to only catch the most egregious of bugs.
	 * See __igt_breadcrumbs_smoketest();
	 *
	 * On real hardware this time.
	 */

	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	file = mock_file(i915);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto out_rpm;
	}

	threads = kcalloc(ncpus * I915_NUM_ENGINES,
			  sizeof(*threads),
			  GFP_KERNEL);
	if (!threads) {
		ret = -ENOMEM;
		goto out_file;
	}

	memset(&t[0], 0, sizeof(t[0]));
	t[0].request_alloc = __live_request_alloc;
	t[0].ncontexts = 64;
	t[0].contexts = kmalloc_array(t[0].ncontexts,
				      sizeof(*t[0].contexts),
				      GFP_KERNEL);
	if (!t[0].contexts) {
		ret = -ENOMEM;
		goto out_threads;
	}

	mutex_lock(&i915->drm.struct_mutex);
	for (n = 0; n < t[0].ncontexts; n++) {
		t[0].contexts[n] = live_context(i915, file);
		if (!t[0].contexts[n]) {
			ret = -ENOMEM;
			goto out_contexts;
		}
	}

	ret = igt_live_test_begin(&live, i915, __func__, "");
	if (ret)
		goto out_contexts;

	for_each_engine(engine, i915, id) {
		t[id] = t[0];
		t[id].engine = engine;
		t[id].max_batch = max_batches(t[0].contexts[0], engine);
		if (t[id].max_batch < 0) {
			ret = t[id].max_batch;
			mutex_unlock(&i915->drm.struct_mutex);
			goto out_flush;
		}
		/* One ring interleaved between requests from all cpus */
		t[id].max_batch /= num_online_cpus() + 1;
		pr_debug("Limiting batches to %d requests on %s\n",
			 t[id].max_batch, engine->name);

		for (n = 0; n < ncpus; n++) {
			struct task_struct *tsk;

			tsk = kthread_run(__igt_breadcrumbs_smoketest,
					  &t[id], "igt/%d.%d", id, n);
			if (IS_ERR(tsk)) {
				ret = PTR_ERR(tsk);
				mutex_unlock(&i915->drm.struct_mutex);
				goto out_flush;
			}

			get_task_struct(tsk);
			threads[id * ncpus + n] = tsk;
		}
	}
	mutex_unlock(&i915->drm.struct_mutex);

	msleep(jiffies_to_msecs(i915_selftest.timeout_jiffies));

out_flush:
	num_waits = 0;
	num_fences = 0;
	for_each_engine(engine, i915, id) {
		for (n = 0; n < ncpus; n++) {
			struct task_struct *tsk = threads[id * ncpus + n];
			int err;

			if (!tsk)
				continue;

			err = kthread_stop(tsk);
			if (err < 0 && !ret)
				ret = err;

			put_task_struct(tsk);
		}

		num_waits += atomic_long_read(&t[id].num_waits);
		num_fences += atomic_long_read(&t[id].num_fences);
	}
	pr_info("Completed %lu waits for %lu fences across %d engines and %d cpus\n",
		num_waits, num_fences, RUNTIME_INFO(i915)->num_engines, ncpus);

	mutex_lock(&i915->drm.struct_mutex);
	ret = igt_live_test_end(&live) ?: ret;
out_contexts:
	mutex_unlock(&i915->drm.struct_mutex);
	kfree(t[0].contexts);
out_threads:
	kfree(threads);
out_file:
	mock_file_free(i915, file);
out_rpm:
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);

	return ret;
}

int i915_request_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_nop_request),
		SUBTEST(live_all_engines),
		SUBTEST(live_sequential_engines),
		SUBTEST(live_empty_request),
		SUBTEST(live_breadcrumbs_smoketest),
	};

	if (i915_terminally_wedged(i915))
		return 0;

	return i915_subtests(tests, i915);
}
