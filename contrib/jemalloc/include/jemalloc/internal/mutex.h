#ifndef JEMALLOC_INTERNAL_MUTEX_H
#define JEMALLOC_INTERNAL_MUTEX_H

#include "jemalloc/internal/atomic.h"
#include "jemalloc/internal/mutex_prof.h"
#include "jemalloc/internal/tsd.h"
#include "jemalloc/internal/witness.h"

typedef enum {
	/* Can only acquire one mutex of a given witness rank at a time. */
	malloc_mutex_rank_exclusive,
	/*
	 * Can acquire multiple mutexes of the same witness rank, but in
	 * address-ascending order only.
	 */
	malloc_mutex_address_ordered
} malloc_mutex_lock_order_t;

typedef struct malloc_mutex_s malloc_mutex_t;
struct malloc_mutex_s {
	union {
		struct {
			/*
			 * prof_data is defined first to reduce cacheline
			 * bouncing: the data is not touched by the mutex holder
			 * during unlocking, while might be modified by
			 * contenders.  Having it before the mutex itself could
			 * avoid prefetching a modified cacheline (for the
			 * unlocking thread).
			 */
			mutex_prof_data_t	prof_data;
#ifdef _WIN32
#  if _WIN32_WINNT >= 0x0600
			SRWLOCK         	lock;
#  else
			CRITICAL_SECTION	lock;
#  endif
#elif (defined(JEMALLOC_OS_UNFAIR_LOCK))
			os_unfair_lock		lock;
#elif (defined(JEMALLOC_OSSPIN))
			OSSpinLock		lock;
#elif (defined(JEMALLOC_MUTEX_INIT_CB))
			pthread_mutex_t		lock;
			malloc_mutex_t		*postponed_next;
#else
			pthread_mutex_t		lock;
#endif
		};
		/*
		 * We only touch witness when configured w/ debug.  However we
		 * keep the field in a union when !debug so that we don't have
		 * to pollute the code base with #ifdefs, while avoid paying the
		 * memory cost.
		 */
#if !defined(JEMALLOC_DEBUG)
		witness_t			witness;
		malloc_mutex_lock_order_t	lock_order;
#endif
	};

#if defined(JEMALLOC_DEBUG)
	witness_t			witness;
	malloc_mutex_lock_order_t	lock_order;
#endif
};

/*
 * Based on benchmark results, a fixed spin with this amount of retries works
 * well for our critical sections.
 */
#define MALLOC_MUTEX_MAX_SPIN 250

#ifdef _WIN32
#  if _WIN32_WINNT >= 0x0600
#    define MALLOC_MUTEX_LOCK(m)    AcquireSRWLockExclusive(&(m)->lock)
#    define MALLOC_MUTEX_UNLOCK(m)  ReleaseSRWLockExclusive(&(m)->lock)
#    define MALLOC_MUTEX_TRYLOCK(m) (!TryAcquireSRWLockExclusive(&(m)->lock))
#  else
#    define MALLOC_MUTEX_LOCK(m)    EnterCriticalSection(&(m)->lock)
#    define MALLOC_MUTEX_UNLOCK(m)  LeaveCriticalSection(&(m)->lock)
#    define MALLOC_MUTEX_TRYLOCK(m) (!TryEnterCriticalSection(&(m)->lock))
#  endif
#elif (defined(JEMALLOC_OS_UNFAIR_LOCK))
#    define MALLOC_MUTEX_LOCK(m)    os_unfair_lock_lock(&(m)->lock)
#    define MALLOC_MUTEX_UNLOCK(m)  os_unfair_lock_unlock(&(m)->lock)
#    define MALLOC_MUTEX_TRYLOCK(m) (!os_unfair_lock_trylock(&(m)->lock))
#elif (defined(JEMALLOC_OSSPIN))
#    define MALLOC_MUTEX_LOCK(m)    OSSpinLockLock(&(m)->lock)
#    define MALLOC_MUTEX_UNLOCK(m)  OSSpinLockUnlock(&(m)->lock)
#    define MALLOC_MUTEX_TRYLOCK(m) (!OSSpinLockTry(&(m)->lock))
#else
#    define MALLOC_MUTEX_LOCK(m)    pthread_mutex_lock(&(m)->lock)
#    define MALLOC_MUTEX_UNLOCK(m)  pthread_mutex_unlock(&(m)->lock)
#    define MALLOC_MUTEX_TRYLOCK(m) (pthread_mutex_trylock(&(m)->lock) != 0)
#endif

