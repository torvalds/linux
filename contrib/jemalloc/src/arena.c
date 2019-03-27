#define JEMALLOC_ARENA_C_
#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/jemalloc_internal_includes.h"

#include "jemalloc/internal/assert.h"
#include "jemalloc/internal/div.h"
#include "jemalloc/internal/extent_dss.h"
#include "jemalloc/internal/extent_mmap.h"
#include "jemalloc/internal/mutex.h"
#include "jemalloc/internal/rtree.h"
#include "jemalloc/internal/size_classes.h"
#include "jemalloc/internal/util.h"

/******************************************************************************/
/* Data. */

/*
 * Define names for both unininitialized and initialized phases, so that
 * options and mallctl processing are straightforward.
 */
const char *percpu_arena_mode_names[] = {
	"percpu",
	"phycpu",
	"disabled",
	"percpu",
	"phycpu"
};
percpu_arena_mode_t opt_percpu_arena = PERCPU_ARENA_DEFAULT;

ssize_t opt_dirty_decay_ms = DIRTY_DECAY_MS_DEFAULT;
ssize_t opt_muzzy_decay_ms = MUZZY_DECAY_MS_DEFAULT;

static atomic_zd_t dirty_decay_ms_default;
static atomic_zd_t muzzy_decay_ms_default;

const uint64_t h_steps[SMOOTHSTEP_NSTEPS] = {
#define STEP(step, h, x, y)			\
		h,
		SMOOTHSTEP
#undef STEP
};

static div_info_t arena_binind_div_info[NBINS];

/******************************************************************************/
/*
 * Function prototypes for static functions that are referenced prior to
 * definition.
 */

static void arena_decay_to_limit(tsdn_t *tsdn, arena_t *arena,
    arena_decay_t *decay, extents_t *extents, bool all, size_t npages_limit,
    size_t npages_decay_max, bool is_background_thread);
static bool arena_decay_dirty(tsdn_t *tsdn, arena_t *arena,
    bool is_background_thread, bool all);
static void arena_dalloc_bin_slab(tsdn_t *tsdn, arena_t *arena, extent_t *slab,
    bin_t *bin);
static void arena_bin_lower_slab(tsdn_t *tsdn, arena_t *arena, extent_t *slab,
    bin_t *bin);

/******************************************************************************/

void
arena_basic_stats_merge(UNUSED tsdn_t *tsdn, arena_t *arena, unsigned *nthreads,
    const char **dss, ssize_t *dirty_decay_ms, ssize_t *muzzy_decay_ms,
    size_t *nactive, size_t *ndirty, size_t *nmuzzy) {
	*nthreads += arena_nthreads_get(arena, false);
	*dss = dss_prec_names[arena_dss_prec_get(arena)];
	*dirty_decay_ms = arena_dirty_decay_ms_get(arena);
	*muzzy_decay_ms = arena_muzzy_decay_ms_get(arena);
	*nactive += atomic_load_zu(&arena->nactive, ATOMIC_RELAXED);
	*ndirty += extents_npages_get(&arena->extents_dirty);
	*nmuzzy += extents_npages_get(&arena->extents_muzzy);
}

void
arena_stats_merge(tsdn_t *tsdn, arena_t *arena, unsigned *nthreads,
    const char **dss, ssize_t *dirty_decay_ms, ssize_t *muzzy_decay_ms,
    size_t *nactive, size_t *ndirty, size_t *nmuzzy, arena_stats_t *astats,
    bin_stats_t *bstats, arena_stats_large_t *lstats) {
	cassert(config_stats);

	arena_basic_stats_merge(tsdn, arena, nthreads, dss, dirty_decay_ms,
	    muzzy_decay_ms, nactive, ndirty, nmuzzy);

	size_t base_allocated, base_resident, base_mapped, metadata_thp;
	base_stats_get(tsdn, arena->base, &base_allocated, &base_resident,
	    &base_mapped, &metadata_thp);

	arena_stats_lock(tsdn, &arena->stats);

	arena_stats_accum_zu(&astats->mapped, base_mapped
	    + arena_stats_read_zu(tsdn, &arena->stats, &arena->stats.mapped));
	arena_stats_accum_zu(&astats->retained,
	    extents_npages_get(&arena->extents_retained) << LG_PAGE);

	arena_stats_accum_u64(&astats->decay_dirty.npurge,
	    arena_stats_read_u64(tsdn, &arena->stats,
	    &arena->stats.decay_dirty.npurge));
	arena_stats_accum_u64(&astats->decay_dirty.nmadvise,
	    arena_stats_read_u64(tsdn, &arena->stats,
	    &arena->stats.decay_dirty.nmadvise));
	arena_stats_accum_u64(&astats->decay_dirty.purged,
	    arena_stats_read_u64(tsdn, &arena->stats,
	    &arena->stats.decay_dirty.purged));

	arena_stats_accum_u64(&astats->decay_muzzy.npurge,
	    arena_stats_read_u64(tsdn, &arena->stats,
	    &arena->stats.decay_muzzy.npurge));
	arena_stats_accum_u64(&astats->decay_muzzy.nmadvise,
	    arena_stats_read_u64(tsdn, &arena->stats,
	    &arena->stats.decay_muzzy.nmadvise));
	arena_stats_accum_u64(&astats->decay_muzzy.purged,
	    arena_stats_read_u64(tsdn, &arena->stats,
	    &arena->stats.decay_muzzy.purged));

	arena_stats_accum_zu(&astats->base, base_allocated);
	arena_stats_accum_zu(&astats->internal, arena_internal_get(arena));
	arena_stats_accum_zu(&astats->metadata_thp, metadata_thp);
	arena_stats_accum_zu(&astats->resident, base_resident +
	    (((atomic_load_zu(&arena->nactive, ATOMIC_RELAXED) +
	    extents_npages_get(&arena->extents_dirty) +
	    extents_npages_get(&arena->extents_muzzy)) << LG_PAGE)));

	for (szind_t i = 0; i < NSIZES - NBINS; i++) {
		uint64_t nmalloc = arena_stats_read_u64(tsdn, &arena->stats,
		    &arena->stats.lstats[i].nmalloc);
		arena_stats_accum_u64(&lstats[i].nmalloc, nmalloc);
		arena_stats_accum_u64(&astats->nmalloc_large, nmalloc);

		uint64_t ndalloc = arena_stats_read_u64(tsdn, &arena->stats,
		    &arena->stats.lstats[i].ndalloc);
		arena_stats_accum_u64(&lstats[i].ndalloc, ndalloc);
		arena_stats_accum_u64(&astats->ndalloc_large, ndalloc);

		uint64_t nrequests = arena_stats_read_u64(tsdn, &arena->stats,
		    &arena->stats.lstats[i].nrequests);
		arena_stats_accum_u64(&lstats[i].nrequests,
		    nmalloc + nrequests);
		arena_stats_accum_u64(&astats->nrequests_large,
		    nmalloc + nrequests);

		assert(nmalloc >= ndalloc);
		assert(nmalloc - ndalloc <= SIZE_T_MAX);
		size_t curlextents = (size_t)(nmalloc - ndalloc);
		lstats[i].curlextents += curlextents;
		arena_stats_accum_zu(&astats->allocated_large,
		    curlextents * sz_index2size(NBINS + i));
	}

	arena_stats_unlock(tsdn, &arena->stats);

	/* tcache_bytes counts currently cached bytes. */
	atomic_store_zu(&astats->tcache_bytes, 0, ATOMIC_RELAXED);
	malloc_mutex_lock(tsdn, &arena->tcache_ql_mtx);
	cache_bin_array_descriptor_t *descriptor;
	ql_foreach(descriptor, &arena->cache_bin_array_descriptor_ql, link) {
		szind_t i = 0;
		for (; i < NBINS; i++) {
			cache_bin_t *tbin = &descriptor->bins_small[i];
			arena_stats_accum_zu(&astats->tcache_bytes,
			    tbin->ncached * sz_index2size(i));
		}
		for (; i < nhbins; i++) {
			cache_bin_t *tbin = &descriptor->bins_large[i];
			arena_stats_accum_zu(&astats->tcache_bytes,
			    tbin->ncached * sz_index2size(i));
		}
	}
	malloc_mutex_prof_read(tsdn,
	    &astats->mutex_prof_data[arena_prof_mutex_tcache_list],
	    &arena->tcache_ql_mtx);
	malloc_mutex_unlock(tsdn, &arena->tcache_ql_mtx);

#define READ_ARENA_MUTEX_PROF_DATA(mtx, ind)				\
    malloc_mutex_lock(tsdn, &arena->mtx);				\
    malloc_mutex_prof_read(tsdn, &astats->mutex_prof_data[ind],		\
        &arena->mtx);							\
    malloc_mutex_unlock(tsdn, &arena->mtx);

	/* Gather per arena mutex profiling data. */
	READ_ARENA_MUTEX_PROF_DATA(large_mtx, arena_prof_mutex_large);
	READ_ARENA_MUTEX_PROF_DATA(extent_avail_mtx,
	    arena_prof_mutex_extent_avail)
	READ_ARENA_MUTEX_PROF_DATA(extents_dirty.mtx,
	    arena_prof_mutex_extents_dirty)
	READ_ARENA_MUTEX_PROF_DATA(extents_muzzy.mtx,
	    arena_prof_mutex_extents_muzzy)
	READ_ARENA_MUTEX_PROF_DATA(extents_retained.mtx,
	    arena_prof_mutex_extents_retained)
	READ_ARENA_MUTEX_PROF_DATA(decay_dirty.mtx,
	    arena_prof_mutex_decay_dirty)
	READ_ARENA_MUTEX_PROF_DATA(decay_muzzy.mtx,
	    arena_prof_mutex_decay_muzzy)
	READ_ARENA_MUTEX_PROF_DATA(base->mtx,
	    arena_prof_mutex_base)
#undef READ_ARENA_MUTEX_PROF_DATA

	nstime_copy(&astats->uptime, &arena->create_time);
	nstime_update(&astats->uptime);
	nstime_subtract(&astats->uptime, &arena->create_time);

	for (szind_t i = 0; i < NBINS; i++) {
		bin_stats_merge(tsdn, &bstats[i], &arena->bins[i]);
	}
}

