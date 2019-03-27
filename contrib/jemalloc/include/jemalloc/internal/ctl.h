#ifndef JEMALLOC_INTERNAL_CTL_H
#define JEMALLOC_INTERNAL_CTL_H

#include "jemalloc/internal/jemalloc_internal_types.h"
#include "jemalloc/internal/malloc_io.h"
#include "jemalloc/internal/mutex_prof.h"
#include "jemalloc/internal/ql.h"
#include "jemalloc/internal/size_classes.h"
#include "jemalloc/internal/stats.h"

/* Maximum ctl tree depth. */
#define CTL_MAX_DEPTH	7

typedef struct ctl_node_s {
	bool named;
} ctl_node_t;

typedef struct ctl_named_node_s {
	ctl_node_t node;
	const char *name;
	/* If (nchildren == 0), this is a terminal node. */
	size_t nchildren;
	const ctl_node_t *children;
	int (*ctl)(tsd_t *, const size_t *, size_t, void *, size_t *, void *,
	    size_t);
} ctl_named_node_t;

typedef struct ctl_indexed_node_s {
	struct ctl_node_s node;
	const ctl_named_node_t *(*index)(tsdn_t *, const size_t *, size_t,
	    size_t);
} ctl_indexed_node_t;

typedef struct ctl_arena_stats_s {
	arena_stats_t astats;

	/* Aggregate stats for small size classes, based on bin stats. */
	size_t allocated_small;
	uint64_t nmalloc_small;
	uint64_t ndalloc_small;
	uint64_t nrequests_small;

	bin_stats_t bstats[NBINS];
	arena_stats_large_t lstats[NSIZES - NBINS];
} ctl_arena_stats_t;

typedef struct ctl_stats_s {
	size_t allocated;
	size_t active;
	size_t metadata;
	size_t metadata_thp;
	size_t resident;
	size_t mapped;
	size_t retained;

	background_thread_stats_t background_thread;
	mutex_prof_data_t mutex_prof_data[mutex_prof_num_global_mutexes];
} ctl_stats_t;

typedef struct ctl_arena_s ctl_arena_t;
struct ctl_arena_s {
	unsigned arena_ind;
	bool initialized;
	ql_elm(ctl_arena_t) destroyed_link;

	/* Basic stats, supported even if !config_stats. */
	unsigned nthreads;
	const char *dss;
	ssize_t dirty_decay_ms;
	ssize_t muzzy_decay_ms;
	size_t pactive;
	size_t pdirty;
	size_t pmuzzy;

	/* NULL if !config_stats. */
	ctl_arena_stats_t *astats;
};

typedef struct ctl_arenas_s {
	uint64_t epoch;
	unsigned narenas;
	ql_head(ctl_arena_t) destroyed;

	/*
	 * Element 0 corresponds to merged stats for extant arenas (accessed via
	 * MALLCTL_ARENAS_ALL), element 1 corresponds to merged stats for
	 * destroyed arenas (accessed via MALLCTL_ARENAS_DESTROYED), and the
	 * remaining MALLOCX_ARENA_LIMIT elements correspond to arenas.
	 */
	ctl_arena_t *arenas[2 + MALLOCX_ARENA_LIMIT];
} ctl_arenas_t;

int ctl_byname(tsd_t *tsd, const char *name, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen);
int ctl_nametomib(tsd_t *tsd, const char *name, size_t *mibp, size_t *miblenp);

int ctl_bymib(tsd_t *tsd, const size_t *mib, size_t miblen, void *oldp,
    size_t *oldlenp, void *newp, size_t newlen);
bool ctl_boot(void);
void ctl_prefork(tsdn_t *tsdn);
void ctl_postfork_parent(tsdn_t *tsdn);
void ctl_postfork_child(tsdn_t *tsdn);

#define xmallctl(name, oldp, oldlenp, newp, newlen) do {		\
	if (je_mallctl(name, oldp, oldlenp, newp, newlen)		\
	    != 0) {							\
		malloc_printf(						\
		    "<jemalloc>: Failure in xmallctl(\"%s\", ...)\n",	\
		    name);						\
		abort();						\
	}								\
} while (0)

#define xmallctlnametomib(name, mibp, miblenp) do {			\
	if (je_mallctlnametomib(name, mibp, miblenp) != 0) {		\
		malloc_printf("<jemalloc>: Failure in "			\
		    "xmallctlnametomib(\"%s\", ...)\n", name);		\
		abort();						\
	}								\
} while (0)

#define xmallctlbymib(mib, miblen, oldp, oldlenp, newp, newlen) do {	\
	if (je_mallctlbymib(mib, miblen, oldp, oldlenp, newp,		\
	    newlen) != 0) {						\
		malloc_write(						\
		    "<jemalloc>: Failure in xmallctlbymib()\n");	\
		abort();						\
	}								\
} while (0)

#endif /* JEMALLOC_INTERNAL_CTL_H */
