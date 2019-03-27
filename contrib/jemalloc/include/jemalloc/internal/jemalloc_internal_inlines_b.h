#ifndef JEMALLOC_INTERNAL_INLINES_B_H
#define JEMALLOC_INTERNAL_INLINES_B_H

#include "jemalloc/internal/rtree.h"

/* Choose an arena based on a per-thread value. */
static inline arena_t *
arena_choose_impl(tsd_t *tsd, arena_t *arena, bool internal) {
	arena_t *ret;

	if (arena != NULL) {
		return arena;
	}

	/* During reentrancy, arena 0 is the safest bet. */
	if (unlikely(tsd_reentrancy_level_get(tsd) > 0)) {
		return arena_get(tsd_tsdn(tsd), 0, true);
	}

	ret = internal ? tsd_iarena_get(tsd) : tsd_arena_get(tsd);
	if (unlikely(ret == NULL)) {
		ret = arena_choose_hard(tsd, internal);
		assert(ret);
		if (tcache_available(tsd)) {
			tcache_t *tcache = tcache_get(tsd);
			if (tcache->arena != NULL) {
				/* See comments in tcache_data_init().*/
				assert(tcache->arena ==
				    arena_get(tsd_tsdn(tsd), 0, false));
				if (tcache->arena != ret) {
					tcache_arena_reassociate(tsd_tsdn(tsd),
					    tcache, ret);
				}
			} else {
				tcache_arena_associate(tsd_tsdn(tsd), tcache,
				    ret);
			}
		}
	}

	/*
	 * Note that for percpu arena, if the current arena is outside of the
	 * auto percpu arena range, (i.e. thread is assigned to a manually
	 * managed arena), then percpu arena is skipped.
	 */
	if (have_percpu_arena && PERCPU_ARENA_ENABLED(opt_percpu_arena) &&
	    !internal && (arena_ind_get(ret) <
	    percpu_arena_ind_limit(opt_percpu_arena)) && (ret->last_thd !=
	    tsd_tsdn(tsd))) {
		unsigned ind = percpu_arena_choose();
		if (arena_ind_get(ret) != ind) {
			percpu_arena_update(tsd, ind);
			ret = tsd_arena_get(tsd);
		}
		ret->last_thd = tsd_tsdn(tsd);
	}

	return ret;
}

static inline arena_t *
arena_choose(tsd_t *tsd, arena_t *arena) {
	return arena_choose_impl(tsd, arena, false);
}

static inline arena_t *
arena_ichoose(tsd_t *tsd, arena_t *arena) {
	return arena_choose_impl(tsd, arena, true);
}

static inline bool
arena_is_auto(arena_t *arena) {
	assert(narenas_auto > 0);
	return (arena_ind_get(arena) < narenas_auto);
}

JEMALLOC_ALWAYS_INLINE extent_t *
iealloc(tsdn_t *tsdn, const void *ptr) {
	rtree_ctx_t rtree_ctx_fallback;
	rtree_ctx_t *rtree_ctx = tsdn_rtree_ctx(tsdn, &rtree_ctx_fallback);

	return rtree_extent_read(tsdn, &extents_rtree, rtree_ctx,
	    (uintptr_t)ptr, true);
}

#endif /* JEMALLOC_INTERNAL_INLINES_B_H */
