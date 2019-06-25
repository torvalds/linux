/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#include <linux/mutex.h>

#include "i915_drv.h"
#include "i915_globals.h"
#include "i915_request.h"
#include "i915_scheduler.h"

static struct i915_global_scheduler {
	struct i915_global base;
	struct kmem_cache *slab_dependencies;
	struct kmem_cache *slab_priorities;
} global;

static DEFINE_SPINLOCK(schedule_lock);

static const struct i915_request *
node_to_request(const struct i915_sched_node *node)
{
	return container_of(node, const struct i915_request, sched);
}

static inline bool node_started(const struct i915_sched_node *node)
{
	return i915_request_started(node_to_request(node));
}

static inline bool node_signaled(const struct i915_sched_node *node)
{
	return i915_request_completed(node_to_request(node));
}

static inline struct i915_priolist *to_priolist(struct rb_node *rb)
{
	return rb_entry(rb, struct i915_priolist, node);
}

static void assert_priolists(struct intel_engine_execlists * const execlists)
{
	struct rb_node *rb;
	long last_prio, i;

	if (!IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM))
		return;

	GEM_BUG_ON(rb_first_cached(&execlists->queue) !=
		   rb_first(&execlists->queue.rb_root));

	last_prio = (INT_MAX >> I915_USER_PRIORITY_SHIFT) + 1;
	for (rb = rb_first_cached(&execlists->queue); rb; rb = rb_next(rb)) {
		const struct i915_priolist *p = to_priolist(rb);

		GEM_BUG_ON(p->priority >= last_prio);
		last_prio = p->priority;

		GEM_BUG_ON(!p->used);
		for (i = 0; i < ARRAY_SIZE(p->requests); i++) {
			if (list_empty(&p->requests[i]))
				continue;

			GEM_BUG_ON(!(p->used & BIT(i)));
		}
	}
}

struct list_head *
i915_sched_lookup_priolist(struct intel_engine_cs *engine, int prio)
{
	struct intel_engine_execlists * const execlists = &engine->execlists;
	struct i915_priolist *p;
	struct rb_node **parent, *rb;
	bool first = true;
	int idx, i;

	lockdep_assert_held(&engine->active.lock);
	assert_priolists(execlists);

	/* buckets sorted from highest [in slot 0] to lowest priority */
	idx = I915_PRIORITY_COUNT - (prio & I915_PRIORITY_MASK) - 1;
	prio >>= I915_USER_PRIORITY_SHIFT;
	if (unlikely(execlists->no_priolist))
		prio = I915_PRIORITY_NORMAL;

find_priolist:
	/* most positive priority is scheduled first, equal priorities fifo */
	rb = NULL;
	parent = &execlists->queue.rb_root.rb_node;
	while (*parent) {
		rb = *parent;
		p = to_priolist(rb);
		if (prio > p->priority) {
			parent = &rb->rb_left;
		} else if (prio < p->priority) {
			parent = &rb->rb_right;
			first = false;
		} else {
			goto out;
		}
	}

	if (prio == I915_PRIORITY_NORMAL) {
		p = &execlists->default_priolist;
	} else {
		p = kmem_cache_alloc(global.slab_priorities, GFP_ATOMIC);
		/* Convert an allocation failure to a priority bump */
		if (unlikely(!p)) {
			prio = I915_PRIORITY_NORMAL; /* recurses just once */

			/* To maintain ordering with all rendering, after an
			 * allocation failure we have to disable all scheduling.
			 * Requests will then be executed in fifo, and schedule
			 * will ensure that dependencies are emitted in fifo.
			 * There will be still some reordering with existing
			 * requests, so if userspace lied about their
			 * dependencies that reordering may be visible.
			 */
			execlists->no_priolist = true;
			goto find_priolist;
		}
	}

	p->priority = prio;
	for (i = 0; i < ARRAY_SIZE(p->requests); i++)
		INIT_LIST_HEAD(&p->requests[i]);
	rb_link_node(&p->node, rb, parent);
	rb_insert_color_cached(&p->node, &execlists->queue, first);
	p->used = 0;

out:
	p->used |= BIT(idx);
	return &p->requests[idx];
}

