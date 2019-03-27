#ifndef JEMALLOC_INTERNAL_ARENA_STATS_H
#define JEMALLOC_INTERNAL_ARENA_STATS_H

#include "jemalloc/internal/atomic.h"
#include "jemalloc/internal/mutex.h"
#include "jemalloc/internal/mutex_prof.h"
#include "jemalloc/internal/size_classes.h"

/*
 * In those architectures that support 64-bit atomics, we use atomic updates for
 * our 64-bit values.  Otherwise, we use a plain uint64_t and synchronize
 * externally.
 */
#ifdef JEMALLOC_ATOMIC_U64
typedef atomic_u64_t arena_stats_u64_t;
#else
/* Must hold the arena stats mutex while reading atomically. */
typedef uint64_t arena_stats_u64_t;
#endif

typedef struct arena_stats_large_s arena_stats_large_t;
struct arena_stats_large_s {
	/*
	 * Total number of allocation/deallocation requests served directly by
	 * the arena.
	 */
	arena_stats_u64_t	nmalloc;
	arena_stats_u64_t	ndalloc;

	/*
	 * Number of allocation requests that correspond to this size class.
	 * This includes requests served by tcache, though tcache only
	 * periodically merges into this counter.
	 */
	arena_stats_u64_t	nrequests; /* Partially derived. */

	/* Current number of allocations of this size class. */
	size_t		curlextents; /* Derived. */
};

typedef struct arena_stats_decay_s arena_stats_decay_t;
struct arena_stats_decay_s {
	/* Total number of purge sweeps. */
	arena_stats_u64_t	npurge;
	/* Total number of madvise calls made. */
	arena_stats_u64_t	nmadvise;
	/* Total number of pages purged. */
	arena_stats_u64_t	purged;
};

/*
 * Arena stats.  Note that fields marked "derived" are not directly maintained
 * within the arena code; rather their values are derived during stats merge
 * requests.
 */
typedef struct arena_stats_s arena_stats_t;
struct arena_stats_s {
#ifndef JEMALLOC_ATOMIC_U64
	malloc_mutex_t		mtx;
#endif

	/* Number of bytes currently mapped, excluding retained memory. */
	atomic_zu_t		mapped; /* Partially derived. */

	/*
	 * Number of unused virtual memory bytes currently retained.  Retained
	 * bytes are technically mapped (though always decommitted or purged),
	 * but they are excluded from the mapped statistic (above).
	 */
	atomic_zu_t		retained; /* Derived. */

	arena_stats_decay_t	decay_dirty;
	arena_stats_decay_t	decay_muzzy;

	atomic_zu_t		base; /* Derived. */
	atomic_zu_t		internal;
	atomic_zu_t		resident; /* Derived. */
	atomic_zu_t		metadata_thp;

	atomic_zu_t		allocated_large; /* Derived. */
	arena_stats_u64_t	nmalloc_large; /* Derived. */
	arena_stats_u64_t	ndalloc_large; /* Derived. */
	arena_stats_u64_t	nrequests_large; /* Derived. */

	/* Number of bytes cached in tcache associated with this arena. */
	atomic_zu_t		tcache_bytes; /* Derived. */

	mutex_prof_data_t mutex_prof_data[mutex_prof_num_arena_mutexes];

	/* One element for each large size class. */
	arena_stats_large_t	lstats[NSIZES - NBINS];

	/* Arena uptime. */
	nstime_t		uptime;
};

static inline bool
arena_stats_init(UNUSED tsdn_t *tsdn, arena_stats_t *arena_stats) {
	if (config_debug) {
		for (size_t i = 0; i < sizeof(arena_stats_t); i++) {
			assert(((char *)arena_stats)[i] == 0);
		}
	}
#ifndef JEMALLOC_ATOMIC_U64
	if (malloc_mutex_init(&arena_stats->mtx, "arena_stats",
	    WITNESS_RANK_ARENA_STATS, malloc_mutex_rank_exclusive)) {
		return true;
	}
#endif
	/* Memory is zeroed, so there is no need to clear stats. */
	return false;
}

static inline void
arena_stats_lock(tsdn_t *tsdn, arena_stats_t *arena_stats) {
#ifndef JEMALLOC_ATOMIC_U64
	malloc_mutex_lock(tsdn, &arena_stats->mtx);
#endif
}

static inline void
arena_stats_unlock(tsdn_t *tsdn, arena_stats_t *arena_stats) {
#ifndef JEMALLOC_ATOMIC_U64
	malloc_mutex_unlock(tsdn, &arena_stats->mtx);
#endif
}

