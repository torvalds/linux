#ifndef JEMALLOC_INTERNAL_PROF_INLINES_B_H
#define JEMALLOC_INTERNAL_PROF_INLINES_B_H

#include "jemalloc/internal/sz.h"

JEMALLOC_ALWAYS_INLINE bool
prof_gdump_get_unlocked(void) {
	/*
	 * No locking is used when reading prof_gdump_val in the fast path, so
	 * there are no guarantees regarding how long it will take for all
	 * threads to notice state changes.
	 */
	return prof_gdump_val;
}

JEMALLOC_ALWAYS_INLINE prof_tdata_t *
prof_tdata_get(tsd_t *tsd, bool create) {
	prof_tdata_t *tdata;

	cassert(config_prof);

	tdata = tsd_prof_tdata_get(tsd);
	if (create) {
		if (unlikely(tdata == NULL)) {
			if (tsd_nominal(tsd)) {
				tdata = prof_tdata_init(tsd);
				tsd_prof_tdata_set(tsd, tdata);
			}
		} else if (unlikely(tdata->expired)) {
			tdata = prof_tdata_reinit(tsd, tdata);
			tsd_prof_tdata_set(tsd, tdata);
		}
		assert(tdata == NULL || tdata->attached);
	}

	return tdata;
}

JEMALLOC_ALWAYS_INLINE prof_tctx_t *
prof_tctx_get(tsdn_t *tsdn, const void *ptr, alloc_ctx_t *alloc_ctx) {
	cassert(config_prof);
	assert(ptr != NULL);

	return arena_prof_tctx_get(tsdn, ptr, alloc_ctx);
}

JEMALLOC_ALWAYS_INLINE void
prof_tctx_set(tsdn_t *tsdn, const void *ptr, size_t usize,
    alloc_ctx_t *alloc_ctx, prof_tctx_t *tctx) {
	cassert(config_prof);
	assert(ptr != NULL);

	arena_prof_tctx_set(tsdn, ptr, usize, alloc_ctx, tctx);
}

JEMALLOC_ALWAYS_INLINE void
prof_tctx_reset(tsdn_t *tsdn, const void *ptr, prof_tctx_t *tctx) {
	cassert(config_prof);
	assert(ptr != NULL);

	arena_prof_tctx_reset(tsdn, ptr, tctx);
}

JEMALLOC_ALWAYS_INLINE bool
prof_sample_accum_update(tsd_t *tsd, size_t usize, bool update,
    prof_tdata_t **tdata_out) {
	prof_tdata_t *tdata;

	cassert(config_prof);

	tdata = prof_tdata_get(tsd, true);
	if (unlikely((uintptr_t)tdata <= (uintptr_t)PROF_TDATA_STATE_MAX)) {
		tdata = NULL;
	}

	if (tdata_out != NULL) {
		*tdata_out = tdata;
	}

	if (unlikely(tdata == NULL)) {
		return true;
	}

	if (likely(tdata->bytes_until_sample >= usize)) {
		if (update) {
			tdata->bytes_until_sample -= usize;
		}
		return true;
	} else {
		if (tsd_reentrancy_level_get(tsd) > 0) {
			return true;
		}
		/* Compute new sample threshold. */
		if (update) {
			prof_sample_threshold_update(tdata);
		}
		return !tdata->active;
	}
}

JEMALLOC_ALWAYS_INLINE prof_tctx_t *
prof_alloc_prep(tsd_t *tsd, size_t usize, bool prof_active, bool update) {
	prof_tctx_t *ret;
	prof_tdata_t *tdata;
	prof_bt_t bt;

	assert(usize == sz_s2u(usize));

	if (!prof_active || likely(prof_sample_accum_update(tsd, usize, update,
	    &tdata))) {
		ret = (prof_tctx_t *)(uintptr_t)1U;
	} else {
		bt_init(&bt, tdata->vec);
		prof_backtrace(&bt);
		ret = prof_lookup(tsd, &bt);
	}

	return ret;
}

