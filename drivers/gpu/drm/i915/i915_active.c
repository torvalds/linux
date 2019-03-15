/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_active.h"

#define BKL(ref) (&(ref)->i915->drm.struct_mutex)

/*
 * Active refs memory management
 *
 * To be more economical with memory, we reap all the i915_active trees as
 * they idle (when we know the active requests are inactive) and allocate the
 * nodes from a local slab cache to hopefully reduce the fragmentation.
 */
static struct i915_global_active {
	struct kmem_cache *slab_cache;
} global;

struct active_node {
	struct i915_active_request base;
	struct i915_active *ref;
	struct rb_node node;
	u64 timeline;
};

static void
__active_park(struct i915_active *ref)
{
	struct active_node *it, *n;

	rbtree_postorder_for_each_entry_safe(it, n, &ref->tree, node) {
		GEM_BUG_ON(i915_active_request_isset(&it->base));
		kmem_cache_free(global.slab_cache, it);
	}
	ref->tree = RB_ROOT;
}

static void
__active_retire(struct i915_active *ref)
{
	GEM_BUG_ON(!ref->count);
	if (--ref->count)
		return;

	/* return the unused nodes to our slabcache */
	__active_park(ref);

	ref->retire(ref);
}

static void
node_retire(struct i915_active_request *base, struct i915_request *rq)
{
	__active_retire(container_of(base, struct active_node, base)->ref);
}

static void
last_retire(struct i915_active_request *base, struct i915_request *rq)
{
	__active_retire(container_of(base, struct i915_active, last));
}

static struct i915_active_request *
active_instance(struct i915_active *ref, u64 idx)
{
	struct active_node *node;
	struct rb_node **p, *parent;
	struct i915_request *old;

	/*
	 * We track the most recently used timeline to skip a rbtree search
	 * for the common case, under typical loads we never need the rbtree
	 * at all. We can reuse the last slot if it is empty, that is
	 * after the previous activity has been retired, or if it matches the
	 * current timeline.
	 *
	 * Note that we allow the timeline to be active simultaneously in
	 * the rbtree and the last cache. We do this to avoid having
	 * to search and replace the rbtree element for a new timeline, with
	 * the cost being that we must be aware that the ref may be retired
	 * twice for the same timeline (as the older rbtree element will be
	 * retired before the new request added to last).
	 */
	old = i915_active_request_raw(&ref->last, BKL(ref));
	if (!old || old->fence.context == idx)
		goto out;

	/* Move the currently active fence into the rbtree */
	idx = old->fence.context;

	parent = NULL;
	p = &ref->tree.rb_node;
	while (*p) {
		parent = *p;

		node = rb_entry(parent, struct active_node, node);
		if (node->timeline == idx)
			goto replace;

		if (node->timeline < idx)
			p = &parent->rb_right;
		else
			p = &parent->rb_left;
	}

	node = kmem_cache_alloc(global.slab_cache, GFP_KERNEL);

	/* kmalloc may retire the ref->last (thanks shrinker)! */
	if (unlikely(!i915_active_request_raw(&ref->last, BKL(ref)))) {
		kmem_cache_free(global.slab_cache, node);
		goto out;
	}

	if (unlikely(!node))
		return ERR_PTR(-ENOMEM);

	i915_active_request_init(&node->base, NULL, node_retire);
	node->ref = ref;
	node->timeline = idx;

	rb_link_node(&node->node, parent, p);
	rb_insert_color(&node->node, &ref->tree);

replace:
	/*
	 * Overwrite the previous active slot in the rbtree with last,
	 * leaving last zeroed. If the previous slot is still active,
	 * we must be careful as we now only expect to receive one retire
	 * callback not two, and so much undo the active counting for the
	 * overwritten slot.
	 */
	if (i915_active_request_isset(&node->base)) {
		/* Retire ourselves from the old rq->active_list */
		__list_del_entry(&node->base.link);
		ref->count--;
		GEM_BUG_ON(!ref->count);
	}
	GEM_BUG_ON(list_empty(&ref->last.link));
	list_replace_init(&ref->last.link, &node->base.link);
	node->base.request = fetch_and_zero(&ref->last.request);

out:
	return &ref->last;
}

void i915_active_init(struct drm_i915_private *i915,
		      struct i915_active *ref,
		      void (*retire)(struct i915_active *ref))
{
	ref->i915 = i915;
	ref->retire = retire;
	ref->tree = RB_ROOT;
	i915_active_request_init(&ref->last, NULL, last_retire);
	ref->count = 0;
}

int i915_active_ref(struct i915_active *ref,
		    u64 timeline,
		    struct i915_request *rq)
{
	struct i915_active_request *active;
	int err = 0;

	/* Prevent reaping in case we malloc/wait while building the tree */
	i915_active_acquire(ref);

	active = active_instance(ref, timeline);
	if (IS_ERR(active)) {
		err = PTR_ERR(active);
		goto out;
	}

	if (!i915_active_request_isset(active))
		ref->count++;
	__i915_active_request_set(active, rq);

	GEM_BUG_ON(!ref->count);
out:
	i915_active_release(ref);
	return err;
}

bool i915_active_acquire(struct i915_active *ref)
{
	lockdep_assert_held(BKL(ref));
	return !ref->count++;
}

void i915_active_release(struct i915_active *ref)
{
	lockdep_assert_held(BKL(ref));
	__active_retire(ref);
}

int i915_active_wait(struct i915_active *ref)
{
	struct active_node *it, *n;
	int ret = 0;

	if (i915_active_acquire(ref))
		goto out_release;

	ret = i915_active_request_retire(&ref->last, BKL(ref));
	if (ret)
		goto out_release;

	rbtree_postorder_for_each_entry_safe(it, n, &ref->tree, node) {
		ret = i915_active_request_retire(&it->base, BKL(ref));
		if (ret)
			break;
	}

out_release:
	i915_active_release(ref);
	return ret;
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
	int err = 0;

	/* await allocates and so we need to avoid hitting the shrinker */
	if (i915_active_acquire(ref))
		goto out; /* was idle */

	err = i915_request_await_active_request(rq, &ref->last);
	if (err)
		goto out;

	rbtree_postorder_for_each_entry_safe(it, n, &ref->tree, node) {
		err = i915_request_await_active_request(rq, &it->base);
		if (err)
			goto out;
	}

out:
	i915_active_release(ref);
	return err;
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM)
void i915_active_fini(struct i915_active *ref)
{
	GEM_BUG_ON(i915_active_request_isset(&ref->last));
	GEM_BUG_ON(!RB_EMPTY_ROOT(&ref->tree));
	GEM_BUG_ON(ref->count);
}
#endif

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

int __init i915_global_active_init(void)
{
	global.slab_cache = KMEM_CACHE(active_node, SLAB_HWCACHE_ALIGN);
	if (!global.slab_cache)
		return -ENOMEM;

	return 0;
}

void __exit i915_global_active_exit(void)
{
	kmem_cache_destroy(global.slab_cache);
}
