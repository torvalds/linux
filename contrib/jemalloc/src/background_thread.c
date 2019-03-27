#define JEMALLOC_BACKGROUND_THREAD_C_
#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/jemalloc_internal_includes.h"

#include "jemalloc/internal/assert.h"

/******************************************************************************/
/* Data. */

/* This option should be opt-in only. */
#define BACKGROUND_THREAD_DEFAULT false
/* Read-only after initialization. */
bool opt_background_thread = BACKGROUND_THREAD_DEFAULT;
size_t opt_max_background_threads = MAX_BACKGROUND_THREAD_LIMIT;

/* Used for thread creation, termination and stats. */
malloc_mutex_t background_thread_lock;
/* Indicates global state.  Atomic because decay reads this w/o locking. */
atomic_b_t background_thread_enabled_state;
size_t n_background_threads;
size_t max_background_threads;
/* Thread info per-index. */
background_thread_info_t *background_thread_info;

/* False if no necessary runtime support. */
bool can_enable_background_thread;

/******************************************************************************/

#ifdef JEMALLOC_PTHREAD_CREATE_WRAPPER
#include <dlfcn.h>

static int (*pthread_create_fptr)(pthread_t *__restrict, const pthread_attr_t *,
    void *(*)(void *), void *__restrict);

static void
pthread_create_wrapper_init(void) {
#ifdef JEMALLOC_LAZY_LOCK
	if (!isthreaded) {
		isthreaded = true;
	}
#endif
}

int
pthread_create_wrapper(pthread_t *__restrict thread, const pthread_attr_t *attr,
    void *(*start_routine)(void *), void *__restrict arg) {
	pthread_create_wrapper_init();

	return pthread_create_fptr(thread, attr, start_routine, arg);
}
#endif /* JEMALLOC_PTHREAD_CREATE_WRAPPER */

#ifndef JEMALLOC_BACKGROUND_THREAD
#define NOT_REACHED { not_reached(); }
bool background_thread_create(tsd_t *tsd, unsigned arena_ind) NOT_REACHED
bool background_threads_enable(tsd_t *tsd) NOT_REACHED
bool background_threads_disable(tsd_t *tsd) NOT_REACHED
void background_thread_interval_check(tsdn_t *tsdn, arena_t *arena,
    arena_decay_t *decay, size_t npages_new) NOT_REACHED
void background_thread_prefork0(tsdn_t *tsdn) NOT_REACHED
void background_thread_prefork1(tsdn_t *tsdn) NOT_REACHED
void background_thread_postfork_parent(tsdn_t *tsdn) NOT_REACHED
void background_thread_postfork_child(tsdn_t *tsdn) NOT_REACHED
bool background_thread_stats_read(tsdn_t *tsdn,
    background_thread_stats_t *stats) NOT_REACHED
void background_thread_ctl_init(tsdn_t *tsdn) NOT_REACHED
#undef NOT_REACHED
#else

static bool background_thread_enabled_at_fork;

static void
background_thread_info_init(tsdn_t *tsdn, background_thread_info_t *info) {
	background_thread_wakeup_time_set(tsdn, info, 0);
	info->npages_to_purge_new = 0;
	if (config_stats) {
		info->tot_n_runs = 0;
		nstime_init(&info->tot_sleep_time, 0);
	}
}

static inline bool
set_current_thread_affinity(UNUSED int cpu) {
#if defined(JEMALLOC_HAVE_SCHED_SETAFFINITY)
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(cpu, &cpuset);
	int ret = sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);

	return (ret != 0);
#else
	return false;
#endif
}

/* Threshold for determining when to wake up the background thread. */
#define BACKGROUND_THREAD_NPAGES_THRESHOLD UINT64_C(1024)
#define BILLION UINT64_C(1000000000)
/* Minimal sleep interval 100 ms. */
#define BACKGROUND_THREAD_MIN_INTERVAL_NS (BILLION / 10)

static inline size_t
decay_npurge_after_interval(arena_decay_t *decay, size_t interval) {
	size_t i;
	uint64_t sum = 0;
	for (i = 0; i < interval; i++) {
		sum += decay->backlog[i] * h_steps[i];
	}
	for (; i < SMOOTHSTEP_NSTEPS; i++) {
		sum += decay->backlog[i] * (h_steps[i] - h_steps[i - interval]);
	}

	return (size_t)(sum >> SMOOTHSTEP_BFP);
}