void
arena_extents_dirty_dalloc(tsdn_t *tsdn, arena_t *arena,
    extent_hooks_t **r_extent_hooks, extent_t *extent) {
	witness_assert_depth_to_rank(tsdn_witness_tsdp_get(tsdn),
	    WITNESS_RANK_CORE, 0);

	extents_dalloc(tsdn, arena, r_extent_hooks, &arena->extents_dirty,
	    extent);
	if (arena_dirty_decay_ms_get(arena) == 0) {
		arena_decay_dirty(tsdn, arena, false, true);
	} else {
		arena_background_thread_inactivity_check(tsdn, arena, false);
	}
}

static void *
arena_slab_reg_alloc(extent_t *slab, const bin_info_t *bin_info) {
	void *ret;
	arena_slab_data_t *slab_data = extent_slab_data_get(slab);
	size_t regind;

	assert(extent_nfree_get(slab) > 0);
	assert(!bitmap_full(slab_data->bitmap, &bin_info->bitmap_info));

	regind = bitmap_sfu(slab_data->bitmap, &bin_info->bitmap_info);
	ret = (void *)((uintptr_t)extent_addr_get(slab) +
	    (uintptr_t)(bin_info->reg_size * regind));
	extent_nfree_dec(slab);
	return ret;
}

#ifndef JEMALLOC_JET
static
#endif
size_t
arena_slab_regind(extent_t *slab, szind_t binind, const void *ptr) {
	size_t diff, regind;

	/* Freeing a pointer outside the slab can cause assertion failure. */
	assert((uintptr_t)ptr >= (uintptr_t)extent_addr_get(slab));
	assert((uintptr_t)ptr < (uintptr_t)extent_past_get(slab));
	/* Freeing an interior pointer can cause assertion failure. */
	assert(((uintptr_t)ptr - (uintptr_t)extent_addr_get(slab)) %
	    (uintptr_t)bin_infos[binind].reg_size == 0);

	diff = (size_t)((uintptr_t)ptr - (uintptr_t)extent_addr_get(slab));

	/* Avoid doing division with a variable divisor. */
	regind = div_compute(&arena_binind_div_info[binind], diff);

	assert(regind < bin_infos[binind].nregs);

	return regind;
}

static void
arena_slab_reg_dalloc(extent_t *slab, arena_slab_data_t *slab_data, void *ptr) {
	szind_t binind = extent_szind_get(slab);
	const bin_info_t *bin_info = &bin_infos[binind];
	size_t regind = arena_slab_regind(slab, binind, ptr);

	assert(extent_nfree_get(slab) < bin_info->nregs);
	/* Freeing an unallocated pointer can cause assertion failure. */
	assert(bitmap_get(slab_data->bitmap, &bin_info->bitmap_info, regind));

	bitmap_unset(slab_data->bitmap, &bin_info->bitmap_info, regind);
	extent_nfree_inc(slab);
}

static void
arena_nactive_add(arena_t *arena, size_t add_pages) {
	atomic_fetch_add_zu(&arena->nactive, add_pages, ATOMIC_RELAXED);
}

static void
arena_nactive_sub(arena_t *arena, size_t sub_pages) {
	assert(atomic_load_zu(&arena->nactive, ATOMIC_RELAXED) >= sub_pages);
	atomic_fetch_sub_zu(&arena->nactive, sub_pages, ATOMIC_RELAXED);
}

static void
arena_large_malloc_stats_update(tsdn_t *tsdn, arena_t *arena, size_t usize) {
	szind_t index, hindex;

	cassert(config_stats);

	if (usize < LARGE_MINCLASS) {
		usize = LARGE_MINCLASS;
	}
	index = sz_size2index(usize);
	hindex = (index >= NBINS) ? index - NBINS : 0;

	arena_stats_add_u64(tsdn, &arena->stats,
	    &arena->stats.lstats[hindex].nmalloc, 1);
}

static void
arena_large_dalloc_stats_update(tsdn_t *tsdn, arena_t *arena, size_t usize) {
	szind_t index, hindex;

	cassert(config_stats);

	if (usize < LARGE_MINCLASS) {
		usize = LARGE_MINCLASS;
	}
	index = sz_size2index(usize);
	hindex = (index >= NBINS) ? index - NBINS : 0;

	arena_stats_add_u64(tsdn, &arena->stats,
	    &arena->stats.lstats[hindex].ndalloc, 1);
}

static void
arena_large_ralloc_stats_update(tsdn_t *tsdn, arena_t *arena, size_t oldusize,
    size_t usize) {
	arena_large_dalloc_stats_update(tsdn, arena, oldusize);
	arena_large_malloc_stats_update(tsdn, arena, usize);
}

extent_t *
arena_extent_alloc_large(tsdn_t *tsdn, arena_t *arena, size_t usize,
    size_t alignment, bool *zero) {
	extent_hooks_t *extent_hooks = EXTENT_HOOKS_INITIALIZER;

	witness_assert_depth_to_rank(tsdn_witness_tsdp_get(tsdn),
	    WITNESS_RANK_CORE, 0);

	szind_t szind = sz_size2index(usize);
	size_t mapped_add;
	bool commit = true;
	extent_t *extent = extents_alloc(tsdn, arena, &extent_hooks,
	    &arena->extents_dirty, NULL, usize, sz_large_pad, alignment, false,
	    szind, zero, &commit);
	if (extent == NULL) {
		extent = extents_alloc(tsdn, arena, &extent_hooks,
		    &arena->extents_muzzy, NULL, usize, sz_large_pad, alignment,
		    false, szind, zero, &commit);
	}
	size_t size = usize + sz_large_pad;
	if (extent == NULL) {
		extent = extent_alloc_wrapper(tsdn, arena, &extent_hooks, NULL,
		    usize, sz_large_pad, alignment, false, szind, zero,
		    &commit);
		if (config_stats) {
			/*
			 * extent may be NULL on OOM, but in that case
			 * mapped_add isn't used below, so there's no need to
			 * conditionlly set it to 0 here.
			 */
			mapped_add = size;
		}
	} else if (config_stats) {
		mapped_add = 0;
	}

	if (extent != NULL) {
		if (config_stats) {
			arena_stats_lock(tsdn, &arena->stats);
			arena_large_malloc_stats_update(tsdn, arena, usize);
			if (mapped_add != 0) {
				arena_stats_add_zu(tsdn, &arena->stats,
				    &arena->stats.mapped, mapped_add);
			}
			arena_stats_unlock(tsdn, &arena->stats);
		}
		arena_nactive_add(arena, size >> LG_PAGE);
	}

	return extent;
}

void
arena_extent_dalloc_large_prep(tsdn_t *tsdn, arena_t *arena, extent_t *extent) {
	if (config_stats) {
		arena_stats_lock(tsdn, &arena->stats);
		arena_large_dalloc_stats_update(tsdn, arena,
		    extent_usize_get(extent));
		arena_stats_unlock(tsdn, &arena->stats);
	}
	arena_nactive_sub(arena, extent_size_get(extent) >> LG_PAGE);
}

void
arena_extent_ralloc_large_shrink(tsdn_t *tsdn, arena_t *arena, extent_t *extent,
    size_t oldusize) {
	size_t usize = extent_usize_get(extent);
	size_t udiff = oldusize - usize;

	if (config_stats) {
		arena_stats_lock(tsdn, &arena->stats);
		arena_large_ralloc_stats_update(tsdn, arena, oldusize, usize);
		arena_stats_unlock(tsdn, &arena->stats);
	}
	arena_nactive_sub(arena, udiff >> LG_PAGE);
}

void
arena_extent_ralloc_large_expand(tsdn_t *tsdn, arena_t *arena, extent_t *extent,
    size_t oldusize) {
	size_t usize = extent_usize_get(extent);
	size_t udiff = usize - oldusize;

	if (config_stats) {
		arena_stats_lock(tsdn, &arena->stats);
		arena_large_ralloc_stats_update(tsdn, arena, oldusize, usize);
		arena_stats_unlock(tsdn, &arena->stats);
	}
	arena_nactive_add(arena, udiff >> LG_PAGE);
}

static ssize_t
arena_decay_ms_read(arena_decay_t *decay) {
	return atomic_load_zd(&decay->time_ms, ATOMIC_RELAXED);
}

static void
arena_decay_ms_write(arena_decay_t *decay, ssize_t decay_ms) {
	atomic_store_zd(&decay->time_ms, decay_ms, ATOMIC_RELAXED);
}

static void
arena_decay_deadline_init(arena_decay_t *decay) {
	/*
	 * Generate a new deadline that is uniformly random within the next
	 * epoch after the current one.
	 */
	nstime_copy(&decay->deadline, &decay->epoch);
	nstime_add(&decay->deadline, &decay->interval);
	if (arena_decay_ms_read(decay) > 0) {
		nstime_t jitter;

		nstime_init(&jitter, prng_range_u64(&decay->jitter_state,
		    nstime_ns(&decay->interval)));
		nstime_add(&decay->deadline, &jitter);
	}
}

static bool
arena_decay_deadline_reached(const arena_decay_t *decay, const nstime_t *time) {
	return (nstime_compare(&decay->deadline, time) <= 0);
}