void __i915_priolist_free(struct i915_priolist *p)
{
	kmem_cache_free(global.slab_priorities, p);
}

struct sched_cache {
	struct list_head *priolist;
};

static struct intel_engine_cs *
sched_lock_engine(const struct i915_sched_node *node,
		  struct intel_engine_cs *locked,
		  struct sched_cache *cache)
{
	const struct i915_request *rq = node_to_request(node);
	struct intel_engine_cs *engine;

	GEM_BUG_ON(!locked);

	/*
	 * Virtual engines complicate acquiring the engine timeline lock,
	 * as their rq->engine pointer is not stable until under that
	 * engine lock. The simple ploy we use is to take the lock then
	 * check that the rq still belongs to the newly locked engine.
	 */
	while (locked != (engine = READ_ONCE(rq->engine))) {
		spin_unlock(&locked->active.lock);
		memset(cache, 0, sizeof(*cache));
		spin_lock(&engine->active.lock);
		locked = engine;
	}

	GEM_BUG_ON(locked != engine);
	return locked;
}

static inline int rq_prio(const struct i915_request *rq)
{
	return rq->sched.attr.priority | __NO_PREEMPTION;
}

static void kick_submission(struct intel_engine_cs *engine, int prio)
{
	const struct i915_request *inflight =
		port_request(engine->execlists.port);

	/*
	 * If we are already the currently executing context, don't
	 * bother evaluating if we should preempt ourselves, or if
	 * we expect nothing to change as a result of running the
	 * tasklet, i.e. we have not change the priority queue
	 * sufficiently to oust the running context.
	 */
	if (!inflight || !i915_scheduler_need_preempt(prio, rq_prio(inflight)))
		return;

	tasklet_hi_schedule(&engine->execlists.tasklet);
}

