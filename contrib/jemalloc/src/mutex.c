#define JEMALLOC_MUTEX_C_
#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/jemalloc_internal_includes.h"

#include "jemalloc/internal/assert.h"
#include "jemalloc/internal/malloc_io.h"
#include "jemalloc/internal/spin.h"

#ifndef _CRT_SPINCOUNT
#define _CRT_SPINCOUNT 4000
#endif

/******************************************************************************/
/* Data. */

#ifdef JEMALLOC_LAZY_LOCK
bool isthreaded = false;
#endif
#ifdef JEMALLOC_MUTEX_INIT_CB
static bool		postpone_init = true;
static malloc_mutex_t	*postponed_mutexes = NULL;
#endif

/******************************************************************************/
/*
 * We intercept pthread_create() calls in order to toggle isthreaded if the
 * process goes multi-threaded.
 */

#if defined(JEMALLOC_LAZY_LOCK) && !defined(_WIN32)
JEMALLOC_EXPORT int
pthread_create(pthread_t *__restrict thread,
    const pthread_attr_t *__restrict attr, void *(*start_routine)(void *),
    void *__restrict arg) {
	return pthread_create_wrapper(thread, attr, start_routine, arg);
}
#endif

/******************************************************************************/

#ifdef JEMALLOC_MUTEX_INIT_CB
JEMALLOC_EXPORT int	_pthread_mutex_init_calloc_cb(pthread_mutex_t *mutex,
    void *(calloc_cb)(size_t, size_t));

#pragma weak _pthread_mutex_init_calloc_cb
int
_pthread_mutex_init_calloc_cb(pthread_mutex_t *mutex,
    void *(calloc_cb)(size_t, size_t))
{

	return (((int (*)(pthread_mutex_t *, void *(*)(size_t, size_t)))
	    __libc_interposing[INTERPOS__pthread_mutex_init_calloc_cb])(mutex,
	    calloc_cb));
}
#endif

void
malloc_mutex_lock_slow(malloc_mutex_t *mutex) {
	mutex_prof_data_t *data = &mutex->prof_data;
	UNUSED nstime_t before = NSTIME_ZERO_INITIALIZER;

	if (ncpus == 1) {
		goto label_spin_done;
	}

	int cnt = 0, max_cnt = MALLOC_MUTEX_MAX_SPIN;
	do {
		spin_cpu_spinwait();
		if (!malloc_mutex_trylock_final(mutex)) {
			data->n_spin_acquired++;
			return;
		}
	} while (cnt++ < max_cnt);

	if (!config_stats) {
		/* Only spin is useful when stats is off. */
		malloc_mutex_lock_final(mutex);
		return;
	}
label_spin_done:
	nstime_update(&before);
	/* Copy before to after to avoid clock skews. */
	nstime_t after;
	nstime_copy(&after, &before);
	uint32_t n_thds = atomic_fetch_add_u32(&data->n_waiting_thds, 1,
	    ATOMIC_RELAXED) + 1;
	/* One last try as above two calls may take quite some cycles. */
	if (!malloc_mutex_trylock_final(mutex)) {
		atomic_fetch_sub_u32(&data->n_waiting_thds, 1, ATOMIC_RELAXED);
		data->n_spin_acquired++;
		return;
	}

	/* True slow path. */
	malloc_mutex_lock_final(mutex);
	/* Update more slow-path only counters. */
	atomic_fetch_sub_u32(&data->n_waiting_thds, 1, ATOMIC_RELAXED);
	nstime_update(&after);

	nstime_t delta;
	nstime_copy(&delta, &after);
	nstime_subtract(&delta, &before);

	data->n_wait_times++;
	nstime_add(&data->tot_wait_time, &delta);
	if (nstime_compare(&data->max_wait_time, &delta) < 0) {
		nstime_copy(&data->max_wait_time, &delta);
	}
	if (n_thds > data->max_n_thds) {
		data->max_n_thds = n_thds;
	}
}

static void
mutex_prof_data_init(mutex_prof_data_t *data) {
	memset(data, 0, sizeof(mutex_prof_data_t));
	nstime_init(&data->max_wait_time, 0);
	nstime_init(&data->tot_wait_time, 0);
	data->prev_owner = NULL;
}

