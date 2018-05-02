/*
 * Copyright © 2017 Intel Corporation
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

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/prime_numbers.h>

#include "../i915_selftest.h"

static int __i915_sw_fence_call
fence_notify(struct i915_sw_fence *fence, enum i915_sw_fence_notify state)
{
	switch (state) {
	case FENCE_COMPLETE:
		break;

	case FENCE_FREE:
		/* Leave the fence for the caller to free it after testing */
		break;
	}

	return NOTIFY_DONE;
}

static struct i915_sw_fence *alloc_fence(void)
{
	struct i915_sw_fence *fence;

	fence = kmalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return NULL;

	i915_sw_fence_init(fence, fence_notify);
	return fence;
}

static void free_fence(struct i915_sw_fence *fence)
{
	i915_sw_fence_fini(fence);
	kfree(fence);
}

static int __test_self(struct i915_sw_fence *fence)
{
	if (i915_sw_fence_done(fence))
		return -EINVAL;

	i915_sw_fence_commit(fence);
	if (!i915_sw_fence_done(fence))
		return -EINVAL;

	i915_sw_fence_wait(fence);
	if (!i915_sw_fence_done(fence))
		return -EINVAL;

	return 0;
}

static int test_self(void *arg)
{
	struct i915_sw_fence *fence;
	int ret;

	/* Test i915_sw_fence signaling and completion testing */
	fence = alloc_fence();
	if (!fence)
		return -ENOMEM;

	ret = __test_self(fence);

	free_fence(fence);
	return ret;
}

static int test_dag(void *arg)
{
	struct i915_sw_fence *A, *B, *C;
	int ret = -EINVAL;

	/* Test detection of cycles within the i915_sw_fence graphs */
	if (!IS_ENABLED(CONFIG_DRM_I915_SW_FENCE_CHECK_DAG))
		return 0;

	A = alloc_fence();
	if (!A)
		return -ENOMEM;

	if (i915_sw_fence_await_sw_fence_gfp(A, A, GFP_KERNEL) != -EINVAL) {
		pr_err("recursive cycle not detected (AA)\n");
		goto err_A;
	}

	B = alloc_fence();
	if (!B) {
		ret = -ENOMEM;
		goto err_A;
	}

	i915_sw_fence_await_sw_fence_gfp(A, B, GFP_KERNEL);
	if (i915_sw_fence_await_sw_fence_gfp(B, A, GFP_KERNEL) != -EINVAL) {
		pr_err("single depth cycle not detected (BAB)\n");
		goto err_B;
	}

	C = alloc_fence();
	if (!C) {
		ret = -ENOMEM;
		goto err_B;
	}

	if (i915_sw_fence_await_sw_fence_gfp(B, C, GFP_KERNEL) == -EINVAL) {
		pr_err("invalid cycle detected\n");
		goto err_C;
	}
	if (i915_sw_fence_await_sw_fence_gfp(C, B, GFP_KERNEL) != -EINVAL) {
		pr_err("single depth cycle not detected (CBC)\n");
		goto err_C;
	}
	if (i915_sw_fence_await_sw_fence_gfp(C, A, GFP_KERNEL) != -EINVAL) {
		pr_err("cycle not detected (BA, CB, AC)\n");
		goto err_C;
	}
	if (i915_sw_fence_await_sw_fence_gfp(A, C, GFP_KERNEL) == -EINVAL) {
		pr_err("invalid cycle detected\n");
		goto err_C;
	}

	i915_sw_fence_commit(A);
	i915_sw_fence_commit(B);
	i915_sw_fence_commit(C);

	ret = 0;
	if (!i915_sw_fence_done(C)) {
		pr_err("fence C not done\n");
		ret = -EINVAL;
	}
	if (!i915_sw_fence_done(B)) {
		pr_err("fence B not done\n");
		ret = -EINVAL;
	}
	if (!i915_sw_fence_done(A)) {
		pr_err("fence A not done\n");
		ret = -EINVAL;
	}
err_C:
	free_fence(C);
err_B:
	free_fence(B);
err_A:
	free_fence(A);
	return ret;
}

static int test_AB(void *arg)
{
	struct i915_sw_fence *A, *B;
	int ret;

	/* Test i915_sw_fence (A) waiting on an event source (B) */
	A = alloc_fence();
	if (!A)
		return -ENOMEM;
	B = alloc_fence();
	if (!B) {
		ret = -ENOMEM;
		goto err_A;
	}

	ret = i915_sw_fence_await_sw_fence_gfp(A, B, GFP_KERNEL);
	if (ret < 0)
		goto err_B;
	if (ret == 0) {
		pr_err("Incorrectly reported fence A was complete before await\n");
		ret = -EINVAL;
		goto err_B;
	}

	ret = -EINVAL;
	i915_sw_fence_commit(A);
	if (i915_sw_fence_done(A))
		goto err_B;

	i915_sw_fence_commit(B);
	if (!i915_sw_fence_done(B)) {
		pr_err("Fence B is not done\n");
		goto err_B;
	}

	if (!i915_sw_fence_done(A)) {
		pr_err("Fence A is not done\n");
		goto err_B;
	}

	ret = 0;
err_B:
	free_fence(B);
err_A:
	free_fence(A);
	return ret;
}

