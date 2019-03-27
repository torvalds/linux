#define JEMALLOC_TCACHE_C_
#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/jemalloc_internal_includes.h"

#include "jemalloc/internal/assert.h"
#include "jemalloc/internal/mutex.h"
#include "jemalloc/internal/size_classes.h"

/******************************************************************************/
/* Data. */

bool	opt_tcache = true;
ssize_t	opt_lg_tcache_max = LG_TCACHE_MAXCLASS_DEFAULT;

cache_bin_info_t	*tcache_bin_info;
static unsigned		stack_nelms; /* Total stack elms per tcache. */

unsigned		nhbins;
size_t			tcache_maxclass;

tcaches_t		*tcaches;

/* Index of first element within tcaches that has never been used. */
static unsigned		tcaches_past;

/* Head of singly linked list tracking available tcaches elements. */
static tcaches_t	*tcaches_avail;

/* Protects tcaches{,_past,_avail}. */
static malloc_mutex_t	tcaches_mtx;

/******************************************************************************/

size_t
tcache_salloc(tsdn_t *tsdn, const void *ptr) {
	return arena_salloc(tsdn, ptr);
}

void
tcache_event_hard(tsd_t *tsd, tcache_t *tcache) {
	szind_t binind = tcache->next_gc_bin;

	cache_bin_t *tbin;
	if (binind < NBINS) {
		tbin = tcache_small_bin_get(tcache, binind);
	} else {
		tbin = tcache_large_bin_get(tcache, binind);
	}
	if (tbin->low_water > 0) {
		/*
		 * Flush (ceiling) 3/4 of the objects below the low water mark.
		 */
		if (binind < NBINS) {
			tcache_bin_flush_small(tsd, tcache, tbin, binind,
			    tbin->ncached - tbin->low_water + (tbin->low_water
			    >> 2));
			/*
			 * Reduce fill count by 2X.  Limit lg_fill_div such that
			 * the fill count is always at least 1.
			 */
			cache_bin_info_t *tbin_info = &tcache_bin_info[binind];
			if ((tbin_info->ncached_max >>
			     (tcache->lg_fill_div[binind] + 1)) >= 1) {
				tcache->lg_fill_div[binind]++;
			}
		} else {
			tcache_bin_flush_large(tsd, tbin, binind, tbin->ncached
			    - tbin->low_water + (tbin->low_water >> 2), tcache);
		}
	} else if (tbin->low_water < 0) {
		/*
		 * Increase fill count by 2X for small bins.  Make sure
		 * lg_fill_div stays greater than 0.
		 */
		if (binind < NBINS && tcache->lg_fill_div[binind] > 1) {
			tcache->lg_fill_div[binind]--;
		}
	}
	tbin->low_water = tbin->ncached;

	tcache->next_gc_bin++;
	if (tcache->next_gc_bin == nhbins) {
		tcache->next_gc_bin = 0;
	}
}

void *
tcache_alloc_small_hard(tsdn_t *tsdn, arena_t *arena, tcache_t *tcache,
    cache_bin_t *tbin, szind_t binind, bool *tcache_success) {
	void *ret;

	assert(tcache->arena != NULL);
	arena_tcache_fill_small(tsdn, arena, tcache, tbin, binind,
	    config_prof ? tcache->prof_accumbytes : 0);
	if (config_prof) {
		tcache->prof_accumbytes = 0;
	}
	ret = cache_bin_alloc_easy(tbin, tcache_success);

	return ret;
}