static uint64_t
arena_decay_compute_purge_interval_impl(tsdn_t *tsdn, arena_decay_t *decay,
    extents_t *extents) {
	if (malloc_mutex_trylock(tsdn, &decay->mtx)) {
		/* Use minimal interval if decay is contended. */
		return BACKGROUND_THREAD_MIN_INTERVAL_NS;
	}

	uint64_t interval;
	ssize_t decay_time = atomic_load_zd(&decay->time_ms, ATOMIC_RELAXED);
	if (decay_time <= 0) {
		/* Purging is eagerly done or disabled currently. */
		interval = BACKGROUND_THREAD_INDEFINITE_SLEEP;
		goto label_done;
	}

	uint64_t decay_interval_ns = nstime_ns(&decay->interval);
	assert(decay_interval_ns > 0);
	size_t npages = extents_npages_get(extents);
	if (npages == 0) {
		unsigned i;
		for (i = 0; i < SMOOTHSTEP_NSTEPS; i++) {
			if (decay->backlog[i] > 0) {
				break;
			}
		}
		if (i == SMOOTHSTEP_NSTEPS) {
			/* No dirty pages recorded.  Sleep indefinitely. */
			interval = BACKGROUND_THREAD_INDEFINITE_SLEEP;
			goto label_done;
		}
	}
	if (npages <= BACKGROUND_THREAD_NPAGES_THRESHOLD) {
		/* Use max interval. */
		interval = decay_interval_ns * SMOOTHSTEP_NSTEPS;
		goto label_done;
	}

	size_t lb = BACKGROUND_THREAD_MIN_INTERVAL_NS / decay_interval_ns;
	size_t ub = SMOOTHSTEP_NSTEPS;
	/* Minimal 2 intervals to ensure reaching next epoch deadline. */
	lb = (lb < 2) ? 2 : lb;
	if ((decay_interval_ns * ub <= BACKGROUND_THREAD_MIN_INTERVAL_NS) ||
	    (lb + 2 > ub)) {
		interval = BACKGROUND_THREAD_MIN_INTERVAL_NS;
		goto label_done;
	}

	assert(lb + 2 <= ub);
	size_t npurge_lb, npurge_ub;
	npurge_lb = decay_npurge_after_interval(decay, lb);
	if (npurge_lb > BACKGROUND_THREAD_NPAGES_THRESHOLD) {
		interval = decay_interval_ns * lb;
		goto label_done;
	}
	npurge_ub = decay_npurge_after_interval(decay, ub);
	if (npurge_ub < BACKGROUND_THREAD_NPAGES_THRESHOLD) {
		interval = decay_interval_ns * ub;
		goto label_done;
	}

	unsigned n_search = 0;
	size_t target, npurge;
	while ((npurge_lb + BACKGROUND_THREAD_NPAGES_THRESHOLD < npurge_ub)
	    && (lb + 2 < ub)) {
		target = (lb + ub) / 2;
		npurge = decay_npurge_after_interval(decay, target);
		if (npurge > BACKGROUND_THREAD_NPAGES_THRESHOLD) {
			ub = target;
			npurge_ub = npurge;
		} else {
			lb = target;
			npurge_lb = npurge;
		}
		assert(n_search++ < lg_floor(SMOOTHSTEP_NSTEPS) + 1);
	}
	interval = decay_interval_ns * (ub + lb) / 2;
label_done:
	interval = (interval < BACKGROUND_THREAD_MIN_INTERVAL_NS) ?
	    BACKGROUND_THREAD_MIN_INTERVAL_NS : interval;
	malloc_mutex_unlock(tsdn, &decay->mtx);

	return interval;
}

/* Compute purge interval for background threads. */
static uint64_t
arena_decay_compute_purge_interval(tsdn_t *tsdn, arena_t *arena) {
	uint64_t i1, i2;
	i1 = arena_decay_compute_purge_interval_impl(tsdn, &arena->decay_dirty,
	    &arena->extents_dirty);
	if (i1 == BACKGROUND_THREAD_MIN_INTERVAL_NS) {
		return i1;
	}
	i2 = arena_decay_compute_purge_interval_impl(tsdn, &arena->decay_muzzy,
	    &arena->extents_muzzy);

	return i1 < i2 ? i1 : i2;
}

