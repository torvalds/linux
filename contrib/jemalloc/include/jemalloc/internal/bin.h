#ifndef JEMALLOC_INTERNAL_BIN_H
#define JEMALLOC_INTERNAL_BIN_H

#include "jemalloc/internal/extent_types.h"
#include "jemalloc/internal/extent_structs.h"
#include "jemalloc/internal/mutex.h"
#include "jemalloc/internal/bin_stats.h"

/*
 * A bin contains a set of extents that are currently being used for slab
 * allocations.
 */

/*
 * Read-only information associated with each element of arena_t's bins array
 * is stored separately, partly to reduce memory usage (only one copy, rather
 * than one per arena), but mainly to avoid false cacheline sharing.
 *
 * Each slab has the following layout:
 *
 *   /--------------------\
 *   | region 0           |
 *   |--------------------|
 *   | region 1           |
 *   |--------------------|
 *   | ...                |
 *   | ...                |
 *   | ...                |
 *   |--------------------|
 *   | region nregs-1     |
 *   \--------------------/
 */
typedef struct bin_info_s bin_info_t;
struct bin_info_s {
	/* Size of regions in a slab for this bin's size class. */
	size_t			reg_size;

	/* Total size of a slab for this bin's size class. */
	size_t			slab_size;

	/* Total number of regions in a slab for this bin's size class. */
	uint32_t		nregs;

	/*
	 * Metadata used to manipulate bitmaps for slabs associated with this
	 * bin.
	 */
	bitmap_info_t		bitmap_info;
};

extern const bin_info_t bin_infos[NBINS];


typedef struct bin_s bin_t;
struct bin_s {
	/* All operations on bin_t fields require lock ownership. */
	malloc_mutex_t		lock;

	/*
	 * Current slab being used to service allocations of this bin's size
	 * class.  slabcur is independent of slabs_{nonfull,full}; whenever
	 * slabcur is reassigned, the previous slab must be deallocated or
	 * inserted into slabs_{nonfull,full}.
	 */
	extent_t		*slabcur;

	/*
	 * Heap of non-full slabs.  This heap is used to assure that new
	 * allocations come from the non-full slab that is oldest/lowest in
	 * memory.
	 */
	extent_heap_t		slabs_nonfull;

	/* List used to track full slabs. */
	extent_list_t		slabs_full;

	/* Bin statistics. */
	bin_stats_t	stats;
};

/* Initializes a bin to empty.  Returns true on error. */
bool bin_init(bin_t *bin);

/* Forking. */
void bin_prefork(tsdn_t *tsdn, bin_t *bin);
void bin_postfork_parent(tsdn_t *tsdn, bin_t *bin);
void bin_postfork_child(tsdn_t *tsdn, bin_t *bin);

/* Stats. */
static inline void
bin_stats_merge(tsdn_t *tsdn, bin_stats_t *dst_bin_stats, bin_t *bin) {
	malloc_mutex_lock(tsdn, &bin->lock);
	malloc_mutex_prof_read(tsdn, &dst_bin_stats->mutex_data, &bin->lock);
	dst_bin_stats->nmalloc += bin->stats.nmalloc;
	dst_bin_stats->ndalloc += bin->stats.ndalloc;
	dst_bin_stats->nrequests += bin->stats.nrequests;
	dst_bin_stats->curregs += bin->stats.curregs;
	dst_bin_stats->nfills += bin->stats.nfills;
	dst_bin_stats->nflushes += bin->stats.nflushes;
	dst_bin_stats->nslabs += bin->stats.nslabs;
	dst_bin_stats->reslabs += bin->stats.reslabs;
	dst_bin_stats->curslabs += bin->stats.curslabs;
	malloc_mutex_unlock(tsdn, &bin->lock);
}

#endif /* JEMALLOC_INTERNAL_BIN_H */