void
tcache_bin_flush_small(tsd_t *tsd, tcache_t *tcache, cache_bin_t *tbin,
    szind_t binind, unsigned rem) {
	bool merged_stats = false;

	assert(binind < NBINS);
	assert((cache_bin_sz_t)rem <= tbin->ncached);

	arena_t *arena = tcache->arena;
	assert(arena != NULL);
	unsigned nflush = tbin->ncached - rem;
	VARIABLE_ARRAY(extent_t *, item_extent, nflush);
	/* Look up extent once per item. */
	for (unsigned i = 0 ; i < nflush; i++) {
		item_extent[i] = iealloc(tsd_tsdn(tsd), *(tbin->avail - 1 - i));
	}

	while (nflush > 0) {
		/* Lock the arena bin associated with the first object. */
		extent_t *extent = item_extent[0];
		arena_t *bin_arena = extent_arena_get(extent);
		bin_t *bin = &bin_arena->bins[binind];

		if (config_prof && bin_arena == arena) {
			if (arena_prof_accum(tsd_tsdn(tsd), arena,
			    tcache->prof_accumbytes)) {
				prof_idump(tsd_tsdn(tsd));
			}
			tcache->prof_accumbytes = 0;
		}

		malloc_mutex_lock(tsd_tsdn(tsd), &bin->lock);
		if (config_stats && bin_arena == arena) {
			assert(!merged_stats);
			merged_stats = true;
			bin->stats.nflushes++;
			bin->stats.nrequests += tbin->tstats.nrequests;
			tbin->tstats.nrequests = 0;
		}
		unsigned ndeferred = 0;
		for (unsigned i = 0; i < nflush; i++) {
			void *ptr = *(tbin->avail - 1 - i);
			extent = item_extent[i];
			assert(ptr != NULL && extent != NULL);

			if (extent_arena_get(extent) == bin_arena) {
				arena_dalloc_bin_junked_locked(tsd_tsdn(tsd),
				    bin_arena, extent, ptr);
			} else {
				/*
				 * This object was allocated via a different
				 * arena bin than the one that is currently
				 * locked.  Stash the object, so that it can be
				 * handled in a future pass.
				 */
				*(tbin->avail - 1 - ndeferred) = ptr;
				item_extent[ndeferred] = extent;
				ndeferred++;
			}
		}
		malloc_mutex_unlock(tsd_tsdn(tsd), &bin->lock);
		arena_decay_ticks(tsd_tsdn(tsd), bin_arena, nflush - ndeferred);
		nflush = ndeferred;
	}
	if (config_stats && !merged_stats) {
		/*
		 * The flush loop didn't happen to flush to this thread's
		 * arena, so the stats didn't get merged.  Manually do so now.
		 */
		bin_t *bin = &arena->bins[binind];
		malloc_mutex_lock(tsd_tsdn(tsd), &bin->lock);
		bin->stats.nflushes++;
		bin->stats.nrequests += tbin->tstats.nrequests;
		tbin->tstats.nrequests = 0;
		malloc_mutex_unlock(tsd_tsdn(tsd), &bin->lock);
	}

	memmove(tbin->avail - rem, tbin->avail - tbin->ncached, rem *
	    sizeof(void *));
	tbin->ncached = rem;
	if (tbin->ncached < tbin->low_water) {
		tbin->low_water = tbin->ncached;
	}
}

void
tcache_bin_flush_large(tsd_t *tsd, cache_bin_t *tbin, szind_t binind,
    unsigned rem, tcache_t *tcache) {
	bool merged_stats = false;

	assert(binind < nhbins);
	assert((cache_bin_sz_t)rem <= tbin->ncached);

	arena_t *arena = tcache->arena;
	assert(arena != NULL);
	unsigned nflush = tbin->ncached - rem;
	VARIABLE_ARRAY(extent_t *, item_extent, nflush);
	/* Look up extent once per item. */
	for (unsigned i = 0 ; i < nflush; i++) {
		item_extent[i] = iealloc(tsd_tsdn(tsd), *(tbin->avail - 1 - i));
	}

	while (nflush > 0) {
		/* Lock the arena associated with the first object. */
		extent_t *extent = item_extent[0];
		arena_t *locked_arena = extent_arena_get(extent);
		UNUSED bool idump;

		if (config_prof) {
			idump = false;
		}

		malloc_mutex_lock(tsd_tsdn(tsd), &locked_arena->large_mtx);
		for (unsigned i = 0; i < nflush; i++) {
			void *ptr = *(tbin->avail - 1 - i);
			assert(ptr != NULL);
			extent = item_extent[i];
			if (extent_arena_get(extent) == locked_arena) {
				large_dalloc_prep_junked_locked(tsd_tsdn(tsd),
				    extent);
			}
		}
		if ((config_prof || config_stats) && locked_arena == arena) {
			if (config_prof) {
				idump = arena_prof_accum(tsd_tsdn(tsd), arena,
				    tcache->prof_accumbytes);
				tcache->prof_accumbytes = 0;
			}
			if (config_stats) {
				merged_stats = true;
				arena_stats_large_nrequests_add(tsd_tsdn(tsd),
				    &arena->stats, binind,
				    tbin->tstats.nrequests);
				tbin->tstats.nrequests = 0;
			}
		}
		malloc_mutex_unlock(tsd_tsdn(tsd), &locked_arena->large_mtx);

		unsigned ndeferred = 0;
		for (unsigned i = 0; i < nflush; i++) {
			void *ptr = *(tbin->avail - 1 - i);
			extent = item_extent[i];
			assert(ptr != NULL && extent != NULL);

			if (extent_arena_get(extent) == locked_arena) {
				large_dalloc_finish(tsd_tsdn(tsd), extent);
			} else {
				/*
				 * This object was allocated via a different
				 * arena than the one that is currently locked.
				 * Stash the object, so that it can be handled
				 * in a future pass.
				 */
				*(tbin->avail - 1 - ndeferred) = ptr;
				item_extent[ndeferred] = extent;
				ndeferred++;
			}
		}
		if (config_prof && idump) {
			prof_idump(tsd_tsdn(tsd));
		}
		arena_decay_ticks(tsd_tsdn(tsd), locked_arena, nflush -
		    ndeferred);
		nflush = ndeferred;
	}
	if (config_stats && !merged_stats) {
		/*
		 * The flush loop didn't happen to flush to this thread's
		 * arena, so the stats didn't get merged.  Manually do so now.
		 */
		arena_stats_large_nrequests_add(tsd_tsdn(tsd), &arena->stats,
		    binind, tbin->tstats.nrequests);
		tbin->tstats.nrequests = 0;
	}

	memmove(tbin->avail - rem, tbin->avail - tbin->ncached, rem *
	    sizeof(void *));
	tbin->ncached = rem;
	if (tbin->ncached < tbin->low_water) {
		tbin->low_water = tbin->ncached;
	}
}

