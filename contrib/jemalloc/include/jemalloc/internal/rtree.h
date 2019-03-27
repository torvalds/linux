#ifndef JEMALLOC_INTERNAL_RTREE_H
#define JEMALLOC_INTERNAL_RTREE_H

#include "jemalloc/internal/atomic.h"
#include "jemalloc/internal/mutex.h"
#include "jemalloc/internal/rtree_tsd.h"
#include "jemalloc/internal/size_classes.h"
#include "jemalloc/internal/tsd.h"

/*
 * This radix tree implementation is tailored to the singular purpose of
 * associating metadata with extents that are currently owned by jemalloc.
 *
 *******************************************************************************
 */

/* Number of high insignificant bits. */
#define RTREE_NHIB ((1U << (LG_SIZEOF_PTR+3)) - LG_VADDR)
/* Number of low insigificant bits. */
#define RTREE_NLIB LG_PAGE
/* Number of significant bits. */
#define RTREE_NSB (LG_VADDR - RTREE_NLIB)
/* Number of levels in radix tree. */
#if RTREE_NSB <= 10
#  define RTREE_HEIGHT 1
#elif RTREE_NSB <= 36
#  define RTREE_HEIGHT 2
#elif RTREE_NSB <= 52
#  define RTREE_HEIGHT 3
#else
#  error Unsupported number of significant virtual address bits
#endif
/* Use compact leaf representation if virtual address encoding allows. */
#if RTREE_NHIB >= LG_CEIL_NSIZES
#  define RTREE_LEAF_COMPACT
#endif

/* Needed for initialization only. */
#define RTREE_LEAFKEY_INVALID ((uintptr_t)1)

typedef struct rtree_node_elm_s rtree_node_elm_t;
struct rtree_node_elm_s {
	atomic_p_t	child; /* (rtree_{node,leaf}_elm_t *) */
};

struct rtree_leaf_elm_s {
#ifdef RTREE_LEAF_COMPACT
	/*
	 * Single pointer-width field containing all three leaf element fields.
	 * For example, on a 64-bit x64 system with 48 significant virtual
	 * memory address bits, the index, extent, and slab fields are packed as
	 * such:
	 *
	 * x: index
	 * e: extent
	 * b: slab
	 *
	 *   00000000 xxxxxxxx eeeeeeee [...] eeeeeeee eeee000b
	 */
	atomic_p_t	le_bits;
#else
	atomic_p_t	le_extent; /* (extent_t *) */
	atomic_u_t	le_szind; /* (szind_t) */
	atomic_b_t	le_slab; /* (bool) */
#endif
};

typedef struct rtree_level_s rtree_level_t;
struct rtree_level_s {
	/* Number of key bits distinguished by this level. */
	unsigned		bits;
	/*
	 * Cumulative number of key bits distinguished by traversing to
	 * corresponding tree level.
	 */
	unsigned		cumbits;
};

typedef struct rtree_s rtree_t;
struct rtree_s {
	malloc_mutex_t		init_lock;
	/* Number of elements based on rtree_levels[0].bits. */
#if RTREE_HEIGHT > 1
	rtree_node_elm_t	root[1U << (RTREE_NSB/RTREE_HEIGHT)];
#else
	rtree_leaf_elm_t	root[1U << (RTREE_NSB/RTREE_HEIGHT)];
#endif
};

/*
 * Split the bits into one to three partitions depending on number of
 * significant bits.  It the number of bits does not divide evenly into the
 * number of levels, place one remainder bit per level starting at the leaf
 * level.
 */
static const rtree_level_t rtree_levels[] = {
#if RTREE_HEIGHT == 1
	{RTREE_NSB, RTREE_NHIB + RTREE_NSB}
#elif RTREE_HEIGHT == 2
	{RTREE_NSB/2, RTREE_NHIB + RTREE_NSB/2},
	{RTREE_NSB/2 + RTREE_NSB%2, RTREE_NHIB + RTREE_NSB}
#elif RTREE_HEIGHT == 3
	{RTREE_NSB/3, RTREE_NHIB + RTREE_NSB/3},
	{RTREE_NSB/3 + RTREE_NSB%3/2,
	    RTREE_NHIB + RTREE_NSB/3*2 + RTREE_NSB%3/2},
	{RTREE_NSB/3 + RTREE_NSB%3 - RTREE_NSB%3/2, RTREE_NHIB + RTREE_NSB}
#else
#  error Unsupported rtree height
#endif
};

