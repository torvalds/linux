// SPDX-License-Identifier: MIT

/*
 * Copyright © 2019 Intel Corporation
 */

#include <kunit/test.h>
#include <linux/delay.h>
#include <linux/dma-fence.h>
#include <linux/dma-fence-chain.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/random.h>

#define CHAIN_SZ (4 << 10)

static struct kmem_cache *slab_fences;

static inline struct mock_fence {
	struct dma_fence base;
	spinlock_t lock;
} *to_mock_fence(struct dma_fence *f) {
	return container_of(f, struct mock_fence, base);
}

static const char *mock_name(struct dma_fence *f)
{
	return "mock";
}

static void mock_fence_release(struct dma_fence *f)
{
	kmem_cache_free(slab_fences, to_mock_fence(f));
}

static const struct dma_fence_ops mock_ops = {
	.get_driver_name = mock_name,
	.get_timeline_name = mock_name,
	.release = mock_fence_release,
};

static struct dma_fence *mock_fence(void)
{
	struct mock_fence *f;

	f = kmem_cache_alloc(slab_fences, GFP_KERNEL);
	if (!f)
		return NULL;

	spin_lock_init(&f->lock);
	dma_fence_init(&f->base, &mock_ops, &f->lock, 0, 0);

	return &f->base;
}

static struct dma_fence *mock_chain(struct dma_fence *prev,
				    struct dma_fence *fence,
				    u64 seqno)
{
	struct dma_fence_chain *f;

	f = dma_fence_chain_alloc();
	if (!f)
		return NULL;

	dma_fence_chain_init(f, dma_fence_get(prev), dma_fence_get(fence),
			     seqno);

	return &f->base;
}

static void test_sanitycheck(struct kunit *test)
{
	struct dma_fence *f, *chain;

	f = mock_fence();
	KUNIT_ASSERT_NOT_NULL(test, f);

	chain = mock_chain(NULL, f, 1);
	if (chain)
		dma_fence_enable_sw_signaling(chain);
	else
		KUNIT_FAIL(test, "Failed to create chain");

	dma_fence_signal(f);
	dma_fence_put(f);

	dma_fence_put(chain);
}

struct fence_chains {
	unsigned int chain_length;
	struct dma_fence **fences;
	struct dma_fence **chains;

	struct dma_fence *tail;
};

static uint64_t seqno_inc(unsigned int i)
{
	return i + 1;
}

static int fence_chains_init(struct fence_chains *fc, unsigned int count,
			     uint64_t (*seqno_fn)(unsigned int))
{
	unsigned int i;
	int err = 0;

	fc->chains = kvmalloc_objs(*fc->chains, count, GFP_KERNEL | __GFP_ZERO);
	if (!fc->chains)
		return -ENOMEM;

	fc->fences = kvmalloc_objs(*fc->fences, count, GFP_KERNEL | __GFP_ZERO);
	if (!fc->fences) {
		err = -ENOMEM;
		goto err_chains;
	}

	fc->tail = NULL;
	for (i = 0; i < count; i++) {
		fc->fences[i] = mock_fence();
		if (!fc->fences[i]) {
			err = -ENOMEM;
			goto unwind;
		}

		fc->chains[i] = mock_chain(fc->tail,
					   fc->fences[i],
					   seqno_fn(i));
		if (!fc->chains[i]) {
			err = -ENOMEM;
			goto unwind;
		}

		fc->tail = fc->chains[i];

		dma_fence_enable_sw_signaling(fc->chains[i]);
	}

	fc->chain_length = i;
	return 0;

unwind:
	for (i = 0; i < count; i++) {
		dma_fence_put(fc->fences[i]);
		dma_fence_put(fc->chains[i]);
	}
	kvfree(fc->fences);
err_chains:
	kvfree(fc->chains);
	return err;
}

