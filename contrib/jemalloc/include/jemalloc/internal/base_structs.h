#ifndef JEMALLOC_INTERNAL_BASE_STRUCTS_H
#define JEMALLOC_INTERNAL_BASE_STRUCTS_H

#include "jemalloc/internal/jemalloc_internal_types.h"
#include "jemalloc/internal/mutex.h"
#include "jemalloc/internal/size_classes.h"

/* Embedded at the beginning of every block of base-managed virtual memory. */
struct base_block_s {
	/* Total size of block's virtual memory mapping. */
	size_t		size;

	/* Next block in list of base's blocks. */
	base_block_t	*next;

	/* Tracks unused trailing space. */
	extent_t	extent;
};

struct base_s {
	/* Associated arena's index within the arenas array. */
	unsigned	ind;

	/*
	 * User-configurable extent hook functions.  Points to an
	 * extent_hooks_t.
	 */
	atomic_p_t	extent_hooks;

	/* Protects base_alloc() and base_stats_get() operations. */
	malloc_mutex_t	mtx;

	/* Using THP when true (metadata_thp auto mode). */
	bool		auto_thp_switched;
	/*
	 * Most recent size class in the series of increasingly large base
	 * extents.  Logarithmic spacing between subsequent allocations ensures
	 * that the total number of distinct mappings remains small.
	 */
	pszind_t	pind_last;

	/* Serial number generation state. */
	size_t		extent_sn_next;

	/* Chain of all blocks associated with base. */
	base_block_t	*blocks;

	/* Heap of extents that track unused trailing space within blocks. */
	extent_heap_t	avail[NSIZES];

	/* Stats, only maintained if config_stats. */
	size_t		allocated;
	size_t		resident;
	size_t		mapped;
	/* Number of THP regions touched. */
	size_t		n_thp;
};

#endif /* JEMALLOC_INTERNAL_BASE_STRUCTS_H */
