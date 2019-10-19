/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include <linux/debugobjects.h>

#include "gt/intel_engine_pm.h"

#include "i915_drv.h"
#include "i915_active.h"
#include "i915_globals.h"

#define BKL(ref) (&(ref)->i915->drm.struct_mutex)

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
	struct i915_active_request base;
	struct i915_active *ref;
	struct rb_node node;
	u64 timeline;
};

static inline struct active_node *
node_from_active(struct i915_active_request *active)
{
	return container_of(active, struct active_node, base);
}

#define take_preallocated_barriers(x) llist_del_all(&(x)->preallocated_barriers)

static inline bool is_barrier(const struct i915_active_request *active)
{
	return IS_ERR(rcu_access_pointer(active->request));
}

static inline struct llist_node *barrier_to_ll(struct active_node *node)
{
	GEM_BUG_ON(!is_barrier(&node->base));
	return (struct llist_node *)&node->base.link;
}

static inline struct intel_engine_cs *
__barrier_to_engine(struct active_node *node)
{
	return (struct intel_engine_cs *)READ_ONCE(node->base.link.prev);
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
			    struct active_node, base.link);
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM) && IS_ENABLED(CONFIG_DEBUG_OBJECTS)

static void *active_debug_hint(void *addr)
{
	struct i915_active *ref = addr;

	return (void *)ref->active ?: (void *)ref->retire ?: (void *)ref;
}

static struct debug_obj_descr active_debug_desc = {
	.name = "i915_active",
	.debug_hint = active_debug_hint,
};

static void debug_active_init(struct i915_active *ref)
{
	debug_object_init(ref, &active_debug_desc);
}

static void debug_active_activate(struct i915_active *ref)
{
	debug_object_activate(ref, &active_debug_desc);
}

static void debug_active_deactivate(struct i915_active *ref)
{
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
	bool retire = false;

	lockdep_assert_held(&ref->mutex);

	/* return the unused nodes to our slabcache -- flushing the allocator */
	if (atomic_dec_and_test(&ref->count)) {
		debug_active_deactivate(ref);
		root = ref->tree;
		ref->tree = RB_ROOT;
		ref->cache = NULL;
		retire = true;
	}

	mutex_unlock(&ref->mutex);
	if (!retire)
		return;

	rbtree_postorder_for_each_entry_safe(it, n, &root, node) {
		GEM_BUG_ON(i915_active_request_isset(&it->base));
		kmem_cache_free(global.slab_cache, it);
	}

	/* After the final retire, the entire struct may be freed */
	if (ref->retire)
		ref->retire(ref);
}

static void
active_retire(struct i915_active *ref)
{
	GEM_BUG_ON(!atomic_read(&ref->count));
	if (atomic_add_unless(&ref->count, -1, 1))
		return;

	/* One active may be flushed from inside the acquire of another */
	mutex_lock_nested(&ref->mutex, SINGLE_DEPTH_NESTING);
	__active_retire(ref);
}

static void
node_retire(struct i915_active_request *base, struct i915_request *rq)
{
	active_retire(node_from_active(base)->ref);
}

static struct i915_active_request *
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

	mutex_lock(&ref->mutex);
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
	i915_active_request_init(&node->base, &tl->mutex, NULL, node_retire);
	node->ref = ref;
	node->timeline = idx;

	rb_link_node(&node->node, parent, p);
	rb_insert_color(&node->node, &ref->tree);

out:
	ref->cache = node;
	mutex_unlock(&ref->mutex);

	BUILD_BUG_ON(offsetof(typeof(*node), base));
	return &node->base;
}

void __i915_active_init(struct drm_i915_private *i915,
			struct i915_active *ref,
			int (*active)(struct i915_active *ref),
			void (*retire)(struct i915_active *ref),
			struct lock_class_key *key)
{
	debug_active_init(ref);

	ref->i915 = i915;
	ref->flags = 0;
	ref->active = active;
	ref->retire = retire;
	ref->tree = RB_ROOT;
	ref->cache = NULL;
	init_llist_head(&ref->preallocated_barriers);
	atomic_set(&ref->count, 0);
	__mutex_init(&ref->mutex, "i915_active", key);
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
		    struct i915_request *rq)
{
	struct i915_active_request *active;
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
		RCU_INIT_POINTER(active->request, NULL);
		INIT_LIST_HEAD(&active->link);
	} else {
		if (!i915_active_request_isset(active))
			atomic_inc(&ref->count);
	}
	GEM_BUG_ON(!atomic_read(&ref->count));
	__i915_active_request_set(active, rq);

