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

	ref->retire(ref);

	rbtree_postorder_for_each_entry_safe(it, n, &root, node) {
		GEM_BUG_ON(i915_active_request_isset(&it->base));
		kmem_cache_free(global.slab_cache, it);
	}
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
	active_retire(container_of(base, struct active_node, base)->ref);
}

static struct i915_active_request *
active_instance(struct i915_active *ref, u64 idx)
{
	struct active_node *node, *prealloc;
	struct rb_node **p, *parent;

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
	i915_active_request_init(&node->base, NULL, node_retire);
	node->ref = ref;
	node->timeline = idx;

	rb_link_node(&node->node, parent, p);
	rb_insert_color(&node->node, &ref->tree);

out:
	ref->cache = node;
	mutex_unlock(&ref->mutex);

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
	ref->active = active;
	ref->retire = retire;
	ref->tree = RB_ROOT;
	ref->cache = NULL;
	init_llist_head(&ref->barriers);
	atomic_set(&ref->count, 0);
	__mutex_init(&ref->mutex, "i915_active", key);
}

int i915_active_ref(struct i915_active *ref,
		    u64 timeline,
		    struct i915_request *rq)
{
	struct i915_active_request *active;
	int err;

	/* Prevent reaping in case we malloc/wait while building the tree */
	err = i915_active_acquire(ref);
	if (err)
		return err;

	active = active_instance(ref, timeline);
	if (!active) {
		err = -ENOMEM;
		goto out;
	}

	if (!i915_active_request_isset(active))
		atomic_inc(&ref->count);
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

int i915_active_wait(struct i915_active *ref)
{
	struct active_node *it, *n;
	int err;

	might_sleep();
	if (RB_EMPTY_ROOT(&ref->tree))
		return 0;

	err = mutex_lock_interruptible(&ref->mutex);
	if (err)
		return err;

	if (!atomic_add_unless(&ref->count, 1, 0)) {
		mutex_unlock(&ref->mutex);
		return 0;
	}

	rbtree_postorder_for_each_entry_safe(it, n, &ref->tree, node) {
		err = i915_active_request_retire(&it->base, BKL(ref));
		if (err)
			break;
	}

	__active_retire(ref);
	if (err)
		return err;

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

int i915_active_acquire_preallocate_barrier(struct i915_active *ref,
					    struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;
	struct llist_node *pos, *next;
	unsigned long tmp;
	int err;

	GEM_BUG_ON(!engine->mask);
	for_each_engine_masked(engine, i915, engine->mask, tmp) {
		struct intel_context *kctx = engine->kernel_context;
		struct active_node *node;

		node = kmem_cache_alloc(global.slab_cache, GFP_KERNEL);
		if (unlikely(!node)) {
			err = -ENOMEM;
			goto unwind;
		}

		i915_active_request_init(&node->base,
					 (void *)engine, node_retire);
		node->timeline = kctx->ring->timeline->fence_context;
		node->ref = ref;
		atomic_inc(&ref->count);

		intel_engine_pm_get(engine);
		llist_add((struct llist_node *)&node->base.link,
			  &ref->barriers);
	}

	return 0;

unwind:
	llist_for_each_safe(pos, next, llist_del_all(&ref->barriers)) {
		struct active_node *node;

		node = container_of((struct list_head *)pos,
				    typeof(*node), base.link);
		engine = (void *)rcu_access_pointer(node->base.request);

		intel_engine_pm_put(engine);
		kmem_cache_free(global.slab_cache, node);
	}
	return err;
}

void i915_active_acquire_barrier(struct i915_active *ref)
{
	struct llist_node *pos, *next;

	GEM_BUG_ON(i915_active_is_idle(ref));

	mutex_lock_nested(&ref->mutex, SINGLE_DEPTH_NESTING);
	llist_for_each_safe(pos, next, llist_del_all(&ref->barriers)) {
		struct intel_engine_cs *engine;
		struct active_node *node;
		struct rb_node **p, *parent;

		node = container_of((struct list_head *)pos,
				    typeof(*node), base.link);

		engine = (void *)rcu_access_pointer(node->base.request);
		RCU_INIT_POINTER(node->base.request, ERR_PTR(-EAGAIN));

		parent = NULL;
		p = &ref->tree.rb_node;
		while (*p) {
			parent = *p;
			if (rb_entry(parent,
				     struct active_node,
				     node)->timeline < node->timeline)
				p = &parent->rb_right;
			else
				p = &parent->rb_left;
		}
		rb_link_node(&node->node, parent, p);
		rb_insert_color(&node->node, &ref->tree);

		llist_add((struct llist_node *)&node->base.link,
			  &engine->barrier_tasks);
		intel_engine_pm_put(engine);
	}
	mutex_unlock(&ref->mutex);
}

void i915_request_add_barriers(struct i915_request *rq)
{
	struct intel_engine_cs *engine = rq->engine;
	struct llist_node *node, *next;

	llist_for_each_safe(node, next, llist_del_all(&engine->barrier_tasks))
		list_add_tail((struct list_head *)node, &rq->active_list);
}

int i915_active_request_set(struct i915_active_request *active,
			    struct i915_request *rq)
{
	int err;

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
