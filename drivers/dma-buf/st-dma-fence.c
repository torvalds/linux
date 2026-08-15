/* SPDX-License-Identifier: MIT */

/*
 * Copyright © 2019 Intel Corporation
 */

#include <kunit/test.h>
#include <linux/delay.h>
#include <linux/dma-fence.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

static const char *mock_name(struct dma_fence *f)
{
	return "mock";
}

static const struct dma_fence_ops mock_ops = {
	.get_driver_name = mock_name,
	.get_timeline_name = mock_name,
};

static struct dma_fence *mock_fence(void)
{
	struct dma_fence *f;

	f = kmalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return NULL;

	dma_fence_init(f, &mock_ops, NULL, 0, 0);
	return f;
}

static void test_sanitycheck(struct kunit *test)
{
	struct dma_fence *f;

	f = mock_fence();
	KUNIT_ASSERT_NOT_NULL(test, f);

	dma_fence_enable_sw_signaling(f);

	dma_fence_signal(f);
	dma_fence_put(f);
}

static void test_signaling(struct kunit *test)
{
	struct dma_fence *f;

	f = mock_fence();
	KUNIT_ASSERT_NOT_NULL(test, f);

	dma_fence_enable_sw_signaling(f);

	if (dma_fence_is_signaled(f)) {
		KUNIT_FAIL(test, "Fence unexpectedly signaled on creation");
		goto err_free;
	}

	if (dma_fence_check_and_signal(f)) {
		KUNIT_FAIL(test, "Fence reported being already signaled");
		goto err_free;
	}

	if (!dma_fence_is_signaled(f)) {
		KUNIT_FAIL(test, "Fence not reporting signaled");
		goto err_free;
	}

	if (!dma_fence_test_signaled_flag(f)) {
		KUNIT_FAIL(test, "Fence reported not being already signaled");
		goto err_free;
	}

	if (rcu_dereference_protected(f->ops, true)) {
		KUNIT_FAIL(test, "Fence ops not cleared on signal");
		goto err_free;
	}

err_free:
	dma_fence_put(f);
}

struct simple_cb {
	struct dma_fence_cb cb;
	bool seen;
};

static void simple_callback(struct dma_fence *f, struct dma_fence_cb *cb)
{
	smp_store_mb(container_of(cb, struct simple_cb, cb)->seen, true);
}

static void test_add_callback(struct kunit *test)
{
	struct simple_cb cb = {};
	struct dma_fence *f;

	f = mock_fence();
	KUNIT_ASSERT_NOT_NULL(test, f);

	if (dma_fence_add_callback(f, &cb.cb, simple_callback)) {
		KUNIT_FAIL(test, "Failed to add callback, fence already signaled!");
		goto err_free;
	}

	dma_fence_signal(f);
	if (!cb.seen) {
		KUNIT_FAIL(test, "Callback failed!");
		goto err_free;
	}

err_free:
	dma_fence_put(f);
}

static void test_late_add_callback(struct kunit *test)
{
	struct simple_cb cb = {};
	struct dma_fence *f;

	f = mock_fence();
	KUNIT_ASSERT_NOT_NULL(test, f);

	dma_fence_enable_sw_signaling(f);

	dma_fence_signal(f);

	if (!dma_fence_add_callback(f, &cb.cb, simple_callback)) {
		KUNIT_FAIL(test, "Added callback, but fence was already signaled!");
		goto err_free;
	}

	dma_fence_signal(f);
	if (cb.seen) {
		KUNIT_FAIL(test, "Callback called after failed attachment!");
		goto err_free;
	}

err_free:
	dma_fence_put(f);
}

static void test_rm_callback(struct kunit *test)
{
	struct simple_cb cb = {};
	struct dma_fence *f;

	f = mock_fence();
	KUNIT_ASSERT_NOT_NULL(test, f);

	if (dma_fence_add_callback(f, &cb.cb, simple_callback)) {
		KUNIT_FAIL(test, "Failed to add callback, fence already signaled!");
		goto err_free;
	}

	if (!dma_fence_remove_callback(f, &cb.cb)) {
		KUNIT_FAIL(test, "Failed to remove callback!");
		goto err_free;
	}

	dma_fence_signal(f);
	if (cb.seen) {
		KUNIT_FAIL(test, "Callback still signaled after removal!");
		goto err_free;
	}

err_free:
	dma_fence_put(f);
}

static void test_late_rm_callback(struct kunit *test)
{
	struct simple_cb cb = {};
	struct dma_fence *f;

	f = mock_fence();
	KUNIT_ASSERT_NOT_NULL(test, f);

	if (dma_fence_add_callback(f, &cb.cb, simple_callback)) {
		KUNIT_FAIL(test, "Failed to add callback, fence already signaled!");
		goto err_free;
	}

	dma_fence_signal(f);
	if (!cb.seen) {
		KUNIT_FAIL(test, "Callback failed!");
		goto err_free;
	}

	if (dma_fence_remove_callback(f, &cb.cb)) {
		KUNIT_FAIL(test, "Callback removal succeeded after being executed!");
		goto err_free;
	}

err_free:
	dma_fence_put(f);
}