static void fence_chains_fini(struct fence_chains *fc)
{
	unsigned int i;

	for (i = 0; i < fc->chain_length; i++) {
		dma_fence_signal(fc->fences[i]);
		dma_fence_put(fc->fences[i]);
	}
	kvfree(fc->fences);

	for (i = 0; i < fc->chain_length; i++)
		dma_fence_put(fc->chains[i]);
	kvfree(fc->chains);
}

static void test_find_seqno(struct kunit *test)
{
	struct fence_chains fc;
	struct dma_fence *fence;
	int err;
	int i;

	err = fence_chains_init(&fc, 64, seqno_inc);
	KUNIT_ASSERT_EQ_MSG(test, err, 0, "Failed to init fence chains");

	fence = dma_fence_get(fc.tail);
	err = dma_fence_chain_find_seqno(&fence, 0);
	dma_fence_put(fence);
	if (err) {
		KUNIT_FAIL(test, "Reported %d for find_seqno(0)!", err);
		goto err;
	}

	for (i = 0; i < fc.chain_length; i++) {
		fence = dma_fence_get(fc.tail);
		err = dma_fence_chain_find_seqno(&fence, i + 1);
		dma_fence_put(fence);
		if (err) {
			KUNIT_FAIL(test, "Reported %d for find_seqno(%d:%d)!",
				   err, fc.chain_length + 1, i + 1);
			goto err;
		}
		if (fence != fc.chains[i]) {
			KUNIT_FAIL(test, "Incorrect fence reported by find_seqno(%d:%d)",
				   fc.chain_length + 1, i + 1);
			goto err;
		}

		dma_fence_get(fence);
		err = dma_fence_chain_find_seqno(&fence, i + 1);
		dma_fence_put(fence);
		if (err) {
			KUNIT_FAIL(test, "Error reported for finding self");
			goto err;
		}
		if (fence != fc.chains[i]) {
			KUNIT_FAIL(test, "Incorrect fence reported by find self");
			goto err;
		}

		dma_fence_get(fence);
		err = dma_fence_chain_find_seqno(&fence, i + 2);
		dma_fence_put(fence);
		if (!err) {
			KUNIT_FAIL(test, "Error not reported for future fence: find_seqno(%d:%d)!",
				   i + 1, i + 2);
			goto err;
		}

		dma_fence_get(fence);
		err = dma_fence_chain_find_seqno(&fence, i);
		dma_fence_put(fence);
		if (err) {
			KUNIT_FAIL(test, "Error reported for previous fence!");
			goto err;
		}
		if (i > 0 && fence != fc.chains[i - 1]) {
			KUNIT_FAIL(test, "Incorrect fence reported by find_seqno(%d:%d)",
				   i + 1, i);
			goto err;
		}
	}

err:
	fence_chains_fini(&fc);
}

static void test_find_signaled(struct kunit *test)
{
	struct fence_chains fc;
	struct dma_fence *fence;
	int err;

	err = fence_chains_init(&fc, 2, seqno_inc);
	KUNIT_ASSERT_EQ_MSG(test, err, 0, "Failed to init fence chains");

	dma_fence_signal(fc.fences[0]);

	fence = dma_fence_get(fc.tail);
	err = dma_fence_chain_find_seqno(&fence, 1);
	dma_fence_put(fence);
	if (err) {
		KUNIT_FAIL(test, "Reported %d for find_seqno()!", err);
		goto err;
	}

	if (fence && fence != fc.chains[0]) {
		KUNIT_FAIL(test, "Incorrect chain-fence.seqno:%lld reported for completed seqno:1",
			   fence->seqno);

		dma_fence_get(fence);
		err = dma_fence_chain_find_seqno(&fence, 1);
		dma_fence_put(fence);
		if (err)
			KUNIT_FAIL(test, "Reported %d for finding self!", err);
	}

err:
	fence_chains_fini(&fc);
}