static void
background_thread_sleep(tsdn_t *tsdn, background_thread_info_t *info,
    uint64_t interval) {
	if (config_stats) {
		info->tot_n_runs++;
	}
	info->npages_to_purge_new = 0;

	struct timeval tv;
	/* Specific clock required by timedwait. */
	gettimeofday(&tv, NULL);
	nstime_t before_sleep;
	nstime_init2(&before_sleep, tv.tv_sec, tv.tv_usec * 1000);

	int ret;
	if (interval == BACKGROUND_THREAD_INDEFINITE_SLEEP) {
		assert(background_thread_indefinite_sleep(info));
		ret = pthread_cond_wait(&info->cond, &info->mtx.lock);
		assert(ret == 0);
	} else {
		assert(interval >= BACKGROUND_THREAD_MIN_INTERVAL_NS &&
		    interval <= BACKGROUND_THREAD_INDEFINITE_SLEEP);
		/* We need malloc clock (can be different from tv). */
		nstime_t next_wakeup;
		nstime_init(&next_wakeup, 0);
		nstime_update(&next_wakeup);
		nstime_iadd(&next_wakeup, interval);
		assert(nstime_ns(&next_wakeup) <
		    BACKGROUND_THREAD_INDEFINITE_SLEEP);
		background_thread_wakeup_time_set(tsdn, info,
		    nstime_ns(&next_wakeup));

		nstime_t ts_wakeup;
		nstime_copy(&ts_wakeup, &before_sleep);
		nstime_iadd(&ts_wakeup, interval);
		struct timespec ts;
		ts.tv_sec = (size_t)nstime_sec(&ts_wakeup);
		ts.tv_nsec = (size_t)nstime_nsec(&ts_wakeup);

		assert(!background_thread_indefinite_sleep(info));
		ret = pthread_cond_timedwait(&info->cond, &info->mtx.lock, &ts);
		assert(ret == ETIMEDOUT || ret == 0);
		background_thread_wakeup_time_set(tsdn, info,
		    BACKGROUND_THREAD_INDEFINITE_SLEEP);
	}
	if (config_stats) {
		gettimeofday(&tv, NULL);
		nstime_t after_sleep;
		nstime_init2(&after_sleep, tv.tv_sec, tv.tv_usec * 1000);
		if (nstime_compare(&after_sleep, &before_sleep) > 0) {
			nstime_subtract(&after_sleep, &before_sleep);
			nstime_add(&info->tot_sleep_time, &after_sleep);
		}
	}
}

static bool
background_thread_pause_check(tsdn_t *tsdn, background_thread_info_t *info) {
	if (unlikely(info->state == background_thread_paused)) {
		malloc_mutex_unlock(tsdn, &info->mtx);
		/* Wait on global lock to update status. */
		malloc_mutex_lock(tsdn, &background_thread_lock);
		malloc_mutex_unlock(tsdn, &background_thread_lock);
		malloc_mutex_lock(tsdn, &info->mtx);
		return true;
	}

	return false;
}

static inline void
background_work_sleep_once(tsdn_t *tsdn, background_thread_info_t *info, unsigned ind) {
	uint64_t min_interval = BACKGROUND_THREAD_INDEFINITE_SLEEP;
	unsigned narenas = narenas_total_get();

	for (unsigned i = ind; i < narenas; i += max_background_threads) {
		arena_t *arena = arena_get(tsdn, i, false);
		if (!arena) {
			continue;
		}
		arena_decay(tsdn, arena, true, false);
		if (min_interval == BACKGROUND_THREAD_MIN_INTERVAL_NS) {
			/* Min interval will be used. */
			continue;
		}
		uint64_t interval = arena_decay_compute_purge_interval(tsdn,
		    arena);
		assert(interval >= BACKGROUND_THREAD_MIN_INTERVAL_NS);
		if (min_interval > interval) {
			min_interval = interval;
		}
	}
	background_thread_sleep(tsdn, info, min_interval);
}