void
tcache_arena_associate(tsdn_t *tsdn, tcache_t *tcache, arena_t *arena) {
	assert(tcache->arena == NULL);
	tcache->arena = arena;

	if (config_stats) {
		/* Link into list of extant tcaches. */
		malloc_mutex_lock(tsdn, &arena->tcache_ql_mtx);

		ql_elm_new(tcache, link);
		ql_tail_insert(&arena->tcache_ql, tcache, link);
		cache_bin_array_descriptor_init(
		    &tcache->cache_bin_array_descriptor, tcache->bins_small,
		    tcache->bins_large);
		ql_tail_insert(&arena->cache_bin_array_descriptor_ql,
		    &tcache->cache_bin_array_descriptor, link);

		malloc_mutex_unlock(tsdn, &arena->tcache_ql_mtx);
	}
}

static void
tcache_arena_dissociate(tsdn_t *tsdn, tcache_t *tcache) {
	arena_t *arena = tcache->arena;
	assert(arena != NULL);
	if (config_stats) {
		/* Unlink from list of extant tcaches. */
		malloc_mutex_lock(tsdn, &arena->tcache_ql_mtx);
		if (config_debug) {
			bool in_ql = false;
			tcache_t *iter;
			ql_foreach(iter, &arena->tcache_ql, link) {
				if (iter == tcache) {
					in_ql = true;
					break;
				}
			}
			assert(in_ql);
		}
		ql_remove(&arena->tcache_ql, tcache, link);
		ql_remove(&arena->cache_bin_array_descriptor_ql,
		    &tcache->cache_bin_array_descriptor, link);
		tcache_stats_merge(tsdn, tcache, arena);
		malloc_mutex_unlock(tsdn, &arena->tcache_ql_mtx);
	}
	tcache->arena = NULL;
}

void
tcache_arena_reassociate(tsdn_t *tsdn, tcache_t *tcache, arena_t *arena) {
	tcache_arena_dissociate(tsdn, tcache);
	tcache_arena_associate(tsdn, tcache, arena);
}

bool
tsd_tcache_enabled_data_init(tsd_t *tsd) {
	/* Called upon tsd initialization. */
	tsd_tcache_enabled_set(tsd, opt_tcache);
	tsd_slow_update(tsd);

	if (opt_tcache) {
		/* Trigger tcache init. */
		tsd_tcache_data_init(tsd);
	}

	return false;
}

