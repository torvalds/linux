/* SPDX-License-Identifier: MIT */

/*
 * Copyright © 2019 Intel Corporation
 */

#include <linux/delay.h>
#include <linux/dma-fence.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "selftest.h"

static struct kmem_cache *slab_fences;

static struct mock_fence {
	struct dma_fence base;
	struct spinlock lock;
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

struct wait_cb {
	struct dma_fence_cb cb;
	struct task_struct *task;
};

static void mock_wakeup(struct dma_fence *f, struct dma_fence_cb *cb)
{
	wake_up_process(container_of(cb, struct wait_cb, cb)->task);
}

static long mock_wait(struct dma_fence *f, bool intr, long timeout)
{
	const int state = intr ? TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE;
	struct wait_cb cb = { .task = current };

	if (dma_fence_add_callback(f, &cb.cb, mock_wakeup))
		return timeout;

	while (timeout) {
		set_current_state(state);

		if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &f->flags))
			break;

		if (signal_pending_state(state, current))
			break;

		timeout = schedule_timeout(timeout);
	}
	__set_current_state(TASK_RUNNING);

	if (!dma_fence_remove_callback(f, &cb.cb))
		return timeout;

	if (signal_pending_state(state, current))
		return -ERESTARTSYS;

	return -ETIME;
}