static bool
background_threads_disable_single(tsd_t *tsd, background_thread_info_t *info) {
	if (info == &background_thread_info[0]) {
		malloc_mutex_assert_owner(tsd_tsdn(tsd),
		    &background_thread_lock);
	} else {
		malloc_mutex_assert_not_owner(tsd_tsdn(tsd),
		    &background_thread_lock);
	}

	pre_reentrancy(tsd, NULL);
	malloc_mutex_lock(tsd_tsdn(tsd), &info->mtx);
	bool has_thread;
	assert(info->state != background_thread_paused);
	if (info->state == background_thread_started) {
		has_thread = true;
		info->state = background_thread_stopped;
		pthread_cond_signal(&info->cond);
	} else {
		has_thread = false;
	}
	malloc_mutex_unlock(tsd_tsdn(tsd), &info->mtx);

	if (!has_thread) {
		post_reentrancy(tsd);
		return false;
	}
	void *ret;
	if (pthread_join(info->thread, &ret)) {
		post_reentrancy(tsd);
		return true;
	}
	assert(ret == NULL);
	n_background_threads--;
	post_reentrancy(tsd);

	return false;
}

static void *background_thread_entry(void *ind_arg);

static int
background_thread_create_signals_masked(pthread_t *thread,
    const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg) {
	/*
	 * Mask signals during thread creation so that the thread inherits
	 * an empty signal set.
	 */
	sigset_t set;
	sigfillset(&set);
	sigset_t oldset;
	int mask_err = pthread_sigmask(SIG_SETMASK, &set, &oldset);
	if (mask_err != 0) {
		return mask_err;
	}
	int create_err = pthread_create_wrapper(thread, attr, start_routine,
	    arg);
	/*
	 * Restore the signal mask.  Failure to restore the signal mask here
	 * changes program behavior.
	 */
	int restore_err = pthread_sigmask(SIG_SETMASK, &oldset, NULL);
	if (restore_err != 0) {
		malloc_printf("<jemalloc>: background thread creation "
		    "failed (%d), and signal mask restoration failed "
		    "(%d)\n", create_err, restore_err);
		if (opt_abort) {
			abort();
		}
	}
	return create_err;
}

static bool
check_background_thread_creation(tsd_t *tsd, unsigned *n_created,
    bool *created_threads) {
	bool ret = false;
	if (likely(*n_created == n_background_threads)) {
		return ret;
	}

	tsdn_t *tsdn = tsd_tsdn(tsd);
	malloc_mutex_unlock(tsdn, &background_thread_info[0].mtx);
	for (unsigned i = 1; i < max_background_threads; i++) {
		if (created_threads[i]) {
			continue;
		}
		background_thread_info_t *info = &background_thread_info[i];
		malloc_mutex_lock(tsdn, &info->mtx);
		/*
		 * In case of the background_thread_paused state because of
		 * arena reset, delay the creation.
		 */
		bool create = (info->state == background_thread_started);
		malloc_mutex_unlock(tsdn, &info->mtx);
		if (!create) {
			continue;
		}

		pre_reentrancy(tsd, NULL);
		int err = background_thread_create_signals_masked(&info->thread,
		    NULL, background_thread_entry, (void *)(uintptr_t)i);
		post_reentrancy(tsd);

		if (err == 0) {
			(*n_created)++;
			created_threads[i] = true;
		} else {
			malloc_printf("<jemalloc>: background thread "
			    "creation failed (%d)\n", err);
			if (opt_abort) {
				abort();
			}
		}
		/* Return to restart the loop since we unlocked. */
		ret = true;
		break;
	}
	malloc_mutex_lock(tsdn, &background_thread_info[0].mtx);

	return ret;
}