static size_t
arena_decay_backlog_npages_limit(const arena_decay_t *decay) {
	uint64_t sum;
	size_t npages_limit_backlog;
	unsigned i;

	/*
	 * For each element of decay_backlog, multiply by the corresponding
	 * fixed-point smoothstep decay factor.  Sum the products, then divide
	 * to round down to the nearest whole number of pages.
	 */
	sum = 0;
	for (i = 0; i < SMOOTHSTEP_NSTEPS; i++) {
		sum += decay->backlog[i] * h_steps[i];
	}
	npages_limit_backlog = (size_t)(sum >> SMOOTHSTEP_BFP);

	return npages_limit_backlog;
}

static void
arena_decay_backlog_update_last(arena_decay_t *decay, size_t current_npages) {
	size_t npages_delta = (current_npages > decay->nunpurged) ?
	    current_npages - decay->nunpurged : 0;
	decay->backlog[SMOOTHSTEP_NSTEPS-1] = npages_delta;

	if (config_debug) {
		if (current_npages > decay->ceil_npages) {
			decay->ceil_npages = current_npages;
		}
		size_t npages_limit = arena_decay_backlog_npages_limit(decay);
		assert(decay->ceil_npages >= npages_limit);
		if (decay->ceil_npages > npages_limit) {
			decay->ceil_npages = npages_limit;
		}
	}
}

static void
arena_decay_backlog_update(arena_decay_t *decay, uint64_t nadvance_u64,
    size_t current_npages) {
	if (nadvance_u64 >= SMOOTHSTEP_NSTEPS) {
		memset(decay->backlog, 0, (SMOOTHSTEP_NSTEPS-1) *
		    sizeof(size_t));
	} else {
		size_t nadvance_z = (size_t)nadvance_u64;

		assert((uint64_t)nadvance_z == nadvance_u64);

		memmove(decay->backlog, &decay->backlog[nadvance_z],
		    (SMOOTHSTEP_NSTEPS - nadvance_z) * sizeof(size_t));
		if (nadvance_z > 1) {
			memset(&decay->backlog[SMOOTHSTEP_NSTEPS -
			    nadvance_z], 0, (nadvance_z-1) * sizeof(size_t));
		}
	}

	arena_decay_backlog_update_last(decay, current_npages);
}

static void
arena_decay_try_purge(tsdn_t *tsdn, arena_t *arena, arena_decay_t *decay,
    extents_t *extents, size_t current_npages, size_t npages_limit,
    bool is_background_thread) {
	if (current_npages > npages_limit) {
		arena_decay_to_limit(tsdn, arena, decay, extents, false,
		    npages_limit, current_npages - npages_limit,
		    is_background_thread);
	}
}

static void
arena_decay_epoch_advance_helper(arena_decay_t *decay, const nstime_t *time,
    size_t current_npages) {
	assert(arena_decay_deadline_reached(decay, time));

	nstime_t delta;
	nstime_copy(&delta, time);
	nstime_subtract(&delta, &decay->epoch);

	uint64_t nadvance_u64 = nstime_divide(&delta, &decay->interval);
	assert(nadvance_u64 > 0);

	/* Add nadvance_u64 decay intervals to epoch. */
	nstime_copy(&delta, &decay->interval);
	nstime_imultiply(&delta, nadvance_u64);
	nstime_add(&decay->epoch, &delta);

	/* Set a new deadline. */
	arena_decay_deadline_init(decay);

	/* Update the backlog. */
	arena_decay_backlog_update(decay, nadvance_u64, current_npages);
}

static void
arena_decay_epoch_advance(tsdn_t *tsdn, arena_t *arena, arena_decay_t *decay,
    extents_t *extents, const nstime_t *time, bool is_background_thread) {
	size_t current_npages = extents_npages_get(extents);
	arena_decay_epoch_advance_helper(decay, time, current_npages);

	size_t npages_limit = arena_decay_backlog_npages_limit(decay);
	/* We may unlock decay->mtx when try_purge(). Finish logging first. */
	decay->nunpurged = (npages_limit > current_npages) ? npages_limit :
	    current_npages;

	if (!background_thread_enabled() || is_background_thread) {
		arena_decay_try_purge(tsdn, arena, decay, extents,
		    current_npages, npages_limit, is_background_thread);
	}
}

static void
arena_decay_reinit(arena_decay_t *decay, ssize_t decay_ms) {
	arena_decay_ms_write(decay, decay_ms);
	if (decay_ms > 0) {
		nstime_init(&decay->interval, (uint64_t)decay_ms *
		    KQU(1000000));
		nstime_idivide(&decay->interval, SMOOTHSTEP_NSTEPS);
	}

	nstime_init(&decay->epoch, 0);
	nstime_update(&decay->epoch);
	decay->jitter_state = (uint64_t)(uintptr_t)decay;
	arena_decay_deadline_init(decay);
	decay->nunpurged = 0;
	memset(decay->backlog, 0, SMOOTHSTEP_NSTEPS * sizeof(size_t));
}

static bool
arena_decay_init(arena_decay_t *decay, ssize_t decay_ms,
    arena_stats_decay_t *stats) {
	if (config_debug) {
		for (size_t i = 0; i < sizeof(arena_decay_t); i++) {
			assert(((char *)decay)[i] == 0);
		}
		decay->ceil_npages = 0;
	}
	if (malloc_mutex_init(&decay->mtx, "decay", WITNESS_RANK_DECAY,
	    malloc_mutex_rank_exclusive)) {
		return true;
	}
	decay->purging = false;
	arena_decay_reinit(decay, decay_ms);
	/* Memory is zeroed, so there is no need to clear stats. */
	if (config_stats) {
		decay->stats = stats;
	}
	return false;
}

static bool
arena_decay_ms_valid(ssize_t decay_ms) {
	if (decay_ms < -1) {
		return false;
	}
	if (decay_ms == -1 || (uint64_t)decay_ms <= NSTIME_SEC_MAX *
	    KQU(1000)) {
		return true;
	}
	return false;
}

static bool
arena_maybe_decay(tsdn_t *tsdn, arena_t *arena, arena_decay_t *decay,
    extents_t *extents, bool is_background_thread) {
	malloc_mutex_assert_owner(tsdn, &decay->mtx);

	/* Purge all or nothing if the option is disabled. */
	ssize_t decay_ms = arena_decay_ms_read(decay);
	if (decay_ms <= 0) {
		if (decay_ms == 0) {
			arena_decay_to_limit(tsdn, arena, decay, extents, false,
			    0, extents_npages_get(extents),
			    is_background_thread);
		}
		return false;
	}

	nstime_t time;
	nstime_init(&time, 0);
	nstime_update(&time);
	if (unlikely(!nstime_monotonic() && nstime_compare(&decay->epoch, &time)
	    > 0)) {
		/*
		 * Time went backwards.  Move the epoch back in time and
		 * generate a new deadline, with the expectation that time
		 * typically flows forward for long enough periods of time that
		 * epochs complete.  Unfortunately, this strategy is susceptible
		 * to clock jitter triggering premature epoch advances, but
		 * clock jitter estimation and compensation isn't feasible here
		 * because calls into this code are event-driven.
		 */
		nstime_copy(&decay->epoch, &time);
		arena_decay_deadline_init(decay);
	} else {
		/* Verify that time does not go backwards. */
		assert(nstime_compare(&decay->epoch, &time) <= 0);
	}

	/*
	 * If the deadline has been reached, advance to the current epoch and
	 * purge to the new limit if necessary.  Note that dirty pages created
	 * during the current epoch are not subject to purge until a future
	 * epoch, so as a result purging only happens during epoch advances, or
	 * being triggered by background threads (scheduled event).
	 */
	bool advance_epoch = arena_decay_deadline_reached(decay, &time);
	if (advance_epoch) {
		arena_decay_epoch_advance(tsdn, arena, decay, extents, &time,
		    is_background_thread);
	} else if (is_background_thread) {
		arena_decay_try_purge(tsdn, arena, decay, extents,
		    extents_npages_get(extents),
		    arena_decay_backlog_npages_limit(decay),
		    is_background_thread);
	}

	return advance_epoch;
}

static ssize_t
arena_decay_ms_get(arena_decay_t *decay) {
	return arena_decay_ms_read(decay);
}

ssize_t
arena_dirty_decay_ms_get(arena_t *arena) {
	return arena_decay_ms_get(&arena->decay_dirty);
}

ssize_t
arena_muzzy_decay_ms_get(arena_t *arena) {
	return arena_decay_ms_get(&arena->decay_muzzy);
}

static bool
arena_decay_ms_set(tsdn_t *tsdn, arena_t *arena, arena_decay_t *decay,
    extents_t *extents, ssize_t decay_ms) {
	if (!arena_decay_ms_valid(decay_ms)) {
		return true;
	}

	malloc_mutex_lock(tsdn, &decay->mtx);
	/*
	 * Restart decay backlog from scratch, which may cause many dirty pages
	 * to be immediately purged.  It would conceptually be possible to map
	 * the old backlog onto the new backlog, but there is no justification
	 * for such complexity since decay_ms changes are intended to be
	 * infrequent, either between the {-1, 0, >0} states, or a one-time
	 * arbitrary change during initial arena configuration.
	 */
	arena_decay_reinit(decay, decay_ms);
	arena_maybe_decay(tsdn, arena, decay, extents, false);
	malloc_mutex_unlock(tsdn, &decay->mtx);

	return false;
}

bool
arena_dirty_decay_ms_set(tsdn_t *tsdn, arena_t *arena,
    ssize_t decay_ms) {
	return arena_decay_ms_set(tsdn, arena, &arena->decay_dirty,
	    &arena->extents_dirty, decay_ms);
}

bool
arena_muzzy_decay_ms_set(tsdn_t *tsdn, arena_t *arena,
    ssize_t decay_ms) {
	return arena_decay_ms_set(tsdn, arena, &arena->decay_muzzy,
	    &arena->extents_muzzy, decay_ms);
}

