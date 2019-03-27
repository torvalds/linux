#define JEMALLOC_MUTEX_POOL_C_

#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/jemalloc_internal_includes.h"

#include "jemalloc/internal/mutex.h"
#include "jemalloc/internal/mutex_pool.h"

bool
mutex_pool_init(mutex_pool_t *pool, const char *name, witness_rank_t rank) {
	for (int i = 0; i < MUTEX_POOL_SIZE; ++i) {
		if (malloc_mutex_init(&pool->mutexes[i], name, rank,
		    malloc_mutex_address_ordered)) {
			return true;
		}
	}
	return false;
}