static void test_find_out_of_order(struct kunit *test)
{
	struct fence_chains fc;
	struct dma_fence *fence;
	int err;

	err = fence_chains_init(&fc, 3, seqno_inc);
	KUNIT_ASSERT_EQ_MSG(test, err, 0, "Failed to init fence chains");

	dma_fence_signal(fc.fences[1]);

	fence = dma_fence_get(fc.tail);
	err = dma_fence_chain_find_seqno(&fence, 2);
	dma_fence_put(fence);
	if (err) {
		KUNIT_FAIL(test, "Reported %d for find_seqno()!", err);
		goto err;
	}

	/*
	 * We signaled the middle fence (2) of the 1-2-3 chain. The behavior
	 * of the dma-fence-chain is to make us wait for all the fences up to
	 * the point we want. Since fence 1 is still not signaled, this what
	 * we should get as fence to wait upon (fence 2 being garbage
	 * collected during the traversal of the chain).
	 */
	if (fence != fc.chains[0])
		KUNIT_FAIL(test, "Incorrect chain-fence.seqno:%lld reported for completed seqno:2",
			   fence ? fence->seqno : 0);

err:
	fence_chains_fini(&fc);
}

static uint64_t seqno_inc2(unsigned int i)
{
	return 2 * i + 2;
}

static void test_find_gap(struct kunit *test)
{
	struct fence_chains fc;
	struct dma_fence *fence;
	int err;
	int i;

	err = fence_chains_init(&fc, 64, seqno_inc2);
	KUNIT_ASSERT_EQ_MSG(test, err, 0, "Failed to init fence chains");

	for (i = 0; i < fc.chain_length; i++) {
		fence = dma_fence_get(fc.tail);
		err = dma_fence_chain_find_seqno(&fence, 2 * i + 1);
		dma_fence_put(fence);
		if (err) {
			KUNIT_FAIL(test, "Reported %d for find_seqno(%d:%d)!",
				   err, fc.chain_length + 1, 2 * i + 1);
			goto err;
		}
		if (fence != fc.chains[i]) {
			KUNIT_FAIL(test, "Incorrect fence.seqno:%lld reported by find_seqno(%d:%d)",
				   fence->seqno,
				   fc.chain_length + 1,
				   2 * i + 1);
			goto err;
		}

		dma_fence_get(fence);
		err = dma_fence_chain_find_seqno(&fence, 2 * i + 2);
		dma_fence_put(fence);
		if (err) {
			KUNIT_FAIL(test, "Error reported for finding self");
			goto err;
		}
		if (fence != fc.chains[i]) {
			KUNIT_FAIL(test, "Incorrect fence reported by find self");
			goto err;
		}
	}

err:
	fence_chains_fini(&fc);
}

struct find_race {
	struct fence_chains fc;
	atomic_t children;
};

static int __find_race(void *arg)
{
	struct find_race *data = arg;
	int err = 0;

	while (!kthread_should_stop()) {
		struct dma_fence *fence = dma_fence_get(data->fc.tail);
		int seqno;

		seqno = get_random_u32_inclusive(1, data->fc.chain_length);

		err = dma_fence_chain_find_seqno(&fence, seqno);
		if (err) {
			pr_err("Failed to find fence seqno:%d\n",
			       seqno);
			dma_fence_put(fence);
			break;
		}
		if (!fence)
			goto signal;

		/*
		 * We can only find ourselves if we are on fence we were
		 * looking for.
		 */
		if (fence->seqno == seqno) {
			err = dma_fence_chain_find_seqno(&fence, seqno);
			if (err) {
				pr_err("Reported an invalid fence for find-self:%d\n",
				       seqno);
				dma_fence_put(fence);
				break;
			}
		}

		dma_fence_put(fence);

signal:
		seqno = get_random_u32_below(data->fc.chain_length - 1);
		dma_fence_signal(data->fc.fences[seqno]);
		cond_resched();
	}

	if (atomic_dec_and_test(&data->children))
		wake_up_var(&data->children);
	return err;
}