static int test_ABC(void *arg)
{
	struct i915_sw_fence *A, *B, *C;
	int ret;

	/* Test a chain of fences, A waits on B who waits on C */
	A = alloc_fence();
	if (!A)
		return -ENOMEM;

	B = alloc_fence();
	if (!B) {
		ret = -ENOMEM;
		goto err_A;
	}

	C = alloc_fence();
	if (!C) {
		ret = -ENOMEM;
		goto err_B;
	}

	ret = i915_sw_fence_await_sw_fence_gfp(A, B, GFP_KERNEL);
	if (ret < 0)
		goto err_C;
	if (ret == 0) {
		pr_err("Incorrectly reported fence B was complete before await\n");
		goto err_C;
	}

	ret = i915_sw_fence_await_sw_fence_gfp(B, C, GFP_KERNEL);
	if (ret < 0)
		goto err_C;
	if (ret == 0) {
		pr_err("Incorrectly reported fence C was complete before await\n");
		goto err_C;
	}

	ret = -EINVAL;
	i915_sw_fence_commit(A);
	if (i915_sw_fence_done(A)) {
		pr_err("Fence A completed early\n");
		goto err_C;
	}

	i915_sw_fence_commit(B);
	if (i915_sw_fence_done(B)) {
		pr_err("Fence B completed early\n");
		goto err_C;
	}

	if (i915_sw_fence_done(A)) {
		pr_err("Fence A completed early (after signaling B)\n");
		goto err_C;
	}

	i915_sw_fence_commit(C);

	ret = 0;
	if (!i915_sw_fence_done(C)) {
		pr_err("Fence C not done\n");
		ret = -EINVAL;
	}
	if (!i915_sw_fence_done(B)) {
		pr_err("Fence B not done\n");
		ret = -EINVAL;
	}
	if (!i915_sw_fence_done(A)) {
		pr_err("Fence A not done\n");
		ret = -EINVAL;
	}
err_C:
	free_fence(C);
err_B:
	free_fence(B);
err_A:
	free_fence(A);
	return ret;
}

static int test_AB_C(void *arg)
{
	struct i915_sw_fence *A, *B, *C;
	int ret = -EINVAL;

	/* Test multiple fences (AB) waiting on a single event (C) */
	A = alloc_fence();
	if (!A)
		return -ENOMEM;

	B = alloc_fence();
	if (!B) {
		ret = -ENOMEM;
		goto err_A;
	}

	C = alloc_fence();
	if (!C) {
		ret = -ENOMEM;
		goto err_B;
	}

	ret = i915_sw_fence_await_sw_fence_gfp(A, C, GFP_KERNEL);
	if (ret < 0)
		goto err_C;
	if (ret == 0) {
		ret = -EINVAL;
		goto err_C;
	}

	ret = i915_sw_fence_await_sw_fence_gfp(B, C, GFP_KERNEL);
	if (ret < 0)
		goto err_C;
	if (ret == 0) {
		ret = -EINVAL;
		goto err_C;
	}

	i915_sw_fence_commit(A);
	i915_sw_fence_commit(B);

	ret = 0;
	if (i915_sw_fence_done(A)) {
		pr_err("Fence A completed early\n");
		ret = -EINVAL;
	}

	if (i915_sw_fence_done(B)) {
		pr_err("Fence B completed early\n");
		ret = -EINVAL;
	}

	i915_sw_fence_commit(C);
	if (!i915_sw_fence_done(C)) {
		pr_err("Fence C not done\n");
		ret = -EINVAL;
	}

	if (!i915_sw_fence_done(B)) {
		pr_err("Fence B not done\n");
		ret = -EINVAL;
	}

	if (!i915_sw_fence_done(A)) {
		pr_err("Fence A not done\n");
		ret = -EINVAL;
	}

err_C:
	free_fence(C);
err_B:
	free_fence(B);
err_A:
	free_fence(A);
	return ret;
}