static inline uint64_t
arena_stats_read_u64(tsdn_t *tsdn, arena_stats_t *arena_stats,
    arena_stats_u64_t *p) {
#ifdef JEMALLOC_ATOMIC_U64
	return atomic_load_u64(p, ATOMIC_RELAXED);
#else
	malloc_mutex_assert_owner(tsdn, &arena_stats->mtx);
	return *p;
#endif
}

static inline void
arena_stats_add_u64(tsdn_t *tsdn, arena_stats_t *arena_stats,
    arena_stats_u64_t *p, uint64_t x) {
#ifdef JEMALLOC_ATOMIC_U64
	atomic_fetch_add_u64(p, x, ATOMIC_RELAXED);
#else
	malloc_mutex_assert_owner(tsdn, &arena_stats->mtx);
	*p += x;
#endif
}

UNUSED static inline void
arena_stats_sub_u64(tsdn_t *tsdn, arena_stats_t *arena_stats,
    arena_stats_u64_t *p, uint64_t x) {
#ifdef JEMALLOC_ATOMIC_U64
	UNUSED uint64_t r = atomic_fetch_sub_u64(p, x, ATOMIC_RELAXED);
	assert(r - x <= r);
#else
	malloc_mutex_assert_owner(tsdn, &arena_stats->mtx);
	*p -= x;
	assert(*p + x >= *p);
#endif
}

/*
 * Non-atomically sets *dst += src.  *dst needs external synchronization.
 * This lets us avoid the cost of a fetch_add when its unnecessary (note that
 * the types here are atomic).
 */
static inline void
arena_stats_accum_u64(arena_stats_u64_t *dst, uint64_t src) {
#ifdef JEMALLOC_ATOMIC_U64
	uint64_t cur_dst = atomic_load_u64(dst, ATOMIC_RELAXED);
	atomic_store_u64(dst, src + cur_dst, ATOMIC_RELAXED);
#else
	*dst += src;
#endif
}

static inline size_t
arena_stats_read_zu(tsdn_t *tsdn, arena_stats_t *arena_stats, atomic_zu_t *p) {
#ifdef JEMALLOC_ATOMIC_U64
	return atomic_load_zu(p, ATOMIC_RELAXED);
#else
	malloc_mutex_assert_owner(tsdn, &arena_stats->mtx);
	return atomic_load_zu(p, ATOMIC_RELAXED);
#endif
}

static inline void
arena_stats_add_zu(tsdn_t *tsdn, arena_stats_t *arena_stats, atomic_zu_t *p,
    size_t x) {
#ifdef JEMALLOC_ATOMIC_U64
	atomic_fetch_add_zu(p, x, ATOMIC_RELAXED);
#else
	malloc_mutex_assert_owner(tsdn, &arena_stats->mtx);
	size_t cur = atomic_load_zu(p, ATOMIC_RELAXED);
	atomic_store_zu(p, cur + x, ATOMIC_RELAXED);
#endif
}

static inline void
arena_stats_sub_zu(tsdn_t *tsdn, arena_stats_t *arena_stats, atomic_zu_t *p,
    size_t x) {
#ifdef JEMALLOC_ATOMIC_U64
	UNUSED size_t r = atomic_fetch_sub_zu(p, x, ATOMIC_RELAXED);
	assert(r - x <= r);
#else
	malloc_mutex_assert_owner(tsdn, &arena_stats->mtx);
	size_t cur = atomic_load_zu(p, ATOMIC_RELAXED);
	atomic_store_zu(p, cur - x, ATOMIC_RELAXED);
#endif
}

/* Like the _u64 variant, needs an externally synchronized *dst. */
static inline void
arena_stats_accum_zu(atomic_zu_t *dst, size_t src) {
	size_t cur_dst = atomic_load_zu(dst, ATOMIC_RELAXED);
	atomic_store_zu(dst, src + cur_dst, ATOMIC_RELAXED);
}

static inline void
arena_stats_large_nrequests_add(tsdn_t *tsdn, arena_stats_t *arena_stats,
    szind_t szind, uint64_t nrequests) {
	arena_stats_lock(tsdn, arena_stats);
	arena_stats_add_u64(tsdn, arena_stats, &arena_stats->lstats[szind -
	    NBINS].nrequests, nrequests);
	arena_stats_unlock(tsdn, arena_stats);
}

static inline void
arena_stats_mapped_add(tsdn_t *tsdn, arena_stats_t *arena_stats, size_t size) {
	arena_stats_lock(tsdn, arena_stats);
	arena_stats_add_zu(tsdn, arena_stats, &arena_stats->mapped, size);
	arena_stats_unlock(tsdn, arena_stats);
}


#endif /* JEMALLOC_INTERNAL_ARENA_STATS_H */