static size_t
arena_stash_decayed(tsdn_t *tsdn, arena_t *arena,
    extent_hooks_t **r_extent_hooks, extents_t *extents, size_t npages_limit,
	size_t npages_decay_max, extent_list_t *decay_extents) {
	witness_assert_depth_to_rank(tsdn_witness_tsdp_get(tsdn),
	    WITNESS_RANK_CORE, 0);

	/* Stash extents according to npages_limit. */
	size_t nstashed = 0;
	extent_t *extent;
	while (nstashed < npages_decay_max &&
	    (extent = extents_evict(tsdn, arena, r_extent_hooks, extents,
	    npages_limit)) != NULL) {
		extent_list_append(decay_extents, extent);
		nstashed += extent_size_get(extent) >> LG_PAGE;
	}
	return nstashed;
}

static size_t
arena_decay_stashed(tsdn_t *tsdn, arena_t *arena,
    extent_hooks_t **r_extent_hooks, arena_decay_t *decay, extents_t *extents,
    bool all, extent_list_t *decay_extents, bool is_background_thread) {
	UNUSED size_t nmadvise, nunmapped;
	size_t npurged;

	if (config_stats) {
		nmadvise = 0;
		nunmapped = 0;
	}
	npurged = 0;

	ssize_t muzzy_decay_ms = arena_muzzy_decay_ms_get(arena);
	for (extent_t *extent = extent_list_first(decay_extents); extent !=
	    NULL; extent = extent_list_first(decay_extents)) {
		if (config_stats) {
			nmadvise++;
		}
		size_t npages = extent_size_get(extent) >> LG_PAGE;
		npurged += npages;
		extent_list_remove(decay_extents, extent);
		switch (extents_state_get(extents)) {
		case extent_state_active:
			not_reached();
		case extent_state_dirty:
			if (!all && muzzy_decay_ms != 0 &&
			    !extent_purge_lazy_wrapper(tsdn, arena,
			    r_extent_hooks, extent, 0,
			    extent_size_get(extent))) {
				extents_dalloc(tsdn, arena, r_extent_hooks,
				    &arena->extents_muzzy, extent);
				arena_background_thread_inactivity_check(tsdn,
				    arena, is_background_thread);
				break;
			}
			/* Fall through. */
		case extent_state_muzzy:
			extent_dalloc_wrapper(tsdn, arena, r_extent_hooks,
			    extent);
			if (config_stats) {
				nunmapped += npages;
			}
			break;
		case extent_state_retained:
		default:
			not_reached();
		}
	}

	if (config_stats) {
		arena_stats_lock(tsdn, &arena->stats);
		arena_stats_add_u64(tsdn, &arena->stats, &decay->stats->npurge,
		    1);
		arena_stats_add_u64(tsdn, &arena->stats,
		    &decay->stats->nmadvise, nmadvise);
		arena_stats_add_u64(tsdn, &arena->stats, &decay->stats->purged,
		    npurged);
		arena_stats_sub_zu(tsdn, &arena->stats, &arena->stats.mapped,
		    nunmapped << LG_PAGE);
		arena_stats_unlock(tsdn, &arena->stats);
	}

	return npurged;
}

/*
 * npages_limit: Decay at most npages_decay_max pages without violating the
 * invariant: (extents_npages_get(extents) >= npages_limit).  We need an upper
 * bound on number of pages in order to prevent unbounded growth (namely in
 * stashed), otherwise unbounded new pages could be added to extents during the
 * current decay run, so that the purging thread never finishes.
 */
static void
arena_decay_to_limit(tsdn_t *tsdn, arena_t *arena, arena_decay_t *decay,
    extents_t *extents, bool all, size_t npages_limit, size_t npages_decay_max,
    bool is_background_thread) {
	witness_assert_depth_to_rank(tsdn_witness_tsdp_get(tsdn),
	    WITNESS_RANK_CORE, 1);
	malloc_mutex_assert_owner(tsdn, &decay->mtx);

	if (decay->purging) {
		return;
	}
	decay->purging = true;
	malloc_mutex_unlock(tsdn, &decay->mtx);

	extent_hooks_t *extent_hooks = extent_hooks_get(arena);

	extent_list_t decay_extents;
	extent_list_init(&decay_extents);

	size_t npurge = arena_stash_decayed(tsdn, arena, &extent_hooks, extents,
	    npages_limit, npages_decay_max, &decay_extents);
	if (npurge != 0) {
		UNUSED size_t npurged = arena_decay_stashed(tsdn, arena,
		    &extent_hooks, decay, extents, all, &decay_extents,
		    is_background_thread);
		assert(npurged == npurge);
	}

	malloc_mutex_lock(tsdn, &decay->mtx);
	decay->purging = false;
}

static bool
arena_decay_impl(tsdn_t *tsdn, arena_t *arena, arena_decay_t *decay,
    extents_t *extents, bool is_background_thread, bool all) {
	if (all) {
		malloc_mutex_lock(tsdn, &decay->mtx);
		arena_decay_to_limit(tsdn, arena, decay, extents, all, 0,
		    extents_npages_get(extents), is_background_thread);
		malloc_mutex_unlock(tsdn, &decay->mtx);

		return false;
	}

	if (malloc_mutex_trylock(tsdn, &decay->mtx)) {
		/* No need to wait if another thread is in progress. */
		return true;
	}

	bool epoch_advanced = arena_maybe_decay(tsdn, arena, decay, extents,
	    is_background_thread);
	UNUSED size_t npages_new;
	if (epoch_advanced) {
		/* Backlog is updated on epoch advance. */
		npages_new = decay->backlog[SMOOTHSTEP_NSTEPS-1];
	}
	malloc_mutex_unlock(tsdn, &decay->mtx);

	if (have_background_thread && background_thread_enabled() &&
	    epoch_advanced && !is_background_thread) {
		background_thread_interval_check(tsdn, arena, decay,
		    npages_new);
	}

	return false;
}

static bool
arena_decay_dirty(tsdn_t *tsdn, arena_t *arena, bool is_background_thread,
    bool all) {
	return arena_decay_impl(tsdn, arena, &arena->decay_dirty,
	    &arena->extents_dirty, is_background_thread, all);
}

static bool
arena_decay_muzzy(tsdn_t *tsdn, arena_t *arena, bool is_background_thread,
    bool all) {
	return arena_decay_impl(tsdn, arena, &arena->decay_muzzy,
	    &arena->extents_muzzy, is_background_thread, all);
}

void
arena_decay(tsdn_t *tsdn, arena_t *arena, bool is_background_thread, bool all) {
	if (arena_decay_dirty(tsdn, arena, is_background_thread, all)) {
		return;
	}
	arena_decay_muzzy(tsdn, arena, is_background_thread, all);
}

static void
arena_slab_dalloc(tsdn_t *tsdn, arena_t *arena, extent_t *slab) {
	arena_nactive_sub(arena, extent_size_get(slab) >> LG_PAGE);

	extent_hooks_t *extent_hooks = EXTENT_HOOKS_INITIALIZER;
	arena_extents_dirty_dalloc(tsdn, arena, &extent_hooks, slab);
}

static void
arena_bin_slabs_nonfull_insert(bin_t *bin, extent_t *slab) {
	assert(extent_nfree_get(slab) > 0);
	extent_heap_insert(&bin->slabs_nonfull, slab);
}

static void
arena_bin_slabs_nonfull_remove(bin_t *bin, extent_t *slab) {
	extent_heap_remove(&bin->slabs_nonfull, slab);
}

static extent_t *
arena_bin_slabs_nonfull_tryget(bin_t *bin) {
	extent_t *slab = extent_heap_remove_first(&bin->slabs_nonfull);
	if (slab == NULL) {
		return NULL;
	}
	if (config_stats) {
		bin->stats.reslabs++;
	}
	return slab;
}

static void
arena_bin_slabs_full_insert(arena_t *arena, bin_t *bin, extent_t *slab) {
	assert(extent_nfree_get(slab) == 0);
	/*
	 *  Tracking extents is required by arena_reset, which is not allowed
	 *  for auto arenas.  Bypass this step to avoid touching the extent
	 *  linkage (often results in cache misses) for auto arenas.
	 */
	if (arena_is_auto(arena)) {
		return;
	}
	extent_list_append(&bin->slabs_full, slab);
}

static void
arena_bin_slabs_full_remove(arena_t *arena, bin_t *bin, extent_t *slab) {
	if (arena_is_auto(arena)) {
		return;
	}
	extent_list_remove(&bin->slabs_full, slab);
}