static int test_C_AB(void *arg)
{
	struct i915_sw_fence *A, *B, *C;
	int ret;

	/* Test multiple event sources (A,B) for a single fence (C) */
	A = alloc_fence();
	if (!A)
		return -ENOMEM;

	B = alloc_fence();
	if (!B) {
		ret = -ENOMEM;
		goto err_A;
	}

	C = alloc_fence();
	if (!C) {
		ret = -ENOMEM;
		goto err_B;
	}

	ret = i915_sw_fence_await_sw_fence_gfp(C, A, GFP_KERNEL);
	if (ret < 0)
		goto err_C;
	if (ret == 0) {
		ret = -EINVAL;
		goto err_C;
	}

	ret = i915_sw_fence_await_sw_fence_gfp(C, B, GFP_KERNEL);
	if (ret < 0)
		goto err_C;
	if (ret == 0) {
		ret = -EINVAL;
		goto err_C;
	}

	ret = 0;
	i915_sw_fence_commit(C);
	if (i915_sw_fence_done(C))
		ret = -EINVAL;

	i915_sw_fence_commit(A);
	i915_sw_fence_commit(B);

	if (!i915_sw_fence_done(A)) {
		pr_err("Fence A not done\n");
		ret = -EINVAL;
	}

	if (!i915_sw_fence_done(B)) {
		pr_err("Fence B not done\n");
		ret = -EINVAL;
	}

	if (!i915_sw_fence_done(C)) {
		pr_err("Fence C not done\n");
		ret = -EINVAL;
	}

err_C:
	free_fence(C);
err_B:
	free_fence(B);
err_A:
	free_fence(A);
	return ret;
}

static int test_chain(void *arg)
{
	int nfences = 4096;
	struct i915_sw_fence **fences;
	int ret, i;

	/* Test a long chain of fences */
	fences = kmalloc_array(nfences, sizeof(*fences), GFP_KERNEL);
	if (!fences)
		return -ENOMEM;

	for (i = 0; i < nfences; i++) {
		fences[i] = alloc_fence();
		if (!fences[i]) {
			nfences = i;
			ret = -ENOMEM;
			goto err;
		}

		if (i > 0) {
			ret = i915_sw_fence_await_sw_fence_gfp(fences[i],
							       fences[i - 1],
							       GFP_KERNEL);
			if (ret < 0) {
				nfences = i + 1;
				goto err;
			}

			i915_sw_fence_commit(fences[i]);
		}
	}

	ret = 0;
	for (i = nfences; --i; ) {
		if (i915_sw_fence_done(fences[i])) {
			if (ret == 0)
				pr_err("Fence[%d] completed early\n", i);
			ret = -EINVAL;
		}
	}
	i915_sw_fence_commit(fences[0]);
	for (i = 0; ret == 0 && i < nfences; i++) {
		if (!i915_sw_fence_done(fences[i])) {
			pr_err("Fence[%d] is not done\n", i);
			ret = -EINVAL;
		}
	}

err:
	for (i = 0; i < nfences; i++)
		free_fence(fences[i]);
	kfree(fences);
	return ret;
}

struct task_ipc {
	struct work_struct work;
	struct completion started;
	struct i915_sw_fence *in, *out;
	int value;
};

static void task_ipc(struct work_struct *work)
{
	struct task_ipc *ipc = container_of(work, typeof(*ipc), work);

	complete(&ipc->started);

	i915_sw_fence_wait(ipc->in);
	smp_store_mb(ipc->value, 1);
	i915_sw_fence_commit(ipc->out);
}

static int test_ipc(void *arg)
{
	struct task_ipc ipc;
	int ret = 0;

	/* Test use of i915_sw_fence as an interprocess signaling mechanism */
	ipc.in = alloc_fence();
	if (!ipc.in)
		return -ENOMEM;
	ipc.out = alloc_fence();
	if (!ipc.out) {
		ret = -ENOMEM;
		goto err_in;
	}

	/* use a completion to avoid chicken-and-egg testing */
	init_completion(&ipc.started);

	ipc.value = 0;
	INIT_WORK_ONSTACK(&ipc.work, task_ipc);
	schedule_work(&ipc.work);

	wait_for_completion(&ipc.started);

	usleep_range(1000, 2000);
	if (READ_ONCE(ipc.value)) {
		pr_err("worker updated value before i915_sw_fence was signaled\n");
		ret = -EINVAL;
	}

	i915_sw_fence_commit(ipc.in);
	i915_sw_fence_wait(ipc.out);

	if (!READ_ONCE(ipc.value)) {
		pr_err("worker signaled i915_sw_fence before value was posted\n");
		ret = -EINVAL;
	}

	flush_work(&ipc.work);
	destroy_work_on_stack(&ipc.work);
	free_fence(ipc.out);
err_in:
	free_fence(ipc.in);
	return ret;
}

