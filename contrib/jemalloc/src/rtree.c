#define JEMALLOC_RTREE_C_
#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/jemalloc_internal_includes.h"

#include "jemalloc/internal/assert.h"
#include "jemalloc/internal/mutex.h"

/*
 * Only the most significant bits of keys passed to rtree_{read,write}() are
 * used.
 */
bool
rtree_new(rtree_t *rtree, bool zeroed) {
#ifdef JEMALLOC_JET
	if (!zeroed) {
		memset(rtree, 0, sizeof(rtree_t)); /* Clear root. */
	}
#else
	assert(zeroed);
#endif

	if (malloc_mutex_init(&rtree->init_lock, "rtree", WITNESS_RANK_RTREE,
	    malloc_mutex_rank_exclusive)) {
		return true;
	}

	return false;
}

static rtree_node_elm_t *
rtree_node_alloc_impl(tsdn_t *tsdn, rtree_t *rtree, size_t nelms) {
	return (rtree_node_elm_t *)base_alloc(tsdn, b0get(), nelms *
	    sizeof(rtree_node_elm_t), CACHELINE);
}
rtree_node_alloc_t *JET_MUTABLE rtree_node_alloc = rtree_node_alloc_impl;

static void
rtree_node_dalloc_impl(tsdn_t *tsdn, rtree_t *rtree, rtree_node_elm_t *node) {
	/* Nodes are never deleted during normal operation. */
	not_reached();
}
UNUSED rtree_node_dalloc_t *JET_MUTABLE rtree_node_dalloc =
    rtree_node_dalloc_impl;

static rtree_leaf_elm_t *
rtree_leaf_alloc_impl(tsdn_t *tsdn, rtree_t *rtree, size_t nelms) {
	return (rtree_leaf_elm_t *)base_alloc(tsdn, b0get(), nelms *
	    sizeof(rtree_leaf_elm_t), CACHELINE);
}
rtree_leaf_alloc_t *JET_MUTABLE rtree_leaf_alloc = rtree_leaf_alloc_impl;

static void
rtree_leaf_dalloc_impl(tsdn_t *tsdn, rtree_t *rtree, rtree_leaf_elm_t *leaf) {
	/* Leaves are never deleted during normal operation. */
	not_reached();
}
UNUSED rtree_leaf_dalloc_t *JET_MUTABLE rtree_leaf_dalloc =
    rtree_leaf_dalloc_impl;

#ifdef JEMALLOC_JET
#  if RTREE_HEIGHT > 1
static void
rtree_delete_subtree(tsdn_t *tsdn, rtree_t *rtree, rtree_node_elm_t *subtree,
    unsigned level) {
	size_t nchildren = ZU(1) << rtree_levels[level].bits;
	if (level + 2 < RTREE_HEIGHT) {
		for (size_t i = 0; i < nchildren; i++) {
			rtree_node_elm_t *node =
			    (rtree_node_elm_t *)atomic_load_p(&subtree[i].child,
			    ATOMIC_RELAXED);
			if (node != NULL) {
				rtree_delete_subtree(tsdn, rtree, node, level +
				    1);
			}
		}
	} else {
		for (size_t i = 0; i < nchildren; i++) {
			rtree_leaf_elm_t *leaf =
			    (rtree_leaf_elm_t *)atomic_load_p(&subtree[i].child,
			    ATOMIC_RELAXED);
			if (leaf != NULL) {
				rtree_leaf_dalloc(tsdn, rtree, leaf);
			}
		}
	}

	if (subtree != rtree->root) {
		rtree_node_dalloc(tsdn, rtree, subtree);
	}
}
#  endif

void
rtree_delete(tsdn_t *tsdn, rtree_t *rtree) {
#  if RTREE_HEIGHT > 1
	rtree_delete_subtree(tsdn, rtree, rtree->root, 0);
#  endif
}
#endif

