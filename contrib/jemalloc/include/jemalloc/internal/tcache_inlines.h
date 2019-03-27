#ifndef JEMALLOC_INTERNAL_TCACHE_INLINES_H
#define JEMALLOC_INTERNAL_TCACHE_INLINES_H

#include "jemalloc/internal/bin.h"
#include "jemalloc/internal/jemalloc_internal_types.h"
#include "jemalloc/internal/size_classes.h"
#include "jemalloc/internal/sz.h"
#include "jemalloc/internal/ticker.h"
#include "jemalloc/internal/util.h"

static inline bool
tcache_enabled_get(tsd_t *tsd) {
	return tsd_tcache_enabled_get(tsd);
}

static inline void
tcache_enabled_set(tsd_t *tsd, bool enabled) {
	bool was_enabled = tsd_tcache_enabled_get(tsd);

	if (!was_enabled && enabled) {
		tsd_tcache_data_init(tsd);
	} else if (was_enabled && !enabled) {
		tcache_cleanup(tsd);
	}
	/* Commit the state last.  Above calls check current state. */
	tsd_tcache_enabled_set(tsd, enabled);
	tsd_slow_update(tsd);
}

JEMALLOC_ALWAYS_INLINE void
tcache_event(tsd_t *tsd, tcache_t *tcache) {
	if (TCACHE_GC_INCR == 0) {
		return;
	}

	if (unlikely(ticker_tick(&tcache->gc_ticker))) {
		tcache_event_hard(tsd, tcache);
	}
}

JEMALLOC_ALWAYS_INLINE void *
tcache_alloc_small(tsd_t *tsd, arena_t *arena, tcache_t *tcache,
    UNUSED size_t size, szind_t binind, bool zero, bool slow_path) {
	void *ret;
	cache_bin_t *bin;
	bool tcache_success;
	size_t usize JEMALLOC_CC_SILENCE_INIT(0);

	assert(binind < NBINS);
	bin = tcache_small_bin_get(tcache, binind);
	ret = cache_bin_alloc_easy(bin, &tcache_success);
	assert(tcache_success == (ret != NULL));
	if (unlikely(!tcache_success)) {
		bool tcache_hard_success;
		arena = arena_choose(tsd, arena);
		if (unlikely(arena == NULL)) {
			return NULL;
		}

		ret = tcache_alloc_small_hard(tsd_tsdn(tsd), arena, tcache,
		    bin, binind, &tcache_hard_success);
		if (tcache_hard_success == false) {
			return NULL;
		}
	}

	assert(ret);
	/*
	 * Only compute usize if required.  The checks in the following if
	 * statement are all static.
	 */
	if (config_prof || (slow_path && config_fill) || unlikely(zero)) {
		usize = sz_index2size(binind);
		assert(tcache_salloc(tsd_tsdn(tsd), ret) == usize);
	}

	if (likely(!zero)) {
		if (slow_path && config_fill) {
			if (unlikely(opt_junk_alloc)) {
				arena_alloc_junk_small(ret, &bin_infos[binind],
				    false);
			} else if (unlikely(opt_zero)) {
				memset(ret, 0, usize);
			}
		}
	} else {
		if (slow_path && config_fill && unlikely(opt_junk_alloc)) {
			arena_alloc_junk_small(ret, &bin_infos[binind], true);
		}
		memset(ret, 0, usize);
	}

	if (config_stats) {
		bin->tstats.nrequests++;
	}
	if (config_prof) {
		tcache->prof_accumbytes += usize;
	}
	tcache_event(tsd, tcache);
	return ret;
}