bool rtree_new(rtree_t *rtree, bool zeroed);

typedef rtree_node_elm_t *(rtree_node_alloc_t)(tsdn_t *, rtree_t *, size_t);
extern rtree_node_alloc_t *JET_MUTABLE rtree_node_alloc;

typedef rtree_leaf_elm_t *(rtree_leaf_alloc_t)(tsdn_t *, rtree_t *, size_t);
extern rtree_leaf_alloc_t *JET_MUTABLE rtree_leaf_alloc;

typedef void (rtree_node_dalloc_t)(tsdn_t *, rtree_t *, rtree_node_elm_t *);
extern rtree_node_dalloc_t *JET_MUTABLE rtree_node_dalloc;

typedef void (rtree_leaf_dalloc_t)(tsdn_t *, rtree_t *, rtree_leaf_elm_t *);
extern rtree_leaf_dalloc_t *JET_MUTABLE rtree_leaf_dalloc;
#ifdef JEMALLOC_JET
void rtree_delete(tsdn_t *tsdn, rtree_t *rtree);
#endif
rtree_leaf_elm_t *rtree_leaf_elm_lookup_hard(tsdn_t *tsdn, rtree_t *rtree,
    rtree_ctx_t *rtree_ctx, uintptr_t key, bool dependent, bool init_missing);

JEMALLOC_ALWAYS_INLINE uintptr_t
rtree_leafkey(uintptr_t key) {
	unsigned ptrbits = ZU(1) << (LG_SIZEOF_PTR+3);
	unsigned cumbits = (rtree_levels[RTREE_HEIGHT-1].cumbits -
	    rtree_levels[RTREE_HEIGHT-1].bits);
	unsigned maskbits = ptrbits - cumbits;
	uintptr_t mask = ~((ZU(1) << maskbits) - 1);
	return (key & mask);
}

JEMALLOC_ALWAYS_INLINE size_t
rtree_cache_direct_map(uintptr_t key) {
	unsigned ptrbits = ZU(1) << (LG_SIZEOF_PTR+3);
	unsigned cumbits = (rtree_levels[RTREE_HEIGHT-1].cumbits -
	    rtree_levels[RTREE_HEIGHT-1].bits);
	unsigned maskbits = ptrbits - cumbits;
	return (size_t)((key >> maskbits) & (RTREE_CTX_NCACHE - 1));
}

JEMALLOC_ALWAYS_INLINE uintptr_t
rtree_subkey(uintptr_t key, unsigned level) {
	unsigned ptrbits = ZU(1) << (LG_SIZEOF_PTR+3);
	unsigned cumbits = rtree_levels[level].cumbits;
	unsigned shiftbits = ptrbits - cumbits;
	unsigned maskbits = rtree_levels[level].bits;
	uintptr_t mask = (ZU(1) << maskbits) - 1;
	return ((key >> shiftbits) & mask);
}

/*
 * Atomic getters.
 *
 * dependent: Reading a value on behalf of a pointer to a valid allocation
 *            is guaranteed to be a clean read even without synchronization,
 *            because the rtree update became visible in memory before the
 *            pointer came into existence.
 * !dependent: An arbitrary read, e.g. on behalf of ivsalloc(), may not be
 *             dependent on a previous rtree write, which means a stale read
 *             could result if synchronization were omitted here.
 */
#  ifdef RTREE_LEAF_COMPACT
JEMALLOC_ALWAYS_INLINE uintptr_t
rtree_leaf_elm_bits_read(tsdn_t *tsdn, rtree_t *rtree, rtree_leaf_elm_t *elm,
    bool dependent) {
	return (uintptr_t)atomic_load_p(&elm->le_bits, dependent
	    ? ATOMIC_RELAXED : ATOMIC_ACQUIRE);
}