static void test_status(struct kunit *test)
{
	struct dma_fence *f;

	f = mock_fence();
	KUNIT_ASSERT_NOT_NULL(test, f);

	dma_fence_enable_sw_signaling(f);

	if (dma_fence_get_status(f)) {
		KUNIT_FAIL(test, "Fence unexpectedly has signaled status on creation");
		goto err_free;
	}

	dma_fence_signal(f);
	if (!dma_fence_get_status(f)) {
		KUNIT_FAIL(test, "Fence not reporting signaled status");
		goto err_free;
	}

err_free:
	dma_fence_put(f);
}

static void test_error(struct kunit *test)
{
	struct dma_fence *f;

	f = mock_fence();
	KUNIT_ASSERT_NOT_NULL(test, f);

	dma_fence_enable_sw_signaling(f);

	dma_fence_set_error(f, -EIO);

	if (dma_fence_get_status(f)) {
		KUNIT_FAIL(test, "Fence unexpectedly has error status before signal");
		goto err_free;
	}

	dma_fence_signal(f);
	if (dma_fence_get_status(f) != -EIO) {
		KUNIT_FAIL(test, "Fence not reporting error status, got %d",
			   dma_fence_get_status(f));
		goto err_free;
	}

err_free:
	dma_fence_put(f);
}

static void test_wait(struct kunit *test)
{
	struct dma_fence *f;

	f = mock_fence();
	KUNIT_ASSERT_NOT_NULL(test, f);

	dma_fence_enable_sw_signaling(f);

	if (dma_fence_wait_timeout(f, false, 0) != 0) {
		KUNIT_FAIL(test, "Wait reported complete before being signaled");
		goto err_free;
	}

	dma_fence_signal(f);

	if (dma_fence_wait_timeout(f, false, 0) != 1) {
		KUNIT_FAIL(test, "Wait reported incomplete after being signaled");
		goto err_free;
	}

err_free:
	dma_fence_signal(f);
	dma_fence_put(f);
}

struct wait_timer {
	struct timer_list timer;
	struct dma_fence *f;
};

static void wait_timer(struct timer_list *timer)
{
	struct wait_timer *wt = timer_container_of(wt, timer, timer);

	dma_fence_signal(wt->f);
}

static void test_wait_timeout(struct kunit *test)
{
	struct wait_timer wt;

	timer_setup_on_stack(&wt.timer, wait_timer, 0);

	wt.f = mock_fence();
	KUNIT_ASSERT_NOT_NULL(test, wt.f);

	dma_fence_enable_sw_signaling(wt.f);

	if (dma_fence_wait_timeout(wt.f, false, 1) != 0) {
		KUNIT_FAIL(test, "Wait reported complete before being signaled");
		goto err_free;
	}

	mod_timer(&wt.timer, jiffies + 1);

	if (dma_fence_wait_timeout(wt.f, false, HZ) == 0) {
		if (timer_pending(&wt.timer)) {
			kunit_mark_skipped(
				test, "Timer did not fire within on HZ!\n");
		} else {
			KUNIT_FAIL(test,
				   "Wait reported incomplete after timeout");
		}
		goto err_free;
	}

err_free:
	timer_delete_sync(&wt.timer);
	timer_destroy_on_stack(&wt.timer);
	dma_fence_signal(wt.f);
	dma_fence_put(wt.f);
}

static void test_stub(struct kunit *test)
{
	struct dma_fence *f[64];
	int i;

	for (i = 0; i < ARRAY_SIZE(f); i++) {
		f[i] = dma_fence_get_stub();
		if (!dma_fence_is_signaled(f[i])) {
			KUNIT_FAIL(test, "Obtained unsignaled stub fence!");
			goto err;
		}
	}

err:
	while (i--)
		dma_fence_put(f[i]);
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
	unsigned long flags;

	dma_fence_lock_irqsave(f, flags);
	dma_fence_unlock_irqrestore(f, flags);
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

static void test_race_signal_callback(struct kunit *test)
{
	struct dma_fence __rcu *f[2] = {};
	int ret = 0;
	int pass;

	/*
	 * thread_signal_callback() spins under RCU and it cannot make forward
	 * progress unless the threads are truly running concurrently.
	 */
	if (num_online_cpus() < 2)
		kunit_skip(test, "requires at least 2 CPUs");

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
				KUNIT_FAIL(test, "Failed to create kthread");
				while (--i >= 0)
					kthread_stop_put(t[i].task);
				return;
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

	KUNIT_EXPECT_EQ(test, ret, 0);
}

static int dma_fence_suite_init(struct kunit_suite *suite)
{
	pr_info("sizeof(dma_fence)=%zu\n", sizeof(struct dma_fence));
	return 0;
}

static struct kunit_case dma_fence_cases[] = {
	KUNIT_CASE(test_sanitycheck),
	KUNIT_CASE(test_signaling),
	KUNIT_CASE(test_add_callback),
	KUNIT_CASE(test_late_add_callback),
	KUNIT_CASE(test_rm_callback),
	KUNIT_CASE(test_late_rm_callback),
	KUNIT_CASE(test_status),
	KUNIT_CASE(test_error),
	KUNIT_CASE(test_wait),
	KUNIT_CASE(test_wait_timeout),
	KUNIT_CASE(test_stub),
	KUNIT_CASE(test_race_signal_callback),
	{}
};

static struct kunit_suite dma_fence_test_suite = {
	.name = "dma-buf-fence",
	.suite_init = dma_fence_suite_init,
	.test_cases = dma_fence_cases,
};

kunit_test_suite(dma_fence_test_suite);
