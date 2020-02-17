/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#include <linux/kref.h>

#include "gem/i915_gem_pm.h"
#include "gt/intel_gt.h"

#include "i915_selftest.h"

#include "igt_flush_test.h"
#include "lib_sw_fence.h"

struct live_active {
	struct i915_active base;
	struct kref ref;
	bool retired;
};

static void __live_get(struct live_active *active)
{
	kref_get(&active->ref);
}

static void __live_free(struct live_active *active)
{
	i915_active_fini(&active->base);
	kfree(active);
}

static void __live_release(struct kref *ref)
{
	struct live_active *active = container_of(ref, typeof(*active), ref);

	__live_free(active);
}

static void __live_put(struct live_active *active)
{
	kref_put(&active->ref, __live_release);
}

static int __live_active(struct i915_active *base)
{
	struct live_active *active = container_of(base, typeof(*active), base);

	__live_get(active);
	return 0;
}

static void __live_retire(struct i915_active *base)
{
	struct live_active *active = container_of(base, typeof(*active), base);

	active->retired = true;
	__live_put(active);
}

static struct live_active *__live_alloc(struct drm_i915_private *i915)
{
	struct live_active *active;

	active = kzalloc(sizeof(*active), GFP_KERNEL);
	if (!active)
		return NULL;

	kref_init(&active->ref);
	i915_active_init(&active->base, __live_active, __live_retire);

	return active;
}

static struct live_active *
__live_active_setup(struct drm_i915_private *i915)
{
	struct intel_engine_cs *engine;
	struct i915_sw_fence *submit;
	struct live_active *active;
	unsigned int count = 0;
	int err = 0;

	active = __live_alloc(i915);
	if (!active)
		return ERR_PTR(-ENOMEM);

	submit = heap_fence_create(GFP_KERNEL);
	if (!submit) {
		kfree(active);
		return ERR_PTR(-ENOMEM);
	}

	err = i915_active_acquire(&active->base);
	if (err)
		goto out;

	for_each_uabi_engine(engine, i915) {
		struct i915_request *rq;

		rq = intel_engine_create_kernel_request(engine);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			break;
		}

		err = i915_sw_fence_await_sw_fence_gfp(&rq->submit,
						       submit,
						       GFP_KERNEL);
		if (err >= 0)
			err = i915_active_add_request(&active->base, rq);
		i915_request_add(rq);
		if (err) {
			pr_err("Failed to track active ref!\n");
			break;
		}

		count++;
	}

	i915_active_release(&active->base);
	if (READ_ONCE(active->retired) && count) {
		pr_err("i915_active retired before submission!\n");
		err = -EINVAL;
	}
	if (atomic_read(&active->base.count) != count) {
		pr_err("i915_active not tracking all requests, found %d, expected %d\n",
		       atomic_read(&active->base.count), count);
		err = -EINVAL;
	}

out:
	i915_sw_fence_commit(submit);
	heap_fence_put(submit);
	if (err) {
		__live_put(active);
		active = ERR_PTR(err);
	}

	return active;
}

static int live_active_wait(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct live_active *active;
	int err = 0;

	/* Check that we get a callback when requests retire upon waiting */

	active = __live_active_setup(i915);
	if (IS_ERR(active))
		return PTR_ERR(active);

	i915_active_wait(&active->base);
	if (!READ_ONCE(active->retired)) {
		struct drm_printer p = drm_err_printer(__func__);

		pr_err("i915_active not retired after waiting!\n");
		i915_active_print(&active->base, &p);

		err = -EINVAL;
	}

	__live_put(active);

	if (igt_flush_test(i915))
		err = -EIO;

	return err;
}

static int live_active_retire(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct live_active *active;
	int err = 0;

	/* Check that we get a callback when requests are indirectly retired */

	active = __live_active_setup(i915);
	if (IS_ERR(active))
		return PTR_ERR(active);

	/* waits for & retires all requests */
	if (igt_flush_test(i915))
		err = -EIO;

	if (!READ_ONCE(active->retired)) {
		struct drm_printer p = drm_err_printer(__func__);

		pr_err("i915_active not retired after flushing!\n");
		i915_active_print(&active->base, &p);

		err = -EINVAL;
	}

	__live_put(active);

	return err;
}

int i915_active_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_active_wait),
		SUBTEST(live_active_retire),
	};

	if (intel_gt_is_wedged(&i915->gt))
		return 0;

	return i915_subtests(tests, i915);
}

static struct intel_engine_cs *node_to_barrier(struct active_node *it)
{
	struct intel_engine_cs *engine;

	if (!is_barrier(&it->base))
		return NULL;

	engine = __barrier_to_engine(it);
	smp_rmb(); /* serialise with add_active_barriers */
	if (!is_barrier(&it->base))
		return NULL;

	return engine;
}

void i915_active_print(struct i915_active *ref, struct drm_printer *m)
{
	drm_printf(m, "active %pS:%pS\n", ref->active, ref->retire);
	drm_printf(m, "\tcount: %d\n", atomic_read(&ref->count));
	drm_printf(m, "\tpreallocated barriers? %s\n",
		   yesno(!llist_empty(&ref->preallocated_barriers)));

	if (i915_active_acquire_if_busy(ref)) {
		struct active_node *it, *n;

		rbtree_postorder_for_each_entry_safe(it, n, &ref->tree, node) {
			struct intel_engine_cs *engine;

			engine = node_to_barrier(it);
			if (engine) {
				drm_printf(m, "\tbarrier: %s\n", engine->name);
				continue;
			}

			if (i915_active_fence_isset(&it->base)) {
				drm_printf(m,
					   "\ttimeline: %llx\n", it->timeline);
				continue;
			}
		}

		i915_active_release(ref);
	}
}

static void spin_unlock_wait(spinlock_t *lock)
{
	spin_lock_irq(lock);
	spin_unlock_irq(lock);
}

void i915_active_unlock_wait(struct i915_active *ref)
{
	if (i915_active_acquire_if_busy(ref)) {
		struct active_node *it, *n;

		rcu_read_lock();
		rbtree_postorder_for_each_entry_safe(it, n, &ref->tree, node) {
			struct dma_fence *f;

			/* Wait for all active callbacks */
			f = rcu_dereference(it->base.fence);
			if (f)
				spin_unlock_wait(f->lock);
		}
		rcu_read_unlock();

		i915_active_release(ref);
	}

	/* And wait for the retire callback */
	spin_lock_irq(&ref->tree_lock);
	spin_unlock_irq(&ref->tree_lock);

	/* ... which may have been on a thread instead */
	flush_work(&ref->work);
}