JEMALLOC_ALWAYS_INLINE extent_t *
rtree_leaf_elm_bits_extent_get(uintptr_t bits) {
#    ifdef __aarch64__
	/*
	 * aarch64 doesn't sign extend the highest virtual address bit to set
	 * the higher ones.  Instead, the high bits gets zeroed.
	 */
	uintptr_t high_bit_mask = ((uintptr_t)1 << LG_VADDR) - 1;
	/* Mask off the slab bit. */
	uintptr_t low_bit_mask = ~(uintptr_t)1;
	uintptr_t mask = high_bit_mask & low_bit_mask;
	return (extent_t *)(bits & mask);
#    else
	/* Restore sign-extended high bits, mask slab bit. */
	return (extent_t *)((uintptr_t)((intptr_t)(bits << RTREE_NHIB) >>
	    RTREE_NHIB) & ~((uintptr_t)0x1));
#    endif
}

JEMALLOC_ALWAYS_INLINE szind_t
rtree_leaf_elm_bits_szind_get(uintptr_t bits) {
	return (szind_t)(bits >> LG_VADDR);
}

JEMALLOC_ALWAYS_INLINE bool
rtree_leaf_elm_bits_slab_get(uintptr_t bits) {
	return (bool)(bits & (uintptr_t)0x1);
}

#  endif

JEMALLOC_ALWAYS_INLINE extent_t *
rtree_leaf_elm_extent_read(UNUSED tsdn_t *tsdn, UNUSED rtree_t *rtree,
    rtree_leaf_elm_t *elm, bool dependent) {
#ifdef RTREE_LEAF_COMPACT
	uintptr_t bits = rtree_leaf_elm_bits_read(tsdn, rtree, elm, dependent);
	return rtree_leaf_elm_bits_extent_get(bits);
#else
	extent_t *extent = (extent_t *)atomic_load_p(&elm->le_extent, dependent
	    ? ATOMIC_RELAXED : ATOMIC_ACQUIRE);
	return extent;
#endif
}

JEMALLOC_ALWAYS_INLINE szind_t
rtree_leaf_elm_szind_read(UNUSED tsdn_t *tsdn, UNUSED rtree_t *rtree,
    rtree_leaf_elm_t *elm, bool dependent) {
#ifdef RTREE_LEAF_COMPACT
	uintptr_t bits = rtree_leaf_elm_bits_read(tsdn, rtree, elm, dependent);
	return rtree_leaf_elm_bits_szind_get(bits);
#else
	return (szind_t)atomic_load_u(&elm->le_szind, dependent ? ATOMIC_RELAXED
	    : ATOMIC_ACQUIRE);
#endif
}

JEMALLOC_ALWAYS_INLINE bool
rtree_leaf_elm_slab_read(UNUSED tsdn_t *tsdn, UNUSED rtree_t *rtree,
    rtree_leaf_elm_t *elm, bool dependent) {
#ifdef RTREE_LEAF_COMPACT
	uintptr_t bits = rtree_leaf_elm_bits_read(tsdn, rtree, elm, dependent);
	return rtree_leaf_elm_bits_slab_get(bits);
#else
	return atomic_load_b(&elm->le_slab, dependent ? ATOMIC_RELAXED :
	    ATOMIC_ACQUIRE);
#endif
}

static inline void
rtree_leaf_elm_extent_write(UNUSED tsdn_t *tsdn, UNUSED rtree_t *rtree,
    rtree_leaf_elm_t *elm, extent_t *extent) {
#ifdef RTREE_LEAF_COMPACT
	uintptr_t old_bits = rtree_leaf_elm_bits_read(tsdn, rtree, elm, true);
	uintptr_t bits = ((uintptr_t)rtree_leaf_elm_bits_szind_get(old_bits) <<
	    LG_VADDR) | ((uintptr_t)extent & (((uintptr_t)0x1 << LG_VADDR) - 1))
	    | ((uintptr_t)rtree_leaf_elm_bits_slab_get(old_bits));
	atomic_store_p(&elm->le_bits, (void *)bits, ATOMIC_RELEASE);
#else
	atomic_store_p(&elm->le_extent, extent, ATOMIC_RELEASE);
#endif
}

