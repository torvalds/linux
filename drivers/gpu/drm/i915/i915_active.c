/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include <linux/debugobjects.h>

#include "gt/intel_context.h"
#include "gt/intel_engine_heartbeat.h"
#include "gt/intel_engine_pm.h"
#include "gt/intel_ring.h"

#include "i915_drv.h"
#include "i915_active.h"
#include "i915_globals.h"

/*
 * Active refs memory management
 *
 * To be more economical with memory, we reap all the i915_active trees as
 * they idle (when we know the active requests are inactive) and allocate the
 * nodes from a local slab cache to hopefully reduce the fragmentation.
 */
static struct i915_global_active {
	struct i915_global base;
	struct kmem_cache *slab_cache;
} global;

struct active_node {
	struct i915_active_fence base;
	struct i915_active *ref;
	struct rb_node node;
	u64 timeline;
};

static inline struct active_node *
node_from_active(struct i915_active_fence *active)
{
	return container_of(active, struct active_node, base);
}

#define take_preallocated_barriers(x) llist_del_all(&(x)->preallocated_barriers)

static inline bool is_barrier(const struct i915_active_fence *active)
{
	return IS_ERR(rcu_access_pointer(active->fence));
}

static inline struct llist_node *barrier_to_ll(struct active_node *node)
{
	GEM_BUG_ON(!is_barrier(&node->base));
	return (struct llist_node *)&node->base.cb.node;
}

static inline struct intel_engine_cs *
__barrier_to_engine(struct active_node *node)
{
	return (struct intel_engine_cs *)READ_ONCE(node->base.cb.node.prev);
}

static inline struct intel_engine_cs *
barrier_to_engine(struct active_node *node)
{
	GEM_BUG_ON(!is_barrier(&node->base));
	return __barrier_to_engine(node);
}

static inline struct active_node *barrier_from_ll(struct llist_node *x)
{
	return container_of((struct list_head *)x,
			    struct active_node, base.cb.node);
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM) && IS_ENABLED(CONFIG_DEBUG_OBJECTS)

static void *active_debug_hint(void *addr)
{
	struct i915_active *ref = addr;

	return (void *)ref->active ?: (void *)ref->retire ?: (void *)ref;
}

static const struct debug_obj_descr active_debug_desc = {
	.name = "i915_active",
	.debug_hint = active_debug_hint,
};

static void debug_active_init(struct i915_active *ref)
{
	debug_object_init(ref, &active_debug_desc);
}

static void debug_active_activate(struct i915_active *ref)
{
	lockdep_assert_held(&ref->tree_lock);
	if (!atomic_read(&ref->count)) /* before the first inc */
		debug_object_activate(ref, &active_debug_desc);
}

static void debug_active_deactivate(struct i915_active *ref)
{
	lockdep_assert_held(&ref->tree_lock);
	if (!atomic_read(&ref->count)) /* after the last dec */
		debug_object_deactivate(ref, &active_debug_desc);
}

static void debug_active_fini(struct i915_active *ref)
{
	debug_object_free(ref, &active_debug_desc);
}

static void debug_active_assert(struct i915_active *ref)
{
	debug_object_assert_init(ref, &active_debug_desc);
}

#else

static inline void debug_active_init(struct i915_active *ref) { }
static inline void debug_active_activate(struct i915_active *ref) { }
static inline void debug_active_deactivate(struct i915_active *ref) { }
static inline void debug_active_fini(struct i915_active *ref) { }
static inline void debug_active_assert(struct i915_active *ref) { }

#endif

static void
__active_retire(struct i915_active *ref)
{
	struct active_node *it, *n;
	struct rb_root root;
	unsigned long flags;

	GEM_BUG_ON(i915_active_is_idle(ref));

	/* return the unused nodes to our slabcache -- flushing the allocator */
	if (!atomic_dec_and_lock_irqsave(&ref->count, &ref->tree_lock, flags))
		return;

	GEM_BUG_ON(rcu_access_pointer(ref->excl.fence));
	debug_active_deactivate(ref);

	root = ref->tree;
	ref->tree = RB_ROOT;
	ref->cache = NULL;

	spin_unlock_irqrestore(&ref->tree_lock, flags);

	/* After the final retire, the entire struct may be freed */
	if (ref->retire)
		ref->retire(ref);

	/* ... except if you wait on it, you must manage your own references! */
	wake_up_var(ref);

	rbtree_postorder_for_each_entry_safe(it, n, &root, node) {
		GEM_BUG_ON(i915_active_fence_isset(&it->base));
		kmem_cache_free(global.slab_cache, it);
	}
}