static rtree_node_elm_t *
rtree_node_init(tsdn_t *tsdn, rtree_t *rtree, unsigned level,
    atomic_p_t *elmp) {
	malloc_mutex_lock(tsdn, &rtree->init_lock);
	/*
	 * If *elmp is non-null, then it was initialized with the init lock
	 * held, so we can get by with 'relaxed' here.
	 */
	rtree_node_elm_t *node = atomic_load_p(elmp, ATOMIC_RELAXED);
	if (node == NULL) {
		node = rtree_node_alloc(tsdn, rtree, ZU(1) <<
		    rtree_levels[level].bits);
		if (node == NULL) {
			malloc_mutex_unlock(tsdn, &rtree->init_lock);
			return NULL;
		}
		/*
		 * Even though we hold the lock, a later reader might not; we
		 * need release semantics.
		 */
		atomic_store_p(elmp, node, ATOMIC_RELEASE);
	}
	malloc_mutex_unlock(tsdn, &rtree->init_lock);

	return node;
}

static rtree_leaf_elm_t *
rtree_leaf_init(tsdn_t *tsdn, rtree_t *rtree, atomic_p_t *elmp) {
	malloc_mutex_lock(tsdn, &rtree->init_lock);
	/*
	 * If *elmp is non-null, then it was initialized with the init lock
	 * held, so we can get by with 'relaxed' here.
	 */
	rtree_leaf_elm_t *leaf = atomic_load_p(elmp, ATOMIC_RELAXED);
	if (leaf == NULL) {
		leaf = rtree_leaf_alloc(tsdn, rtree, ZU(1) <<
		    rtree_levels[RTREE_HEIGHT-1].bits);
		if (leaf == NULL) {
			malloc_mutex_unlock(tsdn, &rtree->init_lock);
			return NULL;
		}
		/*
		 * Even though we hold the lock, a later reader might not; we
		 * need release semantics.
		 */
		atomic_store_p(elmp, leaf, ATOMIC_RELEASE);
	}
	malloc_mutex_unlock(tsdn, &rtree->init_lock);

	return leaf;
}

static bool
rtree_node_valid(rtree_node_elm_t *node) {
	return ((uintptr_t)node != (uintptr_t)0);
}

static bool
rtree_leaf_valid(rtree_leaf_elm_t *leaf) {
	return ((uintptr_t)leaf != (uintptr_t)0);
}

static rtree_node_elm_t *
rtree_child_node_tryread(rtree_node_elm_t *elm, bool dependent) {
	rtree_node_elm_t *node;

	if (dependent) {
		node = (rtree_node_elm_t *)atomic_load_p(&elm->child,
		    ATOMIC_RELAXED);
	} else {
		node = (rtree_node_elm_t *)atomic_load_p(&elm->child,
		    ATOMIC_ACQUIRE);
	}

	assert(!dependent || node != NULL);
	return node;
}

static rtree_node_elm_t *
rtree_child_node_read(tsdn_t *tsdn, rtree_t *rtree, rtree_node_elm_t *elm,
    unsigned level, bool dependent) {
	rtree_node_elm_t *node;

	node = rtree_child_node_tryread(elm, dependent);
	if (!dependent && unlikely(!rtree_node_valid(node))) {
		node = rtree_node_init(tsdn, rtree, level + 1, &elm->child);
	}
	assert(!dependent || node != NULL);
	return node;
}

static rtree_leaf_elm_t *
rtree_child_leaf_tryread(rtree_node_elm_t *elm, bool dependent) {
	rtree_leaf_elm_t *leaf;

	if (dependent) {
		leaf = (rtree_leaf_elm_t *)atomic_load_p(&elm->child,
		    ATOMIC_RELAXED);
	} else {
		leaf = (rtree_leaf_elm_t *)atomic_load_p(&elm->child,
		    ATOMIC_ACQUIRE);
	}

	assert(!dependent || leaf != NULL);
	return leaf;
}

static rtree_leaf_elm_t *
rtree_child_leaf_read(tsdn_t *tsdn, rtree_t *rtree, rtree_node_elm_t *elm,
    unsigned level, bool dependent) {
	rtree_leaf_elm_t *leaf;

	leaf = rtree_child_leaf_tryread(elm, dependent);
	if (!dependent && unlikely(!rtree_leaf_valid(leaf))) {
		leaf = rtree_leaf_init(tsdn, rtree, &elm->child);
	}
	assert(!dependent || leaf != NULL);
	return leaf;
}

