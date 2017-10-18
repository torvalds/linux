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
#include "i915_random.h"

#include "mock_gem_device.h"
#include "mock_engine.h"

static int check_rbtree(struct intel_engine_cs *engine,
			const unsigned long *bitmap,
			const struct intel_wait *waiters,
			const int count)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;
	struct rb_node *rb;
	int n;

	if (&b->irq_wait->node != rb_first(&b->waiters)) {
		pr_err("First waiter does not match first element of wait-tree\n");
		return -EINVAL;
	}

	n = find_first_bit(bitmap, count);
	for (rb = rb_first(&b->waiters); rb; rb = rb_next(rb)) {
		struct intel_wait *w = container_of(rb, typeof(*w), node);
		int idx = w - waiters;

		if (!test_bit(idx, bitmap)) {
			pr_err("waiter[%d, seqno=%d] removed but still in wait-tree\n",
			       idx, w->seqno);
			return -EINVAL;
		}

		if (n != idx) {
			pr_err("waiter[%d, seqno=%d] does not match expected next element in tree [%d]\n",
			       idx, w->seqno, n);
			return -EINVAL;
		}

		n = find_next_bit(bitmap, count, n + 1);
	}

	return 0;
}

static int check_completion(struct intel_engine_cs *engine,
			    const unsigned long *bitmap,
			    const struct intel_wait *waiters,
			    const int count)
{
	int n;

	for (n = 0; n < count; n++) {
		if (intel_wait_complete(&waiters[n]) != !!test_bit(n, bitmap))
			continue;

		pr_err("waiter[%d, seqno=%d] is %s, but expected %s\n",
		       n, waiters[n].seqno,
		       intel_wait_complete(&waiters[n]) ? "complete" : "active",
		       test_bit(n, bitmap) ? "active" : "complete");
		return -EINVAL;
	}

	return 0;
}

static int check_rbtree_empty(struct intel_engine_cs *engine)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;

	if (b->irq_wait) {
		pr_err("Empty breadcrumbs still has a waiter\n");
		return -EINVAL;
	}

	if (!RB_EMPTY_ROOT(&b->waiters)) {
		pr_err("Empty breadcrumbs, but wait-tree not empty\n");
		return -EINVAL;
	}

	return 0;
}

static int igt_random_insert_remove(void *arg)
{
	const u32 seqno_bias = 0x1000;
	I915_RND_STATE(prng);
	struct intel_engine_cs *engine = arg;
	struct intel_wait *waiters;
	const int count = 4096;
	unsigned int *order;
	unsigned long *bitmap;
	int err = -ENOMEM;
	int n;

	mock_engine_reset(engine);

	waiters = kvmalloc_array(count, sizeof(*waiters), GFP_TEMPORARY);
	if (!waiters)
		goto out_engines;

	bitmap = kcalloc(DIV_ROUND_UP(count, BITS_PER_LONG), sizeof(*bitmap),
			 GFP_TEMPORARY);
	if (!bitmap)
		goto out_waiters;

	order = i915_random_order(count, &prng);
	if (!order)
		goto out_bitmap;

	for (n = 0; n < count; n++)
		intel_wait_init_for_seqno(&waiters[n], seqno_bias + n);

	err = check_rbtree(engine, bitmap, waiters, count);
	if (err)
		goto out_order;

	/* Add and remove waiters into the rbtree in random order. At each
	 * step, we verify that the rbtree is correctly ordered.
	 */
	for (n = 0; n < count; n++) {
		int i = order[n];

		intel_engine_add_wait(engine, &waiters[i]);
		__set_bit(i, bitmap);

		err = check_rbtree(engine, bitmap, waiters, count);
		if (err)
			goto out_order;
	}

	i915_random_reorder(order, count, &prng);
	for (n = 0; n < count; n++) {
		int i = order[n];

		intel_engine_remove_wait(engine, &waiters[i]);
		__clear_bit(i, bitmap);

		err = check_rbtree(engine, bitmap, waiters, count);
		if (err)
			goto out_order;
	}

	err = check_rbtree_empty(engine);
out_order:
	kfree(order);
out_bitmap:
	kfree(bitmap);
out_waiters:
	kvfree(waiters);
out_engines:
	mock_engine_flush(engine);
	return err;
}