static void
active_work(struct work_struct *wrk)
{
	struct i915_active *ref = container_of(wrk, typeof(*ref), work);

	GEM_BUG_ON(!atomic_read(&ref->count));
	if (atomic_add_unless(&ref->count, -1, 1))
		return;

	__active_retire(ref);
}

static void
active_retire(struct i915_active *ref)
{
	GEM_BUG_ON(!atomic_read(&ref->count));
	if (atomic_add_unless(&ref->count, -1, 1))
		return;

	if (ref->flags & I915_ACTIVE_RETIRE_SLEEPS) {
		queue_work(system_unbound_wq, &ref->work);
		return;
	}

	__active_retire(ref);
}

static inline struct dma_fence **
__active_fence_slot(struct i915_active_fence *active)
{
	return (struct dma_fence ** __force)&active->fence;
}

static inline bool
active_fence_cb(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	struct i915_active_fence *active =
		container_of(cb, typeof(*active), cb);

	return cmpxchg(__active_fence_slot(active), fence, NULL) == fence;
}

static void
node_retire(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	if (active_fence_cb(fence, cb))
		active_retire(container_of(cb, struct active_node, base.cb)->ref);
}

static void
excl_retire(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	if (active_fence_cb(fence, cb))
		active_retire(container_of(cb, struct i915_active, excl.cb));
}

static struct i915_active_fence *
active_instance(struct i915_active *ref, struct intel_timeline *tl)
{
	struct active_node *node, *prealloc;
	struct rb_node **p, *parent;
	u64 idx = tl->fence_context;

	/*
	 * We track the most recently used timeline to skip a rbtree search
	 * for the common case, under typical loads we never need the rbtree
	 * at all. We can reuse the last slot if it is empty, that is
	 * after the previous activity has been retired, or if it matches the
	 * current timeline.
	 */
	node = READ_ONCE(ref->cache);
	if (node && node->timeline == idx)
		return &node->base;

	/* Preallocate a replacement, just in case */
	prealloc = kmem_cache_alloc(global.slab_cache, GFP_KERNEL);
	if (!prealloc)
		return NULL;

	spin_lock_irq(&ref->tree_lock);
	GEM_BUG_ON(i915_active_is_idle(ref));

	parent = NULL;
	p = &ref->tree.rb_node;
	while (*p) {
		parent = *p;

		node = rb_entry(parent, struct active_node, node);
		if (node->timeline == idx) {
			kmem_cache_free(global.slab_cache, prealloc);
			goto out;
		}

		if (node->timeline < idx)
			p = &parent->rb_right;
		else
			p = &parent->rb_left;
	}

	node = prealloc;
	__i915_active_fence_init(&node->base, NULL, node_retire);
	node->ref = ref;
	node->timeline = idx;

	rb_link_node(&node->node, parent, p);
	rb_insert_color(&node->node, &ref->tree);

out:
	ref->cache = node;
	spin_unlock_irq(&ref->tree_lock);

	BUILD_BUG_ON(offsetof(typeof(*node), base));
	return &node->base;
}

void __i915_active_init(struct i915_active *ref,
			int (*active)(struct i915_active *ref),
			void (*retire)(struct i915_active *ref),
			struct lock_class_key *mkey,
			struct lock_class_key *wkey)
{
	unsigned long bits;

	debug_active_init(ref);

	ref->flags = 0;
	ref->active = active;
	ref->retire = ptr_unpack_bits(retire, &bits, 2);
	if (bits & I915_ACTIVE_MAY_SLEEP)
		ref->flags |= I915_ACTIVE_RETIRE_SLEEPS;

	spin_lock_init(&ref->tree_lock);
	ref->tree = RB_ROOT;
	ref->cache = NULL;

	init_llist_head(&ref->preallocated_barriers);
	atomic_set(&ref->count, 0);
	__mutex_init(&ref->mutex, "i915_active", mkey);
	__i915_active_fence_init(&ref->excl, NULL, excl_retire);
	INIT_WORK(&ref->work, active_work);
#if IS_ENABLED(CONFIG_LOCKDEP)
	lockdep_init_map(&ref->work.lockdep_map, "i915_active.work", wkey, 0);
#endif
}

