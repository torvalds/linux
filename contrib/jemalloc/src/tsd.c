#define JEMALLOC_TSD_C_
#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/jemalloc_internal_includes.h"

#include "jemalloc/internal/assert.h"
#include "jemalloc/internal/mutex.h"
#include "jemalloc/internal/rtree.h"

/******************************************************************************/
/* Data. */

static unsigned ncleanups;
static malloc_tsd_cleanup_t cleanups[MALLOC_TSD_CLEANUPS_MAX];

#ifdef JEMALLOC_MALLOC_THREAD_CLEANUP
__thread tsd_t JEMALLOC_TLS_MODEL tsd_tls = TSD_INITIALIZER;
__thread bool JEMALLOC_TLS_MODEL tsd_initialized = false;
bool tsd_booted = false;
#elif (defined(JEMALLOC_TLS))
__thread tsd_t JEMALLOC_TLS_MODEL tsd_tls = TSD_INITIALIZER;
pthread_key_t tsd_tsd;
bool tsd_booted = false;
#elif (defined(_WIN32))
DWORD tsd_tsd;
tsd_wrapper_t tsd_boot_wrapper = {false, TSD_INITIALIZER};
bool tsd_booted = false;
#else

/*
 * This contains a mutex, but it's pretty convenient to allow the mutex code to
 * have a dependency on tsd.  So we define the struct here, and only refer to it
 * by pointer in the header.
 */
struct tsd_init_head_s {
	ql_head(tsd_init_block_t) blocks;
	malloc_mutex_t lock;
};

pthread_key_t tsd_tsd;
tsd_init_head_t	tsd_init_head = {
	ql_head_initializer(blocks),
	MALLOC_MUTEX_INITIALIZER
};
tsd_wrapper_t tsd_boot_wrapper = {
	false,
	TSD_INITIALIZER
};
bool tsd_booted = false;
#endif


/******************************************************************************/

void
tsd_slow_update(tsd_t *tsd) {
	if (tsd_nominal(tsd)) {
		if (malloc_slow || !tsd_tcache_enabled_get(tsd) ||
		    tsd_reentrancy_level_get(tsd) > 0) {
			tsd->state = tsd_state_nominal_slow;
		} else {
			tsd->state = tsd_state_nominal;
		}
	}
}

static bool
tsd_data_init(tsd_t *tsd) {
	/*
	 * We initialize the rtree context first (before the tcache), since the
	 * tcache initialization depends on it.
	 */
	rtree_ctx_data_init(tsd_rtree_ctxp_get_unsafe(tsd));

	/*
	 * A nondeterministic seed based on the address of tsd reduces
	 * the likelihood of lockstep non-uniform cache index
	 * utilization among identical concurrent processes, but at the
	 * cost of test repeatability.  For debug builds, instead use a
	 * deterministic seed.
	 */
	*tsd_offset_statep_get(tsd) = config_debug ? 0 :
	    (uint64_t)(uintptr_t)tsd;

	return tsd_tcache_enabled_data_init(tsd);
}

static void
assert_tsd_data_cleanup_done(tsd_t *tsd) {
	assert(!tsd_nominal(tsd));
	assert(*tsd_arenap_get_unsafe(tsd) == NULL);
	assert(*tsd_iarenap_get_unsafe(tsd) == NULL);
	assert(*tsd_arenas_tdata_bypassp_get_unsafe(tsd) == true);
	assert(*tsd_arenas_tdatap_get_unsafe(tsd) == NULL);
	assert(*tsd_tcache_enabledp_get_unsafe(tsd) == false);
	assert(*tsd_prof_tdatap_get_unsafe(tsd) == NULL);
}