/* Initialize auto tcache (embedded in TSD). */
static void
tcache_init(tsd_t *tsd, tcache_t *tcache, void *avail_stack) {
	memset(&tcache->link, 0, sizeof(ql_elm(tcache_t)));
	tcache->prof_accumbytes = 0;
	tcache->next_gc_bin = 0;
	tcache->arena = NULL;

	ticker_init(&tcache->gc_ticker, TCACHE_GC_INCR);

	size_t stack_offset = 0;
	assert((TCACHE_NSLOTS_SMALL_MAX & 1U) == 0);
	memset(tcache->bins_small, 0, sizeof(cache_bin_t) * NBINS);
	memset(tcache->bins_large, 0, sizeof(cache_bin_t) * (nhbins - NBINS));
	unsigned i = 0;
	for (; i < NBINS; i++) {
		tcache->lg_fill_div[i] = 1;
		stack_offset += tcache_bin_info[i].ncached_max * sizeof(void *);
		/*
		 * avail points past the available space.  Allocations will
		 * access the slots toward higher addresses (for the benefit of
		 * prefetch).
		 */
		tcache_small_bin_get(tcache, i)->avail =
		    (void **)((uintptr_t)avail_stack + (uintptr_t)stack_offset);
	}
	for (; i < nhbins; i++) {
		stack_offset += tcache_bin_info[i].ncached_max * sizeof(void *);
		tcache_large_bin_get(tcache, i)->avail =
		    (void **)((uintptr_t)avail_stack + (uintptr_t)stack_offset);
	}
	assert(stack_offset == stack_nelms * sizeof(void *));
}

/* Initialize auto tcache (embedded in TSD). */
bool
tsd_tcache_data_init(tsd_t *tsd) {
	tcache_t *tcache = tsd_tcachep_get_unsafe(tsd);
	assert(tcache_small_bin_get(tcache, 0)->avail == NULL);
	size_t size = stack_nelms * sizeof(void *);
	/* Avoid false cacheline sharing. */
	size = sz_sa2u(size, CACHELINE);

	void *avail_array = ipallocztm(tsd_tsdn(tsd), size, CACHELINE, true,
	    NULL, true, arena_get(TSDN_NULL, 0, true));
	if (avail_array == NULL) {
		return true;
	}

	tcache_init(tsd, tcache, avail_array);
	/*
	 * Initialization is a bit tricky here.  After malloc init is done, all
	 * threads can rely on arena_choose and associate tcache accordingly.
	 * However, the thread that does actual malloc bootstrapping relies on
	 * functional tsd, and it can only rely on a0.  In that case, we
	 * associate its tcache to a0 temporarily, and later on
	 * arena_choose_hard() will re-associate properly.
	 */
	tcache->arena = NULL;
	arena_t *arena;
	if (!malloc_initialized()) {
		/* If in initialization, assign to a0. */
		arena = arena_get(tsd_tsdn(tsd), 0, false);
		tcache_arena_associate(tsd_tsdn(tsd), tcache, arena);
	} else {
		arena = arena_choose(tsd, NULL);
		/* This may happen if thread.tcache.enabled is used. */
		if (tcache->arena == NULL) {
			tcache_arena_associate(tsd_tsdn(tsd), tcache, arena);
		}
	}
	assert(arena == tcache->arena);

	return false;
}

/* Created manual tcache for tcache.create mallctl. */
tcache_t *
tcache_create_explicit(tsd_t *tsd) {
	tcache_t *tcache;
	size_t size, stack_offset;

	size = sizeof(tcache_t);
	/* Naturally align the pointer stacks. */
	size = PTR_CEILING(size);
	stack_offset = size;
	size += stack_nelms * sizeof(void *);
	/* Avoid false cacheline sharing. */
	size = sz_sa2u(size, CACHELINE);

	tcache = ipallocztm(tsd_tsdn(tsd), size, CACHELINE, true, NULL, true,
	    arena_get(TSDN_NULL, 0, true));
	if (tcache == NULL) {
		return NULL;
	}

	tcache_init(tsd, tcache,
	    (void *)((uintptr_t)tcache + (uintptr_t)stack_offset));
	tcache_arena_associate(tsd_tsdn(tsd), tcache, arena_ichoose(tsd, NULL));

	return tcache;
}

static void
tcache_flush_cache(tsd_t *tsd, tcache_t *tcache) {
	assert(tcache->arena != NULL);

	for (unsigned i = 0; i < NBINS; i++) {
		cache_bin_t *tbin = tcache_small_bin_get(tcache, i);
		tcache_bin_flush_small(tsd, tcache, tbin, i, 0);

		if (config_stats) {
			assert(tbin->tstats.nrequests == 0);
		}
	}
	for (unsigned i = NBINS; i < nhbins; i++) {
		cache_bin_t *tbin = tcache_large_bin_get(tcache, i);
		tcache_bin_flush_large(tsd, tbin, i, 0, tcache);

		if (config_stats) {
			assert(tbin->tstats.nrequests == 0);
		}
	}

	if (config_prof && tcache->prof_accumbytes > 0 &&
	    arena_prof_accum(tsd_tsdn(tsd), tcache->arena,
	    tcache->prof_accumbytes)) {
		prof_idump(tsd_tsdn(tsd));
	}
}