static bool ____active_del_barrier(struct i915_active *ref,
				   struct active_node *node,
				   struct intel_engine_cs *engine)

{
	struct llist_node *head = NULL, *tail = NULL;
	struct llist_node *pos, *next;

	GEM_BUG_ON(node->timeline != engine->kernel_context->timeline->fence_context);

	/*
	 * Rebuild the llist excluding our node. We may perform this
	 * outside of the kernel_context timeline mutex and so someone
	 * else may be manipulating the engine->barrier_tasks, in
	 * which case either we or they will be upset :)
	 *
	 * A second __active_del_barrier() will report failure to claim
	 * the active_node and the caller will just shrug and know not to
	 * claim ownership of its node.
	 *
	 * A concurrent i915_request_add_active_barriers() will miss adding
	 * any of the tasks, but we will try again on the next -- and since
	 * we are actively using the barrier, we know that there will be
	 * at least another opportunity when we idle.
	 */
	llist_for_each_safe(pos, next, llist_del_all(&engine->barrier_tasks)) {
		if (node == barrier_from_ll(pos)) {
			node = NULL;
			continue;
		}

		pos->next = head;
		head = pos;
		if (!tail)
			tail = pos;
	}
	if (head)
		llist_add_batch(head, tail, &engine->barrier_tasks);

	return !node;
}

static bool
__active_del_barrier(struct i915_active *ref, struct active_node *node)
{
	return ____active_del_barrier(ref, node, barrier_to_engine(node));
}

int i915_active_ref(struct i915_active *ref,
		    struct intel_timeline *tl,
		    struct dma_fence *fence)
{
	struct i915_active_fence *active;
	int err;

	lockdep_assert_held(&tl->mutex);

	/* Prevent reaping in case we malloc/wait while building the tree */
	err = i915_active_acquire(ref);
	if (err)
		return err;

	active = active_instance(ref, tl);
	if (!active) {
		err = -ENOMEM;
		goto out;
	}

	if (is_barrier(active)) { /* proto-node used by our idle barrier */
		/*
		 * This request is on the kernel_context timeline, and so
		 * we can use it to substitute for the pending idle-barrer
		 * request that we want to emit on the kernel_context.
		 */
		__active_del_barrier(ref, node_from_active(active));
		RCU_INIT_POINTER(active->fence, NULL);
		atomic_dec(&ref->count);
	}
	if (!__i915_active_fence_set(active, fence))
		atomic_inc(&ref->count);

out:
	i915_active_release(ref);
	return err;
}

struct dma_fence *
i915_active_set_exclusive(struct i915_active *ref, struct dma_fence *f)
{
	struct dma_fence *prev;

	/* We expect the caller to manage the exclusive timeline ordering */
	GEM_BUG_ON(i915_active_is_idle(ref));

	rcu_read_lock();
	prev = __i915_active_fence_set(&ref->excl, f);
	if (prev)
		prev = dma_fence_get_rcu(prev);
	else
		atomic_inc(&ref->count);
	rcu_read_unlock();

	return prev;
}

bool i915_active_acquire_if_busy(struct i915_active *ref)
{
	debug_active_assert(ref);
	return atomic_add_unless(&ref->count, 1, 0);
}

int i915_active_acquire(struct i915_active *ref)
{
	int err;

	if (i915_active_acquire_if_busy(ref))
		return 0;

	err = mutex_lock_interruptible(&ref->mutex);
	if (err)
		return err;

	if (likely(!i915_active_acquire_if_busy(ref))) {
		if (ref->active)
			err = ref->active(ref);
		if (!err) {
			spin_lock_irq(&ref->tree_lock); /* __active_retire() */
			debug_active_activate(ref);
			atomic_inc(&ref->count);
			spin_unlock_irq(&ref->tree_lock);
		}
	}

	mutex_unlock(&ref->mutex);

	return err;
}

void i915_active_release(struct i915_active *ref)
{
	debug_active_assert(ref);
	active_retire(ref);
}

static void enable_signaling(struct i915_active_fence *active)
{
	struct dma_fence *fence;

	if (unlikely(is_barrier(active)))
		return;

	fence = i915_active_fence_get(active);
	if (!fence)
		return;

	dma_fence_enable_sw_signaling(fence);
	dma_fence_put(fence);
}

