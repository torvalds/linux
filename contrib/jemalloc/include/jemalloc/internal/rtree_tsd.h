#ifndef JEMALLOC_INTERNAL_RTREE_CTX_H
#define JEMALLOC_INTERNAL_RTREE_CTX_H

/*
 * Number of leafkey/leaf pairs to cache in L1 and L2 level respectively.  Each
 * entry supports an entire leaf, so the cache hit rate is typically high even
 * with a small number of entries.  In rare cases extent activity will straddle
 * the boundary between two leaf nodes.  Furthermore, an arena may use a
 * combination of dss and mmap.  Note that as memory usage grows past the amount
 * that this cache can directly cover, the cache will become less effective if
 * locality of reference is low, but the consequence is merely cache misses
 * while traversing the tree nodes.
 *
 * The L1 direct mapped cache offers consistent and low cost on cache hit.
 * However collision could affect hit rate negatively.  This is resolved by
 * combining with a L2 LRU cache, which requires linear search and re-ordering
 * on access but suffers no collision.  Note that, the cache will itself suffer
 * cache misses if made overly large, plus the cost of linear search in the LRU
 * cache.
 */
#define RTREE_CTX_LG_NCACHE 4
#define RTREE_CTX_NCACHE (1 << RTREE_CTX_LG_NCACHE)
#define RTREE_CTX_NCACHE_L2 8

/*
 * Zero initializer required for tsd initialization only.  Proper initialization
 * done via rtree_ctx_data_init().
 */
#define RTREE_CTX_ZERO_INITIALIZER {{{0}}, {{0}}}


typedef struct rtree_leaf_elm_s rtree_leaf_elm_t;

typedef struct rtree_ctx_cache_elm_s rtree_ctx_cache_elm_t;
struct rtree_ctx_cache_elm_s {
	uintptr_t		leafkey;
	rtree_leaf_elm_t	*leaf;
};

typedef struct rtree_ctx_s rtree_ctx_t;
struct rtree_ctx_s {
	/* Direct mapped cache. */
	rtree_ctx_cache_elm_t	cache[RTREE_CTX_NCACHE];
	/* L2 LRU cache. */
	rtree_ctx_cache_elm_t	l2_cache[RTREE_CTX_NCACHE_L2];
};

void rtree_ctx_data_init(rtree_ctx_t *ctx);

#endif /* JEMALLOC_INTERNAL_RTREE_CTX_H */