#define LOCK_PROF_DATA_INITIALIZER					\
    {NSTIME_ZERO_INITIALIZER, NSTIME_ZERO_INITIALIZER, 0, 0, 0,		\
	    ATOMIC_INIT(0), 0, NULL, 0}

#ifdef _WIN32
#  define MALLOC_MUTEX_INITIALIZER
#elif (defined(JEMALLOC_OS_UNFAIR_LOCK))
#  define MALLOC_MUTEX_INITIALIZER					\
     {{{LOCK_PROF_DATA_INITIALIZER, OS_UNFAIR_LOCK_INIT}},		\
      WITNESS_INITIALIZER("mutex", WITNESS_RANK_OMIT)}
#elif (defined(JEMALLOC_OSSPIN))
#  define MALLOC_MUTEX_INITIALIZER					\
     {{{LOCK_PROF_DATA_INITIALIZER, 0}},				\
      WITNESS_INITIALIZER("mutex", WITNESS_RANK_OMIT)}
#elif (defined(JEMALLOC_MUTEX_INIT_CB))
#  define MALLOC_MUTEX_INITIALIZER					\
     {{{LOCK_PROF_DATA_INITIALIZER, PTHREAD_MUTEX_INITIALIZER, NULL}},	\
      WITNESS_INITIALIZER("mutex", WITNESS_RANK_OMIT)}
#else
#    define MALLOC_MUTEX_TYPE PTHREAD_MUTEX_DEFAULT
#    define MALLOC_MUTEX_INITIALIZER					\
       {{{LOCK_PROF_DATA_INITIALIZER, PTHREAD_MUTEX_INITIALIZER}},	\
        WITNESS_INITIALIZER("mutex", WITNESS_RANK_OMIT)}
#endif

#ifdef JEMALLOC_LAZY_LOCK
extern bool isthreaded;
#endif

bool malloc_mutex_init(malloc_mutex_t *mutex, const char *name,
    witness_rank_t rank, malloc_mutex_lock_order_t lock_order);
void malloc_mutex_prefork(tsdn_t *tsdn, malloc_mutex_t *mutex);
void malloc_mutex_postfork_parent(tsdn_t *tsdn, malloc_mutex_t *mutex);
void malloc_mutex_postfork_child(tsdn_t *tsdn, malloc_mutex_t *mutex);
bool malloc_mutex_first_thread(void);
bool malloc_mutex_boot(void);
void malloc_mutex_prof_data_reset(tsdn_t *tsdn, malloc_mutex_t *mutex);

void malloc_mutex_lock_slow(malloc_mutex_t *mutex);

static inline void
malloc_mutex_lock_final(malloc_mutex_t *mutex) {
	MALLOC_MUTEX_LOCK(mutex);
}

static inline bool
malloc_mutex_trylock_final(malloc_mutex_t *mutex) {
	return MALLOC_MUTEX_TRYLOCK(mutex);
}

static inline void
mutex_owner_stats_update(tsdn_t *tsdn, malloc_mutex_t *mutex) {
	if (config_stats) {
		mutex_prof_data_t *data = &mutex->prof_data;
		data->n_lock_ops++;
		if (data->prev_owner != tsdn) {
			data->prev_owner = tsdn;
			data->n_owner_switches++;
		}
	}
}