static int flush_barrier(struct active_node *it)
{
	struct intel_engine_cs *engine;

	if (likely(!is_barrier(&it->base)))
		return 0;

	engine = __barrier_to_engine(it);
	smp_rmb(); /* serialise with add_active_barriers */
	if (!is_barrier(&it->base))
		return 0;

	return intel_engine_flush_barriers(engine);
}

static int flush_lazy_signals(struct i915_active *ref)
{
	struct active_node *it, *n;
	int err = 0;

	enable_signaling(&ref->excl);
	rbtree_postorder_for_each_entry_safe(it, n, &ref->tree, node) {
		err = flush_barrier(it); /* unconnected idle barrier? */
		if (err)
			break;

		enable_signaling(&it->base);
	}

	return err;
}

int __i915_active_wait(struct i915_active *ref, int state)
{
	int err;

	might_sleep();

	if (!i915_active_acquire_if_busy(ref))
		return 0;

	/* Any fence added after the wait begins will not be auto-signaled */
	err = flush_lazy_signals(ref);
	i915_active_release(ref);
	if (err)
		return err;

	if (!i915_active_is_idle(ref) &&
	    ___wait_var_event(ref, i915_active_is_idle(ref),
			      state, 0, 0, schedule()))
		return -EINTR;

	flush_work(&ref->work);
	return 0;
}

static int __await_active(struct i915_active_fence *active,
			  int (*fn)(void *arg, struct dma_fence *fence),
			  void *arg)
{
	struct dma_fence *fence;

	if (is_barrier(active)) /* XXX flush the barrier? */
		return 0;

	fence = i915_active_fence_get(active);
	if (fence) {
		int err;

		err = fn(arg, fence);
		dma_fence_put(fence);
		if (err < 0)
			return err;
	}

	return 0;
}

struct wait_barrier {
	struct wait_queue_entry base;
	struct i915_active *ref;
};

static int
barrier_wake(wait_queue_entry_t *wq, unsigned int mode, int flags, void *key)
{
	struct wait_barrier *wb = container_of(wq, typeof(*wb), base);

	if (i915_active_is_idle(wb->ref)) {
		list_del(&wq->entry);
		i915_sw_fence_complete(wq->private);
		kfree(wq);
	}

	return 0;
}

static int __await_barrier(struct i915_active *ref, struct i915_sw_fence *fence)
{
	struct wait_barrier *wb;

	wb = kmalloc(sizeof(*wb), GFP_KERNEL);
	if (unlikely(!wb))
		return -ENOMEM;

	GEM_BUG_ON(i915_active_is_idle(ref));
	if (!i915_sw_fence_await(fence)) {
		kfree(wb);
		return -EINVAL;
	}

	wb->base.flags = 0;
	wb->base.func = barrier_wake;
	wb->base.private = fence;
	wb->ref = ref;

	add_wait_queue(__var_waitqueue(ref), &wb->base);
	return 0;
}

static int await_active(struct i915_active *ref,
			unsigned int flags,
			int (*fn)(void *arg, struct dma_fence *fence),
			void *arg, struct i915_sw_fence *barrier)
{
	int err = 0;

	if (!i915_active_acquire_if_busy(ref))
		return 0;

	if (flags & I915_ACTIVE_AWAIT_EXCL &&
	    rcu_access_pointer(ref->excl.fence)) {
		err = __await_active(&ref->excl, fn, arg);
		if (err)
			goto out;
	}

	if (flags & I915_ACTIVE_AWAIT_ACTIVE) {
		struct active_node *it, *n;

		rbtree_postorder_for_each_entry_safe(it, n, &ref->tree, node) {
			err = __await_active(&it->base, fn, arg);
			if (err)
				goto out;
		}
	}

	if (flags & I915_ACTIVE_AWAIT_BARRIER) {
		err = flush_lazy_signals(ref);
		if (err)
			goto out;

		err = __await_barrier(ref, barrier);
		if (err)
			goto out;
	}

out:
	i915_active_release(ref);
	return err;
}

static int rq_await_fence(void *arg, struct dma_fence *fence)
{
	return i915_request_await_dma_fence(arg, fence);
}

int i915_request_await_active(struct i915_request *rq,
			      struct i915_active *ref,
			      unsigned int flags)
{
	return await_active(ref, flags, rq_await_fence, rq, &rq->submit);
}

static int sw_await_fence(void *arg, struct dma_fence *fence)
{
	return i915_sw_fence_await_dma_fence(arg, fence, 0,
					     GFP_NOWAIT | __GFP_NOWARN);
}

