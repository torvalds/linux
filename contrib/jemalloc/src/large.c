#define JEMALLOC_LARGE_C_
#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/jemalloc_internal_includes.h"

#include "jemalloc/internal/assert.h"
#include "jemalloc/internal/extent_mmap.h"
#include "jemalloc/internal/mutex.h"
#include "jemalloc/internal/rtree.h"
#include "jemalloc/internal/util.h"

/******************************************************************************/

void *
large_malloc(tsdn_t *tsdn, arena_t *arena, size_t usize, bool zero) {
	assert(usize == sz_s2u(usize));

	return large_palloc(tsdn, arena, usize, CACHELINE, zero);
}

void *
large_palloc(tsdn_t *tsdn, arena_t *arena, size_t usize, size_t alignment,
    bool zero) {
	size_t ausize;
	extent_t *extent;
	bool is_zeroed;
	UNUSED bool idump JEMALLOC_CC_SILENCE_INIT(false);

	assert(!tsdn_null(tsdn) || arena != NULL);

	ausize = sz_sa2u(usize, alignment);
	if (unlikely(ausize == 0 || ausize > LARGE_MAXCLASS)) {
		return NULL;
	}

	if (config_fill && unlikely(opt_zero)) {
		zero = true;
	}
	/*
	 * Copy zero into is_zeroed and pass the copy when allocating the
	 * extent, so that it is possible to make correct junk/zero fill
	 * decisions below, even if is_zeroed ends up true when zero is false.
	 */
	is_zeroed = zero;
	if (likely(!tsdn_null(tsdn))) {
		arena = arena_choose(tsdn_tsd(tsdn), arena);
	}
	if (unlikely(arena == NULL) || (extent = arena_extent_alloc_large(tsdn,
	    arena, usize, alignment, &is_zeroed)) == NULL) {
		return NULL;
	}

	/* See comments in arena_bin_slabs_full_insert(). */
	if (!arena_is_auto(arena)) {
		/* Insert extent into large. */
		malloc_mutex_lock(tsdn, &arena->large_mtx);
		extent_list_append(&arena->large, extent);
		malloc_mutex_unlock(tsdn, &arena->large_mtx);
	}
	if (config_prof && arena_prof_accum(tsdn, arena, usize)) {
		prof_idump(tsdn);
	}

	if (zero) {
		assert(is_zeroed);
	} else if (config_fill && unlikely(opt_junk_alloc)) {
		memset(extent_addr_get(extent), JEMALLOC_ALLOC_JUNK,
		    extent_usize_get(extent));
	}

	arena_decay_tick(tsdn, arena);
	return extent_addr_get(extent);
}

static void
large_dalloc_junk_impl(void *ptr, size_t size) {
	memset(ptr, JEMALLOC_FREE_JUNK, size);
}
large_dalloc_junk_t *JET_MUTABLE large_dalloc_junk = large_dalloc_junk_impl;

static void
large_dalloc_maybe_junk_impl(void *ptr, size_t size) {
	if (config_fill && have_dss && unlikely(opt_junk_free)) {
		/*
		 * Only bother junk filling if the extent isn't about to be
		 * unmapped.
		 */
		if (opt_retain || (have_dss && extent_in_dss(ptr))) {
			large_dalloc_junk(ptr, size);
		}
	}
}
large_dalloc_maybe_junk_t *JET_MUTABLE large_dalloc_maybe_junk =
    large_dalloc_maybe_junk_impl;

static bool
large_ralloc_no_move_shrink(tsdn_t *tsdn, extent_t *extent, size_t usize) {
	arena_t *arena = extent_arena_get(extent);
	size_t oldusize = extent_usize_get(extent);
	extent_hooks_t *extent_hooks = extent_hooks_get(arena);
	size_t diff = extent_size_get(extent) - (usize + sz_large_pad);

	assert(oldusize > usize);

	if (extent_hooks->split == NULL) {
		return true;
	}

	/* Split excess pages. */
	if (diff != 0) {
		extent_t *trail = extent_split_wrapper(tsdn, arena,
		    &extent_hooks, extent, usize + sz_large_pad,
		    sz_size2index(usize), false, diff, NSIZES, false);
		if (trail == NULL) {
			return true;
		}

		if (config_fill && unlikely(opt_junk_free)) {
			large_dalloc_maybe_junk(extent_addr_get(trail),
			    extent_size_get(trail));
		}

		arena_extents_dirty_dalloc(tsdn, arena, &extent_hooks, trail);
	}

	arena_extent_ralloc_large_shrink(tsdn, arena, extent, oldusize);

	return false;
}