static void __i915_schedule(struct i915_sched_node *node,
			    const struct i915_sched_attr *attr)
{
	struct intel_engine_cs *engine;
	struct i915_dependency *dep, *p;
	struct i915_dependency stack;
	const int prio = attr->priority;
	struct sched_cache cache;
	LIST_HEAD(dfs);

	/* Needed in order to use the temporary link inside i915_dependency */
	lockdep_assert_held(&schedule_lock);
	GEM_BUG_ON(prio == I915_PRIORITY_INVALID);

	if (prio <= READ_ONCE(node->attr.priority))
		return;

	if (node_signaled(node))
		return;

	stack.signaler = node;
	list_add(&stack.dfs_link, &dfs);

	/*
	 * Recursively bump all dependent priorities to match the new request.
	 *
	 * A naive approach would be to use recursion:
	 * static void update_priorities(struct i915_sched_node *node, prio) {
	 *	list_for_each_entry(dep, &node->signalers_list, signal_link)
	 *		update_priorities(dep->signal, prio)
	 *	queue_request(node);
	 * }
	 * but that may have unlimited recursion depth and so runs a very
	 * real risk of overunning the kernel stack. Instead, we build
	 * a flat list of all dependencies starting with the current request.
	 * As we walk the list of dependencies, we add all of its dependencies
	 * to the end of the list (this may include an already visited
	 * request) and continue to walk onwards onto the new dependencies. The
	 * end result is a topological list of requests in reverse order, the
	 * last element in the list is the request we must execute first.
	 */
	list_for_each_entry(dep, &dfs, dfs_link) {
		struct i915_sched_node *node = dep->signaler;

		/* If we are already flying, we know we have no signalers */
		if (node_started(node))
			continue;

		/*
		 * Within an engine, there can be no cycle, but we may
		 * refer to the same dependency chain multiple times
		 * (redundant dependencies are not eliminated) and across
		 * engines.
		 */
		list_for_each_entry(p, &node->signalers_list, signal_link) {
			GEM_BUG_ON(p == dep); /* no cycles! */

			if (node_signaled(p->signaler))
				continue;

			if (prio > READ_ONCE(p->signaler->attr.priority))
				list_move_tail(&p->dfs_link, &dfs);
		}
	}

	/*
	 * If we didn't need to bump any existing priorities, and we haven't
	 * yet submitted this request (i.e. there is no potential race with
	 * execlists_submit_request()), we can set our own priority and skip
	 * acquiring the engine locks.
	 */
	if (node->attr.priority == I915_PRIORITY_INVALID) {
		GEM_BUG_ON(!list_empty(&node->link));
		node->attr = *attr;

		if (stack.dfs_link.next == stack.dfs_link.prev)
			return;

		__list_del_entry(&stack.dfs_link);
	}

	memset(&cache, 0, sizeof(cache));
	engine = node_to_request(node)->engine;
	spin_lock(&engine->active.lock);

	/* Fifo and depth-first replacement ensure our deps execute before us */
	engine = sched_lock_engine(node, engine, &cache);
	list_for_each_entry_safe_reverse(dep, p, &dfs, dfs_link) {
		INIT_LIST_HEAD(&dep->dfs_link);

		node = dep->signaler;
		engine = sched_lock_engine(node, engine, &cache);
		lockdep_assert_held(&engine->active.lock);

		/* Recheck after acquiring the engine->timeline.lock */
		if (prio <= node->attr.priority || node_signaled(node))
			continue;

		GEM_BUG_ON(node_to_request(node)->engine != engine);

		node->attr.priority = prio;

		if (list_empty(&node->link)) {
			/*
			 * If the request is not in the priolist queue because
			 * it is not yet runnable, then it doesn't contribute
			 * to our preemption decisions. On the other hand,
			 * if the request is on the HW, it too is not in the
			 * queue; but in that case we may still need to reorder
			 * the inflight requests.
			 */
			continue;
		}

		if (!intel_engine_is_virtual(engine) &&
		    !i915_request_is_active(node_to_request(node))) {
			if (!cache.priolist)
				cache.priolist =
					i915_sched_lookup_priolist(engine,
								   prio);
			list_move_tail(&node->link, cache.priolist);
		}

		if (prio <= engine->execlists.queue_priority_hint)
			continue;

		engine->execlists.queue_priority_hint = prio;

		/* Defer (tasklet) submission until after all of our updates. */
		kick_submission(engine, prio);
	}

	spin_unlock(&engine->active.lock);
}

void i915_schedule(struct i915_request *rq, const struct i915_sched_attr *attr)
{
	spin_lock_irq(&schedule_lock);
	__i915_schedule(&rq->sched, attr);
	spin_unlock_irq(&schedule_lock);
}

static void __bump_priority(struct i915_sched_node *node, unsigned int bump)
{
	struct i915_sched_attr attr = node->attr;

	attr.priority |= bump;
	__i915_schedule(node, &attr);
}

void i915_schedule_bump_priority(struct i915_request *rq, unsigned int bump)
{
	unsigned long flags;

	GEM_BUG_ON(bump & ~I915_PRIORITY_MASK);

	if (READ_ONCE(rq->sched.attr.priority) == I915_PRIORITY_INVALID)
		return;

	spin_lock_irqsave(&schedule_lock, flags);
	__bump_priority(&rq->sched, bump);
	spin_unlock_irqrestore(&schedule_lock, flags);
}

void i915_sched_node_init(struct i915_sched_node *node)
{
	INIT_LIST_HEAD(&node->signalers_list);
	INIT_LIST_HEAD(&node->waiters_list);
	INIT_LIST_HEAD(&node->link);
	node->attr.priority = I915_PRIORITY_INVALID;
	node->semaphores = 0;
	node->flags = 0;
}

static struct i915_dependency *
i915_dependency_alloc(void)
{
	return kmem_cache_alloc(global.slab_dependencies, GFP_KERNEL);
}

static void
i915_dependency_free(struct i915_dependency *dep)
{
	kmem_cache_free(global.slab_dependencies, dep);
}