void
tcache_flush(tsd_t *tsd) {
	assert(tcache_available(tsd));
	tcache_flush_cache(tsd, tsd_tcachep_get(tsd));
}

static void
tcache_destroy(tsd_t *tsd, tcache_t *tcache, bool tsd_tcache) {
	tcache_flush_cache(tsd, tcache);
	tcache_arena_dissociate(tsd_tsdn(tsd), tcache);

	if (tsd_tcache) {
		/* Release the avail array for the TSD embedded auto tcache. */
		void *avail_array =
		    (void *)((uintptr_t)tcache_small_bin_get(tcache, 0)->avail -
		    (uintptr_t)tcache_bin_info[0].ncached_max * sizeof(void *));
		idalloctm(tsd_tsdn(tsd), avail_array, NULL, NULL, true, true);
	} else {
		/* Release both the tcache struct and avail array. */
		idalloctm(tsd_tsdn(tsd), tcache, NULL, NULL, true, true);
	}
}

/* For auto tcache (embedded in TSD) only. */
void
tcache_cleanup(tsd_t *tsd) {
	tcache_t *tcache = tsd_tcachep_get(tsd);
	if (!tcache_available(tsd)) {
		assert(tsd_tcache_enabled_get(tsd) == false);
		if (config_debug) {
			assert(tcache_small_bin_get(tcache, 0)->avail == NULL);
		}
		return;
	}
	assert(tsd_tcache_enabled_get(tsd));
	assert(tcache_small_bin_get(tcache, 0)->avail != NULL);

	tcache_destroy(tsd, tcache, true);
	if (config_debug) {
		tcache_small_bin_get(tcache, 0)->avail = NULL;
	}
}

void
tcache_stats_merge(tsdn_t *tsdn, tcache_t *tcache, arena_t *arena) {
	unsigned i;

	cassert(config_stats);

	/* Merge and reset tcache stats. */
	for (i = 0; i < NBINS; i++) {
		bin_t *bin = &arena->bins[i];
		cache_bin_t *tbin = tcache_small_bin_get(tcache, i);
		malloc_mutex_lock(tsdn, &bin->lock);
		bin->stats.nrequests += tbin->tstats.nrequests;
		malloc_mutex_unlock(tsdn, &bin->lock);
		tbin->tstats.nrequests = 0;
	}

	for (; i < nhbins; i++) {
		cache_bin_t *tbin = tcache_large_bin_get(tcache, i);
		arena_stats_large_nrequests_add(tsdn, &arena->stats, i,
		    tbin->tstats.nrequests);
		tbin->tstats.nrequests = 0;
	}
}

static bool
tcaches_create_prep(tsd_t *tsd) {
	bool err;

	malloc_mutex_lock(tsd_tsdn(tsd), &tcaches_mtx);

	if (tcaches == NULL) {
		tcaches = base_alloc(tsd_tsdn(tsd), b0get(), sizeof(tcache_t *)
		    * (MALLOCX_TCACHE_MAX+1), CACHELINE);
		if (tcaches == NULL) {
			err = true;
			goto label_return;
		}
	}

	if (tcaches_avail == NULL && tcaches_past > MALLOCX_TCACHE_MAX) {
		err = true;
		goto label_return;
	}

	err = false;
label_return:
	malloc_mutex_unlock(tsd_tsdn(tsd), &tcaches_mtx);
	return err;
}

bool
tcaches_create(tsd_t *tsd, unsigned *r_ind) {
	witness_assert_depth(tsdn_witness_tsdp_get(tsd_tsdn(tsd)), 0);

	bool err;

	if (tcaches_create_prep(tsd)) {
		err = true;
		goto label_return;
	}

	tcache_t *tcache = tcache_create_explicit(tsd);
	if (tcache == NULL) {
		err = true;
		goto label_return;
	}

	tcaches_t *elm;
	malloc_mutex_lock(tsd_tsdn(tsd), &tcaches_mtx);
	if (tcaches_avail != NULL) {
		elm = tcaches_avail;
		tcaches_avail = tcaches_avail->next;
		elm->tcache = tcache;
		*r_ind = (unsigned)(elm - tcaches);
	} else {
		elm = &tcaches[tcaches_past];
		elm->tcache = tcache;
		*r_ind = tcaches_past;
		tcaches_past++;
	}
	malloc_mutex_unlock(tsd_tsdn(tsd), &tcaches_mtx);

	err = false;
label_return:
	witness_assert_depth(tsdn_witness_tsdp_get(tsd_tsdn(tsd)), 0);
	return err;
}