out:
	i915_active_release(ref);
	return err;
}

int i915_active_acquire(struct i915_active *ref)
{
	int err;

	debug_active_assert(ref);
	if (atomic_add_unless(&ref->count, 1, 0))
		return 0;

	err = mutex_lock_interruptible(&ref->mutex);
	if (err)
		return err;

	if (!atomic_read(&ref->count) && ref->active)
		err = ref->active(ref);
	if (!err) {
		debug_active_activate(ref);
		atomic_inc(&ref->count);
	}

	mutex_unlock(&ref->mutex);

	return err;
}

void i915_active_release(struct i915_active *ref)
{
	debug_active_assert(ref);
	active_retire(ref);
}

static void __active_ungrab(struct i915_active *ref)
{
	clear_and_wake_up_bit(I915_ACTIVE_GRAB_BIT, &ref->flags);
}

bool i915_active_trygrab(struct i915_active *ref)
{
	debug_active_assert(ref);

	if (test_and_set_bit(I915_ACTIVE_GRAB_BIT, &ref->flags))
		return false;

	if (!atomic_add_unless(&ref->count, 1, 0)) {
		__active_ungrab(ref);
		return false;
	}

	return true;
}

void i915_active_ungrab(struct i915_active *ref)
{
	GEM_BUG_ON(!test_bit(I915_ACTIVE_GRAB_BIT, &ref->flags));

	active_retire(ref);
	__active_ungrab(ref);
}

int i915_active_wait(struct i915_active *ref)
{
	struct active_node *it, *n;
	int err;

	might_sleep();
	might_lock(&ref->mutex);

	if (i915_active_is_idle(ref))
		return 0;

	err = mutex_lock_interruptible(&ref->mutex);
	if (err)
		return err;

	if (!atomic_add_unless(&ref->count, 1, 0)) {
		mutex_unlock(&ref->mutex);
		return 0;
	}

	rbtree_postorder_for_each_entry_safe(it, n, &ref->tree, node) {
		if (is_barrier(&it->base)) { /* unconnected idle-barrier */
			err = -EBUSY;
			break;
		}

		err = i915_active_request_retire(&it->base, BKL(ref));
		if (err)
			break;
	}

	__active_retire(ref);
	if (err)
		return err;

	if (wait_on_bit(&ref->flags, I915_ACTIVE_GRAB_BIT, TASK_KILLABLE))
		return -EINTR;

	if (!i915_active_is_idle(ref))
		return -EBUSY;

	return 0;
}

int i915_request_await_active_request(struct i915_request *rq,
				      struct i915_active_request *active)
{
	struct i915_request *barrier =
		i915_active_request_raw(active, &rq->i915->drm.struct_mutex);

	return barrier ? i915_request_await_dma_fence(rq, &barrier->fence) : 0;
}

int i915_request_await_active(struct i915_request *rq, struct i915_active *ref)
{
	struct active_node *it, *n;
	int err;

	if (RB_EMPTY_ROOT(&ref->tree))
		return 0;

	/* await allocates and so we need to avoid hitting the shrinker */
	err = i915_active_acquire(ref);
	if (err)
		return err;

	mutex_lock(&ref->mutex);
	rbtree_postorder_for_each_entry_safe(it, n, &ref->tree, node) {
		err = i915_request_await_active_request(rq, &it->base);
		if (err)
			break;
	}
	mutex_unlock(&ref->mutex);

	i915_active_release(ref);
	return err;
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM)
void i915_active_fini(struct i915_active *ref)
{
	debug_active_fini(ref);
	GEM_BUG_ON(!RB_EMPTY_ROOT(&ref->tree));
	GEM_BUG_ON(atomic_read(&ref->count));
	mutex_destroy(&ref->mutex);
}
#endif

static inline bool is_idle_barrier(struct active_node *node, u64 idx)
{
	return node->timeline == idx && !i915_active_request_isset(&node->base);
}

static struct active_node *reuse_idle_barrier(struct i915_active *ref, u64 idx)
{
	struct rb_node *prev, *p;

	if (RB_EMPTY_ROOT(&ref->tree))
		return NULL;

	mutex_lock(&ref->mutex);
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

	mutex_unlock(&ref->mutex);

	return NULL;

match:
	rb_erase(p, &ref->tree); /* Hide from waits and sibling allocations */
	if (p == &ref->cache->node)
		ref->cache = NULL;
	mutex_unlock(&ref->mutex);