static const struct dma_fence_ops mock_ops = {
	.get_driver_name = mock_name,
	.get_timeline_name = mock_name,
	.wait = mock_wait,
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

static int sanitycheck(void *arg)
{
	struct dma_fence *f;

	f = mock_fence();
	if (!f)
		return -ENOMEM;

	dma_fence_enable_sw_signaling(f);

	dma_fence_signal(f);
	dma_fence_put(f);

	return 0;
}

static int test_signaling(void *arg)
{
	struct dma_fence *f;
	int err = -EINVAL;

	f = mock_fence();
	if (!f)
		return -ENOMEM;

	dma_fence_enable_sw_signaling(f);

	if (dma_fence_is_signaled(f)) {
		pr_err("Fence unexpectedly signaled on creation\n");
		goto err_free;
	}

	if (dma_fence_signal(f)) {
		pr_err("Fence reported being already signaled\n");
		goto err_free;
	}

	if (!dma_fence_is_signaled(f)) {
		pr_err("Fence not reporting signaled\n");
		goto err_free;
	}

	if (!dma_fence_signal(f)) {
		pr_err("Fence reported not being already signaled\n");
		goto err_free;
	}

	err = 0;
err_free:
	dma_fence_put(f);
	return err;
}

struct simple_cb {
	struct dma_fence_cb cb;
	bool seen;
};

static void simple_callback(struct dma_fence *f, struct dma_fence_cb *cb)
{
	smp_store_mb(container_of(cb, struct simple_cb, cb)->seen, true);
}

static int test_add_callback(void *arg)
{
	struct simple_cb cb = {};
	struct dma_fence *f;
	int err = -EINVAL;

	f = mock_fence();
	if (!f)
		return -ENOMEM;

	if (dma_fence_add_callback(f, &cb.cb, simple_callback)) {
		pr_err("Failed to add callback, fence already signaled!\n");
		goto err_free;
	}

	dma_fence_signal(f);
	if (!cb.seen) {
		pr_err("Callback failed!\n");
		goto err_free;
	}

	err = 0;
err_free:
	dma_fence_put(f);
	return err;
}

static int test_late_add_callback(void *arg)
{
	struct simple_cb cb = {};
	struct dma_fence *f;
	int err = -EINVAL;

	f = mock_fence();
	if (!f)
		return -ENOMEM;

	dma_fence_enable_sw_signaling(f);

	dma_fence_signal(f);

	if (!dma_fence_add_callback(f, &cb.cb, simple_callback)) {
		pr_err("Added callback, but fence was already signaled!\n");
		goto err_free;
	}

	dma_fence_signal(f);
	if (cb.seen) {
		pr_err("Callback called after failed attachment !\n");
		goto err_free;
	}

	err = 0;
err_free:
	dma_fence_put(f);
	return err;
}

static int test_rm_callback(void *arg)
{
	struct simple_cb cb = {};
	struct dma_fence *f;
	int err = -EINVAL;

	f = mock_fence();
	if (!f)
		return -ENOMEM;

	if (dma_fence_add_callback(f, &cb.cb, simple_callback)) {
		pr_err("Failed to add callback, fence already signaled!\n");
		goto err_free;
	}

	if (!dma_fence_remove_callback(f, &cb.cb)) {
		pr_err("Failed to remove callback!\n");
		goto err_free;
	}

	dma_fence_signal(f);
	if (cb.seen) {
		pr_err("Callback still signaled after removal!\n");
		goto err_free;
	}

	err = 0;
err_free:
	dma_fence_put(f);
	return err;
}

static int test_late_rm_callback(void *arg)
{
	struct simple_cb cb = {};
	struct dma_fence *f;
	int err = -EINVAL;

	f = mock_fence();
	if (!f)
		return -ENOMEM;

	if (dma_fence_add_callback(f, &cb.cb, simple_callback)) {
		pr_err("Failed to add callback, fence already signaled!\n");
		goto err_free;
	}

	dma_fence_signal(f);
	if (!cb.seen) {
		pr_err("Callback failed!\n");
		goto err_free;
	}

	if (dma_fence_remove_callback(f, &cb.cb)) {
		pr_err("Callback removal succeed after being executed!\n");
		goto err_free;
	}

	err = 0;
err_free:
	dma_fence_put(f);
	return err;
}

static int test_status(void *arg)
{
	struct dma_fence *f;
	int err = -EINVAL;

	f = mock_fence();
	if (!f)
		return -ENOMEM;

	dma_fence_enable_sw_signaling(f);

	if (dma_fence_get_status(f)) {
		pr_err("Fence unexpectedly has signaled status on creation\n");
		goto err_free;
	}

	dma_fence_signal(f);
	if (!dma_fence_get_status(f)) {
		pr_err("Fence not reporting signaled status\n");
		goto err_free;
	}

	err = 0;
err_free:
	dma_fence_put(f);
	return err;
}

static int test_error(void *arg)
{
	struct dma_fence *f;
	int err = -EINVAL;

	f = mock_fence();
	if (!f)
		return -ENOMEM;

	dma_fence_enable_sw_signaling(f);

	dma_fence_set_error(f, -EIO);

	if (dma_fence_get_status(f)) {
		pr_err("Fence unexpectedly has error status before signal\n");
		goto err_free;
	}

	dma_fence_signal(f);
	if (dma_fence_get_status(f) != -EIO) {
		pr_err("Fence not reporting error status, got %d\n",
		       dma_fence_get_status(f));
		goto err_free;
	}

	err = 0;
err_free:
	dma_fence_put(f);
	return err;
}

static int test_wait(void *arg)
{
	struct dma_fence *f;
	int err = -EINVAL;

	f = mock_fence();
	if (!f)
		return -ENOMEM;

	dma_fence_enable_sw_signaling(f);

	if (dma_fence_wait_timeout(f, false, 0) != -ETIME) {
		pr_err("Wait reported complete before being signaled\n");
		goto err_free;
	}

	dma_fence_signal(f);

	if (dma_fence_wait_timeout(f, false, 0) != 0) {
		pr_err("Wait reported incomplete after being signaled\n");
		goto err_free;
	}

	err = 0;
err_free:
	dma_fence_signal(f);
	dma_fence_put(f);
	return err;
}

struct wait_timer {
	struct timer_list timer;
	struct dma_fence *f;
};

static void wait_timer(struct timer_list *timer)
{
	struct wait_timer *wt = from_timer(wt, timer, timer);

	dma_fence_signal(wt->f);
}

static int test_wait_timeout(void *arg)
{
	struct wait_timer wt;
	int err = -EINVAL;

	timer_setup_on_stack(&wt.timer, wait_timer, 0);

	wt.f = mock_fence();
	if (!wt.f)
		return -ENOMEM;

	dma_fence_enable_sw_signaling(wt.f);

	if (dma_fence_wait_timeout(wt.f, false, 1) != -ETIME) {
		pr_err("Wait reported complete before being signaled\n");
		goto err_free;
	}

	mod_timer(&wt.timer, jiffies + 1);

	if (dma_fence_wait_timeout(wt.f, false, 2) == -ETIME) {
		if (timer_pending(&wt.timer)) {
			pr_notice("Timer did not fire within the jiffy!\n");
			err = 0; /* not our fault! */
		} else {
			pr_err("Wait reported incomplete after timeout\n");
		}
		goto err_free;
	}

	err = 0;
err_free:
	timer_delete_sync(&wt.timer);
	destroy_timer_on_stack(&wt.timer);
	dma_fence_signal(wt.f);
	dma_fence_put(wt.f);
	return err;
}

static int test_stub(void *arg)
{
	struct dma_fence *f[64];
	int err = -EINVAL;
	int i;

	for (i = 0; i < ARRAY_SIZE(f); i++) {
		f[i] = dma_fence_get_stub();
		if (!dma_fence_is_signaled(f[i])) {
			pr_err("Obtained unsignaled stub fence!\n");
			goto err;
		}
	}

	err = 0;
err:
	while (i--)
		dma_fence_put(f[i]);
	return err;
}

/* Now off to the races! */

struct race_thread {
	struct dma_fence __rcu **fences;
	struct task_struct *task;
	bool before;
	int id;
};

static void __wait_for_callbacks(struct dma_fence *f)
{
	spin_lock_irq(f->lock);
	spin_unlock_irq(f->lock);
}

static int thread_signal_callback(void *arg)
{
	const struct race_thread *t = arg;
	unsigned long pass = 0;
	unsigned long miss = 0;
	int err = 0;

	while (!err && !kthread_should_stop()) {
		struct dma_fence *f1, *f2;
		struct simple_cb cb;

		f1 = mock_fence();
		if (!f1) {
			err = -ENOMEM;
			break;
		}

		dma_fence_enable_sw_signaling(f1);

		rcu_assign_pointer(t->fences[t->id], f1);
		smp_wmb();

		rcu_read_lock();
		do {
			f2 = dma_fence_get_rcu_safe(&t->fences[!t->id]);
		} while (!f2 && !kthread_should_stop());
		rcu_read_unlock();

		if (t->before)
			dma_fence_signal(f1);

		smp_store_mb(cb.seen, false);
		if (!f2 ||
		    dma_fence_add_callback(f2, &cb.cb, simple_callback)) {
			miss++;
			cb.seen = true;
		}

		if (!t->before)
			dma_fence_signal(f1);

		if (!cb.seen) {
			dma_fence_wait(f2, false);
			__wait_for_callbacks(f2);
		}

		if (!READ_ONCE(cb.seen)) {
			pr_err("Callback not seen on thread %d, pass %lu (%lu misses), signaling %s add_callback; fence signaled? %s\n",
			       t->id, pass, miss,
			       t->before ? "before" : "after",
			       dma_fence_is_signaled(f2) ? "yes" : "no");
			err = -EINVAL;
		}

		dma_fence_put(f2);

		rcu_assign_pointer(t->fences[t->id], NULL);
		smp_wmb();

		dma_fence_put(f1);

		pass++;
	}

	pr_info("%s[%d] completed %lu passes, %lu misses\n",
		__func__, t->id, pass, miss);
	return err;
}

static int race_signal_callback(void *arg)
{
	struct dma_fence __rcu *f[2] = {};
	int ret = 0;
	int pass;

	for (pass = 0; !ret && pass <= 1; pass++) {
		struct race_thread t[2];
		int i;

		for (i = 0; i < ARRAY_SIZE(t); i++) {
			t[i].fences = f;
			t[i].id = i;
			t[i].before = pass;
			t[i].task = kthread_run(thread_signal_callback, &t[i],
						"dma-fence:%d", i);
			if (IS_ERR(t[i].task)) {
				ret = PTR_ERR(t[i].task);
				while (--i >= 0)
					kthread_stop_put(t[i].task);
				return ret;
			}
			get_task_struct(t[i].task);
		}

		msleep(50);

		for (i = 0; i < ARRAY_SIZE(t); i++) {
			int err;

			err = kthread_stop_put(t[i].task);
			if (err && !ret)
				ret = err;
		}
	}

	return ret;
}

int dma_fence(void)
{
	static const struct subtest tests[] = {
		SUBTEST(sanitycheck),
		SUBTEST(test_signaling),
		SUBTEST(test_add_callback),
		SUBTEST(test_late_add_callback),
		SUBTEST(test_rm_callback),
		SUBTEST(test_late_rm_callback),
		SUBTEST(test_status),
		SUBTEST(test_error),
		SUBTEST(test_wait),
		SUBTEST(test_wait_timeout),
		SUBTEST(test_stub),
		SUBTEST(race_signal_callback),
	};
	int ret;

	pr_info("sizeof(dma_fence)=%zu\n", sizeof(struct dma_fence));

	slab_fences = KMEM_CACHE(mock_fence,
				 SLAB_TYPESAFE_BY_RCU |
				 SLAB_HWCACHE_ALIGN);
	if (!slab_fences)
		return -ENOMEM;

	ret = subtests(tests, NULL);

	kmem_cache_destroy(slab_fences);

	return ret;
}