int i915_sw_fence_await_active(struct i915_sw_fence *fence,
			       struct i915_active *ref,
			       unsigned int flags)
{
	return await_active(ref, flags, sw_await_fence, fence, fence);
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM)
void i915_active_fini(struct i915_active *ref)
{
	debug_active_fini(ref);
	GEM_BUG_ON(atomic_read(&ref->count));
	GEM_BUG_ON(work_pending(&ref->work));
	GEM_BUG_ON(!RB_EMPTY_ROOT(&ref->tree));
	mutex_destroy(&ref->mutex);
}
#endif

static inline bool is_idle_barrier(struct active_node *node, u64 idx)
{
	return node->timeline == idx && !i915_active_fence_isset(&node->base);
}

static struct active_node *reuse_idle_barrier(struct i915_active *ref, u64 idx)
{
	struct rb_node *prev, *p;

	if (RB_EMPTY_ROOT(&ref->tree))
		return NULL;

	spin_lock_irq(&ref->tree_lock);
	GEM_BUG_ON(i915_active_is_idle(ref));

	/*
	 * Try to reuse any existing barrier nodes already allocated for this
	 * i915_active, due to overlapping active phases there is likely a
	 * node kept alive (as we reuse before parking). We prefer to reuse
	 * completely idle barriers (less hassle in manipulating the llists),
	 * but otherwise any will do.
	 */
	if (ref->cache && is_idle_barrier(ref->cache, idx)) {
		p = &ref->cache->node;
		goto match;
	}

	prev = NULL;
	p = ref->tree.rb_node;
	while (p) {
		struct active_node *node =
			rb_entry(p, struct active_node, node);

		if (is_idle_barrier(node, idx))
			goto match;

		prev = p;
		if (node->timeline < idx)
			p = p->rb_right;
		else
			p = p->rb_left;
	}

	/*
	 * No quick match, but we did find the leftmost rb_node for the
	 * kernel_context. Walk the rb_tree in-order to see if there were
	 * any idle-barriers on this timeline that we missed, or just use
	 * the first pending barrier.
	 */
	for (p = prev; p; p = rb_next(p)) {
		struct active_node *node =
			rb_entry(p, struct active_node, node);
		struct intel_engine_cs *engine;

		if (node->timeline > idx)
			break;

		if (node->timeline < idx)
			continue;

		if (is_idle_barrier(node, idx))
			goto match;

		/*
		 * The list of pending barriers is protected by the
		 * kernel_context timeline, which notably we do not hold
		 * here. i915_request_add_active_barriers() may consume
		 * the barrier before we claim it, so we have to check
		 * for success.
		 */
		engine = __barrier_to_engine(node);
		smp_rmb(); /* serialise with add_active_barriers */
		if (is_barrier(&node->base) &&
		    ____active_del_barrier(ref, node, engine))
			goto match;
	}

	spin_unlock_irq(&ref->tree_lock);

	return NULL;

match:
	rb_erase(p, &ref->tree); /* Hide from waits and sibling allocations */
	if (p == &ref->cache->node)
		ref->cache = NULL;
	spin_unlock_irq(&ref->tree_lock);

	return rb_entry(p, struct active_node, node);
}

int i915_active_acquire_preallocate_barrier(struct i915_active *ref,
					    struct intel_engine_cs *engine)
{
	intel_engine_mask_t tmp, mask = engine->mask;
	struct llist_node *first = NULL, *last = NULL;
	struct intel_gt *gt = engine->gt;
	int err;

	GEM_BUG_ON(i915_active_is_idle(ref));

	/* Wait until the previous preallocation is completed */
	while (!llist_empty(&ref->preallocated_barriers))
		cond_resched();