static void
background_thread0_work(tsd_t *tsd) {
	/* Thread0 is also responsible for launching / terminating threads. */
	VARIABLE_ARRAY(bool, created_threads, max_background_threads);
	unsigned i;
	for (i = 1; i < max_background_threads; i++) {
		created_threads[i] = false;
	}
	/* Start working, and create more threads when asked. */
	unsigned n_created = 1;
	while (background_thread_info[0].state != background_thread_stopped) {
		if (background_thread_pause_check(tsd_tsdn(tsd),
		    &background_thread_info[0])) {
			continue;
		}
		if (check_background_thread_creation(tsd, &n_created,
		    (bool *)&created_threads)) {
			continue;
		}
		background_work_sleep_once(tsd_tsdn(tsd),
		    &background_thread_info[0], 0);
	}

	/*
	 * Shut down other threads at exit.  Note that the ctl thread is holding
	 * the global background_thread mutex (and is waiting) for us.
	 */
	assert(!background_thread_enabled());
	for (i = 1; i < max_background_threads; i++) {
		background_thread_info_t *info = &background_thread_info[i];
		assert(info->state != background_thread_paused);
		if (created_threads[i]) {
			background_threads_disable_single(tsd, info);
		} else {
			malloc_mutex_lock(tsd_tsdn(tsd), &info->mtx);
			if (info->state != background_thread_stopped) {
				/* The thread was not created. */
				assert(info->state ==
				    background_thread_started);
				n_background_threads--;
				info->state = background_thread_stopped;
			}
			malloc_mutex_unlock(tsd_tsdn(tsd), &info->mtx);
		}
	}
	background_thread_info[0].state = background_thread_stopped;
	assert(n_background_threads == 1);
}

static void
background_work(tsd_t *tsd, unsigned ind) {
	background_thread_info_t *info = &background_thread_info[ind];

	malloc_mutex_lock(tsd_tsdn(tsd), &info->mtx);
	background_thread_wakeup_time_set(tsd_tsdn(tsd), info,
	    BACKGROUND_THREAD_INDEFINITE_SLEEP);
	if (ind == 0) {
		background_thread0_work(tsd);
	} else {
		while (info->state != background_thread_stopped) {
			if (background_thread_pause_check(tsd_tsdn(tsd),
			    info)) {
				continue;
			}
			background_work_sleep_once(tsd_tsdn(tsd), info, ind);
		}
	}
	assert(info->state == background_thread_stopped);
	background_thread_wakeup_time_set(tsd_tsdn(tsd), info, 0);
	malloc_mutex_unlock(tsd_tsdn(tsd), &info->mtx);
}

static void *
background_thread_entry(void *ind_arg) {
	unsigned thread_ind = (unsigned)(uintptr_t)ind_arg;
	assert(thread_ind < max_background_threads);
#ifdef JEMALLOC_HAVE_PTHREAD_SETNAME_NP
	pthread_setname_np(pthread_self(), "jemalloc_bg_thd");
#endif
	if (opt_percpu_arena != percpu_arena_disabled) {
		set_current_thread_affinity((int)thread_ind);
	}
	/*
	 * Start periodic background work.  We use internal tsd which avoids
	 * side effects, for example triggering new arena creation (which in
	 * turn triggers another background thread creation).
	 */
	background_work(tsd_internal_fetch(), thread_ind);
	assert(pthread_equal(pthread_self(),
	    background_thread_info[thread_ind].thread));

	return NULL;
}

static void
background_thread_init(tsd_t *tsd, background_thread_info_t *info) {
	malloc_mutex_assert_owner(tsd_tsdn(tsd), &background_thread_lock);
	info->state = background_thread_started;
	background_thread_info_init(tsd_tsdn(tsd), info);
	n_background_threads++;
}