void
arena_reset(tsd_t *tsd, arena_t *arena) {
	/*
	 * Locking in this function is unintuitive.  The caller guarantees that
	 * no concurrent operations are happening in this arena, but there are
	 * still reasons that some locking is necessary:
	 *
	 * - Some of the functions in the transitive closure of calls assume
	 *   appropriate locks are held, and in some cases these locks are
	 *   temporarily dropped to avoid lock order reversal or deadlock due to
	 *   reentry.
	 * - mallctl("epoch", ...) may concurrently refresh stats.  While
	 *   strictly speaking this is a "concurrent operation", disallowing
	 *   stats refreshes would impose an inconvenient burden.
	 */

	/* Large allocations. */
	malloc_mutex_lock(tsd_tsdn(tsd), &arena->large_mtx);

	for (extent_t *extent = extent_list_first(&arena->large); extent !=
	    NULL; extent = extent_list_first(&arena->large)) {
		void *ptr = extent_base_get(extent);
		size_t usize;

		malloc_mutex_unlock(tsd_tsdn(tsd), &arena->large_mtx);
		alloc_ctx_t alloc_ctx;
		rtree_ctx_t *rtree_ctx = tsd_rtree_ctx(tsd);
		rtree_szind_slab_read(tsd_tsdn(tsd), &extents_rtree, rtree_ctx,
		    (uintptr_t)ptr, true, &alloc_ctx.szind, &alloc_ctx.slab);
		assert(alloc_ctx.szind != NSIZES);

		if (config_stats || (config_prof && opt_prof)) {
			usize = sz_index2size(alloc_ctx.szind);
			assert(usize == isalloc(tsd_tsdn(tsd), ptr));
		}
		/* Remove large allocation from prof sample set. */
		if (config_prof && opt_prof) {
			prof_free(tsd, ptr, usize, &alloc_ctx);
		}
		large_dalloc(tsd_tsdn(tsd), extent);
		malloc_mutex_lock(tsd_tsdn(tsd), &arena->large_mtx);
	}
	malloc_mutex_unlock(tsd_tsdn(tsd), &arena->large_mtx);

	/* Bins. */
	for (unsigned i = 0; i < NBINS; i++) {
		extent_t *slab;
		bin_t *bin = &arena->bins[i];
		malloc_mutex_lock(tsd_tsdn(tsd), &bin->lock);
		if (bin->slabcur != NULL) {
			slab = bin->slabcur;
			bin->slabcur = NULL;
			malloc_mutex_unlock(tsd_tsdn(tsd), &bin->lock);
			arena_slab_dalloc(tsd_tsdn(tsd), arena, slab);
			malloc_mutex_lock(tsd_tsdn(tsd), &bin->lock);
		}
		while ((slab = extent_heap_remove_first(&bin->slabs_nonfull)) !=
		    NULL) {
			malloc_mutex_unlock(tsd_tsdn(tsd), &bin->lock);
			arena_slab_dalloc(tsd_tsdn(tsd), arena, slab);
			malloc_mutex_lock(tsd_tsdn(tsd), &bin->lock);
		}
		for (slab = extent_list_first(&bin->slabs_full); slab != NULL;
		    slab = extent_list_first(&bin->slabs_full)) {
			arena_bin_slabs_full_remove(arena, bin, slab);
			malloc_mutex_unlock(tsd_tsdn(tsd), &bin->lock);
			arena_slab_dalloc(tsd_tsdn(tsd), arena, slab);
			malloc_mutex_lock(tsd_tsdn(tsd), &bin->lock);
		}
		if (config_stats) {
			bin->stats.curregs = 0;
			bin->stats.curslabs = 0;
		}
		malloc_mutex_unlock(tsd_tsdn(tsd), &bin->lock);
	}

	atomic_store_zu(&arena->nactive, 0, ATOMIC_RELAXED);
}

static void
arena_destroy_retained(tsdn_t *tsdn, arena_t *arena) {
	/*
	 * Iterate over the retained extents and destroy them.  This gives the
	 * extent allocator underlying the extent hooks an opportunity to unmap
	 * all retained memory without having to keep its own metadata
	 * structures.  In practice, virtual memory for dss-allocated extents is
	 * leaked here, so best practice is to avoid dss for arenas to be
	 * destroyed, or provide custom extent hooks that track retained
	 * dss-based extents for later reuse.
	 */
	extent_hooks_t *extent_hooks = extent_hooks_get(arena);
	extent_t *extent;
	while ((extent = extents_evict(tsdn, arena, &extent_hooks,
	    &arena->extents_retained, 0)) != NULL) {
		extent_destroy_wrapper(tsdn, arena, &extent_hooks, extent);
	}
}

void
arena_destroy(tsd_t *tsd, arena_t *arena) {
	assert(base_ind_get(arena->base) >= narenas_auto);
	assert(arena_nthreads_get(arena, false) == 0);
	assert(arena_nthreads_get(arena, true) == 0);

	/*
	 * No allocations have occurred since arena_reset() was called.
	 * Furthermore, the caller (arena_i_destroy_ctl()) purged all cached
	 * extents, so only retained extents may remain.
	 */
	assert(extents_npages_get(&arena->extents_dirty) == 0);
	assert(extents_npages_get(&arena->extents_muzzy) == 0);

	/* Deallocate retained memory. */
	arena_destroy_retained(tsd_tsdn(tsd), arena);

	/*
	 * Remove the arena pointer from the arenas array.  We rely on the fact
	 * that there is no way for the application to get a dirty read from the
	 * arenas array unless there is an inherent race in the application
	 * involving access of an arena being concurrently destroyed.  The
	 * application must synchronize knowledge of the arena's validity, so as
	 * long as we use an atomic write to update the arenas array, the
	 * application will get a clean read any time after it synchronizes
	 * knowledge that the arena is no longer valid.
	 */
	arena_set(base_ind_get(arena->base), NULL);

	/*
	 * Destroy the base allocator, which manages all metadata ever mapped by
	 * this arena.
	 */
	base_delete(tsd_tsdn(tsd), arena->base);
}

static extent_t *
arena_slab_alloc_hard(tsdn_t *tsdn, arena_t *arena,
    extent_hooks_t **r_extent_hooks, const bin_info_t *bin_info,
    szind_t szind) {
	extent_t *slab;
	bool zero, commit;

	witness_assert_depth_to_rank(tsdn_witness_tsdp_get(tsdn),
	    WITNESS_RANK_CORE, 0);

	zero = false;
	commit = true;
	slab = extent_alloc_wrapper(tsdn, arena, r_extent_hooks, NULL,
	    bin_info->slab_size, 0, PAGE, true, szind, &zero, &commit);

	if (config_stats && slab != NULL) {
		arena_stats_mapped_add(tsdn, &arena->stats,
		    bin_info->slab_size);
	}

	return slab;
}

static extent_t *
arena_slab_alloc(tsdn_t *tsdn, arena_t *arena, szind_t binind,
    const bin_info_t *bin_info) {
	witness_assert_depth_to_rank(tsdn_witness_tsdp_get(tsdn),
	    WITNESS_RANK_CORE, 0);

	extent_hooks_t *extent_hooks = EXTENT_HOOKS_INITIALIZER;
	szind_t szind = sz_size2index(bin_info->reg_size);
	bool zero = false;
	bool commit = true;
	extent_t *slab = extents_alloc(tsdn, arena, &extent_hooks,
	    &arena->extents_dirty, NULL, bin_info->slab_size, 0, PAGE, true,
	    binind, &zero, &commit);
	if (slab == NULL) {
		slab = extents_alloc(tsdn, arena, &extent_hooks,
		    &arena->extents_muzzy, NULL, bin_info->slab_size, 0, PAGE,
		    true, binind, &zero, &commit);
	}
	if (slab == NULL) {
		slab = arena_slab_alloc_hard(tsdn, arena, &extent_hooks,
		    bin_info, szind);
		if (slab == NULL) {
			return NULL;
		}
	}
	assert(extent_slab_get(slab));

	/* Initialize slab internals. */
	arena_slab_data_t *slab_data = extent_slab_data_get(slab);
	extent_nfree_set(slab, bin_info->nregs);
	bitmap_init(slab_data->bitmap, &bin_info->bitmap_info, false);

	arena_nactive_add(arena, extent_size_get(slab) >> LG_PAGE);

	return slab;
}

static extent_t *
arena_bin_nonfull_slab_get(tsdn_t *tsdn, arena_t *arena, bin_t *bin,
    szind_t binind) {
	extent_t *slab;
	const bin_info_t *bin_info;

	/* Look for a usable slab. */
	slab = arena_bin_slabs_nonfull_tryget(bin);
	if (slab != NULL) {
		return slab;
	}
	/* No existing slabs have any space available. */

	bin_info = &bin_infos[binind];

	/* Allocate a new slab. */
	malloc_mutex_unlock(tsdn, &bin->lock);
	/******************************/
	slab = arena_slab_alloc(tsdn, arena, binind, bin_info);
	/********************************/
	malloc_mutex_lock(tsdn, &bin->lock);
	if (slab != NULL) {
		if (config_stats) {
			bin->stats.nslabs++;
			bin->stats.curslabs++;
		}
		return slab;
	}

	/*
	 * arena_slab_alloc() failed, but another thread may have made
	 * sufficient memory available while this one dropped bin->lock above,
	 * so search one more time.
	 */
	slab = arena_bin_slabs_nonfull_tryget(bin);
	if (slab != NULL) {
		return slab;
	}

	return NULL;
}

/* Re-fill bin->slabcur, then call arena_slab_reg_alloc(). */
static void *
arena_bin_malloc_hard(tsdn_t *tsdn, arena_t *arena, bin_t *bin,
    szind_t binind) {
	const bin_info_t *bin_info;
	extent_t *slab;

	bin_info = &bin_infos[binind];
	if (!arena_is_auto(arena) && bin->slabcur != NULL) {
		arena_bin_slabs_full_insert(arena, bin, bin->slabcur);
		bin->slabcur = NULL;
	}
	slab = arena_bin_nonfull_slab_get(tsdn, arena, bin, binind);
	if (bin->slabcur != NULL) {
		/*
		 * Another thread updated slabcur while this one ran without the
		 * bin lock in arena_bin_nonfull_slab_get().
		 */
		if (extent_nfree_get(bin->slabcur) > 0) {
			void *ret = arena_slab_reg_alloc(bin->slabcur,
			    bin_info);
			if (slab != NULL) {
				/*
				 * arena_slab_alloc() may have allocated slab,
				 * or it may have been pulled from
				 * slabs_nonfull.  Therefore it is unsafe to
				 * make any assumptions about how slab has
				 * previously been used, and
				 * arena_bin_lower_slab() must be called, as if
				 * a region were just deallocated from the slab.
				 */
				if (extent_nfree_get(slab) == bin_info->nregs) {
					arena_dalloc_bin_slab(tsdn, arena, slab,
					    bin);
				} else {
					arena_bin_lower_slab(tsdn, arena, slab,
					    bin);
				}
			}
			return ret;
		}

		arena_bin_slabs_full_insert(arena, bin, bin->slabcur);
		bin->slabcur = NULL;
	}

	if (slab == NULL) {
		return NULL;
	}
	bin->slabcur = slab;

	assert(extent_nfree_get(bin->slabcur) > 0);

	return arena_slab_reg_alloc(slab, bin_info);
}

