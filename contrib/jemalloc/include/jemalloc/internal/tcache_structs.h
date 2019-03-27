#ifndef JEMALLOC_INTERNAL_TCACHE_STRUCTS_H
#define JEMALLOC_INTERNAL_TCACHE_STRUCTS_H

#include "jemalloc/internal/ql.h"
#include "jemalloc/internal/size_classes.h"
#include "jemalloc/internal/cache_bin.h"
#include "jemalloc/internal/ticker.h"

struct tcache_s {
	/*
	 * To minimize our cache-footprint, we put the frequently accessed data
	 * together at the start of this struct.
	 */

	/* Cleared after arena_prof_accum(). */
	uint64_t	prof_accumbytes;
	/* Drives incremental GC. */
	ticker_t	gc_ticker;
	/*
	 * The pointer stacks associated with bins follow as a contiguous array.
	 * During tcache initialization, the avail pointer in each element of
	 * tbins is initialized to point to the proper offset within this array.
	 */
	cache_bin_t	bins_small[NBINS];

	/*
	 * This data is less hot; we can be a little less careful with our
	 * footprint here.
	 */
	/* Lets us track all the tcaches in an arena. */
	ql_elm(tcache_t) link;
	/*
	 * The descriptor lets the arena find our cache bins without seeing the
	 * tcache definition.  This enables arenas to aggregate stats across
	 * tcaches without having a tcache dependency.
	 */
	cache_bin_array_descriptor_t cache_bin_array_descriptor;

	/* The arena this tcache is associated with. */
	arena_t		*arena;
	/* Next bin to GC. */
	szind_t		next_gc_bin;
	/* For small bins, fill (ncached_max >> lg_fill_div). */
	uint8_t		lg_fill_div[NBINS];
	/*
	 * We put the cache bins for large size classes at the end of the
	 * struct, since some of them might not get used.  This might end up
	 * letting us avoid touching an extra page if we don't have to.
	 */
	cache_bin_t	bins_large[NSIZES-NBINS];
};

/* Linkage for list of available (previously used) explicit tcache IDs. */
struct tcaches_s {
	union {
		tcache_t	*tcache;
		tcaches_t	*next;
	};
};

#endif /* JEMALLOC_INTERNAL_TCACHE_STRUCTS_H */