JEMALLOC_ALWAYS_INLINE void
prof_malloc(tsdn_t *tsdn, const void *ptr, size_t usize, alloc_ctx_t *alloc_ctx,
    prof_tctx_t *tctx) {
	cassert(config_prof);
	assert(ptr != NULL);
	assert(usize == isalloc(tsdn, ptr));

	if (unlikely((uintptr_t)tctx > (uintptr_t)1U)) {
		prof_malloc_sample_object(tsdn, ptr, usize, tctx);
	} else {
		prof_tctx_set(tsdn, ptr, usize, alloc_ctx,
		    (prof_tctx_t *)(uintptr_t)1U);
	}
}

JEMALLOC_ALWAYS_INLINE void
prof_realloc(tsd_t *tsd, const void *ptr, size_t usize, prof_tctx_t *tctx,
    bool prof_active, bool updated, const void *old_ptr, size_t old_usize,
    prof_tctx_t *old_tctx) {
	bool sampled, old_sampled, moved;

	cassert(config_prof);
	assert(ptr != NULL || (uintptr_t)tctx <= (uintptr_t)1U);

	if (prof_active && !updated && ptr != NULL) {
		assert(usize == isalloc(tsd_tsdn(tsd), ptr));
		if (prof_sample_accum_update(tsd, usize, true, NULL)) {
			/*
			 * Don't sample.  The usize passed to prof_alloc_prep()
			 * was larger than what actually got allocated, so a
			 * backtrace was captured for this allocation, even
			 * though its actual usize was insufficient to cross the
			 * sample threshold.
			 */
			prof_alloc_rollback(tsd, tctx, true);
			tctx = (prof_tctx_t *)(uintptr_t)1U;
		}
	}

	sampled = ((uintptr_t)tctx > (uintptr_t)1U);
	old_sampled = ((uintptr_t)old_tctx > (uintptr_t)1U);
	moved = (ptr != old_ptr);

	if (unlikely(sampled)) {
		prof_malloc_sample_object(tsd_tsdn(tsd), ptr, usize, tctx);
	} else if (moved) {
		prof_tctx_set(tsd_tsdn(tsd), ptr, usize, NULL,
		    (prof_tctx_t *)(uintptr_t)1U);
	} else if (unlikely(old_sampled)) {
		/*
		 * prof_tctx_set() would work for the !moved case as well, but
		 * prof_tctx_reset() is slightly cheaper, and the proper thing
		 * to do here in the presence of explicit knowledge re: moved
		 * state.
		 */
		prof_tctx_reset(tsd_tsdn(tsd), ptr, tctx);
	} else {
		assert((uintptr_t)prof_tctx_get(tsd_tsdn(tsd), ptr, NULL) ==
		    (uintptr_t)1U);
	}

	/*
	 * The prof_free_sampled_object() call must come after the
	 * prof_malloc_sample_object() call, because tctx and old_tctx may be
	 * the same, in which case reversing the call order could cause the tctx
	 * to be prematurely destroyed as a side effect of momentarily zeroed
	 * counters.
	 */
	if (unlikely(old_sampled)) {
		prof_free_sampled_object(tsd, old_usize, old_tctx);
	}
}

JEMALLOC_ALWAYS_INLINE void
prof_free(tsd_t *tsd, const void *ptr, size_t usize, alloc_ctx_t *alloc_ctx) {
	prof_tctx_t *tctx = prof_tctx_get(tsd_tsdn(tsd), ptr, alloc_ctx);

	cassert(config_prof);
	assert(usize == isalloc(tsd_tsdn(tsd), ptr));

	if (unlikely((uintptr_t)tctx > (uintptr_t)1U)) {
		prof_free_sampled_object(tsd, usize, tctx);
	}
}

#endif /* JEMALLOC_INTERNAL_PROF_INLINES_B_H */
