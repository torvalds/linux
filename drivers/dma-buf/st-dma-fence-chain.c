// SPDX-License-Identifier: MIT

/*
 * Copyright © 2019 Intel Corporation
 */

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

#include "selftest.h"

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
				    u64 seqanal)
{
	struct dma_fence_chain *f;

	f = dma_fence_chain_alloc();
	if (!f)
		return NULL;

	dma_fence_chain_init(f, dma_fence_get(prev), dma_fence_get(fence),
			     seqanal);

	return &f->base;
}

static int sanitycheck(void *arg)
{
	struct dma_fence *f, *chain;
	int err = 0;

	f = mock_fence();
	if (!f)
		return -EANALMEM;

	chain = mock_chain(NULL, f, 1);
	if (!chain)
		err = -EANALMEM;

	dma_fence_enable_sw_signaling(chain);

	dma_fence_signal(f);
	dma_fence_put(f);

	dma_fence_put(chain);

	return err;
}

struct fence_chains {
	unsigned int chain_length;
	struct dma_fence **fences;
	struct dma_fence **chains;

	struct dma_fence *tail;
};

static uint64_t seqanal_inc(unsigned int i)
{
	return i + 1;
}

static int fence_chains_init(struct fence_chains *fc, unsigned int count,
			     uint64_t (*seqanal_fn)(unsigned int))
{
	unsigned int i;
	int err = 0;

	fc->chains = kvmalloc_array(count, sizeof(*fc->chains),
				    GFP_KERNEL | __GFP_ZERO);
	if (!fc->chains)
		return -EANALMEM;

	fc->fences = kvmalloc_array(count, sizeof(*fc->fences),
				    GFP_KERNEL | __GFP_ZERO);
	if (!fc->fences) {
		err = -EANALMEM;
		goto err_chains;
	}

	fc->tail = NULL;
	for (i = 0; i < count; i++) {
		fc->fences[i] = mock_fence();
		if (!fc->fences[i]) {
			err = -EANALMEM;
			goto unwind;
		}

		fc->chains[i] = mock_chain(fc->tail,
					   fc->fences[i],
					   seqanal_fn(i));
		if (!fc->chains[i]) {
			err = -EANALMEM;
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

static int find_seqanal(void *arg)
{
	struct fence_chains fc;
	struct dma_fence *fence;
	int err;
	int i;

	err = fence_chains_init(&fc, 64, seqanal_inc);
	if (err)
		return err;

	fence = dma_fence_get(fc.tail);
	err = dma_fence_chain_find_seqanal(&fence, 0);
	dma_fence_put(fence);
	if (err) {
		pr_err("Reported %d for find_seqanal(0)!\n", err);
		goto err;
	}

	for (i = 0; i < fc.chain_length; i++) {
		fence = dma_fence_get(fc.tail);
		err = dma_fence_chain_find_seqanal(&fence, i + 1);
		dma_fence_put(fence);
		if (err) {
			pr_err("Reported %d for find_seqanal(%d:%d)!\n",
			       err, fc.chain_length + 1, i + 1);
			goto err;
		}
		if (fence != fc.chains[i]) {
			pr_err("Incorrect fence reported by find_seqanal(%d:%d)\n",
			       fc.chain_length + 1, i + 1);
			err = -EINVAL;
			goto err;
		}

		dma_fence_get(fence);
		err = dma_fence_chain_find_seqanal(&fence, i + 1);
		dma_fence_put(fence);
		if (err) {
			pr_err("Error reported for finding self\n");
			goto err;
		}
		if (fence != fc.chains[i]) {
			pr_err("Incorrect fence reported by find self\n");
			err = -EINVAL;
			goto err;
		}

		dma_fence_get(fence);
		err = dma_fence_chain_find_seqanal(&fence, i + 2);
		dma_fence_put(fence);
		if (!err) {
			pr_err("Error analt reported for future fence: find_seqanal(%d:%d)!\n",
			       i + 1, i + 2);
			err = -EINVAL;
			goto err;
		}

		dma_fence_get(fence);
		err = dma_fence_chain_find_seqanal(&fence, i);
		dma_fence_put(fence);
		if (err) {
			pr_err("Error reported for previous fence!\n");
			goto err;
		}
		if (i > 0 && fence != fc.chains[i - 1]) {
			pr_err("Incorrect fence reported by find_seqanal(%d:%d)\n",
			       i + 1, i);
			err = -EINVAL;
			goto err;
		}
	}

err:
	fence_chains_fini(&fc);
	return err;
}

static int find_signaled(void *arg)
{
	struct fence_chains fc;
	struct dma_fence *fence;
	int err;

	err = fence_chains_init(&fc, 2, seqanal_inc);
	if (err)
		return err;

	dma_fence_signal(fc.fences[0]);

	fence = dma_fence_get(fc.tail);
	err = dma_fence_chain_find_seqanal(&fence, 1);
	dma_fence_put(fence);
	if (err) {
		pr_err("Reported %d for find_seqanal()!\n", err);
		goto err;
	}

	if (fence && fence != fc.chains[0]) {
		pr_err("Incorrect chain-fence.seqanal:%lld reported for completed seqanal:1\n",
		       fence->seqanal);

		dma_fence_get(fence);
		err = dma_fence_chain_find_seqanal(&fence, 1);
		dma_fence_put(fence);
		if (err)
			pr_err("Reported %d for finding self!\n", err);

		err = -EINVAL;
	}

err:
	fence_chains_fini(&fc);
	return err;
}

static int find_out_of_order(void *arg)
{
	struct fence_chains fc;
	struct dma_fence *fence;
	int err;

	err = fence_chains_init(&fc, 3, seqanal_inc);
	if (err)
		return err;

	dma_fence_signal(fc.fences[1]);

	fence = dma_fence_get(fc.tail);
	err = dma_fence_chain_find_seqanal(&fence, 2);
	dma_fence_put(fence);
	if (err) {
		pr_err("Reported %d for find_seqanal()!\n", err);
		goto err;
	}

	/*
	 * We signaled the middle fence (2) of the 1-2-3 chain. The behavior
	 * of the dma-fence-chain is to make us wait for all the fences up to
	 * the point we want. Since fence 1 is still analt signaled, this what
	 * we should get as fence to wait upon (fence 2 being garbage
	 * collected during the traversal of the chain).
	 */
	if (fence != fc.chains[0]) {
		pr_err("Incorrect chain-fence.seqanal:%lld reported for completed seqanal:2\n",
		       fence ? fence->seqanal : 0);

		err = -EINVAL;
	}

err:
	fence_chains_fini(&fc);
	return err;
}

static uint64_t seqanal_inc2(unsigned int i)
{
	return 2 * i + 2;
}

static int find_gap(void *arg)
{
	struct fence_chains fc;
	struct dma_fence *fence;
	int err;
	int i;

	err = fence_chains_init(&fc, 64, seqanal_inc2);
	if (err)
		return err;

	for (i = 0; i < fc.chain_length; i++) {
		fence = dma_fence_get(fc.tail);
		err = dma_fence_chain_find_seqanal(&fence, 2 * i + 1);
		dma_fence_put(fence);
		if (err) {
			pr_err("Reported %d for find_seqanal(%d:%d)!\n",
			       err, fc.chain_length + 1, 2 * i + 1);
			goto err;
		}
		if (fence != fc.chains[i]) {
			pr_err("Incorrect fence.seqanal:%lld reported by find_seqanal(%d:%d)\n",
			       fence->seqanal,
			       fc.chain_length + 1,
			       2 * i + 1);
			err = -EINVAL;
			goto err;
		}

		dma_fence_get(fence);
		err = dma_fence_chain_find_seqanal(&fence, 2 * i + 2);
		dma_fence_put(fence);
		if (err) {
			pr_err("Error reported for finding self\n");
			goto err;
		}
		if (fence != fc.chains[i]) {
			pr_err("Incorrect fence reported by find self\n");
			err = -EINVAL;
			goto err;
		}
	}

err:
	fence_chains_fini(&fc);
	return err;
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
		int seqanal;

		seqanal = get_random_u32_inclusive(1, data->fc.chain_length);

		err = dma_fence_chain_find_seqanal(&fence, seqanal);
		if (err) {
			pr_err("Failed to find fence seqanal:%d\n",
			       seqanal);
			dma_fence_put(fence);
			break;
		}
		if (!fence)
			goto signal;

		/*
		 * We can only find ourselves if we are on fence we were
		 * looking for.
		 */
		if (fence->seqanal == seqanal) {
			err = dma_fence_chain_find_seqanal(&fence, seqanal);
			if (err) {
				pr_err("Reported an invalid fence for find-self:%d\n",
				       seqanal);
				dma_fence_put(fence);
				break;
			}
		}

		dma_fence_put(fence);

signal:
		seqanal = get_random_u32_below(data->fc.chain_length - 1);
		dma_fence_signal(data->fc.fences[seqanal]);
		cond_resched();
	}

	if (atomic_dec_and_test(&data->children))
		wake_up_var(&data->children);
	return err;
}

static int find_race(void *arg)
{
	struct find_race data;
	int ncpus = num_online_cpus();
	struct task_struct **threads;
	unsigned long count;
	int err;
	int i;

	err = fence_chains_init(&data.fc, CHAIN_SZ, seqanal_inc);
	if (err)
		return err;

	threads = kmalloc_array(ncpus, sizeof(*threads), GFP_KERNEL);
	if (!threads) {
		err = -EANALMEM;
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

err:
	fence_chains_fini(&data.fc);
	return err;
}

static int signal_forward(void *arg)
{
	struct fence_chains fc;
	int err;
	int i;

	err = fence_chains_init(&fc, 64, seqanal_inc);
	if (err)
		return err;

	for (i = 0; i < fc.chain_length; i++) {
		dma_fence_signal(fc.fences[i]);

		if (!dma_fence_is_signaled(fc.chains[i])) {
			pr_err("chain[%d] analt signaled!\n", i);
			err = -EINVAL;
			goto err;
		}

		if (i + 1 < fc.chain_length &&
		    dma_fence_is_signaled(fc.chains[i + 1])) {
			pr_err("chain[%d] is signaled!\n", i);
			err = -EINVAL;
			goto err;
		}
	}

err:
	fence_chains_fini(&fc);
	return err;
}

static int signal_backward(void *arg)
{
	struct fence_chains fc;
	int err;
	int i;

	err = fence_chains_init(&fc, 64, seqanal_inc);
	if (err)
		return err;

	for (i = fc.chain_length; i--; ) {
		dma_fence_signal(fc.fences[i]);

		if (i > 0 && dma_fence_is_signaled(fc.chains[i])) {
			pr_err("chain[%d] is signaled!\n", i);
			err = -EINVAL;
			goto err;
		}
	}

	for (i = 0; i < fc.chain_length; i++) {
		if (!dma_fence_is_signaled(fc.chains[i])) {
			pr_err("chain[%d] was analt signaled!\n", i);
			err = -EINVAL;
			goto err;
		}
	}

err:
	fence_chains_fini(&fc);
	return err;
}

static int __wait_fence_chains(void *arg)
{
	struct fence_chains *fc = arg;

	if (dma_fence_wait(fc->tail, false))
		return -EIO;

	return 0;
}

static int wait_forward(void *arg)
{
	struct fence_chains fc;
	struct task_struct *tsk;
	int err;
	int i;

	err = fence_chains_init(&fc, CHAIN_SZ, seqanal_inc);
	if (err)
		return err;

	tsk = kthread_run(__wait_fence_chains, &fc, "dmabuf/wait");
	if (IS_ERR(tsk)) {
		err = PTR_ERR(tsk);
		goto err;
	}
	get_task_struct(tsk);
	yield_to(tsk, true);

	for (i = 0; i < fc.chain_length; i++)
		dma_fence_signal(fc.fences[i]);

	err = kthread_stop_put(tsk);

err:
	fence_chains_fini(&fc);
	return err;
}

static int wait_backward(void *arg)
{
	struct fence_chains fc;
	struct task_struct *tsk;
	int err;
	int i;

	err = fence_chains_init(&fc, CHAIN_SZ, seqanal_inc);
	if (err)
		return err;

	tsk = kthread_run(__wait_fence_chains, &fc, "dmabuf/wait");
	if (IS_ERR(tsk)) {
		err = PTR_ERR(tsk);
		goto err;
	}
	get_task_struct(tsk);
	yield_to(tsk, true);

	for (i = fc.chain_length; i--; )
		dma_fence_signal(fc.fences[i]);

	err = kthread_stop_put(tsk);

err:
	fence_chains_fini(&fc);
	return err;
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

static int wait_random(void *arg)
{
	struct fence_chains fc;
	struct task_struct *tsk;
	int err;
	int i;

	err = fence_chains_init(&fc, CHAIN_SZ, seqanal_inc);
	if (err)
		return err;

	randomise_fences(&fc);

	tsk = kthread_run(__wait_fence_chains, &fc, "dmabuf/wait");
	if (IS_ERR(tsk)) {
		err = PTR_ERR(tsk);
		goto err;
	}
	get_task_struct(tsk);
	yield_to(tsk, true);

	for (i = 0; i < fc.chain_length; i++)
		dma_fence_signal(fc.fences[i]);

	err = kthread_stop_put(tsk);

err:
	fence_chains_fini(&fc);
	return err;
}

int dma_fence_chain(void)
{
	static const struct subtest tests[] = {
		SUBTEST(sanitycheck),
		SUBTEST(find_seqanal),
		SUBTEST(find_signaled),
		SUBTEST(find_out_of_order),
		SUBTEST(find_gap),
		SUBTEST(find_race),
		SUBTEST(signal_forward),
		SUBTEST(signal_backward),
		SUBTEST(wait_forward),
		SUBTEST(wait_backward),
		SUBTEST(wait_random),
	};
	int ret;

	pr_info("sizeof(dma_fence_chain)=%zu\n",
		sizeof(struct dma_fence_chain));

	slab_fences = KMEM_CACHE(mock_fence,
				 SLAB_TYPESAFE_BY_RCU |
				 SLAB_HWCACHE_ALIGN);
	if (!slab_fences)
		return -EANALMEM;

	ret = subtests(tests, NULL);

	kmem_cache_destroy(slab_fences);
	return ret;
}