static inline void
rtree_leaf_elm_szind_write(UNUSED tsdn_t *tsdn, UNUSED rtree_t *rtree,
    rtree_leaf_elm_t *elm, szind_t szind) {
	assert(szind <= NSIZES);

#ifdef RTREE_LEAF_COMPACT
	uintptr_t old_bits = rtree_leaf_elm_bits_read(tsdn, rtree, elm,
	    true);
	uintptr_t bits = ((uintptr_t)szind << LG_VADDR) |
	    ((uintptr_t)rtree_leaf_elm_bits_extent_get(old_bits) &
	    (((uintptr_t)0x1 << LG_VADDR) - 1)) |
	    ((uintptr_t)rtree_leaf_elm_bits_slab_get(old_bits));
	atomic_store_p(&elm->le_bits, (void *)bits, ATOMIC_RELEASE);
#else
	atomic_store_u(&elm->le_szind, szind, ATOMIC_RELEASE);
#endif
}

static inline void
rtree_leaf_elm_slab_write(UNUSED tsdn_t *tsdn, UNUSED rtree_t *rtree,
    rtree_leaf_elm_t *elm, bool slab) {
#ifdef RTREE_LEAF_COMPACT
	uintptr_t old_bits = rtree_leaf_elm_bits_read(tsdn, rtree, elm,
	    true);
	uintptr_t bits = ((uintptr_t)rtree_leaf_elm_bits_szind_get(old_bits) <<
	    LG_VADDR) | ((uintptr_t)rtree_leaf_elm_bits_extent_get(old_bits) &
	    (((uintptr_t)0x1 << LG_VADDR) - 1)) | ((uintptr_t)slab);
	atomic_store_p(&elm->le_bits, (void *)bits, ATOMIC_RELEASE);
#else
	atomic_store_b(&elm->le_slab, slab, ATOMIC_RELEASE);
#endif
}

static inline void
rtree_leaf_elm_write(tsdn_t *tsdn, rtree_t *rtree, rtree_leaf_elm_t *elm,
    extent_t *extent, szind_t szind, bool slab) {
#ifdef RTREE_LEAF_COMPACT
	uintptr_t bits = ((uintptr_t)szind << LG_VADDR) |
	    ((uintptr_t)extent & (((uintptr_t)0x1 << LG_VADDR) - 1)) |
	    ((uintptr_t)slab);
	atomic_store_p(&elm->le_bits, (void *)bits, ATOMIC_RELEASE);
#else
	rtree_leaf_elm_slab_write(tsdn, rtree, elm, slab);
	rtree_leaf_elm_szind_write(tsdn, rtree, elm, szind);
	/*
	 * Write extent last, since the element is atomically considered valid
	 * as soon as the extent field is non-NULL.
	 */
	rtree_leaf_elm_extent_write(tsdn, rtree, elm, extent);
#endif
}

static inline void
rtree_leaf_elm_szind_slab_update(tsdn_t *tsdn, rtree_t *rtree,
    rtree_leaf_elm_t *elm, szind_t szind, bool slab) {
	assert(!slab || szind < NBINS);

	/*
	 * The caller implicitly assures that it is the only writer to the szind
	 * and slab fields, and that the extent field cannot currently change.
	 */
	rtree_leaf_elm_slab_write(tsdn, rtree, elm, slab);
	rtree_leaf_elm_szind_write(tsdn, rtree, elm, szind);
}

