#ifndef JEMALLOC_INTERNAL_PROF_INLINES_A_H
#define JEMALLOC_INTERNAL_PROF_INLINES_A_H

#include "jemalloc/internal/mutex.h"

static inline bool
prof_accum_add(tsdn_t *tsdn, prof_accum_t *prof_accum, uint64_t accumbytes) {
	cassert(config_prof);

	bool overflow;
	uint64_t a0, a1;

	/*
	 * If the application allocates fast enough (and/or if idump is slow
	 * enough), extreme overflow here (a1 >= prof_interval * 2) can cause
	 * idump trigger coalescing.  This is an intentional mechanism that
	 * avoids rate-limiting allocation.
	 */
#ifdef JEMALLOC_ATOMIC_U64
	a0 = atomic_load_u64(&prof_accum->accumbytes, ATOMIC_RELAXED);
	do {
		a1 = a0 + accumbytes;
		assert(a1 >= a0);
		overflow = (a1 >= prof_interval);
		if (overflow) {
			a1 %= prof_interval;
		}
	} while (!atomic_compare_exchange_weak_u64(&prof_accum->accumbytes, &a0,
	    a1, ATOMIC_RELAXED, ATOMIC_RELAXED));
#else
	malloc_mutex_lock(tsdn, &prof_accum->mtx);
	a0 = prof_accum->accumbytes;
	a1 = a0 + accumbytes;
	overflow = (a1 >= prof_interval);
	if (overflow) {
		a1 %= prof_interval;
	}
	prof_accum->accumbytes = a1;
	malloc_mutex_unlock(tsdn, &prof_accum->mtx);
#endif
	return overflow;
}

static inline void
prof_accum_cancel(tsdn_t *tsdn, prof_accum_t *prof_accum, size_t usize) {
	cassert(config_prof);

	/*
	 * Cancel out as much of the excessive prof_accumbytes increase as
	 * possible without underflowing.  Interval-triggered dumps occur
	 * slightly more often than intended as a result of incomplete
	 * canceling.
	 */
	uint64_t a0, a1;
#ifdef JEMALLOC_ATOMIC_U64
	a0 = atomic_load_u64(&prof_accum->accumbytes, ATOMIC_RELAXED);
	do {
		a1 = (a0 >= LARGE_MINCLASS - usize) ?  a0 - (LARGE_MINCLASS -
		    usize) : 0;
	} while (!atomic_compare_exchange_weak_u64(&prof_accum->accumbytes, &a0,
	    a1, ATOMIC_RELAXED, ATOMIC_RELAXED));
#else
	malloc_mutex_lock(tsdn, &prof_accum->mtx);
	a0 = prof_accum->accumbytes;
	a1 = (a0 >= LARGE_MINCLASS - usize) ?  a0 - (LARGE_MINCLASS - usize) :
	    0;
	prof_accum->accumbytes = a1;
	malloc_mutex_unlock(tsdn, &prof_accum->mtx);
#endif
}

JEMALLOC_ALWAYS_INLINE bool
prof_active_get_unlocked(void) {
	/*
	 * Even if opt_prof is true, sampling can be temporarily disabled by
	 * setting prof_active to false.  No locking is used when reading
	 * prof_active in the fast path, so there are no guarantees regarding
	 * how long it will take for all threads to notice state changes.
	 */
	return prof_active;
}

#endif /* JEMALLOC_INTERNAL_PROF_INLINES_A_H */
