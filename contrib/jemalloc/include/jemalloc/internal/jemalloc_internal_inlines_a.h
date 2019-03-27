#ifndef JEMALLOC_INTERNAL_INLINES_A_H
#define JEMALLOC_INTERNAL_INLINES_A_H

#include "jemalloc/internal/atomic.h"
#include "jemalloc/internal/bit_util.h"
#include "jemalloc/internal/jemalloc_internal_types.h"
#include "jemalloc/internal/size_classes.h"
#include "jemalloc/internal/ticker.h"

JEMALLOC_ALWAYS_INLINE malloc_cpuid_t
malloc_getcpu(void) {
	assert(have_percpu_arena);
#if defined(JEMALLOC_HAVE_SCHED_GETCPU)
	return (malloc_cpuid_t)sched_getcpu();
#else
	not_reached();
	return -1;
#endif
}

/* Return the chosen arena index based on current cpu. */
JEMALLOC_ALWAYS_INLINE unsigned
percpu_arena_choose(void) {
	assert(have_percpu_arena && PERCPU_ARENA_ENABLED(opt_percpu_arena));

	malloc_cpuid_t cpuid = malloc_getcpu();
	assert(cpuid >= 0);

	unsigned arena_ind;
	if ((opt_percpu_arena == percpu_arena) || ((unsigned)cpuid < ncpus /
	    2)) {
		arena_ind = cpuid;
	} else {
		assert(opt_percpu_arena == per_phycpu_arena);
		/* Hyper threads on the same physical CPU share arena. */
		arena_ind = cpuid - ncpus / 2;
	}

	return arena_ind;
}

/* Return the limit of percpu auto arena range, i.e. arenas[0...ind_limit). */
JEMALLOC_ALWAYS_INLINE unsigned
percpu_arena_ind_limit(percpu_arena_mode_t mode) {
	assert(have_percpu_arena && PERCPU_ARENA_ENABLED(mode));
	if (mode == per_phycpu_arena && ncpus > 1) {
		if (ncpus % 2) {
			/* This likely means a misconfig. */
			return ncpus / 2 + 1;
		}
		return ncpus / 2;
	} else {
		return ncpus;
	}
}

static inline arena_tdata_t *
arena_tdata_get(tsd_t *tsd, unsigned ind, bool refresh_if_missing) {
	arena_tdata_t *tdata;
	arena_tdata_t *arenas_tdata = tsd_arenas_tdata_get(tsd);

	if (unlikely(arenas_tdata == NULL)) {
		/* arenas_tdata hasn't been initialized yet. */
		return arena_tdata_get_hard(tsd, ind);
	}
	if (unlikely(ind >= tsd_narenas_tdata_get(tsd))) {
		/*
		 * ind is invalid, cache is old (too small), or tdata to be
		 * initialized.
		 */
		return (refresh_if_missing ? arena_tdata_get_hard(tsd, ind) :
		    NULL);
	}

	tdata = &arenas_tdata[ind];
	if (likely(tdata != NULL) || !refresh_if_missing) {
		return tdata;
	}
	return arena_tdata_get_hard(tsd, ind);
}

static inline arena_t *
arena_get(tsdn_t *tsdn, unsigned ind, bool init_if_missing) {
	arena_t *ret;

	assert(ind < MALLOCX_ARENA_LIMIT);

	ret = (arena_t *)atomic_load_p(&arenas[ind], ATOMIC_ACQUIRE);
	if (unlikely(ret == NULL)) {
		if (init_if_missing) {
			ret = arena_init(tsdn, ind,
			    (extent_hooks_t *)&extent_hooks_default);
		}
	}
	return ret;
}

static inline ticker_t *
decay_ticker_get(tsd_t *tsd, unsigned ind) {
	arena_tdata_t *tdata;

	tdata = arena_tdata_get(tsd, ind, true);
	if (unlikely(tdata == NULL)) {
		return NULL;
	}
	return &tdata->decay_ticker;
}

JEMALLOC_ALWAYS_INLINE cache_bin_t *
tcache_small_bin_get(tcache_t *tcache, szind_t binind) {
	assert(binind < NBINS);
	return &tcache->bins_small[binind];
}

JEMALLOC_ALWAYS_INLINE cache_bin_t *
tcache_large_bin_get(tcache_t *tcache, szind_t binind) {
	assert(binind >= NBINS &&binind < nhbins);
	return &tcache->bins_large[binind - NBINS];
}

JEMALLOC_ALWAYS_INLINE bool
tcache_available(tsd_t *tsd) {
	/*
	 * Thread specific auto tcache might be unavailable if: 1) during tcache
	 * initialization, or 2) disabled through thread.tcache.enabled mallctl
	 * or config options.  This check covers all cases.
	 */
	if (likely(tsd_tcache_enabled_get(tsd))) {
		/* Associated arena == NULL implies tcache init in progress. */
		assert(tsd_tcachep_get(tsd)->arena == NULL ||
		    tcache_small_bin_get(tsd_tcachep_get(tsd), 0)->avail !=
		    NULL);
		return true;
	}

	return false;
}

JEMALLOC_ALWAYS_INLINE tcache_t *
tcache_get(tsd_t *tsd) {
	if (!tcache_available(tsd)) {
		return NULL;
	}

	return tsd_tcachep_get(tsd);
}

static inline void
pre_reentrancy(tsd_t *tsd, arena_t *arena) {
	/* arena is the current context.  Reentry from a0 is not allowed. */
	assert(arena != arena_get(tsd_tsdn(tsd), 0, false));

	bool fast = tsd_fast(tsd);
	assert(tsd_reentrancy_level_get(tsd) < INT8_MAX);
	++*tsd_reentrancy_levelp_get(tsd);
	if (fast) {
		/* Prepare slow path for reentrancy. */
		tsd_slow_update(tsd);
		assert(tsd->state == tsd_state_nominal_slow);
	}
}

static inline void
post_reentrancy(tsd_t *tsd) {
	int8_t *reentrancy_level = tsd_reentrancy_levelp_get(tsd);
	assert(*reentrancy_level > 0);
	if (--*reentrancy_level == 0) {
		tsd_slow_update(tsd);
	}
}

#endif /* JEMALLOC_INTERNAL_INLINES_A_H */