JEMALLOC_ALWAYS_INLINE void *
tcache_alloc_large(tsd_t *tsd, arena_t *arena, tcache_t *tcache, size_t size,
    szind_t binind, bool zero, bool slow_path) {
	void *ret;
	cache_bin_t *bin;
	bool tcache_success;

	assert(binind >= NBINS &&binind < nhbins);
	bin = tcache_large_bin_get(tcache, binind);
	ret = cache_bin_alloc_easy(bin, &tcache_success);
	assert(tcache_success == (ret != NULL));
	if (unlikely(!tcache_success)) {
		/*
		 * Only allocate one large object at a time, because it's quite
		 * expensive to create one and not use it.
		 */
		arena = arena_choose(tsd, arena);
		if (unlikely(arena == NULL)) {
			return NULL;
		}

		ret = large_malloc(tsd_tsdn(tsd), arena, sz_s2u(size), zero);
		if (ret == NULL) {
			return NULL;
		}
	} else {
		size_t usize JEMALLOC_CC_SILENCE_INIT(0);

		/* Only compute usize on demand */
		if (config_prof || (slow_path && config_fill) ||
		    unlikely(zero)) {
			usize = sz_index2size(binind);
			assert(usize <= tcache_maxclass);
		}

		if (likely(!zero)) {
			if (slow_path && config_fill) {
				if (unlikely(opt_junk_alloc)) {
					memset(ret, JEMALLOC_ALLOC_JUNK,
					    usize);
				} else if (unlikely(opt_zero)) {
					memset(ret, 0, usize);
				}
			}
		} else {
			memset(ret, 0, usize);
		}

		if (config_stats) {
			bin->tstats.nrequests++;
		}
		if (config_prof) {
			tcache->prof_accumbytes += usize;
		}
	}

	tcache_event(tsd, tcache);
	return ret;
}

JEMALLOC_ALWAYS_INLINE void
tcache_dalloc_small(tsd_t *tsd, tcache_t *tcache, void *ptr, szind_t binind,
    bool slow_path) {
	cache_bin_t *bin;
	cache_bin_info_t *bin_info;

	assert(tcache_salloc(tsd_tsdn(tsd), ptr) <= SMALL_MAXCLASS);

	if (slow_path && config_fill && unlikely(opt_junk_free)) {
		arena_dalloc_junk_small(ptr, &bin_infos[binind]);
	}

	bin = tcache_small_bin_get(tcache, binind);
	bin_info = &tcache_bin_info[binind];
	if (unlikely(bin->ncached == bin_info->ncached_max)) {
		tcache_bin_flush_small(tsd, tcache, bin, binind,
		    (bin_info->ncached_max >> 1));
	}
	assert(bin->ncached < bin_info->ncached_max);
	bin->ncached++;
	*(bin->avail - bin->ncached) = ptr;

	tcache_event(tsd, tcache);
}

JEMALLOC_ALWAYS_INLINE void
tcache_dalloc_large(tsd_t *tsd, tcache_t *tcache, void *ptr, szind_t binind,
    bool slow_path) {
	cache_bin_t *bin;
	cache_bin_info_t *bin_info;

	assert(tcache_salloc(tsd_tsdn(tsd), ptr) > SMALL_MAXCLASS);
	assert(tcache_salloc(tsd_tsdn(tsd), ptr) <= tcache_maxclass);

	if (slow_path && config_fill && unlikely(opt_junk_free)) {
		large_dalloc_junk(ptr, sz_index2size(binind));
	}

	bin = tcache_large_bin_get(tcache, binind);
	bin_info = &tcache_bin_info[binind];
	if (unlikely(bin->ncached == bin_info->ncached_max)) {
		tcache_bin_flush_large(tsd, bin, binind,
		    (bin_info->ncached_max >> 1), tcache);
	}
	assert(bin->ncached < bin_info->ncached_max);
	bin->ncached++;
	*(bin->avail - bin->ncached) = ptr;

	tcache_event(tsd, tcache);
}

JEMALLOC_ALWAYS_INLINE tcache_t *
tcaches_get(tsd_t *tsd, unsigned ind) {
	tcaches_t *elm = &tcaches[ind];
	if (unlikely(elm->tcache == NULL)) {
		elm->tcache = tcache_create_explicit(tsd);
	}
	return elm->tcache;
}

#endif /* JEMALLOC_INTERNAL_TCACHE_INLINES_H */
