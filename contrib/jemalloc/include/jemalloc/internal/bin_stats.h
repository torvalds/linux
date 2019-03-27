#ifndef JEMALLOC_INTERNAL_BIN_STATS_H
#define JEMALLOC_INTERNAL_BIN_STATS_H

#include "jemalloc/internal/mutex_prof.h"

typedef struct bin_stats_s bin_stats_t;
struct bin_stats_s {
	/*
	 * Total number of allocation/deallocation requests served directly by
	 * the bin.  Note that tcache may allocate an object, then recycle it
	 * many times, resulting many increments to nrequests, but only one
	 * each to nmalloc and ndalloc.
	 */
	uint64_t	nmalloc;
	uint64_t	ndalloc;

	/*
	 * Number of allocation requests that correspond to the size of this
	 * bin.  This includes requests served by tcache, though tcache only
	 * periodically merges into this counter.
	 */
	uint64_t	nrequests;

	/*
	 * Current number of regions of this size class, including regions
	 * currently cached by tcache.
	 */
	size_t		curregs;

	/* Number of tcache fills from this bin. */
	uint64_t	nfills;

	/* Number of tcache flushes to this bin. */
	uint64_t	nflushes;

	/* Total number of slabs created for this bin's size class. */
	uint64_t	nslabs;

	/*
	 * Total number of slabs reused by extracting them from the slabs heap
	 * for this bin's size class.
	 */
	uint64_t	reslabs;

	/* Current number of slabs in this bin. */
	size_t		curslabs;

	mutex_prof_data_t mutex_data;
};

#endif /* JEMALLOC_INTERNAL_BIN_STATS_H */