/* Trylock: return false if the lock is successfully acquired. */
static inline bool
malloc_mutex_trylock(tsdn_t *tsdn, malloc_mutex_t *mutex) {
	witness_assert_not_owner(tsdn_witness_tsdp_get(tsdn), &mutex->witness);
	if (isthreaded) {
		if (malloc_mutex_trylock_final(mutex)) {
			return true;
		}
		mutex_owner_stats_update(tsdn, mutex);
	}
	witness_lock(tsdn_witness_tsdp_get(tsdn), &mutex->witness);

	return false;
}

/* Aggregate lock prof data. */
static inline void
malloc_mutex_prof_merge(mutex_prof_data_t *sum, mutex_prof_data_t *data) {
	nstime_add(&sum->tot_wait_time, &data->tot_wait_time);
	if (nstime_compare(&sum->max_wait_time, &data->max_wait_time) < 0) {
		nstime_copy(&sum->max_wait_time, &data->max_wait_time);
	}

	sum->n_wait_times += data->n_wait_times;
	sum->n_spin_acquired += data->n_spin_acquired;

	if (sum->max_n_thds < data->max_n_thds) {
		sum->max_n_thds = data->max_n_thds;
	}
	uint32_t cur_n_waiting_thds = atomic_load_u32(&sum->n_waiting_thds,
	    ATOMIC_RELAXED);
	uint32_t new_n_waiting_thds = cur_n_waiting_thds + atomic_load_u32(
	    &data->n_waiting_thds, ATOMIC_RELAXED);
	atomic_store_u32(&sum->n_waiting_thds, new_n_waiting_thds,
	    ATOMIC_RELAXED);
	sum->n_owner_switches += data->n_owner_switches;
	sum->n_lock_ops += data->n_lock_ops;
}

static inline void
malloc_mutex_lock(tsdn_t *tsdn, malloc_mutex_t *mutex) {
	witness_assert_not_owner(tsdn_witness_tsdp_get(tsdn), &mutex->witness);
	if (isthreaded) {
		if (malloc_mutex_trylock_final(mutex)) {
			malloc_mutex_lock_slow(mutex);
		}
		mutex_owner_stats_update(tsdn, mutex);
	}
	witness_lock(tsdn_witness_tsdp_get(tsdn), &mutex->witness);
}

static inline void
malloc_mutex_unlock(tsdn_t *tsdn, malloc_mutex_t *mutex) {
	witness_unlock(tsdn_witness_tsdp_get(tsdn), &mutex->witness);
	if (isthreaded) {
		MALLOC_MUTEX_UNLOCK(mutex);
	}
}

static inline void
malloc_mutex_assert_owner(tsdn_t *tsdn, malloc_mutex_t *mutex) {
	witness_assert_owner(tsdn_witness_tsdp_get(tsdn), &mutex->witness);
}

static inline void
malloc_mutex_assert_not_owner(tsdn_t *tsdn, malloc_mutex_t *mutex) {
	witness_assert_not_owner(tsdn_witness_tsdp_get(tsdn), &mutex->witness);
}

/* Copy the prof data from mutex for processing. */
static inline void
malloc_mutex_prof_read(tsdn_t *tsdn, mutex_prof_data_t *data,
    malloc_mutex_t *mutex) {
	mutex_prof_data_t *source = &mutex->prof_data;
	/* Can only read holding the mutex. */
	malloc_mutex_assert_owner(tsdn, mutex);

	/*
	 * Not *really* allowed (we shouldn't be doing non-atomic loads of
	 * atomic data), but the mutex protection makes this safe, and writing
	 * a member-for-member copy is tedious for this situation.
	 */
	*data = *source;
	/* n_wait_thds is not reported (modified w/o locking). */
	atomic_store_u32(&data->n_waiting_thds, 0, ATOMIC_RELAXED);
}

#endif /* JEMALLOC_INTERNAL_MUTEX_H */
