#ifndef JEMALLOC_INTERNAL_ARENA_STRUCTS_B_H
#define JEMALLOC_INTERNAL_ARENA_STRUCTS_B_H

#include "jemalloc/internal/arena_stats.h"
#include "jemalloc/internal/atomic.h"
#include "jemalloc/internal/bin.h"
#include "jemalloc/internal/bitmap.h"
#include "jemalloc/internal/extent_dss.h"
#include "jemalloc/internal/jemalloc_internal_types.h"
#include "jemalloc/internal/mutex.h"
#include "jemalloc/internal/nstime.h"
#include "jemalloc/internal/ql.h"
#include "jemalloc/internal/size_classes.h"
#include "jemalloc/internal/smoothstep.h"
#include "jemalloc/internal/ticker.h"

struct arena_decay_s {
	/* Synchronizes all non-atomic fields. */
	malloc_mutex_t		mtx;
	/*
	 * True if a thread is currently purging the extents associated with
	 * this decay structure.
	 */
	bool			purging;
	/*
	 * Approximate time in milliseconds from the creation of a set of unused
	 * dirty pages until an equivalent set of unused dirty pages is purged
	 * and/or reused.
	 */
	atomic_zd_t		time_ms;
	/* time / SMOOTHSTEP_NSTEPS. */
	nstime_t		interval;
	/*
	 * Time at which the current decay interval logically started.  We do
	 * not actually advance to a new epoch until sometime after it starts
	 * because of scheduling and computation delays, and it is even possible
	 * to completely skip epochs.  In all cases, during epoch advancement we
	 * merge all relevant activity into the most recently recorded epoch.
	 */
	nstime_t		epoch;
	/* Deadline randomness generator. */
	uint64_t		jitter_state;
	/*
	 * Deadline for current epoch.  This is the sum of interval and per
	 * epoch jitter which is a uniform random variable in [0..interval).
	 * Epochs always advance by precise multiples of interval, but we
	 * randomize the deadline to reduce the likelihood of arenas purging in
	 * lockstep.
	 */
	nstime_t		deadline;
	/*
	 * Number of unpurged pages at beginning of current epoch.  During epoch
	 * advancement we use the delta between arena->decay_*.nunpurged and
	 * extents_npages_get(&arena->extents_*) to determine how many dirty
	 * pages, if any, were generated.
	 */
	size_t			nunpurged;
	/*
	 * Trailing log of how many unused dirty pages were generated during
	 * each of the past SMOOTHSTEP_NSTEPS decay epochs, where the last
	 * element is the most recent epoch.  Corresponding epoch times are
	 * relative to epoch.
	 */
	size_t			backlog[SMOOTHSTEP_NSTEPS];

	/*
	 * Pointer to associated stats.  These stats are embedded directly in
	 * the arena's stats due to how stats structures are shared between the
	 * arena and ctl code.
	 *
	 * Synchronization: Same as associated arena's stats field. */
	arena_stats_decay_t	*stats;
	/* Peak number of pages in associated extents.  Used for debug only. */
	uint64_t		ceil_npages;
};

struct arena_s {
	/*
	 * Number of threads currently assigned to this arena.  Each thread has
	 * two distinct assignments, one for application-serving allocation, and
	 * the other for internal metadata allocation.  Internal metadata must
	 * not be allocated from arenas explicitly created via the arenas.create
	 * mallctl, because the arena.<i>.reset mallctl indiscriminately
	 * discards all allocations for the affected arena.
	 *
	 *   0: Application allocation.
	 *   1: Internal metadata allocation.
	 *
	 * Synchronization: atomic.
	 */
	atomic_u_t		nthreads[2];

	/*
	 * When percpu_arena is enabled, to amortize the cost of reading /
	 * updating the current CPU id, track the most recent thread accessing
	 * this arena, and only read CPU if there is a mismatch.
	 */
	tsdn_t		*last_thd;

	/* Synchronization: internal. */
	arena_stats_t		stats;

	/*
	 * Lists of tcaches and cache_bin_array_descriptors for extant threads
	 * associated with this arena.  Stats from these are merged
	 * incrementally, and at exit if opt_stats_print is enabled.
	 *
	 * Synchronization: tcache_ql_mtx.
	 */
	ql_head(tcache_t)			tcache_ql;
	ql_head(cache_bin_array_descriptor_t)	cache_bin_array_descriptor_ql;
	malloc_mutex_t				tcache_ql_mtx;

	/* Synchronization: internal. */
	prof_accum_t		prof_accum;
	uint64_t		prof_accumbytes;

	/*
	 * PRNG state for cache index randomization of large allocation base
	 * pointers.
	 *
	 * Synchronization: atomic.
	 */
	atomic_zu_t		offset_state;

	/*
	 * Extent serial number generator state.
	 *
	 * Synchronization: atomic.
	 */
	atomic_zu_t		extent_sn_next;

	/*
	 * Represents a dss_prec_t, but atomically.
	 *
	 * Synchronization: atomic.
	 */
	atomic_u_t		dss_prec;

	/*
	 * Number of pages in active extents.
	 *
	 * Synchronization: atomic.
	 */
	atomic_zu_t		nactive;

	/*
	 * Extant large allocations.
	 *
	 * Synchronization: large_mtx.
	 */
	extent_list_t		large;
	/* Synchronizes all large allocation/update/deallocation. */
	malloc_mutex_t		large_mtx;

	/*
	 * Collections of extents that were previously allocated.  These are
	 * used when allocating extents, in an attempt to re-use address space.
	 *
	 * Synchronization: internal.
	 */
	extents_t		extents_dirty;
	extents_t		extents_muzzy;
	extents_t		extents_retained;

	/*
	 * Decay-based purging state, responsible for scheduling extent state
	 * transitions.
	 *
	 * Synchronization: internal.
	 */
	arena_decay_t		decay_dirty; /* dirty --> muzzy */
	arena_decay_t		decay_muzzy; /* muzzy --> retained */

	/*
	 * Next extent size class in a growing series to use when satisfying a
	 * request via the extent hooks (only if opt_retain).  This limits the
	 * number of disjoint virtual memory ranges so that extent merging can
	 * be effective even if multiple arenas' extent allocation requests are
	 * highly interleaved.
	 *
	 * retain_grow_limit is the max allowed size ind to expand (unless the
	 * required size is greater).  Default is no limit, and controlled
	 * through mallctl only.
	 *
	 * Synchronization: extent_grow_mtx
	 */
	pszind_t		extent_grow_next;
	pszind_t		retain_grow_limit;
	malloc_mutex_t		extent_grow_mtx;

	/*
	 * Available extent structures that were allocated via
	 * base_alloc_extent().
	 *
	 * Synchronization: extent_avail_mtx.
	 */
	extent_tree_t		extent_avail;
	malloc_mutex_t		extent_avail_mtx;

	/*
	 * bins is used to store heaps of free regions.
	 *
	 * Synchronization: internal.
	 */
	bin_t			bins[NBINS];

	/*
	 * Base allocator, from which arena metadata are allocated.
	 *
	 * Synchronization: internal.
	 */
	base_t			*base;
	/* Used to determine uptime.  Read-only after initialization. */
	nstime_t		create_time;
};

/* Used in conjunction with tsd for fast arena-related context lookup. */
struct arena_tdata_s {
	ticker_t		decay_ticker;
};

/* Used to pass rtree lookup context down the path. */
struct alloc_ctx_s {
	szind_t szind;
	bool slab;
};

#endif /* JEMALLOC_INTERNAL_ARENA_STRUCTS_B_H */