static int test_timer(void *arg)
{
	unsigned long target, delay;
	struct timed_fence tf;

	timed_fence_init(&tf, target = jiffies);
	if (!i915_sw_fence_done(&tf.fence)) {
		pr_err("Fence with immediate expiration not signaled\n");
		goto err;
	}
	timed_fence_fini(&tf);

	for_each_prime_number(delay, i915_selftest.timeout_jiffies/2) {
		timed_fence_init(&tf, target = jiffies + delay);
		if (i915_sw_fence_done(&tf.fence)) {
			pr_err("Fence with future expiration (%lu jiffies) already signaled\n", delay);
			goto err;
		}

		i915_sw_fence_wait(&tf.fence);
		if (!i915_sw_fence_done(&tf.fence)) {
			pr_err("Fence not signaled after wait\n");
			goto err;
		}
		if (time_before(jiffies, target)) {
			pr_err("Fence signaled too early, target=%lu, now=%lu\n",
			       target, jiffies);
			goto err;
		}

		timed_fence_fini(&tf);
	}

	return 0;

err:
	timed_fence_fini(&tf);
	return -EINVAL;
}

static const char *mock_name(struct dma_fence *fence)
{
	return "mock";
}

static bool mock_enable_signaling(struct dma_fence *fence)
{
	return true;
}

static const struct dma_fence_ops mock_fence_ops = {
	.get_driver_name = mock_name,
	.get_timeline_name = mock_name,
	.enable_signaling = mock_enable_signaling,
	.wait = dma_fence_default_wait,
	.release = dma_fence_free,
};

static DEFINE_SPINLOCK(mock_fence_lock);

static struct dma_fence *alloc_dma_fence(void)
{
	struct dma_fence *dma;

	dma = kmalloc(sizeof(*dma), GFP_KERNEL);
	if (dma)
		dma_fence_init(dma, &mock_fence_ops, &mock_fence_lock, 0, 0);

	return dma;
}

static struct i915_sw_fence *
wrap_dma_fence(struct dma_fence *dma, unsigned long delay)
{
	struct i915_sw_fence *fence;
	int err;

	fence = alloc_fence();
	if (!fence)
		return ERR_PTR(-ENOMEM);

	err = i915_sw_fence_await_dma_fence(fence, dma, delay, GFP_NOWAIT);
	i915_sw_fence_commit(fence);
	if (err < 0) {
		free_fence(fence);
		return ERR_PTR(err);
	}

	return fence;
}

static int test_dma_fence(void *arg)
{
	struct i915_sw_fence *timeout = NULL, *not = NULL;
	unsigned long delay = i915_selftest.timeout_jiffies;
	unsigned long end, sleep;
	struct dma_fence *dma;
	int err;

	dma = alloc_dma_fence();
	if (!dma)
		return -ENOMEM;

	timeout = wrap_dma_fence(dma, delay);
	if (IS_ERR(timeout)) {
		err = PTR_ERR(timeout);
		goto err;
	}

	not = wrap_dma_fence(dma, 0);
	if (IS_ERR(not)) {
		err = PTR_ERR(not);
		goto err;
	}

	err = -EINVAL;
	if (i915_sw_fence_done(timeout) || i915_sw_fence_done(not)) {
		pr_err("Fences immediately signaled\n");
		goto err;
	}

	/* We round the timeout for the fence up to the next second */
	end = round_jiffies_up(jiffies + delay);

	sleep = jiffies_to_usecs(delay) / 3;
	usleep_range(sleep, 2 * sleep);
	if (time_after(jiffies, end)) {
		pr_debug("Slept too long, delay=%lu, (target=%lu, now=%lu) skipping\n",
			 delay, end, jiffies);
		goto skip;
	}

	if (i915_sw_fence_done(timeout) || i915_sw_fence_done(not)) {
		pr_err("Fences signaled too early\n");
		goto err;
	}

	if (!wait_event_timeout(timeout->wait,
				i915_sw_fence_done(timeout),
				2 * (end - jiffies) + 1)) {
		pr_err("Timeout fence unsignaled!\n");
		goto err;
	}

	if (i915_sw_fence_done(not)) {
		pr_err("No timeout fence signaled!\n");
		goto err;
	}

skip:
	dma_fence_signal(dma);

	if (!i915_sw_fence_done(timeout) || !i915_sw_fence_done(not)) {
		pr_err("Fences unsignaled\n");
		goto err;
	}

	free_fence(not);
	free_fence(timeout);
	dma_fence_put(dma);

	return 0;

err:
	dma_fence_signal(dma);
	if (!IS_ERR_OR_NULL(timeout))
		free_fence(timeout);
	if (!IS_ERR_OR_NULL(not))
		free_fence(not);
	dma_fence_put(dma);
	return err;
}

int i915_sw_fence_mock_selftests(void)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(test_self),
		SUBTEST(test_dag),
		SUBTEST(test_AB),
		SUBTEST(test_ABC),
		SUBTEST(test_AB_C),
		SUBTEST(test_C_AB),
		SUBTEST(test_chain),
		SUBTEST(test_ipc),
		SUBTEST(test_timer),
		SUBTEST(test_dma_fence),
	};

	return i915_subtests(tests, NULL);
}