static bool
large_ralloc_no_move_expand(tsdn_t *tsdn, extent_t *extent, size_t usize,
    bool zero) {
	arena_t *arena = extent_arena_get(extent);
	size_t oldusize = extent_usize_get(extent);
	extent_hooks_t *extent_hooks = extent_hooks_get(arena);
	size_t trailsize = usize - oldusize;

	if (extent_hooks->merge == NULL) {
		return true;
	}

	if (config_fill && unlikely(opt_zero)) {
		zero = true;
	}
	/*
	 * Copy zero into is_zeroed_trail and pass the copy when allocating the
	 * extent, so that it is possible to make correct junk/zero fill
	 * decisions below, even if is_zeroed_trail ends up true when zero is
	 * false.
	 */
	bool is_zeroed_trail = zero;
	bool commit = true;
	extent_t *trail;
	bool new_mapping;
	if ((trail = extents_alloc(tsdn, arena, &extent_hooks,
	    &arena->extents_dirty, extent_past_get(extent), trailsize, 0,
	    CACHELINE, false, NSIZES, &is_zeroed_trail, &commit)) != NULL
	    || (trail = extents_alloc(tsdn, arena, &extent_hooks,
	    &arena->extents_muzzy, extent_past_get(extent), trailsize, 0,
	    CACHELINE, false, NSIZES, &is_zeroed_trail, &commit)) != NULL) {
		if (config_stats) {
			new_mapping = false;
		}
	} else {
		if ((trail = extent_alloc_wrapper(tsdn, arena, &extent_hooks,
		    extent_past_get(extent), trailsize, 0, CACHELINE, false,
		    NSIZES, &is_zeroed_trail, &commit)) == NULL) {
			return true;
		}
		if (config_stats) {
			new_mapping = true;
		}
	}

	if (extent_merge_wrapper(tsdn, arena, &extent_hooks, extent, trail)) {
		extent_dalloc_wrapper(tsdn, arena, &extent_hooks, trail);
		return true;
	}
	rtree_ctx_t rtree_ctx_fallback;
	rtree_ctx_t *rtree_ctx = tsdn_rtree_ctx(tsdn, &rtree_ctx_fallback);
	szind_t szind = sz_size2index(usize);
	extent_szind_set(extent, szind);
	rtree_szind_slab_update(tsdn, &extents_rtree, rtree_ctx,
	    (uintptr_t)extent_addr_get(extent), szind, false);

	if (config_stats && new_mapping) {
		arena_stats_mapped_add(tsdn, &arena->stats, trailsize);
	}

	if (zero) {
		if (config_cache_oblivious) {
			/*
			 * Zero the trailing bytes of the original allocation's
			 * last page, since they are in an indeterminate state.
			 * There will always be trailing bytes, because ptr's
			 * offset from the beginning of the extent is a multiple
			 * of CACHELINE in [0 .. PAGE).
			 */
			void *zbase = (void *)
			    ((uintptr_t)extent_addr_get(extent) + oldusize);
			void *zpast = PAGE_ADDR2BASE((void *)((uintptr_t)zbase +
			    PAGE));
			size_t nzero = (uintptr_t)zpast - (uintptr_t)zbase;
			assert(nzero > 0);
			memset(zbase, 0, nzero);
		}
		assert(is_zeroed_trail);
	} else if (config_fill && unlikely(opt_junk_alloc)) {
		memset((void *)((uintptr_t)extent_addr_get(extent) + oldusize),
		    JEMALLOC_ALLOC_JUNK, usize - oldusize);
	}

	arena_extent_ralloc_large_expand(tsdn, arena, extent, oldusize);

	return false;
}

bool
large_ralloc_no_move(tsdn_t *tsdn, extent_t *extent, size_t usize_min,
    size_t usize_max, bool zero) {
	size_t oldusize = extent_usize_get(extent);

	/* The following should have been caught by callers. */
	assert(usize_min > 0 && usize_max <= LARGE_MAXCLASS);
	/* Both allocation sizes must be large to avoid a move. */
	assert(oldusize >= LARGE_MINCLASS && usize_max >= LARGE_MINCLASS);

	if (usize_max > oldusize) {
		/* Attempt to expand the allocation in-place. */
		if (!large_ralloc_no_move_expand(tsdn, extent, usize_max,
		    zero)) {
			arena_decay_tick(tsdn, extent_arena_get(extent));
			return false;
		}
		/* Try again, this time with usize_min. */
		if (usize_min < usize_max && usize_min > oldusize &&
		    large_ralloc_no_move_expand(tsdn, extent, usize_min,
		    zero)) {
			arena_decay_tick(tsdn, extent_arena_get(extent));
			return false;
		}
	}

	/*
	 * Avoid moving the allocation if the existing extent size accommodates
	 * the new size.
	 */
	if (oldusize >= usize_min && oldusize <= usize_max) {
		arena_decay_tick(tsdn, extent_arena_get(extent));
		return false;
	}

	/* Attempt to shrink the allocation in-place. */
	if (oldusize > usize_max) {
		if (!large_ralloc_no_move_shrink(tsdn, extent, usize_max)) {
			arena_decay_tick(tsdn, extent_arena_get(extent));
			return false;
		}
	}
	return true;
}