/* Create a new background thread if needed. */
bool
background_thread_create(tsd_t *tsd, unsigned arena_ind) {
	assert(have_background_thread);
	malloc_mutex_assert_owner(tsd_tsdn(tsd), &background_thread_lock);

	/* We create at most NCPUs threads. */
	size_t thread_ind = arena_ind % max_background_threads;
	background_thread_info_t *info = &background_thread_info[thread_ind];

	bool need_new_thread;
	malloc_mutex_lock(tsd_tsdn(tsd), &info->mtx);
	need_new_thread = background_thread_enabled() &&
	    (info->state == background_thread_stopped);
	if (need_new_thread) {
		background_thread_init(tsd, info);
	}
	malloc_mutex_unlock(tsd_tsdn(tsd), &info->mtx);
	if (!need_new_thread) {
		return false;
	}
	if (arena_ind != 0) {
		/* Threads are created asynchronously by Thread 0. */
		background_thread_info_t *t0 = &background_thread_info[0];
		malloc_mutex_lock(tsd_tsdn(tsd), &t0->mtx);
		assert(t0->state == background_thread_started);
		pthread_cond_signal(&t0->cond);
		malloc_mutex_unlock(tsd_tsdn(tsd), &t0->mtx);

		return false;
	}

	pre_reentrancy(tsd, NULL);
	/*
	 * To avoid complications (besides reentrancy), create internal
	 * background threads with the underlying pthread_create.
	 */
	int err = background_thread_create_signals_masked(&info->thread, NULL,
	    background_thread_entry, (void *)thread_ind);
	post_reentrancy(tsd);

	if (err != 0) {
		malloc_printf("<jemalloc>: arena 0 background thread creation "
		    "failed (%d)\n", err);
		malloc_mutex_lock(tsd_tsdn(tsd), &info->mtx);
		info->state = background_thread_stopped;
		n_background_threads--;
		malloc_mutex_unlock(tsd_tsdn(tsd), &info->mtx);

		return true;
	}

	return false;
}

bool
background_threads_enable(tsd_t *tsd) {
	assert(n_background_threads == 0);
	assert(background_thread_enabled());
	malloc_mutex_assert_owner(tsd_tsdn(tsd), &background_thread_lock);

	VARIABLE_ARRAY(bool, marked, max_background_threads);
	unsigned i, nmarked;
	for (i = 0; i < max_background_threads; i++) {
		marked[i] = false;
	}
	nmarked = 0;
	/* Thread 0 is required and created at the end. */
	marked[0] = true;
	/* Mark the threads we need to create for thread 0. */
	unsigned n = narenas_total_get();
	for (i = 1; i < n; i++) {
		if (marked[i % max_background_threads] ||
		    arena_get(tsd_tsdn(tsd), i, false) == NULL) {
			continue;
		}
		background_thread_info_t *info = &background_thread_info[
		    i % max_background_threads];
		malloc_mutex_lock(tsd_tsdn(tsd), &info->mtx);
		assert(info->state == background_thread_stopped);
		background_thread_init(tsd, info);
		malloc_mutex_unlock(tsd_tsdn(tsd), &info->mtx);
		marked[i % max_background_threads] = true;
		if (++nmarked == max_background_threads) {
			break;
		}
	}

	return background_thread_create(tsd, 0);
}

bool
background_threads_disable(tsd_t *tsd) {
	assert(!background_thread_enabled());
	malloc_mutex_assert_owner(tsd_tsdn(tsd), &background_thread_lock);

	/* Thread 0 will be responsible for terminating other threads. */
	if (background_threads_disable_single(tsd,
	    &background_thread_info[0])) {
		return true;
	}
	assert(n_background_threads == 0);

	return false;
}