JEMALLOC_ALWAYS_INLINE rtree_leaf_elm_t *
rtree_leaf_elm_lookup(tsdn_t *tsdn, rtree_t *rtree, rtree_ctx_t *rtree_ctx,
    uintptr_t key, bool dependent, bool init_missing) {
	assert(key != 0);
	assert(!dependent || !init_missing);

	size_t slot = rtree_cache_direct_map(key);
	uintptr_t leafkey = rtree_leafkey(key);
	assert(leafkey != RTREE_LEAFKEY_INVALID);

	/* Fast path: L1 direct mapped cache. */
	if (likely(rtree_ctx->cache[slot].leafkey == leafkey)) {
		rtree_leaf_elm_t *leaf = rtree_ctx->cache[slot].leaf;
		assert(leaf != NULL);
		uintptr_t subkey = rtree_subkey(key, RTREE_HEIGHT-1);
		return &leaf[subkey];
	}
	/*
	 * Search the L2 LRU cache.  On hit, swap the matching element into the
	 * slot in L1 cache, and move the position in L2 up by 1.
	 */
#define RTREE_CACHE_CHECK_L2(i) do {					\
	if (likely(rtree_ctx->l2_cache[i].leafkey == leafkey)) {	\
		rtree_leaf_elm_t *leaf = rtree_ctx->l2_cache[i].leaf;	\
		assert(leaf != NULL);					\
		if (i > 0) {						\
			/* Bubble up by one. */				\
			rtree_ctx->l2_cache[i].leafkey =		\
				rtree_ctx->l2_cache[i - 1].leafkey;	\
			rtree_ctx->l2_cache[i].leaf =			\
				rtree_ctx->l2_cache[i - 1].leaf;	\
			rtree_ctx->l2_cache[i - 1].leafkey =		\
			    rtree_ctx->cache[slot].leafkey;		\
			rtree_ctx->l2_cache[i - 1].leaf =		\
			    rtree_ctx->cache[slot].leaf;		\
		} else {						\
			rtree_ctx->l2_cache[0].leafkey =		\
			    rtree_ctx->cache[slot].leafkey;		\
			rtree_ctx->l2_cache[0].leaf =			\
			    rtree_ctx->cache[slot].leaf;		\
		}							\
		rtree_ctx->cache[slot].leafkey = leafkey;		\
		rtree_ctx->cache[slot].leaf = leaf;			\
		uintptr_t subkey = rtree_subkey(key, RTREE_HEIGHT-1);	\
		return &leaf[subkey];					\
	}								\
} while (0)
	/* Check the first cache entry. */
	RTREE_CACHE_CHECK_L2(0);
	/* Search the remaining cache elements. */
	for (unsigned i = 1; i < RTREE_CTX_NCACHE_L2; i++) {
		RTREE_CACHE_CHECK_L2(i);
	}
#undef RTREE_CACHE_CHECK_L2

	return rtree_leaf_elm_lookup_hard(tsdn, rtree, rtree_ctx, key,
	    dependent, init_missing);
}

static inline bool
rtree_write(tsdn_t *tsdn, rtree_t *rtree, rtree_ctx_t *rtree_ctx, uintptr_t key,
    extent_t *extent, szind_t szind, bool slab) {
	/* Use rtree_clear() to set the extent to NULL. */
	assert(extent != NULL);

	rtree_leaf_elm_t *elm = rtree_leaf_elm_lookup(tsdn, rtree, rtree_ctx,
	    key, false, true);
	if (elm == NULL) {
		return true;
	}

	assert(rtree_leaf_elm_extent_read(tsdn, rtree, elm, false) == NULL);
	rtree_leaf_elm_write(tsdn, rtree, elm, extent, szind, slab);

	return false;
}

JEMALLOC_ALWAYS_INLINE rtree_leaf_elm_t *
rtree_read(tsdn_t *tsdn, rtree_t *rtree, rtree_ctx_t *rtree_ctx, uintptr_t key,
    bool dependent) {
	rtree_leaf_elm_t *elm = rtree_leaf_elm_lookup(tsdn, rtree, rtree_ctx,
	    key, dependent, false);
	if (!dependent && elm == NULL) {
		return NULL;
	}
	assert(elm != NULL);
	return elm;
}