static int igt_insert_complete(void *arg)
{
	const u32 seqno_bias = 0x1000;
	struct intel_engine_cs *engine = arg;
	struct intel_wait *waiters;
	const int count = 4096;
	unsigned long *bitmap;
	int err = -ENOMEM;
	int n, m;

	mock_engine_reset(engine);

	waiters = kvmalloc_array(count, sizeof(*waiters), GFP_TEMPORARY);
	if (!waiters)
		goto out_engines;

	bitmap = kcalloc(DIV_ROUND_UP(count, BITS_PER_LONG), sizeof(*bitmap),
			 GFP_TEMPORARY);
	if (!bitmap)
		goto out_waiters;

	for (n = 0; n < count; n++) {
		intel_wait_init_for_seqno(&waiters[n], n + seqno_bias);
		intel_engine_add_wait(engine, &waiters[n]);
		__set_bit(n, bitmap);
	}
	err = check_rbtree(engine, bitmap, waiters, count);
	if (err)
		goto out_bitmap;

	/* On each step, we advance the seqno so that several waiters are then
	 * complete (we increase the seqno by increasingly larger values to
	 * retire more and more waiters at once). All retired waiters should
	 * be woken and removed from the rbtree, and so that we check.
	 */
	for (n = 0; n < count; n = m) {
		int seqno = 2 * n;

		GEM_BUG_ON(find_first_bit(bitmap, count) != n);

		if (intel_wait_complete(&waiters[n])) {
			pr_err("waiter[%d, seqno=%d] completed too early\n",
			       n, waiters[n].seqno);
			err = -EINVAL;
			goto out_bitmap;
		}

		/* complete the following waiters */
		mock_seqno_advance(engine, seqno + seqno_bias);
		for (m = n; m <= seqno; m++) {
			if (m == count)
				break;

			GEM_BUG_ON(!test_bit(m, bitmap));
			__clear_bit(m, bitmap);
		}

		intel_engine_remove_wait(engine, &waiters[n]);
		RB_CLEAR_NODE(&waiters[n].node);

		err = check_rbtree(engine, bitmap, waiters, count);
		if (err) {
			pr_err("rbtree corrupt after seqno advance to %d\n",
			       seqno + seqno_bias);
			goto out_bitmap;
		}

		err = check_completion(engine, bitmap, waiters, count);
		if (err) {
			pr_err("completions after seqno advance to %d failed\n",
			       seqno + seqno_bias);
			goto out_bitmap;
		}
	}

	err = check_rbtree_empty(engine);
out_bitmap:
	kfree(bitmap);
out_waiters:
	kvfree(waiters);
out_engines:
	mock_engine_flush(engine);
	return err;
}

struct igt_wakeup {
	struct task_struct *tsk;
	atomic_t *ready, *set, *done;
	struct intel_engine_cs *engine;
	unsigned long flags;
#define STOP 0
#define IDLE 1
	wait_queue_head_t *wq;
	u32 seqno;
};

static int wait_atomic(atomic_t *p)
{
	schedule();
	return 0;
}

static int wait_atomic_timeout(atomic_t *p)
{
	return schedule_timeout(10 * HZ) ? 0 : -ETIMEDOUT;
}

static bool wait_for_ready(struct igt_wakeup *w)
{
	DEFINE_WAIT(ready);

	set_bit(IDLE, &w->flags);
	if (atomic_dec_and_test(w->done))
		wake_up_atomic_t(w->done);

	if (test_bit(STOP, &w->flags))
		goto out;

	for (;;) {
		prepare_to_wait(w->wq, &ready, TASK_INTERRUPTIBLE);
		if (atomic_read(w->ready) == 0)
			break;

		schedule();
	}
	finish_wait(w->wq, &ready);

out:
	clear_bit(IDLE, &w->flags);
	if (atomic_dec_and_test(w->set))
		wake_up_atomic_t(w->set);

	return !test_bit(STOP, &w->flags);
}

static int igt_wakeup_thread(void *arg)
{
	struct igt_wakeup *w = arg;
	struct intel_wait wait;

	while (wait_for_ready(w)) {
		GEM_BUG_ON(kthread_should_stop());

		intel_wait_init_for_seqno(&wait, w->seqno);
		intel_engine_add_wait(w->engine, &wait);
		for (;;) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			if (i915_seqno_passed(intel_engine_get_seqno(w->engine),
					      w->seqno))
				break;

			if (test_bit(STOP, &w->flags)) /* emergency escape */
				break;

			schedule();
		}
		intel_engine_remove_wait(w->engine, &wait);
		__set_current_state(TASK_RUNNING);
	}

	return 0;
}