/* Check if we need to signal the background thread early. */
void
background_thread_interval_check(tsdn_t *tsdn, arena_t *arena,
    arena_decay_t *decay, size_t npages_new) {
	background_thread_info_t *info = arena_background_thread_info_get(
	    arena);
	if (malloc_mutex_trylock(tsdn, &info->mtx)) {
		/*
		 * Background thread may hold the mutex for a long period of
		 * time.  We'd like to avoid the variance on application
		 * threads.  So keep this non-blocking, and leave the work to a
		 * future epoch.
		 */
		return;
	}

	if (info->state != background_thread_started) {
		goto label_done;
	}
	if (malloc_mutex_trylock(tsdn, &decay->mtx)) {
		goto label_done;
	}

	ssize_t decay_time = atomic_load_zd(&decay->time_ms, ATOMIC_RELAXED);
	if (decay_time <= 0) {
		/* Purging is eagerly done or disabled currently. */
		goto label_done_unlock2;
	}
	uint64_t decay_interval_ns = nstime_ns(&decay->interval);
	assert(decay_interval_ns > 0);

	nstime_t diff;
	nstime_init(&diff, background_thread_wakeup_time_get(info));
	if (nstime_compare(&diff, &decay->epoch) <= 0) {
		goto label_done_unlock2;
	}
	nstime_subtract(&diff, &decay->epoch);
	if (nstime_ns(&diff) < BACKGROUND_THREAD_MIN_INTERVAL_NS) {
		goto label_done_unlock2;
	}

	if (npages_new > 0) {
		size_t n_epoch = (size_t)(nstime_ns(&diff) / decay_interval_ns);
		/*
		 * Compute how many new pages we would need to purge by the next
		 * wakeup, which is used to determine if we should signal the
		 * background thread.
		 */
		uint64_t npurge_new;
		if (n_epoch >= SMOOTHSTEP_NSTEPS) {
			npurge_new = npages_new;
		} else {
			uint64_t h_steps_max = h_steps[SMOOTHSTEP_NSTEPS - 1];
			assert(h_steps_max >=
			    h_steps[SMOOTHSTEP_NSTEPS - 1 - n_epoch]);
			npurge_new = npages_new * (h_steps_max -
			    h_steps[SMOOTHSTEP_NSTEPS - 1 - n_epoch]);
			npurge_new >>= SMOOTHSTEP_BFP;
		}
		info->npages_to_purge_new += npurge_new;
	}

	bool should_signal;
	if (info->npages_to_purge_new > BACKGROUND_THREAD_NPAGES_THRESHOLD) {
		should_signal = true;
	} else if (unlikely(background_thread_indefinite_sleep(info)) &&
	    (extents_npages_get(&arena->extents_dirty) > 0 ||
	    extents_npages_get(&arena->extents_muzzy) > 0 ||
	    info->npages_to_purge_new > 0)) {
		should_signal = true;
	} else {
		should_signal = false;
	}

	if (should_signal) {
		info->npages_to_purge_new = 0;
		pthread_cond_signal(&info->cond);
	}
label_done_unlock2:
	malloc_mutex_unlock(tsdn, &decay->mtx);
label_done:
	malloc_mutex_unlock(tsdn, &info->mtx);
}

void
background_thread_prefork0(tsdn_t *tsdn) {
	malloc_mutex_prefork(tsdn, &background_thread_lock);
	background_thread_enabled_at_fork = background_thread_enabled();
}

void
background_thread_prefork1(tsdn_t *tsdn) {
	for (unsigned i = 0; i < max_background_threads; i++) {
		malloc_mutex_prefork(tsdn, &background_thread_info[i].mtx);
	}
}

void
background_thread_postfork_parent(tsdn_t *tsdn) {
	for (unsigned i = 0; i < max_background_threads; i++) {
		malloc_mutex_postfork_parent(tsdn,
		    &background_thread_info[i].mtx);
	}
	malloc_mutex_postfork_parent(tsdn, &background_thread_lock);
}

void
background_thread_postfork_child(tsdn_t *tsdn) {
	for (unsigned i = 0; i < max_background_threads; i++) {
		malloc_mutex_postfork_child(tsdn,
		    &background_thread_info[i].mtx);
	}
	malloc_mutex_postfork_child(tsdn, &background_thread_lock);
	if (!background_thread_enabled_at_fork) {
		return;
	}

	/* Clear background_thread state (reset to disabled for child). */
	malloc_mutex_lock(tsdn, &background_thread_lock);
	n_background_threads = 0;
	background_thread_enabled_set(tsdn, false);
	for (unsigned i = 0; i < max_background_threads; i++) {
		background_thread_info_t *info = &background_thread_info[i];
		malloc_mutex_lock(tsdn, &info->mtx);
		info->state = background_thread_stopped;
		int ret = pthread_cond_init(&info->cond, NULL);
		assert(ret == 0);
		background_thread_info_init(tsdn, info);
		malloc_mutex_unlock(tsdn, &info->mtx);
	}
	malloc_mutex_unlock(tsdn, &background_thread_lock);
}