static void *
large_ralloc_move_helper(tsdn_t *tsdn, arena_t *arena, size_t usize,
    size_t alignment, bool zero) {
	if (alignment <= CACHELINE) {
		return large_malloc(tsdn, arena, usize, zero);
	}
	return large_palloc(tsdn, arena, usize, alignment, zero);
}

void *
large_ralloc(tsdn_t *tsdn, arena_t *arena, extent_t *extent, size_t usize,
    size_t alignment, bool zero, tcache_t *tcache) {
	size_t oldusize = extent_usize_get(extent);

	/* The following should have been caught by callers. */
	assert(usize > 0 && usize <= LARGE_MAXCLASS);
	/* Both allocation sizes must be large to avoid a move. */
	assert(oldusize >= LARGE_MINCLASS && usize >= LARGE_MINCLASS);

	/* Try to avoid moving the allocation. */
	if (!large_ralloc_no_move(tsdn, extent, usize, usize, zero)) {
		return extent_addr_get(extent);
	}

	/*
	 * usize and old size are different enough that we need to use a
	 * different size class.  In that case, fall back to allocating new
	 * space and copying.
	 */
	void *ret = large_ralloc_move_helper(tsdn, arena, usize, alignment,
	    zero);
	if (ret == NULL) {
		return NULL;
	}

	size_t copysize = (usize < oldusize) ? usize : oldusize;
	memcpy(ret, extent_addr_get(extent), copysize);
	isdalloct(tsdn, extent_addr_get(extent), oldusize, tcache, NULL, true);
	return ret;
}

/*
 * junked_locked indicates whether the extent's data have been junk-filled, and
 * whether the arena's large_mtx is currently held.
 */
static void
large_dalloc_prep_impl(tsdn_t *tsdn, arena_t *arena, extent_t *extent,
    bool junked_locked) {
	if (!junked_locked) {
		/* See comments in arena_bin_slabs_full_insert(). */
		if (!arena_is_auto(arena)) {
			malloc_mutex_lock(tsdn, &arena->large_mtx);
			extent_list_remove(&arena->large, extent);
			malloc_mutex_unlock(tsdn, &arena->large_mtx);
		}
		large_dalloc_maybe_junk(extent_addr_get(extent),
		    extent_usize_get(extent));
	} else {
		malloc_mutex_assert_owner(tsdn, &arena->large_mtx);
		if (!arena_is_auto(arena)) {
			extent_list_remove(&arena->large, extent);
		}
	}
	arena_extent_dalloc_large_prep(tsdn, arena, extent);
}

static void
large_dalloc_finish_impl(tsdn_t *tsdn, arena_t *arena, extent_t *extent) {
	extent_hooks_t *extent_hooks = EXTENT_HOOKS_INITIALIZER;
	arena_extents_dirty_dalloc(tsdn, arena, &extent_hooks, extent);
}

void
large_dalloc_prep_junked_locked(tsdn_t *tsdn, extent_t *extent) {
	large_dalloc_prep_impl(tsdn, extent_arena_get(extent), extent, true);
}

void
large_dalloc_finish(tsdn_t *tsdn, extent_t *extent) {
	large_dalloc_finish_impl(tsdn, extent_arena_get(extent), extent);
}

void
large_dalloc(tsdn_t *tsdn, extent_t *extent) {
	arena_t *arena = extent_arena_get(extent);
	large_dalloc_prep_impl(tsdn, arena, extent, false);
	large_dalloc_finish_impl(tsdn, arena, extent);
	arena_decay_tick(tsdn, arena);
}

size_t
large_salloc(tsdn_t *tsdn, const extent_t *extent) {
	return extent_usize_get(extent);
}

prof_tctx_t *
large_prof_tctx_get(tsdn_t *tsdn, const extent_t *extent) {
	return extent_prof_tctx_get(extent);
}

void
large_prof_tctx_set(tsdn_t *tsdn, extent_t *extent, prof_tctx_t *tctx) {
	extent_prof_tctx_set(extent, tctx);
}

void
large_prof_tctx_reset(tsdn_t *tsdn, extent_t *extent) {
	large_prof_tctx_set(tsdn, extent, (prof_tctx_t *)(uintptr_t)1U);
}