	/*
	 * Preallocate a node for each physical engine supporting the target
	 * engine (remember virtual engines have more than one sibling).
	 * We can then use the preallocated nodes in
	 * i915_active_acquire_barrier()
	 */
	GEM_BUG_ON(!mask);
	for_each_engine_masked(engine, gt, mask, tmp) {
		u64 idx = engine->kernel_context->timeline->fence_context;
		struct llist_node *prev = first;
		struct active_node *node;

		node = reuse_idle_barrier(ref, idx);
		if (!node) {
			node = kmem_cache_alloc(global.slab_cache, GFP_KERNEL);
			if (!node) {
				err = ENOMEM;
				goto unwind;
			}

			RCU_INIT_POINTER(node->base.fence, NULL);
			node->base.cb.func = node_retire;
			node->timeline = idx;
			node->ref = ref;
		}

		if (!i915_active_fence_isset(&node->base)) {
			/*
			 * Mark this as being *our* unconnected proto-node.
			 *
			 * Since this node is not in any list, and we have
			 * decoupled it from the rbtree, we can reuse the
			 * request to indicate this is an idle-barrier node
			 * and then we can use the rb_node and list pointers
			 * for our tracking of the pending barrier.
			 */
			RCU_INIT_POINTER(node->base.fence, ERR_PTR(-EAGAIN));
			node->base.cb.node.prev = (void *)engine;
			atomic_inc(&ref->count);
		}
		GEM_BUG_ON(rcu_access_pointer(node->base.fence) != ERR_PTR(-EAGAIN));

		GEM_BUG_ON(barrier_to_engine(node) != engine);
		first = barrier_to_ll(node);
		first->next = prev;
		if (!last)
			last = first;
		intel_engine_pm_get(engine);
	}

	GEM_BUG_ON(!llist_empty(&ref->preallocated_barriers));
	llist_add_batch(first, last, &ref->preallocated_barriers);

	return 0;

unwind:
	while (first) {
		struct active_node *node = barrier_from_ll(first);

		first = first->next;

		atomic_dec(&ref->count);
		intel_engine_pm_put(barrier_to_engine(node));

		kmem_cache_free(global.slab_cache, node);
	}
	return err;
}

void i915_active_acquire_barrier(struct i915_active *ref)
{
	struct llist_node *pos, *next;
	unsigned long flags;

	GEM_BUG_ON(i915_active_is_idle(ref));

	/*
	 * Transfer the list of preallocated barriers into the
	 * i915_active rbtree, but only as proto-nodes. They will be
	 * populated by i915_request_add_active_barriers() to point to the
	 * request that will eventually release them.
	 */
	llist_for_each_safe(pos, next, take_preallocated_barriers(ref)) {
		struct active_node *node = barrier_from_ll(pos);
		struct intel_engine_cs *engine = barrier_to_engine(node);
		struct rb_node **p, *parent;

		spin_lock_irqsave_nested(&ref->tree_lock, flags,
					 SINGLE_DEPTH_NESTING);
		parent = NULL;
		p = &ref->tree.rb_node;
		while (*p) {
			struct active_node *it;

			parent = *p;

			it = rb_entry(parent, struct active_node, node);
			if (it->timeline < node->timeline)
				p = &parent->rb_right;
			else
				p = &parent->rb_left;
		}
		rb_link_node(&node->node, parent, p);
		rb_insert_color(&node->node, &ref->tree);
		spin_unlock_irqrestore(&ref->tree_lock, flags);

		GEM_BUG_ON(!intel_engine_pm_is_awake(engine));
		llist_add(barrier_to_ll(node), &engine->barrier_tasks);
		intel_engine_pm_put_delay(engine, 1);
	}
}

static struct dma_fence **ll_to_fence_slot(struct llist_node *node)
{
	return __active_fence_slot(&barrier_from_ll(node)->base);
}

void i915_request_add_active_barriers(struct i915_request *rq)
{
	struct intel_engine_cs *engine = rq->engine;
	struct llist_node *node, *next;
	unsigned long flags;

	GEM_BUG_ON(!intel_context_is_barrier(rq->context));
	GEM_BUG_ON(intel_engine_is_virtual(engine));
	GEM_BUG_ON(i915_request_timeline(rq) != engine->kernel_context->timeline);

	node = llist_del_all(&engine->barrier_tasks);
	if (!node)
		return;
	/*
	 * Attach the list of proto-fences to the in-flight request such
	 * that the parent i915_active will be released when this request
	 * is retired.
	 */
	spin_lock_irqsave(&rq->lock, flags);
	llist_for_each_safe(node, next, node) {
		/* serialise with reuse_idle_barrier */
		smp_store_mb(*ll_to_fence_slot(node), &rq->fence);
		list_add_tail((struct list_head *)node, &rq->fence.cb_list);
	}
	spin_unlock_irqrestore(&rq->lock, flags);
}