bool __i915_sched_node_add_dependency(struct i915_sched_node *node,
				      struct i915_sched_node *signal,
				      struct i915_dependency *dep,
				      unsigned long flags)
{
	bool ret = false;

	spin_lock_irq(&schedule_lock);

	if (!node_signaled(signal)) {
		INIT_LIST_HEAD(&dep->dfs_link);
		list_add(&dep->wait_link, &signal->waiters_list);
		list_add(&dep->signal_link, &node->signalers_list);
		dep->signaler = signal;
		dep->flags = flags;

		/* Keep track of whether anyone on this chain has a semaphore */
		if (signal->flags & I915_SCHED_HAS_SEMAPHORE_CHAIN &&
		    !node_started(signal))
			node->flags |= I915_SCHED_HAS_SEMAPHORE_CHAIN;

		/*
		 * As we do not allow WAIT to preempt inflight requests,
		 * once we have executed a request, along with triggering
		 * any execution callbacks, we must preserve its ordering
		 * within the non-preemptible FIFO.
		 */
		BUILD_BUG_ON(__NO_PREEMPTION & ~I915_PRIORITY_MASK);
		if (flags & I915_DEPENDENCY_EXTERNAL)
			__bump_priority(signal, __NO_PREEMPTION);

		ret = true;
	}

	spin_unlock_irq(&schedule_lock);

	return ret;
}

int i915_sched_node_add_dependency(struct i915_sched_node *node,
				   struct i915_sched_node *signal)
{
	struct i915_dependency *dep;

	dep = i915_dependency_alloc();
	if (!dep)
		return -ENOMEM;

	if (!__i915_sched_node_add_dependency(node, signal, dep,
					      I915_DEPENDENCY_EXTERNAL |
					      I915_DEPENDENCY_ALLOC))
		i915_dependency_free(dep);

	return 0;
}

void i915_sched_node_fini(struct i915_sched_node *node)
{
	struct i915_dependency *dep, *tmp;

	spin_lock_irq(&schedule_lock);

	/*
	 * Everyone we depended upon (the fences we wait to be signaled)
	 * should retire before us and remove themselves from our list.
	 * However, retirement is run independently on each timeline and
	 * so we may be called out-of-order.
	 */
	list_for_each_entry_safe(dep, tmp, &node->signalers_list, signal_link) {
		GEM_BUG_ON(!node_signaled(dep->signaler));
		GEM_BUG_ON(!list_empty(&dep->dfs_link));

		list_del(&dep->wait_link);
		if (dep->flags & I915_DEPENDENCY_ALLOC)
			i915_dependency_free(dep);
	}

	/* Remove ourselves from everyone who depends upon us */
	list_for_each_entry_safe(dep, tmp, &node->waiters_list, wait_link) {
		GEM_BUG_ON(dep->signaler != node);
		GEM_BUG_ON(!list_empty(&dep->dfs_link));

		list_del(&dep->signal_link);
		if (dep->flags & I915_DEPENDENCY_ALLOC)
			i915_dependency_free(dep);
	}

	spin_unlock_irq(&schedule_lock);
}

static void i915_global_scheduler_shrink(void)
{
	kmem_cache_shrink(global.slab_dependencies);
	kmem_cache_shrink(global.slab_priorities);
}

static void i915_global_scheduler_exit(void)
{
	kmem_cache_destroy(global.slab_dependencies);
	kmem_cache_destroy(global.slab_priorities);
}

static struct i915_global_scheduler global = { {
	.shrink = i915_global_scheduler_shrink,
	.exit = i915_global_scheduler_exit,
} };

int __init i915_global_scheduler_init(void)
{
	global.slab_dependencies = KMEM_CACHE(i915_dependency,
					      SLAB_HWCACHE_ALIGN);
	if (!global.slab_dependencies)
		return -ENOMEM;

	global.slab_priorities = KMEM_CACHE(i915_priolist,
					    SLAB_HWCACHE_ALIGN);
	if (!global.slab_priorities)
		goto err_priorities;

	i915_global_register(&global.base);
	return 0;

err_priorities:
	kmem_cache_destroy(global.slab_priorities);
	return -ENOMEM;
}