rtree_leaf_elm_t *
rtree_leaf_elm_lookup_hard(tsdn_t *tsdn, rtree_t *rtree, rtree_ctx_t *rtree_ctx,
    uintptr_t key, bool dependent, bool init_missing) {
	rtree_node_elm_t *node;
	rtree_leaf_elm_t *leaf;
#if RTREE_HEIGHT > 1
	node = rtree->root;
#else
	leaf = rtree->root;
#endif

	if (config_debug) {
		uintptr_t leafkey = rtree_leafkey(key);
		for (unsigned i = 0; i < RTREE_CTX_NCACHE; i++) {
			assert(rtree_ctx->cache[i].leafkey != leafkey);
		}
		for (unsigned i = 0; i < RTREE_CTX_NCACHE_L2; i++) {
			assert(rtree_ctx->l2_cache[i].leafkey != leafkey);
		}
	}

#define RTREE_GET_CHILD(level) {					\
		assert(level < RTREE_HEIGHT-1);				\
		if (level != 0 && !dependent &&				\
		    unlikely(!rtree_node_valid(node))) {		\
			return NULL;					\
		}							\
		uintptr_t subkey = rtree_subkey(key, level);		\
		if (level + 2 < RTREE_HEIGHT) {				\
			node = init_missing ?				\
			    rtree_child_node_read(tsdn, rtree,		\
			    &node[subkey], level, dependent) :		\
			    rtree_child_node_tryread(&node[subkey],	\
			    dependent);					\
		} else {						\
			leaf = init_missing ?				\
			    rtree_child_leaf_read(tsdn, rtree,		\
			    &node[subkey], level, dependent) :		\
			    rtree_child_leaf_tryread(&node[subkey],	\
			    dependent);					\
		}							\
	}
	/*
	 * Cache replacement upon hard lookup (i.e. L1 & L2 rtree cache miss):
	 * (1) evict last entry in L2 cache; (2) move the collision slot from L1
	 * cache down to L2; and 3) fill L1.
	 */
#define RTREE_GET_LEAF(level) {						\
		assert(level == RTREE_HEIGHT-1);			\
		if (!dependent && unlikely(!rtree_leaf_valid(leaf))) {	\
			return NULL;					\
		}							\
		if (RTREE_CTX_NCACHE_L2 > 1) {				\
			memmove(&rtree_ctx->l2_cache[1],		\
			    &rtree_ctx->l2_cache[0],			\
			    sizeof(rtree_ctx_cache_elm_t) *		\
			    (RTREE_CTX_NCACHE_L2 - 1));			\
		}							\
		size_t slot = rtree_cache_direct_map(key);		\
		rtree_ctx->l2_cache[0].leafkey =			\
		    rtree_ctx->cache[slot].leafkey;			\
		rtree_ctx->l2_cache[0].leaf =				\
		    rtree_ctx->cache[slot].leaf;			\
		uintptr_t leafkey = rtree_leafkey(key);			\
		rtree_ctx->cache[slot].leafkey = leafkey;		\
		rtree_ctx->cache[slot].leaf = leaf;			\
		uintptr_t subkey = rtree_subkey(key, level);		\
		return &leaf[subkey];					\
	}
	if (RTREE_HEIGHT > 1) {
		RTREE_GET_CHILD(0)
	}
	if (RTREE_HEIGHT > 2) {
		RTREE_GET_CHILD(1)
	}
	if (RTREE_HEIGHT > 3) {
		for (unsigned i = 2; i < RTREE_HEIGHT-1; i++) {
			RTREE_GET_CHILD(i)
		}
	}
	RTREE_GET_LEAF(RTREE_HEIGHT-1)
#undef RTREE_GET_CHILD
#undef RTREE_GET_LEAF
	not_reached();
}

void
rtree_ctx_data_init(rtree_ctx_t *ctx) {
	for (unsigned i = 0; i < RTREE_CTX_NCACHE; i++) {
		rtree_ctx_cache_elm_t *cache = &ctx->cache[i];
		cache->leafkey = RTREE_LEAFKEY_INVALID;
		cache->leaf = NULL;
	}
	for (unsigned i = 0; i < RTREE_CTX_NCACHE_L2; i++) {
		rtree_ctx_cache_elm_t *cache = &ctx->l2_cache[i];
		cache->leafkey = RTREE_LEAFKEY_INVALID;
		cache->leaf = NULL;
	}
}