JEMALLOC_ALWAYS_INLINE extent_t *
rtree_extent_read(tsdn_t *tsdn, rtree_t *rtree, rtree_ctx_t *rtree_ctx,
    uintptr_t key, bool dependent) {
	rtree_leaf_elm_t *elm = rtree_read(tsdn, rtree, rtree_ctx, key,
	    dependent);
	if (!dependent && elm == NULL) {
		return NULL;
	}
	return rtree_leaf_elm_extent_read(tsdn, rtree, elm, dependent);
}

JEMALLOC_ALWAYS_INLINE szind_t
rtree_szind_read(tsdn_t *tsdn, rtree_t *rtree, rtree_ctx_t *rtree_ctx,
    uintptr_t key, bool dependent) {
	rtree_leaf_elm_t *elm = rtree_read(tsdn, rtree, rtree_ctx, key,
	    dependent);
	if (!dependent && elm == NULL) {
		return NSIZES;
	}
	return rtree_leaf_elm_szind_read(tsdn, rtree, elm, dependent);
}

/*
 * rtree_slab_read() is intentionally omitted because slab is always read in
 * conjunction with szind, which makes rtree_szind_slab_read() a better choice.
 */

JEMALLOC_ALWAYS_INLINE bool
rtree_extent_szind_read(tsdn_t *tsdn, rtree_t *rtree, rtree_ctx_t *rtree_ctx,
    uintptr_t key, bool dependent, extent_t **r_extent, szind_t *r_szind) {
	rtree_leaf_elm_t *elm = rtree_read(tsdn, rtree, rtree_ctx, key,
	    dependent);
	if (!dependent && elm == NULL) {
		return true;
	}
	*r_extent = rtree_leaf_elm_extent_read(tsdn, rtree, elm, dependent);
	*r_szind = rtree_leaf_elm_szind_read(tsdn, rtree, elm, dependent);
	return false;
}

JEMALLOC_ALWAYS_INLINE bool
rtree_szind_slab_read(tsdn_t *tsdn, rtree_t *rtree, rtree_ctx_t *rtree_ctx,
    uintptr_t key, bool dependent, szind_t *r_szind, bool *r_slab) {
	rtree_leaf_elm_t *elm = rtree_read(tsdn, rtree, rtree_ctx, key,
	    dependent);
	if (!dependent && elm == NULL) {
		return true;
	}
#ifdef RTREE_LEAF_COMPACT
	uintptr_t bits = rtree_leaf_elm_bits_read(tsdn, rtree, elm, dependent);
	*r_szind = rtree_leaf_elm_bits_szind_get(bits);
	*r_slab = rtree_leaf_elm_bits_slab_get(bits);
#else
	*r_szind = rtree_leaf_elm_szind_read(tsdn, rtree, elm, dependent);
	*r_slab = rtree_leaf_elm_slab_read(tsdn, rtree, elm, dependent);
#endif
	return false;
}

static inline void
rtree_szind_slab_update(tsdn_t *tsdn, rtree_t *rtree, rtree_ctx_t *rtree_ctx,
    uintptr_t key, szind_t szind, bool slab) {
	assert(!slab || szind < NBINS);

	rtree_leaf_elm_t *elm = rtree_read(tsdn, rtree, rtree_ctx, key, true);
	rtree_leaf_elm_szind_slab_update(tsdn, rtree, elm, szind, slab);
}

static inline void
rtree_clear(tsdn_t *tsdn, rtree_t *rtree, rtree_ctx_t *rtree_ctx,
    uintptr_t key) {
	rtree_leaf_elm_t *elm = rtree_read(tsdn, rtree, rtree_ctx, key, true);
	assert(rtree_leaf_elm_extent_read(tsdn, rtree, elm, false) !=
	    NULL);
	rtree_leaf_elm_write(tsdn, rtree, elm, NULL, NSIZES, false);
}

#endif /* JEMALLOC_INTERNAL_RTREE_H */