static void test_find_race(struct kunit *test)
{
	struct find_race data;
	int ncpus = num_online_cpus();
	struct task_struct **threads;
	unsigned long count;
	int err;
	int i;

	err = fence_chains_init(&data.fc, CHAIN_SZ, seqno_inc);
	KUNIT_ASSERT_EQ_MSG(test, err, 0, "Failed to init fence chains");

	threads = kmalloc_objs(*threads, ncpus);
	if (!threads) {
		KUNIT_FAIL(test, "Failed to allocate threads array");
		goto err;
	}

	atomic_set(&data.children, 0);
	for (i = 0; i < ncpus; i++) {
		threads[i] = kthread_run(__find_race, &data, "dmabuf/%d", i);
		if (IS_ERR(threads[i])) {
			ncpus = i;
			break;
		}
		atomic_inc(&data.children);
		get_task_struct(threads[i]);
	}

	wait_var_event_timeout(&data.children,
			       !atomic_read(&data.children),
			       5 * HZ);

	for (i = 0; i < ncpus; i++) {
		int ret;

		ret = kthread_stop_put(threads[i]);
		if (ret && !err)
			err = ret;
	}
	kfree(threads);

	count = 0;
	for (i = 0; i < data.fc.chain_length; i++)
		if (dma_fence_is_signaled(data.fc.fences[i]))
			count++;
	pr_info("Completed %lu cycles\n", count);

	KUNIT_EXPECT_EQ(test, err, 0);

err:
	fence_chains_fini(&data.fc);
}

static void test_signal_forward(struct kunit *test)
{
	struct fence_chains fc;
	int err;
	int i;

	err = fence_chains_init(&fc, 64, seqno_inc);
	KUNIT_ASSERT_EQ_MSG(test, err, 0, "Failed to init fence chains");

	for (i = 0; i < fc.chain_length; i++) {
		dma_fence_signal(fc.fences[i]);

		if (!dma_fence_is_signaled(fc.chains[i])) {
			KUNIT_FAIL(test, "chain[%d] not signaled!", i);
			goto err;
		}

		if (i + 1 < fc.chain_length &&
		    dma_fence_is_signaled(fc.chains[i + 1])) {
			KUNIT_FAIL(test, "chain[%d] is signaled!", i);
			goto err;
		}
	}

err:
	fence_chains_fini(&fc);
}

static void test_signal_backward(struct kunit *test)
{
	struct fence_chains fc;
	int err;
	int i;

	err = fence_chains_init(&fc, 64, seqno_inc);
	KUNIT_ASSERT_EQ_MSG(test, err, 0, "Failed to init fence chains");

	for (i = fc.chain_length; i--; ) {
		dma_fence_signal(fc.fences[i]);

		if (i > 0 && dma_fence_is_signaled(fc.chains[i])) {
			KUNIT_FAIL(test, "chain[%d] is signaled!", i);
			goto err;
		}
	}

	for (i = 0; i < fc.chain_length; i++) {
		if (!dma_fence_is_signaled(fc.chains[i])) {
			KUNIT_FAIL(test, "chain[%d] was not signaled!", i);
			goto err;
		}
	}

err:
	fence_chains_fini(&fc);
}

static int __wait_fence_chains(void *arg)
{
	struct fence_chains *fc = arg;

	if (dma_fence_wait(fc->tail, false))
		return -EIO;

	return 0;
}

static void test_wait_forward(struct kunit *test)
{
	struct fence_chains fc;
	struct task_struct *tsk;
	int err;
	int i;

	err = fence_chains_init(&fc, CHAIN_SZ, seqno_inc);
	KUNIT_ASSERT_EQ_MSG(test, err, 0, "Failed to init fence chains");

	tsk = kthread_run(__wait_fence_chains, &fc, "dmabuf/wait");
	if (IS_ERR(tsk)) {
		KUNIT_FAIL(test, "Failed to create kthread");
		goto err;
	}
	get_task_struct(tsk);
	yield_to(tsk, true);

	for (i = 0; i < fc.chain_length; i++)
		dma_fence_signal(fc.fences[i]);

	err = kthread_stop_put(tsk);
	KUNIT_EXPECT_EQ(test, err, 0);

err:
	fence_chains_fini(&fc);
}

