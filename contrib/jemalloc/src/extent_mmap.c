#define JEMALLOC_EXTENT_MMAP_C_
#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/jemalloc_internal_includes.h"

#include "jemalloc/internal/assert.h"
#include "jemalloc/internal/extent_mmap.h"

/******************************************************************************/
/* Data. */

bool	opt_retain =
#ifdef JEMALLOC_RETAIN
    true
#else
    false
#endif
    ;

/******************************************************************************/

void *
extent_alloc_mmap(void *new_addr, size_t size, size_t alignment, bool *zero,
    bool *commit) {
	void *ret = pages_map(new_addr, size, ALIGNMENT_CEILING(alignment,
	    PAGE), commit);
	if (ret == NULL) {
		return NULL;
	}
	assert(ret != NULL);
	if (*commit) {
		*zero = true;
	}
	return ret;
}

bool
extent_dalloc_mmap(void *addr, size_t size) {
	if (!opt_retain) {
		pages_unmap(addr, size);
	}
	return opt_retain;
}