void
malloc_mutex_prof_data_reset(tsdn_t *tsdn, malloc_mutex_t *mutex) {
	malloc_mutex_assert_owner(tsdn, mutex);
	mutex_prof_data_init(&mutex->prof_data);
}

static int
mutex_addr_comp(const witness_t *witness1, void *mutex1,
    const witness_t *witness2, void *mutex2) {
	assert(mutex1 != NULL);
	assert(mutex2 != NULL);
	uintptr_t mu1int = (uintptr_t)mutex1;
	uintptr_t mu2int = (uintptr_t)mutex2;
	if (mu1int < mu2int) {
		return -1;
	} else if (mu1int == mu2int) {
		return 0;
	} else {
		return 1;
	}
}

bool
malloc_mutex_first_thread(void) {

#ifndef JEMALLOC_MUTEX_INIT_CB
	return (malloc_mutex_first_thread());
#else
	return (false);
#endif
}

bool
malloc_mutex_init(malloc_mutex_t *mutex, const char *name,
    witness_rank_t rank, malloc_mutex_lock_order_t lock_order) {
	mutex_prof_data_init(&mutex->prof_data);
#ifdef _WIN32
#  if _WIN32_WINNT >= 0x0600
	InitializeSRWLock(&mutex->lock);
#  else
	if (!InitializeCriticalSectionAndSpinCount(&mutex->lock,
	    _CRT_SPINCOUNT)) {
		return true;
	}
#  endif
#elif (defined(JEMALLOC_OS_UNFAIR_LOCK))
	mutex->lock = OS_UNFAIR_LOCK_INIT;
#elif (defined(JEMALLOC_OSSPIN))
	mutex->lock = 0;
#elif (defined(JEMALLOC_MUTEX_INIT_CB))
	if (postpone_init) {
		mutex->postponed_next = postponed_mutexes;
		postponed_mutexes = mutex;
	} else {
		if (_pthread_mutex_init_calloc_cb(&mutex->lock,
		    bootstrap_calloc) != 0) {
			return true;
		}
	}
#else
	pthread_mutexattr_t attr;

	if (pthread_mutexattr_init(&attr) != 0) {
		return true;
	}
	pthread_mutexattr_settype(&attr, MALLOC_MUTEX_TYPE);
	if (pthread_mutex_init(&mutex->lock, &attr) != 0) {
		pthread_mutexattr_destroy(&attr);
		return true;
	}
	pthread_mutexattr_destroy(&attr);
#endif
	if (config_debug) {
		mutex->lock_order = lock_order;
		if (lock_order == malloc_mutex_address_ordered) {
			witness_init(&mutex->witness, name, rank,
			    mutex_addr_comp, mutex);
		} else {
			witness_init(&mutex->witness, name, rank, NULL, NULL);
		}
	}
	return false;
}

void
malloc_mutex_prefork(tsdn_t *tsdn, malloc_mutex_t *mutex) {
	malloc_mutex_lock(tsdn, mutex);
}

void
malloc_mutex_postfork_parent(tsdn_t *tsdn, malloc_mutex_t *mutex) {
	malloc_mutex_unlock(tsdn, mutex);
}

void
malloc_mutex_postfork_child(tsdn_t *tsdn, malloc_mutex_t *mutex) {
#ifdef JEMALLOC_MUTEX_INIT_CB
	malloc_mutex_unlock(tsdn, mutex);
#else
	if (malloc_mutex_init(mutex, mutex->witness.name,
	    mutex->witness.rank, mutex->lock_order)) {
		malloc_printf("<jemalloc>: Error re-initializing mutex in "
		    "child\n");
		if (opt_abort) {
			abort();
		}
	}
#endif
}

bool
malloc_mutex_boot(void) {
#ifdef JEMALLOC_MUTEX_INIT_CB
	postpone_init = false;
	while (postponed_mutexes != NULL) {
		if (_pthread_mutex_init_calloc_cb(&postponed_mutexes->lock,
		    bootstrap_calloc) != 0) {
			return true;
		}
		postponed_mutexes = postponed_mutexes->postponed_next;
	}
#endif
	return false;
}