static void igt_wake_all_sync(atomic_t *ready,
			      atomic_t *set,
			      atomic_t *done,
			      wait_queue_head_t *wq,
			      int count)
{
	atomic_set(set, count);
	atomic_set(ready, 0);
	wake_up_all(wq);

	wait_on_atomic_t(set, wait_atomic, TASK_UNINTERRUPTIBLE);
	atomic_set(ready, count);
	atomic_set(done, count);
}

static int igt_wakeup(void *arg)
{
	I915_RND_STATE(prng);
	const int state = TASK_UNINTERRUPTIBLE;
	struct intel_engine_cs *engine = arg;
	struct igt_wakeup *waiters;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq);
	const int count = 4096;
	const u32 max_seqno = count / 4;
	atomic_t ready, set, done;
	int err = -ENOMEM;
	int n, step;

	mock_engine_reset(engine);

	waiters = kvmalloc_array(count, sizeof(*waiters), GFP_TEMPORARY);
	if (!waiters)
		goto out_engines;

	/* Create a large number of threads, each waiting on a random seqno.
	 * Multiple waiters will be waiting for the same seqno.
	 */
	atomic_set(&ready, count);
	for (n = 0; n < count; n++) {
		waiters[n].wq = &wq;
		waiters[n].ready = &ready;
		waiters[n].set = &set;
		waiters[n].done = &done;
		waiters[n].engine = engine;
		waiters[n].flags = BIT(IDLE);

		waiters[n].tsk = kthread_run(igt_wakeup_thread, &waiters[n],
					     "i915/igt:%d", n);
		if (IS_ERR(waiters[n].tsk))
			goto out_waiters;

		get_task_struct(waiters[n].tsk);
	}

	for (step = 1; step <= max_seqno; step <<= 1) {
		u32 seqno;

		/* The waiter threads start paused as we assign them a random
		 * seqno and reset the engine. Once the engine is reset,
		 * we signal that the threads may begin their wait upon their
		 * seqno.
		 */
		for (n = 0; n < count; n++) {
			GEM_BUG_ON(!test_bit(IDLE, &waiters[n].flags));
			waiters[n].seqno =
				1 + prandom_u32_state(&prng) % max_seqno;
		}
		mock_seqno_advance(engine, 0);
		igt_wake_all_sync(&ready, &set, &done, &wq, count);

		/* Simulate the GPU doing chunks of work, with one or more
		 * seqno appearing to finish at the same time. A random number
		 * of threads will be waiting upon the update and hopefully be
		 * woken.
		 */
		for (seqno = 1; seqno <= max_seqno + step; seqno += step) {
			usleep_range(50, 500);
			mock_seqno_advance(engine, seqno);
		}
		GEM_BUG_ON(intel_engine_get_seqno(engine) < 1 + max_seqno);

		/* With the seqno now beyond any of the waiting threads, they
		 * should all be woken, see that they are complete and signal
		 * that they are ready for the next test. We wait until all
		 * threads are complete and waiting for us (i.e. not a seqno).
		 */
		err = wait_on_atomic_t(&done, wait_atomic_timeout, state);
		if (err) {
			pr_err("Timed out waiting for %d remaining waiters\n",
			       atomic_read(&done));
			break;
		}

		err = check_rbtree_empty(engine);
		if (err)
			break;
	}

out_waiters:
	for (n = 0; n < count; n++) {
		if (IS_ERR(waiters[n].tsk))
			break;

		set_bit(STOP, &waiters[n].flags);
	}
	mock_seqno_advance(engine, INT_MAX); /* wakeup any broken waiters */
	igt_wake_all_sync(&ready, &set, &done, &wq, n);

	for (n = 0; n < count; n++) {
		if (IS_ERR(waiters[n].tsk))
			break;

		kthread_stop(waiters[n].tsk);
		put_task_struct(waiters[n].tsk);
	}

	kvfree(waiters);
out_engines:
	mock_engine_flush(engine);
	return err;
}

int intel_breadcrumbs_mock_selftests(void)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_random_insert_remove),
		SUBTEST(igt_insert_complete),
		SUBTEST(igt_wakeup),
	};
	struct drm_i915_private *i915;
	int err;

	i915 = mock_gem_device();
	if (!i915)
		return -ENOMEM;

	err = i915_subtests(tests, i915->engine[RCS]);
	drm_dev_unref(&i915->drm);

	return err;
}
