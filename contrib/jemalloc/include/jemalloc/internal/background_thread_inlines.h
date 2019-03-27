#ifndef JEMALLOC_INTERNAL_BACKGROUND_THREAD_INLINES_H
#define JEMALLOC_INTERNAL_BACKGROUND_THREAD_INLINES_H

JEMALLOC_ALWAYS_INLINE bool
background_thread_enabled(void) {
	return atomic_load_b(&background_thread_enabled_state, ATOMIC_RELAXED);
}

JEMALLOC_ALWAYS_INLINE void
background_thread_enabled_set(tsdn_t *tsdn, bool state) {
	malloc_mutex_assert_owner(tsdn, &background_thread_lock);
	atomic_store_b(&background_thread_enabled_state, state, ATOMIC_RELAXED);
}

JEMALLOC_ALWAYS_INLINE background_thread_info_t *
arena_background_thread_info_get(arena_t *arena) {
	unsigned arena_ind = arena_ind_get(arena);
	return &background_thread_info[arena_ind % ncpus];
}

JEMALLOC_ALWAYS_INLINE uint64_t
background_thread_wakeup_time_get(background_thread_info_t *info) {
	uint64_t next_wakeup = nstime_ns(&info->next_wakeup);
	assert(atomic_load_b(&info->indefinite_sleep, ATOMIC_ACQUIRE) ==
	    (next_wakeup == BACKGROUND_THREAD_INDEFINITE_SLEEP));
	return next_wakeup;
}

JEMALLOC_ALWAYS_INLINE void
background_thread_wakeup_time_set(tsdn_t *tsdn, background_thread_info_t *info,
    uint64_t wakeup_time) {
	malloc_mutex_assert_owner(tsdn, &info->mtx);
	atomic_store_b(&info->indefinite_sleep,
	    wakeup_time == BACKGROUND_THREAD_INDEFINITE_SLEEP, ATOMIC_RELEASE);
	nstime_init(&info->next_wakeup, wakeup_time);
}

JEMALLOC_ALWAYS_INLINE bool
background_thread_indefinite_sleep(background_thread_info_t *info) {
	return atomic_load_b(&info->indefinite_sleep, ATOMIC_ACQUIRE);
}

JEMALLOC_ALWAYS_INLINE void
arena_background_thread_inactivity_check(tsdn_t *tsdn, arena_t *arena,
    bool is_background_thread) {
	if (!background_thread_enabled() || is_background_thread) {
		return;
	}
	background_thread_info_t *info =
	    arena_background_thread_info_get(arena);
	if (background_thread_indefinite_sleep(info)) {
		background_thread_interval_check(tsdn, arena,
		    &arena->decay_dirty, 0);
	}
}

#endif /* JEMALLOC_INTERNAL_BACKGROUND_THREAD_INLINES_H */