static void test_wait_backward(struct kunit *test)
{
	struct fence_chains fc;
	struct task_struct *tsk;
	int err;
	int i;

	err = fence_chains_init(&fc, CHAIN_SZ, seqno_inc);
	KUNIT_ASSERT_EQ_MSG(test, err, 0, "Failed to init fence chains");

	tsk = kthread_run(__wait_fence_chains, &fc, "dmabuf/wait");
	if (IS_ERR(tsk)) {
		KUNIT_FAIL(test, "Failed to create kthread");
		goto err;
	}
	get_task_struct(tsk);
	yield_to(tsk, true);

	for (i = fc.chain_length; i--; )
		dma_fence_signal(fc.fences[i]);

	err = kthread_stop_put(tsk);
	KUNIT_EXPECT_EQ(test, err, 0);

err:
	fence_chains_fini(&fc);
}

static void randomise_fences(struct fence_chains *fc)
{
	unsigned int count = fc->chain_length;

	/* Fisher-Yates shuffle courtesy of Knuth */
	while (--count) {
		unsigned int swp;

		swp = get_random_u32_below(count + 1);
		if (swp == count)
			continue;

		swap(fc->fences[count], fc->fences[swp]);
	}
}

static void test_wait_random(struct kunit *test)
{
	struct fence_chains fc;
	struct task_struct *tsk;
	int err;
	int i;

	err = fence_chains_init(&fc, CHAIN_SZ, seqno_inc);
	KUNIT_ASSERT_EQ_MSG(test, err, 0, "Failed to init fence chains");

	randomise_fences(&fc);

	tsk = kthread_run(__wait_fence_chains, &fc, "dmabuf/wait");
	if (IS_ERR(tsk)) {
		KUNIT_FAIL(test, "Failed to create kthread");
		goto err;
	}
	get_task_struct(tsk);
	yield_to(tsk, true);

	for (i = 0; i < fc.chain_length; i++)
		dma_fence_signal(fc.fences[i]);

	err = kthread_stop_put(tsk);
	KUNIT_EXPECT_EQ(test, err, 0);

err:
	fence_chains_fini(&fc);
}

static int dma_fence_chain_suite_init(struct kunit_suite *suite)
{
	pr_info("sizeof(dma_fence_chain)=%zu\n",
		sizeof(struct dma_fence_chain));

	slab_fences = KMEM_CACHE(mock_fence,
				 SLAB_TYPESAFE_BY_RCU |
				 SLAB_HWCACHE_ALIGN);
	if (!slab_fences)
		return -ENOMEM;
	return 0;
}

static void dma_fence_chain_suite_exit(struct kunit_suite *suite)
{
	kmem_cache_destroy(slab_fences);
}

static struct kunit_case dma_fence_chain_cases[] = {
	KUNIT_CASE(test_sanitycheck),
	KUNIT_CASE(test_find_seqno),
	KUNIT_CASE(test_find_signaled),
	KUNIT_CASE(test_find_out_of_order),
	KUNIT_CASE(test_find_gap),
	KUNIT_CASE(test_find_race),
	KUNIT_CASE(test_signal_forward),
	KUNIT_CASE(test_signal_backward),
	KUNIT_CASE(test_wait_forward),
	KUNIT_CASE(test_wait_backward),
	KUNIT_CASE(test_wait_random),
	{}
};

static struct kunit_suite dma_fence_chain_test_suite = {
	.name = "dma-buf-fence-chain",
	.suite_init = dma_fence_chain_suite_init,
	.suite_exit = dma_fence_chain_suite_exit,
	.test_cases = dma_fence_chain_cases,
};

kunit_test_suite(dma_fence_chain_test_suite);