void
arena_tcache_fill_small(tsdn_t *tsdn, arena_t *arena, tcache_t *tcache,
    cache_bin_t *tbin, szind_t binind, uint64_t prof_accumbytes) {
	unsigned i, nfill;
	bin_t *bin;

	assert(tbin->ncached == 0);

	if (config_prof && arena_prof_accum(tsdn, arena, prof_accumbytes)) {
		prof_idump(tsdn);
	}
	bin = &arena->bins[binind];
	malloc_mutex_lock(tsdn, &bin->lock);
	for (i = 0, nfill = (tcache_bin_info[binind].ncached_max >>
	    tcache->lg_fill_div[binind]); i < nfill; i++) {
		extent_t *slab;
		void *ptr;
		if ((slab = bin->slabcur) != NULL && extent_nfree_get(slab) >
		    0) {
			ptr = arena_slab_reg_alloc(slab, &bin_infos[binind]);
		} else {
			ptr = arena_bin_malloc_hard(tsdn, arena, bin, binind);
		}
		if (ptr == NULL) {
			/*
			 * OOM.  tbin->avail isn't yet filled down to its first
			 * element, so the successful allocations (if any) must
			 * be moved just before tbin->avail before bailing out.
			 */
			if (i > 0) {
				memmove(tbin->avail - i, tbin->avail - nfill,
				    i * sizeof(void *));
			}
			break;
		}
		if (config_fill && unlikely(opt_junk_alloc)) {
			arena_alloc_junk_small(ptr, &bin_infos[binind], true);
		}
		/* Insert such that low regions get used first. */
		*(tbin->avail - nfill + i) = ptr;
	}
	if (config_stats) {
		bin->stats.nmalloc += i;
		bin->stats.nrequests += tbin->tstats.nrequests;
		bin->stats.curregs += i;
		bin->stats.nfills++;
		tbin->tstats.nrequests = 0;
	}
	malloc_mutex_unlock(tsdn, &bin->lock);
	tbin->ncached = i;
	arena_decay_tick(tsdn, arena);
}

void
arena_alloc_junk_small(void *ptr, const bin_info_t *bin_info, bool zero) {
	if (!zero) {
		memset(ptr, JEMALLOC_ALLOC_JUNK, bin_info->reg_size);
	}
}

static void
arena_dalloc_junk_small_impl(void *ptr, const bin_info_t *bin_info) {
	memset(ptr, JEMALLOC_FREE_JUNK, bin_info->reg_size);
}
arena_dalloc_junk_small_t *JET_MUTABLE arena_dalloc_junk_small =
    arena_dalloc_junk_small_impl;

static void *
arena_malloc_small(tsdn_t *tsdn, arena_t *arena, szind_t binind, bool zero) {
	void *ret;
	bin_t *bin;
	size_t usize;
	extent_t *slab;

	assert(binind < NBINS);
	bin = &arena->bins[binind];
	usize = sz_index2size(binind);

	malloc_mutex_lock(tsdn, &bin->lock);
	if ((slab = bin->slabcur) != NULL && extent_nfree_get(slab) > 0) {
		ret = arena_slab_reg_alloc(slab, &bin_infos[binind]);
	} else {
		ret = arena_bin_malloc_hard(tsdn, arena, bin, binind);
	}

	if (ret == NULL) {
		malloc_mutex_unlock(tsdn, &bin->lock);
		return NULL;
	}

	if (config_stats) {
		bin->stats.nmalloc++;
		bin->stats.nrequests++;
		bin->stats.curregs++;
	}
	malloc_mutex_unlock(tsdn, &bin->lock);
	if (config_prof && arena_prof_accum(tsdn, arena, usize)) {
		prof_idump(tsdn);
	}

	if (!zero) {
		if (config_fill) {
			if (unlikely(opt_junk_alloc)) {
				arena_alloc_junk_small(ret,
				    &bin_infos[binind], false);
			} else if (unlikely(opt_zero)) {
				memset(ret, 0, usize);
			}
		}
	} else {
		if (config_fill && unlikely(opt_junk_alloc)) {
			arena_alloc_junk_small(ret, &bin_infos[binind],
			    true);
		}
		memset(ret, 0, usize);
	}

	arena_decay_tick(tsdn, arena);
	return ret;
}

void *
arena_malloc_hard(tsdn_t *tsdn, arena_t *arena, size_t size, szind_t ind,
    bool zero) {
	assert(!tsdn_null(tsdn) || arena != NULL);

	if (likely(!tsdn_null(tsdn))) {
		arena = arena_choose(tsdn_tsd(tsdn), arena);
	}
	if (unlikely(arena == NULL)) {
		return NULL;
	}

	if (likely(size <= SMALL_MAXCLASS)) {
		return arena_malloc_small(tsdn, arena, ind, zero);
	}
	return large_malloc(tsdn, arena, sz_index2size(ind), zero);
}

void *
arena_palloc(tsdn_t *tsdn, arena_t *arena, size_t usize, size_t alignment,
    bool zero, tcache_t *tcache) {
	void *ret;

	if (usize <= SMALL_MAXCLASS && (alignment < PAGE || (alignment == PAGE
	    && (usize & PAGE_MASK) == 0))) {
		/* Small; alignment doesn't require special slab placement. */
		ret = arena_malloc(tsdn, arena, usize, sz_size2index(usize),
		    zero, tcache, true);
	} else {
		if (likely(alignment <= CACHELINE)) {
			ret = large_malloc(tsdn, arena, usize, zero);
		} else {
			ret = large_palloc(tsdn, arena, usize, alignment, zero);
		}
	}
	return ret;
}

void
arena_prof_promote(tsdn_t *tsdn, const void *ptr, size_t usize) {
	cassert(config_prof);
	assert(ptr != NULL);
	assert(isalloc(tsdn, ptr) == LARGE_MINCLASS);
	assert(usize <= SMALL_MAXCLASS);

	rtree_ctx_t rtree_ctx_fallback;
	rtree_ctx_t *rtree_ctx = tsdn_rtree_ctx(tsdn, &rtree_ctx_fallback);

	extent_t *extent = rtree_extent_read(tsdn, &extents_rtree, rtree_ctx,
	    (uintptr_t)ptr, true);
	arena_t *arena = extent_arena_get(extent);

	szind_t szind = sz_size2index(usize);
	extent_szind_set(extent, szind);
	rtree_szind_slab_update(tsdn, &extents_rtree, rtree_ctx, (uintptr_t)ptr,
	    szind, false);

	prof_accum_cancel(tsdn, &arena->prof_accum, usize);

	assert(isalloc(tsdn, ptr) == usize);
}

static size_t
arena_prof_demote(tsdn_t *tsdn, extent_t *extent, const void *ptr) {
	cassert(config_prof);
	assert(ptr != NULL);

	extent_szind_set(extent, NBINS);
	rtree_ctx_t rtree_ctx_fallback;
	rtree_ctx_t *rtree_ctx = tsdn_rtree_ctx(tsdn, &rtree_ctx_fallback);
	rtree_szind_slab_update(tsdn, &extents_rtree, rtree_ctx, (uintptr_t)ptr,
	    NBINS, false);

	assert(isalloc(tsdn, ptr) == LARGE_MINCLASS);

	return LARGE_MINCLASS;
}

void
arena_dalloc_promoted(tsdn_t *tsdn, void *ptr, tcache_t *tcache,
    bool slow_path) {
	cassert(config_prof);
	assert(opt_prof);

	extent_t *extent = iealloc(tsdn, ptr);
	size_t usize = arena_prof_demote(tsdn, extent, ptr);
	if (usize <= tcache_maxclass) {
		tcache_dalloc_large(tsdn_tsd(tsdn), tcache, ptr,
		    sz_size2index(usize), slow_path);
	} else {
		large_dalloc(tsdn, extent);
	}
}

static void
arena_dissociate_bin_slab(arena_t *arena, extent_t *slab, bin_t *bin) {
	/* Dissociate slab from bin. */
	if (slab == bin->slabcur) {
		bin->slabcur = NULL;
	} else {
		szind_t binind = extent_szind_get(slab);
		const bin_info_t *bin_info = &bin_infos[binind];

		/*
		 * The following block's conditional is necessary because if the
		 * slab only contains one region, then it never gets inserted
		 * into the non-full slabs heap.
		 */
		if (bin_info->nregs == 1) {
			arena_bin_slabs_full_remove(arena, bin, slab);
		} else {
			arena_bin_slabs_nonfull_remove(bin, slab);
		}
	}
}

static void
arena_dalloc_bin_slab(tsdn_t *tsdn, arena_t *arena, extent_t *slab,
    bin_t *bin) {
	assert(slab != bin->slabcur);

	malloc_mutex_unlock(tsdn, &bin->lock);
	/******************************/
	arena_slab_dalloc(tsdn, arena, slab);
	/****************************/
	malloc_mutex_lock(tsdn, &bin->lock);
	if (config_stats) {
		bin->stats.curslabs--;
	}
}

static void
arena_bin_lower_slab(UNUSED tsdn_t *tsdn, arena_t *arena, extent_t *slab,
    bin_t *bin) {
	assert(extent_nfree_get(slab) > 0);

	/*
	 * Make sure that if bin->slabcur is non-NULL, it refers to the
	 * oldest/lowest non-full slab.  It is okay to NULL slabcur out rather
	 * than proactively keeping it pointing at the oldest/lowest non-full
	 * slab.
	 */
	if (bin->slabcur != NULL && extent_snad_comp(bin->slabcur, slab) > 0) {
		/* Switch slabcur. */
		if (extent_nfree_get(bin->slabcur) > 0) {
			arena_bin_slabs_nonfull_insert(bin, bin->slabcur);
		} else {
			arena_bin_slabs_full_insert(arena, bin, bin->slabcur);
		}
		bin->slabcur = slab;
		if (config_stats) {
			bin->stats.reslabs++;
		}
	} else {
		arena_bin_slabs_nonfull_insert(bin, slab);
	}
}