bool
background_thread_stats_read(tsdn_t *tsdn, background_thread_stats_t *stats) {
	assert(config_stats);
	malloc_mutex_lock(tsdn, &background_thread_lock);
	if (!background_thread_enabled()) {
		malloc_mutex_unlock(tsdn, &background_thread_lock);
		return true;
	}

	stats->num_threads = n_background_threads;
	uint64_t num_runs = 0;
	nstime_init(&stats->run_interval, 0);
	for (unsigned i = 0; i < max_background_threads; i++) {
		background_thread_info_t *info = &background_thread_info[i];
		malloc_mutex_lock(tsdn, &info->mtx);
		if (info->state != background_thread_stopped) {
			num_runs += info->tot_n_runs;
			nstime_add(&stats->run_interval, &info->tot_sleep_time);
		}
		malloc_mutex_unlock(tsdn, &info->mtx);
	}
	stats->num_runs = num_runs;
	if (num_runs > 0) {
		nstime_idivide(&stats->run_interval, num_runs);
	}
	malloc_mutex_unlock(tsdn, &background_thread_lock);

	return false;
}

#undef BACKGROUND_THREAD_NPAGES_THRESHOLD
#undef BILLION
#undef BACKGROUND_THREAD_MIN_INTERVAL_NS

static bool
pthread_create_fptr_init(void) {
	if (pthread_create_fptr != NULL) {
		return false;
	}
	pthread_create_fptr = dlsym(RTLD_NEXT, "pthread_create");
	if (pthread_create_fptr == NULL) {
		can_enable_background_thread = false;
		if (config_lazy_lock || opt_background_thread) {
			malloc_write("<jemalloc>: Error in dlsym(RTLD_NEXT, "
			    "\"pthread_create\")\n");
			abort();
		}
	} else {
		can_enable_background_thread = true;
	}

	return false;
}

/*
 * When lazy lock is enabled, we need to make sure setting isthreaded before
 * taking any background_thread locks.  This is called early in ctl (instead of
 * wait for the pthread_create calls to trigger) because the mutex is required
 * before creating background threads.
 */
void
background_thread_ctl_init(tsdn_t *tsdn) {
	malloc_mutex_assert_not_owner(tsdn, &background_thread_lock);
#ifdef JEMALLOC_PTHREAD_CREATE_WRAPPER
	pthread_create_fptr_init();
	pthread_create_wrapper_init();
#endif
}

#endif /* defined(JEMALLOC_BACKGROUND_THREAD) */

bool
background_thread_boot0(void) {
	if (!have_background_thread && opt_background_thread) {
		malloc_printf("<jemalloc>: option background_thread currently "
		    "supports pthread only\n");
		return true;
	}
#ifdef JEMALLOC_PTHREAD_CREATE_WRAPPER
	if ((config_lazy_lock || opt_background_thread) &&
	    pthread_create_fptr_init()) {
		return true;
	}
#endif
	return false;
}

bool
background_thread_boot1(tsdn_t *tsdn) {
#ifdef JEMALLOC_BACKGROUND_THREAD
	assert(have_background_thread);
	assert(narenas_total_get() > 0);

	if (opt_max_background_threads == MAX_BACKGROUND_THREAD_LIMIT &&
	    ncpus < MAX_BACKGROUND_THREAD_LIMIT) {
		opt_max_background_threads = ncpus;
	}
	max_background_threads = opt_max_background_threads;

	background_thread_enabled_set(tsdn, opt_background_thread);
	if (malloc_mutex_init(&background_thread_lock,
	    "background_thread_global",
	    WITNESS_RANK_BACKGROUND_THREAD_GLOBAL,
	    malloc_mutex_rank_exclusive)) {
		return true;
	}

	background_thread_info = (background_thread_info_t *)base_alloc(tsdn,
	    b0get(), opt_max_background_threads *
	    sizeof(background_thread_info_t), CACHELINE);
	if (background_thread_info == NULL) {
		return true;
	}

	for (unsigned i = 0; i < max_background_threads; i++) {
		background_thread_info_t *info = &background_thread_info[i];
		/* Thread mutex is rank_inclusive because of thread0. */
		if (malloc_mutex_init(&info->mtx, "background_thread",
		    WITNESS_RANK_BACKGROUND_THREAD,
		    malloc_mutex_address_ordered)) {
			return true;
		}
		if (pthread_cond_init(&info->cond, NULL)) {
			return true;
		}
		malloc_mutex_lock(tsdn, &info->mtx);
		info->state = background_thread_stopped;
		background_thread_info_init(tsdn, info);
		malloc_mutex_unlock(tsdn, &info->mtx);
	}
#endif

	return false;
}