static tcache_t *
tcaches_elm_remove(tsd_t *tsd, tcaches_t *elm) {
	malloc_mutex_assert_owner(tsd_tsdn(tsd), &tcaches_mtx);

	if (elm->tcache == NULL) {
		return NULL;
	}
	tcache_t *tcache = elm->tcache;
	elm->tcache = NULL;
	return tcache;
}

void
tcaches_flush(tsd_t *tsd, unsigned ind) {
	malloc_mutex_lock(tsd_tsdn(tsd), &tcaches_mtx);
	tcache_t *tcache = tcaches_elm_remove(tsd, &tcaches[ind]);
	malloc_mutex_unlock(tsd_tsdn(tsd), &tcaches_mtx);
	if (tcache != NULL) {
		tcache_destroy(tsd, tcache, false);
	}
}

void
tcaches_destroy(tsd_t *tsd, unsigned ind) {
	malloc_mutex_lock(tsd_tsdn(tsd), &tcaches_mtx);
	tcaches_t *elm = &tcaches[ind];
	tcache_t *tcache = tcaches_elm_remove(tsd, elm);
	elm->next = tcaches_avail;
	tcaches_avail = elm;
	malloc_mutex_unlock(tsd_tsdn(tsd), &tcaches_mtx);
	if (tcache != NULL) {
		tcache_destroy(tsd, tcache, false);
	}
}

bool
tcache_boot(tsdn_t *tsdn) {
	/* If necessary, clamp opt_lg_tcache_max. */
	if (opt_lg_tcache_max < 0 || (ZU(1) << opt_lg_tcache_max) <
	    SMALL_MAXCLASS) {
		tcache_maxclass = SMALL_MAXCLASS;
	} else {
		tcache_maxclass = (ZU(1) << opt_lg_tcache_max);
	}

	if (malloc_mutex_init(&tcaches_mtx, "tcaches", WITNESS_RANK_TCACHES,
	    malloc_mutex_rank_exclusive)) {
		return true;
	}

	nhbins = sz_size2index(tcache_maxclass) + 1;

	/* Initialize tcache_bin_info. */
	tcache_bin_info = (cache_bin_info_t *)base_alloc(tsdn, b0get(), nhbins
	    * sizeof(cache_bin_info_t), CACHELINE);
	if (tcache_bin_info == NULL) {
		return true;
	}
	stack_nelms = 0;
	unsigned i;
	for (i = 0; i < NBINS; i++) {
		if ((bin_infos[i].nregs << 1) <= TCACHE_NSLOTS_SMALL_MIN) {
			tcache_bin_info[i].ncached_max =
			    TCACHE_NSLOTS_SMALL_MIN;
		} else if ((bin_infos[i].nregs << 1) <=
		    TCACHE_NSLOTS_SMALL_MAX) {
			tcache_bin_info[i].ncached_max =
			    (bin_infos[i].nregs << 1);
		} else {
			tcache_bin_info[i].ncached_max =
			    TCACHE_NSLOTS_SMALL_MAX;
		}
		stack_nelms += tcache_bin_info[i].ncached_max;
	}
	for (; i < nhbins; i++) {
		tcache_bin_info[i].ncached_max = TCACHE_NSLOTS_LARGE;
		stack_nelms += tcache_bin_info[i].ncached_max;
	}

	return false;
}

void
tcache_prefork(tsdn_t *tsdn) {
	if (!config_prof && opt_tcache) {
		malloc_mutex_prefork(tsdn, &tcaches_mtx);
	}
}

void
tcache_postfork_parent(tsdn_t *tsdn) {
	if (!config_prof && opt_tcache) {
		malloc_mutex_postfork_parent(tsdn, &tcaches_mtx);
	}
}

void
tcache_postfork_child(tsdn_t *tsdn) {
	if (!config_prof && opt_tcache) {
		malloc_mutex_postfork_child(tsdn, &tcaches_mtx);
	}
}
