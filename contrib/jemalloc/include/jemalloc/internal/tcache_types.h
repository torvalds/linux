#ifndef JEMALLOC_INTERNAL_TCACHE_TYPES_H
#define JEMALLOC_INTERNAL_TCACHE_TYPES_H

#include "jemalloc/internal/size_classes.h"

typedef struct tcache_s tcache_t;
typedef struct tcaches_s tcaches_t;

/*
 * tcache pointers close to NULL are used to encode state information that is
 * used for two purposes: preventing thread caching on a per thread basis and
 * cleaning up during thread shutdown.
 */
#define TCACHE_STATE_DISABLED		((tcache_t *)(uintptr_t)1)
#define TCACHE_STATE_REINCARNATED	((tcache_t *)(uintptr_t)2)
#define TCACHE_STATE_PURGATORY		((tcache_t *)(uintptr_t)3)
#define TCACHE_STATE_MAX		TCACHE_STATE_PURGATORY

/*
 * Absolute minimum number of cache slots for each small bin.
 */
#define TCACHE_NSLOTS_SMALL_MIN		20

/*
 * Absolute maximum number of cache slots for each small bin in the thread
 * cache.  This is an additional constraint beyond that imposed as: twice the
 * number of regions per slab for this size class.
 *
 * This constant must be an even number.
 */
#define TCACHE_NSLOTS_SMALL_MAX		200

/* Number of cache slots for large size classes. */
#define TCACHE_NSLOTS_LARGE		20

/* (1U << opt_lg_tcache_max) is used to compute tcache_maxclass. */
#define LG_TCACHE_MAXCLASS_DEFAULT	15

/*
 * TCACHE_GC_SWEEP is the approximate number of allocation events between
 * full GC sweeps.  Integer rounding may cause the actual number to be
 * slightly higher, since GC is performed incrementally.
 */
#define TCACHE_GC_SWEEP			8192

/* Number of tcache allocation/deallocation events between incremental GCs. */
#define TCACHE_GC_INCR							\
    ((TCACHE_GC_SWEEP / NBINS) + ((TCACHE_GC_SWEEP / NBINS == 0) ? 0 : 1))

/* Used in TSD static initializer only. Real init in tcache_data_init(). */
#define TCACHE_ZERO_INITIALIZER {0}

/* Used in TSD static initializer only. Will be initialized to opt_tcache. */
#define TCACHE_ENABLED_ZERO_INITIALIZER false

#endif /* JEMALLOC_INTERNAL_TCACHE_TYPES_H */