static bool
tsd_data_init_nocleanup(tsd_t *tsd) {
	assert(tsd->state == tsd_state_reincarnated ||
	    tsd->state == tsd_state_minimal_initialized);
	/*
	 * During reincarnation, there is no guarantee that the cleanup function
	 * will be called (deallocation may happen after all tsd destructors).
	 * We set up tsd in a way that no cleanup is needed.
	 */
	rtree_ctx_data_init(tsd_rtree_ctxp_get_unsafe(tsd));
	*tsd_arenas_tdata_bypassp_get(tsd) = true;
	*tsd_tcache_enabledp_get_unsafe(tsd) = false;
	*tsd_reentrancy_levelp_get(tsd) = 1;
	assert_tsd_data_cleanup_done(tsd);

	return false;
}

tsd_t *
tsd_fetch_slow(tsd_t *tsd, bool minimal) {
	assert(!tsd_fast(tsd));

	if (tsd->state == tsd_state_nominal_slow) {
		/* On slow path but no work needed. */
		assert(malloc_slow || !tsd_tcache_enabled_get(tsd) ||
		    tsd_reentrancy_level_get(tsd) > 0 ||
		    *tsd_arenas_tdata_bypassp_get(tsd));
	} else if (tsd->state == tsd_state_uninitialized) {
		if (!minimal) {
			tsd->state = tsd_state_nominal;
			tsd_slow_update(tsd);
			/* Trigger cleanup handler registration. */
			tsd_set(tsd);
			tsd_data_init(tsd);
		} else {
			tsd->state = tsd_state_minimal_initialized;
			tsd_set(tsd);
			tsd_data_init_nocleanup(tsd);
		}
	} else if (tsd->state == tsd_state_minimal_initialized) {
		if (!minimal) {
			/* Switch to fully initialized. */
			tsd->state = tsd_state_nominal;
			assert(*tsd_reentrancy_levelp_get(tsd) >= 1);
			(*tsd_reentrancy_levelp_get(tsd))--;
			tsd_slow_update(tsd);
			tsd_data_init(tsd);
		} else {
			assert_tsd_data_cleanup_done(tsd);
		}
	} else if (tsd->state == tsd_state_purgatory) {
		tsd->state = tsd_state_reincarnated;
		tsd_set(tsd);
		tsd_data_init_nocleanup(tsd);
	} else {
		assert(tsd->state == tsd_state_reincarnated);
	}

	return tsd;
}

void *
malloc_tsd_malloc(size_t size) {
	return a0malloc(CACHELINE_CEILING(size));
}

void
malloc_tsd_dalloc(void *wrapper) {
	a0dalloc(wrapper);
}

#if defined(JEMALLOC_MALLOC_THREAD_CLEANUP) || defined(_WIN32)
#ifndef _WIN32
JEMALLOC_EXPORT
#endif
void
_malloc_thread_cleanup(void) {
	bool pending[MALLOC_TSD_CLEANUPS_MAX], again;
	unsigned i;

	for (i = 0; i < ncleanups; i++) {
		pending[i] = true;
	}

	do {
		again = false;
		for (i = 0; i < ncleanups; i++) {
			if (pending[i]) {
				pending[i] = cleanups[i]();
				if (pending[i]) {
					again = true;
				}
			}
		}
	} while (again);
}
#endif

void
malloc_tsd_cleanup_register(bool (*f)(void)) {
	assert(ncleanups < MALLOC_TSD_CLEANUPS_MAX);
	cleanups[ncleanups] = f;
	ncleanups++;
}

static void
tsd_do_data_cleanup(tsd_t *tsd) {
	prof_tdata_cleanup(tsd);
	iarena_cleanup(tsd);
	arena_cleanup(tsd);
	arenas_tdata_cleanup(tsd);
	tcache_cleanup(tsd);
	witnesses_cleanup(tsd_witness_tsdp_get_unsafe(tsd));
}