static void
arena_dalloc_bin_locked_impl(tsdn_t *tsdn, arena_t *arena, extent_t *slab,
    void *ptr, bool junked) {
	arena_slab_data_t *slab_data = extent_slab_data_get(slab);
	szind_t binind = extent_szind_get(slab);
	bin_t *bin = &arena->bins[binind];
	const bin_info_t *bin_info = &bin_infos[binind];

	if (!junked && config_fill && unlikely(opt_junk_free)) {
		arena_dalloc_junk_small(ptr, bin_info);
	}

	arena_slab_reg_dalloc(slab, slab_data, ptr);
	unsigned nfree = extent_nfree_get(slab);
	if (nfree == bin_info->nregs) {
		arena_dissociate_bin_slab(arena, slab, bin);
		arena_dalloc_bin_slab(tsdn, arena, slab, bin);
	} else if (nfree == 1 && slab != bin->slabcur) {
		arena_bin_slabs_full_remove(arena, bin, slab);
		arena_bin_lower_slab(tsdn, arena, slab, bin);
	}

	if (config_stats) {
		bin->stats.ndalloc++;
		bin->stats.curregs--;
	}
}

void
arena_dalloc_bin_junked_locked(tsdn_t *tsdn, arena_t *arena, extent_t *extent,
    void *ptr) {
	arena_dalloc_bin_locked_impl(tsdn, arena, extent, ptr, true);
}

static void
arena_dalloc_bin(tsdn_t *tsdn, arena_t *arena, extent_t *extent, void *ptr) {
	szind_t binind = extent_szind_get(extent);
	bin_t *bin = &arena->bins[binind];

	malloc_mutex_lock(tsdn, &bin->lock);
	arena_dalloc_bin_locked_impl(tsdn, arena, extent, ptr, false);
	malloc_mutex_unlock(tsdn, &bin->lock);
}

void
arena_dalloc_small(tsdn_t *tsdn, void *ptr) {
	extent_t *extent = iealloc(tsdn, ptr);
	arena_t *arena = extent_arena_get(extent);

	arena_dalloc_bin(tsdn, arena, extent, ptr);
	arena_decay_tick(tsdn, arena);
}

bool
arena_ralloc_no_move(tsdn_t *tsdn, void *ptr, size_t oldsize, size_t size,
    size_t extra, bool zero) {
	/* Calls with non-zero extra had to clamp extra. */
	assert(extra == 0 || size + extra <= LARGE_MAXCLASS);

	if (unlikely(size > LARGE_MAXCLASS)) {
		return true;
	}

	extent_t *extent = iealloc(tsdn, ptr);
	size_t usize_min = sz_s2u(size);
	size_t usize_max = sz_s2u(size + extra);
	if (likely(oldsize <= SMALL_MAXCLASS && usize_min <= SMALL_MAXCLASS)) {
		/*
		 * Avoid moving the allocation if the size class can be left the
		 * same.
		 */
		assert(bin_infos[sz_size2index(oldsize)].reg_size ==
		    oldsize);
		if ((usize_max > SMALL_MAXCLASS || sz_size2index(usize_max) !=
		    sz_size2index(oldsize)) && (size > oldsize || usize_max <
		    oldsize)) {
			return true;
		}

		arena_decay_tick(tsdn, extent_arena_get(extent));
		return false;
	} else if (oldsize >= LARGE_MINCLASS && usize_max >= LARGE_MINCLASS) {
		return large_ralloc_no_move(tsdn, extent, usize_min, usize_max,
		    zero);
	}

	return true;
}

static void *
arena_ralloc_move_helper(tsdn_t *tsdn, arena_t *arena, size_t usize,
    size_t alignment, bool zero, tcache_t *tcache) {
	if (alignment == 0) {
		return arena_malloc(tsdn, arena, usize, sz_size2index(usize),
		    zero, tcache, true);
	}
	usize = sz_sa2u(usize, alignment);
	if (unlikely(usize == 0 || usize > LARGE_MAXCLASS)) {
		return NULL;
	}
	return ipalloct(tsdn, usize, alignment, zero, tcache, arena);
}

void *
arena_ralloc(tsdn_t *tsdn, arena_t *arena, void *ptr, size_t oldsize,
    size_t size, size_t alignment, bool zero, tcache_t *tcache) {
	size_t usize = sz_s2u(size);
	if (unlikely(usize == 0 || size > LARGE_MAXCLASS)) {
		return NULL;
	}

	if (likely(usize <= SMALL_MAXCLASS)) {
		/* Try to avoid moving the allocation. */
		if (!arena_ralloc_no_move(tsdn, ptr, oldsize, usize, 0, zero)) {
			return ptr;
		}
	}

	if (oldsize >= LARGE_MINCLASS && usize >= LARGE_MINCLASS) {
		return large_ralloc(tsdn, arena, iealloc(tsdn, ptr), usize,
		    alignment, zero, tcache);
	}

	/*
	 * size and oldsize are different enough that we need to move the
	 * object.  In that case, fall back to allocating new space and copying.
	 */
	void *ret = arena_ralloc_move_helper(tsdn, arena, usize, alignment,
	    zero, tcache);
	if (ret == NULL) {
		return NULL;
	}

	/*
	 * Junk/zero-filling were already done by
	 * ipalloc()/arena_malloc().
	 */

	size_t copysize = (usize < oldsize) ? usize : oldsize;
	memcpy(ret, ptr, copysize);
	isdalloct(tsdn, ptr, oldsize, tcache, NULL, true);
	return ret;
}

dss_prec_t
arena_dss_prec_get(arena_t *arena) {
	return (dss_prec_t)atomic_load_u(&arena->dss_prec, ATOMIC_ACQUIRE);
}

bool
arena_dss_prec_set(arena_t *arena, dss_prec_t dss_prec) {
	if (!have_dss) {
		return (dss_prec != dss_prec_disabled);
	}
	atomic_store_u(&arena->dss_prec, (unsigned)dss_prec, ATOMIC_RELEASE);
	return false;
}

ssize_t
arena_dirty_decay_ms_default_get(void) {
	return atomic_load_zd(&dirty_decay_ms_default, ATOMIC_RELAXED);
}

bool
arena_dirty_decay_ms_default_set(ssize_t decay_ms) {
	if (!arena_decay_ms_valid(decay_ms)) {
		return true;
	}
	atomic_store_zd(&dirty_decay_ms_default, decay_ms, ATOMIC_RELAXED);
	return false;
}

ssize_t
arena_muzzy_decay_ms_default_get(void) {
	return atomic_load_zd(&muzzy_decay_ms_default, ATOMIC_RELAXED);
}

bool
arena_muzzy_decay_ms_default_set(ssize_t decay_ms) {
	if (!arena_decay_ms_valid(decay_ms)) {
		return true;
	}
	atomic_store_zd(&muzzy_decay_ms_default, decay_ms, ATOMIC_RELAXED);
	return false;
}

bool
arena_retain_grow_limit_get_set(tsd_t *tsd, arena_t *arena, size_t *old_limit,
    size_t *new_limit) {
	assert(opt_retain);

	pszind_t new_ind JEMALLOC_CC_SILENCE_INIT(0);
	if (new_limit != NULL) {
		size_t limit = *new_limit;
		/* Grow no more than the new limit. */
		if ((new_ind = sz_psz2ind(limit + 1) - 1) >
		     EXTENT_GROW_MAX_PIND) {
			return true;
		}
	}

	malloc_mutex_lock(tsd_tsdn(tsd), &arena->extent_grow_mtx);
	if (old_limit != NULL) {
		*old_limit = sz_pind2sz(arena->retain_grow_limit);
	}
	if (new_limit != NULL) {
		arena->retain_grow_limit = new_ind;
	}
	malloc_mutex_unlock(tsd_tsdn(tsd), &arena->extent_grow_mtx);

	return false;
}

unsigned
arena_nthreads_get(arena_t *arena, bool internal) {
	return atomic_load_u(&arena->nthreads[internal], ATOMIC_RELAXED);
}

void
arena_nthreads_inc(arena_t *arena, bool internal) {
	atomic_fetch_add_u(&arena->nthreads[internal], 1, ATOMIC_RELAXED);
}

void
arena_nthreads_dec(arena_t *arena, bool internal) {
	atomic_fetch_sub_u(&arena->nthreads[internal], 1, ATOMIC_RELAXED);
}

size_t
arena_extent_sn_next(arena_t *arena) {
	return atomic_fetch_add_zu(&arena->extent_sn_next, 1, ATOMIC_RELAXED);
}