	return rb_entry(p, struct active_node, node);
}

int i915_active_acquire_preallocate_barrier(struct i915_active *ref,
					    struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;
	intel_engine_mask_t tmp, mask = engine->mask;
	struct llist_node *pos, *next;
	int err;

	GEM_BUG_ON(!llist_empty(&ref->preallocated_barriers));

	/*
	 * Preallocate a node for each physical engine supporting the target
	 * engine (remember virtual engines have more than one sibling).
	 * We can then use the preallocated nodes in
	 * i915_active_acquire_barrier()
	 */
	for_each_engine_masked(engine, i915, mask, tmp) {
		u64 idx = engine->kernel_context->timeline->fence_context;
		struct active_node *node;

		node = reuse_idle_barrier(ref, idx);
		if (!node) {
			node = kmem_cache_alloc(global.slab_cache, GFP_KERNEL);
			if (!node) {
				err = ENOMEM;
				goto unwind;
			}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM)
			node->base.lock =
				&engine->kernel_context->timeline->mutex;
#endif
			RCU_INIT_POINTER(node->base.request, NULL);
			node->base.retire = node_retire;
			node->timeline = idx;
			node->ref = ref;
		}

		if (!i915_active_request_isset(&node->base)) {
			/*
			 * Mark this as being *our* unconnected proto-node.
			 *
			 * Since this node is not in any list, and we have
			 * decoupled it from the rbtree, we can reuse the
			 * request to indicate this is an idle-barrier node
			 * and then we can use the rb_node and list pointers
			 * for our tracking of the pending barrier.
			 */
			RCU_INIT_POINTER(node->base.request, ERR_PTR(-EAGAIN));
			node->base.link.prev = (void *)engine;
			atomic_inc(&ref->count);
		}

		GEM_BUG_ON(barrier_to_engine(node) != engine);
		llist_add(barrier_to_ll(node), &ref->preallocated_barriers);
		intel_engine_pm_get(engine);
	}

	return 0;

unwind:
	llist_for_each_safe(pos, next, take_preallocated_barriers(ref)) {
		struct active_node *node = barrier_from_ll(pos);

		atomic_dec(&ref->count);
		intel_engine_pm_put(barrier_to_engine(node));

		kmem_cache_free(global.slab_cache, node);
	}
	return err;
}

void i915_active_acquire_barrier(struct i915_active *ref)
{
	struct llist_node *pos, *next;

	GEM_BUG_ON(i915_active_is_idle(ref));

	/*
	 * Transfer the list of preallocated barriers into the
	 * i915_active rbtree, but only as proto-nodes. They will be
	 * populated by i915_request_add_active_barriers() to point to the
	 * request that will eventually release them.
	 */
	mutex_lock_nested(&ref->mutex, SINGLE_DEPTH_NESTING);
	llist_for_each_safe(pos, next, take_preallocated_barriers(ref)) {
		struct active_node *node = barrier_from_ll(pos);
		struct intel_engine_cs *engine = barrier_to_engine(node);
		struct rb_node **p, *parent;

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

		llist_add(barrier_to_ll(node), &engine->barrier_tasks);
		intel_engine_pm_put(engine);
	}
	mutex_unlock(&ref->mutex);
}

void i915_request_add_active_barriers(struct i915_request *rq)
{
	struct intel_engine_cs *engine = rq->engine;
	struct llist_node *node, *next;

	GEM_BUG_ON(intel_engine_is_virtual(engine));
	GEM_BUG_ON(rq->timeline != engine->kernel_context->timeline);

	/*
	 * Attach the list of proto-fences to the in-flight request such
	 * that the parent i915_active will be released when this request
	 * is retired.
	 */
	llist_for_each_safe(node, next, llist_del_all(&engine->barrier_tasks)) {
		RCU_INIT_POINTER(barrier_from_ll(node)->base.request, rq);
		smp_wmb(); /* serialise with reuse_idle_barrier */
		list_add_tail((struct list_head *)node, &rq->active_list);
	}
}

int i915_active_request_set(struct i915_active_request *active,
			    struct i915_request *rq)
{
	int err;

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM)
	lockdep_assert_held(active->lock);
#endif

	/* Must maintain ordering wrt previous active requests */
	err = i915_request_await_active_request(rq, active);
	if (err)
		return err;

	__i915_active_request_set(active, rq);
	return 0;
}

void i915_active_retire_noop(struct i915_active_request *active,
			     struct i915_request *request)
{
	/* Space left intentionally blank */
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