/*
 * __i915_active_fence_set: Update the last active fence along its timeline
 * @active: the active tracker
 * @fence: the new fence (under construction)
 *
 * Records the new @fence as the last active fence along its timeline in
 * this active tracker, moving the tracking callbacks from the previous
 * fence onto this one. Returns the previous fence (if not already completed),
 * which the caller must ensure is executed before the new fence. To ensure
 * that the order of fences within the timeline of the i915_active_fence is
 * understood, it should be locked by the caller.
 */
struct dma_fence *
__i915_active_fence_set(struct i915_active_fence *active,
			struct dma_fence *fence)
{
	struct dma_fence *prev;
	unsigned long flags;

	if (fence == rcu_access_pointer(active->fence))
		return fence;

	GEM_BUG_ON(test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags));

	/*
	 * Consider that we have two threads arriving (A and B), with
	 * C already resident as the active->fence.
	 *
	 * A does the xchg first, and so it sees C or NULL depending
	 * on the timing of the interrupt handler. If it is NULL, the
	 * previous fence must have been signaled and we know that
	 * we are first on the timeline. If it is still present,
	 * we acquire the lock on that fence and serialise with the interrupt
	 * handler, in the process removing it from any future interrupt
	 * callback. A will then wait on C before executing (if present).
	 *
	 * As B is second, it sees A as the previous fence and so waits for
	 * it to complete its transition and takes over the occupancy for
	 * itself -- remembering that it needs to wait on A before executing.
	 *
	 * Note the strong ordering of the timeline also provides consistent
	 * nesting rules for the fence->lock; the inner lock is always the
	 * older lock.
	 */
	spin_lock_irqsave(fence->lock, flags);
	prev = xchg(__active_fence_slot(active), fence);
	if (prev) {
		GEM_BUG_ON(prev == fence);
		spin_lock_nested(prev->lock, SINGLE_DEPTH_NESTING);
		__list_del_entry(&active->cb.node);
		spin_unlock(prev->lock); /* serialise with prev->cb_list */
	}
	list_add_tail(&active->cb.node, &fence->cb_list);
	spin_unlock_irqrestore(fence->lock, flags);

	return prev;
}

int i915_active_fence_set(struct i915_active_fence *active,
			  struct i915_request *rq)
{
	struct dma_fence *fence;
	int err = 0;

	/* Must maintain timeline ordering wrt previous active requests */
	rcu_read_lock();
	fence = __i915_active_fence_set(active, &rq->fence);
	if (fence) /* but the previous fence may not belong to that timeline! */
		fence = dma_fence_get_rcu(fence);
	rcu_read_unlock();
	if (fence) {
		err = i915_request_await_dma_fence(rq, fence);
		dma_fence_put(fence);
	}

	return err;
}

void i915_active_noop(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	active_fence_cb(fence, cb);
}

struct auto_active {
	struct i915_active base;
	struct kref ref;
};

struct i915_active *i915_active_get(struct i915_active *ref)
{
	struct auto_active *aa = container_of(ref, typeof(*aa), base);

	kref_get(&aa->ref);
	return &aa->base;
}

static void auto_release(struct kref *ref)
{
	struct auto_active *aa = container_of(ref, typeof(*aa), ref);

	i915_active_fini(&aa->base);
	kfree(aa);
}

void i915_active_put(struct i915_active *ref)
{
	struct auto_active *aa = container_of(ref, typeof(*aa), base);

	kref_put(&aa->ref, auto_release);
}

static int auto_active(struct i915_active *ref)
{
	i915_active_get(ref);
	return 0;
}

static void auto_retire(struct i915_active *ref)
{
	i915_active_put(ref);
}

struct i915_active *i915_active_create(void)
{
	struct auto_active *aa;

	aa = kmalloc(sizeof(*aa), GFP_KERNEL);
	if (!aa)
		return NULL;

	kref_init(&aa->ref);
	i915_active_init(&aa->base, auto_active, auto_retire);

	return &aa->base;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/i915_active.c"
#endif

static void i915_global_active_shrink(void)
{
	kmem_cache_shrink(global.slab_cache);
}

static void i915_global_active_exit(void)
{
	kmem_cache_destroy(global.slab_cache);
}

static struct i915_global_active global = { {
	.shrink = i915_global_active_shrink,
	.exit = i915_global_active_exit,
} };

int __init i915_global_active_init(void)
{
	global.slab_cache = KMEM_CACHE(active_node, SLAB_HWCACHE_ALIGN);
	if (!global.slab_cache)
		return -ENOMEM;

	i915_global_register(&global.base);
	return 0;
}