arena_t *
arena_new(tsdn_t *tsdn, unsigned ind, extent_hooks_t *extent_hooks) {
	arena_t *arena;
	base_t *base;
	unsigned i;

	if (ind == 0) {
		base = b0get();
	} else {
		base = base_new(tsdn, ind, extent_hooks);
		if (base == NULL) {
			return NULL;
		}
	}

	arena = (arena_t *)base_alloc(tsdn, base, sizeof(arena_t), CACHELINE);
	if (arena == NULL) {
		goto label_error;
	}

	atomic_store_u(&arena->nthreads[0], 0, ATOMIC_RELAXED);
	atomic_store_u(&arena->nthreads[1], 0, ATOMIC_RELAXED);
	arena->last_thd = NULL;

	if (config_stats) {
		if (arena_stats_init(tsdn, &arena->stats)) {
			goto label_error;
		}

		ql_new(&arena->tcache_ql);
		ql_new(&arena->cache_bin_array_descriptor_ql);
		if (malloc_mutex_init(&arena->tcache_ql_mtx, "tcache_ql",
		    WITNESS_RANK_TCACHE_QL, malloc_mutex_rank_exclusive)) {
			goto label_error;
		}
	}

	if (config_prof) {
		if (prof_accum_init(tsdn, &arena->prof_accum)) {
			goto label_error;
		}
	}

	if (config_cache_oblivious) {
		/*
		 * A nondeterministic seed based on the address of arena reduces
		 * the likelihood of lockstep non-uniform cache index
		 * utilization among identical concurrent processes, but at the
		 * cost of test repeatability.  For debug builds, instead use a
		 * deterministic seed.
		 */
		atomic_store_zu(&arena->offset_state, config_debug ? ind :
		    (size_t)(uintptr_t)arena, ATOMIC_RELAXED);
	}

	atomic_store_zu(&arena->extent_sn_next, 0, ATOMIC_RELAXED);

	atomic_store_u(&arena->dss_prec, (unsigned)extent_dss_prec_get(),
	    ATOMIC_RELAXED);

	atomic_store_zu(&arena->nactive, 0, ATOMIC_RELAXED);

	extent_list_init(&arena->large);
	if (malloc_mutex_init(&arena->large_mtx, "arena_large",
	    WITNESS_RANK_ARENA_LARGE, malloc_mutex_rank_exclusive)) {
		goto label_error;
	}

	/*
	 * Delay coalescing for dirty extents despite the disruptive effect on
	 * memory layout for best-fit extent allocation, since cached extents
	 * are likely to be reused soon after deallocation, and the cost of
	 * merging/splitting extents is non-trivial.
	 */
	if (extents_init(tsdn, &arena->extents_dirty, extent_state_dirty,
	    true)) {
		goto label_error;
	}
	/*
	 * Coalesce muzzy extents immediately, because operations on them are in
	 * the critical path much less often than for dirty extents.
	 */
	if (extents_init(tsdn, &arena->extents_muzzy, extent_state_muzzy,
	    false)) {
		goto label_error;
	}
	/*
	 * Coalesce retained extents immediately, in part because they will
	 * never be evicted (and therefore there's no opportunity for delayed
	 * coalescing), but also because operations on retained extents are not
	 * in the critical path.
	 */
	if (extents_init(tsdn, &arena->extents_retained, extent_state_retained,
	    false)) {
		goto label_error;
	}

	if (arena_decay_init(&arena->decay_dirty,
	    arena_dirty_decay_ms_default_get(), &arena->stats.decay_dirty)) {
		goto label_error;
	}
	if (arena_decay_init(&arena->decay_muzzy,
	    arena_muzzy_decay_ms_default_get(), &arena->stats.decay_muzzy)) {
		goto label_error;
	}

	arena->extent_grow_next = sz_psz2ind(HUGEPAGE);
	arena->retain_grow_limit = EXTENT_GROW_MAX_PIND;
	if (malloc_mutex_init(&arena->extent_grow_mtx, "extent_grow",
	    WITNESS_RANK_EXTENT_GROW, malloc_mutex_rank_exclusive)) {
		goto label_error;
	}

	extent_avail_new(&arena->extent_avail);
	if (malloc_mutex_init(&arena->extent_avail_mtx, "extent_avail",
	    WITNESS_RANK_EXTENT_AVAIL, malloc_mutex_rank_exclusive)) {
		goto label_error;
	}

	/* Initialize bins. */
	for (i = 0; i < NBINS; i++) {
		bool err = bin_init(&arena->bins[i]);
		if (err) {
			goto label_error;
		}
	}

	arena->base = base;
	/* Set arena before creating background threads. */
	arena_set(ind, arena);

	nstime_init(&arena->create_time, 0);
	nstime_update(&arena->create_time);

	/* We don't support reentrancy for arena 0 bootstrapping. */
	if (ind != 0) {
		/*
		 * If we're here, then arena 0 already exists, so bootstrapping
		 * is done enough that we should have tsd.
		 */
		assert(!tsdn_null(tsdn));
		pre_reentrancy(tsdn_tsd(tsdn), arena);
		if (hooks_arena_new_hook) {
			hooks_arena_new_hook();
		}
		post_reentrancy(tsdn_tsd(tsdn));
	}

	return arena;
label_error:
	if (ind != 0) {
		base_delete(tsdn, base);
	}
	return NULL;
}

void
arena_boot(void) {
	arena_dirty_decay_ms_default_set(opt_dirty_decay_ms);
	arena_muzzy_decay_ms_default_set(opt_muzzy_decay_ms);
#define REGIND_bin_yes(index, reg_size) 				\
	div_init(&arena_binind_div_info[(index)], (reg_size));
#define REGIND_bin_no(index, reg_size)
#define SC(index, lg_grp, lg_delta, ndelta, psz, bin, pgs,		\
    lg_delta_lookup)							\
	REGIND_bin_##bin(index, (1U<<lg_grp) + (ndelta << lg_delta))
	SIZE_CLASSES
#undef REGIND_bin_yes
#undef REGIND_bin_no
#undef SC
}

void
arena_prefork0(tsdn_t *tsdn, arena_t *arena) {
	malloc_mutex_prefork(tsdn, &arena->decay_dirty.mtx);
	malloc_mutex_prefork(tsdn, &arena->decay_muzzy.mtx);
}

void
arena_prefork1(tsdn_t *tsdn, arena_t *arena) {
	if (config_stats) {
		malloc_mutex_prefork(tsdn, &arena->tcache_ql_mtx);
	}
}

void
arena_prefork2(tsdn_t *tsdn, arena_t *arena) {
	malloc_mutex_prefork(tsdn, &arena->extent_grow_mtx);
}

void
arena_prefork3(tsdn_t *tsdn, arena_t *arena) {
	extents_prefork(tsdn, &arena->extents_dirty);
	extents_prefork(tsdn, &arena->extents_muzzy);
	extents_prefork(tsdn, &arena->extents_retained);
}

void
arena_prefork4(tsdn_t *tsdn, arena_t *arena) {
	malloc_mutex_prefork(tsdn, &arena->extent_avail_mtx);
}

void
arena_prefork5(tsdn_t *tsdn, arena_t *arena) {
	base_prefork(tsdn, arena->base);
}

void
arena_prefork6(tsdn_t *tsdn, arena_t *arena) {
	malloc_mutex_prefork(tsdn, &arena->large_mtx);
}

void
arena_prefork7(tsdn_t *tsdn, arena_t *arena) {
	for (unsigned i = 0; i < NBINS; i++) {
		bin_prefork(tsdn, &arena->bins[i]);
	}
}

void
arena_postfork_parent(tsdn_t *tsdn, arena_t *arena) {
	unsigned i;

	for (i = 0; i < NBINS; i++) {
		bin_postfork_parent(tsdn, &arena->bins[i]);
	}
	malloc_mutex_postfork_parent(tsdn, &arena->large_mtx);
	base_postfork_parent(tsdn, arena->base);
	malloc_mutex_postfork_parent(tsdn, &arena->extent_avail_mtx);
	extents_postfork_parent(tsdn, &arena->extents_dirty);
	extents_postfork_parent(tsdn, &arena->extents_muzzy);
	extents_postfork_parent(tsdn, &arena->extents_retained);
	malloc_mutex_postfork_parent(tsdn, &arena->extent_grow_mtx);
	malloc_mutex_postfork_parent(tsdn, &arena->decay_dirty.mtx);
	malloc_mutex_postfork_parent(tsdn, &arena->decay_muzzy.mtx);
	if (config_stats) {
		malloc_mutex_postfork_parent(tsdn, &arena->tcache_ql_mtx);
	}
}

void
arena_postfork_child(tsdn_t *tsdn, arena_t *arena) {
	unsigned i;

	atomic_store_u(&arena->nthreads[0], 0, ATOMIC_RELAXED);
	atomic_store_u(&arena->nthreads[1], 0, ATOMIC_RELAXED);
	if (tsd_arena_get(tsdn_tsd(tsdn)) == arena) {
		arena_nthreads_inc(arena, false);
	}
	if (tsd_iarena_get(tsdn_tsd(tsdn)) == arena) {
		arena_nthreads_inc(arena, true);
	}
	if (config_stats) {
		ql_new(&arena->tcache_ql);
		ql_new(&arena->cache_bin_array_descriptor_ql);
		tcache_t *tcache = tcache_get(tsdn_tsd(tsdn));
		if (tcache != NULL && tcache->arena == arena) {
			ql_elm_new(tcache, link);
			ql_tail_insert(&arena->tcache_ql, tcache, link);
			cache_bin_array_descriptor_init(
			    &tcache->cache_bin_array_descriptor,
			    tcache->bins_small, tcache->bins_large);
			ql_tail_insert(&arena->cache_bin_array_descriptor_ql,
			    &tcache->cache_bin_array_descriptor, link);
		}
	}

	for (i = 0; i < NBINS; i++) {
		bin_postfork_child(tsdn, &arena->bins[i]);
	}
	malloc_mutex_postfork_child(tsdn, &arena->large_mtx);
	base_postfork_child(tsdn, arena->base);
	malloc_mutex_postfork_child(tsdn, &arena->extent_avail_mtx);
	extents_postfork_child(tsdn, &arena->extents_dirty);
	extents_postfork_child(tsdn, &arena->extents_muzzy);
	extents_postfork_child(tsdn, &arena->extents_retained);
	malloc_mutex_postfork_child(tsdn, &arena->extent_grow_mtx);
	malloc_mutex_postfork_child(tsdn, &arena->decay_dirty.mtx);
	malloc_mutex_postfork_child(tsdn, &arena->decay_muzzy.mtx);
	if (config_stats) {
		malloc_mutex_postfork_child(tsdn, &arena->tcache_ql_mtx);
	}
}