void
tsd_cleanup(void *arg) {
	tsd_t *tsd = (tsd_t *)arg;

	switch (tsd->state) {
	case tsd_state_uninitialized:
		/* Do nothing. */
		break;
	case tsd_state_minimal_initialized:
		/* This implies the thread only did free() in its life time. */
		/* Fall through. */
	case tsd_state_reincarnated:
		/*
		 * Reincarnated means another destructor deallocated memory
		 * after the destructor was called.  Cleanup isn't required but
		 * is still called for testing and completeness.
		 */
		assert_tsd_data_cleanup_done(tsd);
		/* Fall through. */
	case tsd_state_nominal:
	case tsd_state_nominal_slow:
		tsd_do_data_cleanup(tsd);
		tsd->state = tsd_state_purgatory;
		tsd_set(tsd);
		break;
	case tsd_state_purgatory:
		/*
		 * The previous time this destructor was called, we set the
		 * state to tsd_state_purgatory so that other destructors
		 * wouldn't cause re-creation of the tsd.  This time, do
		 * nothing, and do not request another callback.
		 */
		break;
	default:
		not_reached();
	}
#ifdef JEMALLOC_JET
	test_callback_t test_callback = *tsd_test_callbackp_get_unsafe(tsd);
	int *data = tsd_test_datap_get_unsafe(tsd);
	if (test_callback != NULL) {
		test_callback(data);
	}
#endif
}

tsd_t *
malloc_tsd_boot0(void) {
	tsd_t *tsd;

	ncleanups = 0;
	if (tsd_boot0()) {
		return NULL;
	}
	tsd = tsd_fetch();
	*tsd_arenas_tdata_bypassp_get(tsd) = true;
	return tsd;
}

void
malloc_tsd_boot1(void) {
	tsd_boot1();
	tsd_t *tsd = tsd_fetch();
	/* malloc_slow has been set properly.  Update tsd_slow. */
	tsd_slow_update(tsd);
	*tsd_arenas_tdata_bypassp_get(tsd) = false;
}

#ifdef _WIN32
static BOOL WINAPI
_tls_callback(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
	switch (fdwReason) {
#ifdef JEMALLOC_LAZY_LOCK
	case DLL_THREAD_ATTACH:
		isthreaded = true;
		break;
#endif
	case DLL_THREAD_DETACH:
		_malloc_thread_cleanup();
		break;
	default:
		break;
	}
	return true;
}

/*
 * We need to be able to say "read" here (in the "pragma section"), but have
 * hooked "read". We won't read for the rest of the file, so we can get away
 * with unhooking.
 */
#ifdef read
#  undef read
#endif

#ifdef _MSC_VER
#  ifdef _M_IX86
#    pragma comment(linker, "/INCLUDE:__tls_used")
#    pragma comment(linker, "/INCLUDE:_tls_callback")
#  else
#    pragma comment(linker, "/INCLUDE:_tls_used")
#    pragma comment(linker, "/INCLUDE:tls_callback")
#  endif
#  pragma section(".CRT$XLY",long,read)
#endif
JEMALLOC_SECTION(".CRT$XLY") JEMALLOC_ATTR(used)
BOOL	(WINAPI *const tls_callback)(HINSTANCE hinstDLL,
    DWORD fdwReason, LPVOID lpvReserved) = _tls_callback;
#endif

#if (!defined(JEMALLOC_MALLOC_THREAD_CLEANUP) && !defined(JEMALLOC_TLS) && \
    !defined(_WIN32))
void *
tsd_init_check_recursion(tsd_init_head_t *head, tsd_init_block_t *block) {
	pthread_t self = pthread_self();
	tsd_init_block_t *iter;

	/* Check whether this thread has already inserted into the list. */
	malloc_mutex_lock(TSDN_NULL, &head->lock);
	ql_foreach(iter, &head->blocks, link) {
		if (iter->thread == self) {
			malloc_mutex_unlock(TSDN_NULL, &head->lock);
			return iter->data;
		}
	}
	/* Insert block into list. */
	ql_elm_new(block, link);
	block->thread = self;
	ql_tail_insert(&head->blocks, block, link);
	malloc_mutex_unlock(TSDN_NULL, &head->lock);
	return NULL;
}

void
tsd_init_finish(tsd_init_head_t *head, tsd_init_block_t *block) {
	malloc_mutex_lock(TSDN_NULL, &head->lock);
	ql_remove(&head->blocks, block, link);
	malloc_mutex_unlock(TSDN_NULL, &head->lock);
}
#endif
