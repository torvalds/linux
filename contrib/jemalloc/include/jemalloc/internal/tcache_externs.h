#ifndef JEMALLOC_INTERNAL_TCACHE_EXTERNS_H
#define JEMALLOC_INTERNAL_TCACHE_EXTERNS_H

#include "jemalloc/internal/size_classes.h"

extern bool	opt_tcache;
extern ssize_t	opt_lg_tcache_max;

extern cache_bin_info_t	*tcache_bin_info;

/*
 * Number of tcache bins.  There are NBINS small-object bins, plus 0 or more
 * large-object bins.
 */
extern unsigned	nhbins;

/* Maximum cached size class. */
extern size_t	tcache_maxclass;

/*
 * Explicit tcaches, managed via the tcache.{create,flush,destroy} mallctls and
 * usable via the MALLOCX_TCACHE() flag.  The automatic per thread tcaches are
 * completely disjoint from this data structure.  tcaches starts off as a sparse
 * array, so it has no physical memory footprint until individual pages are
 * touched.  This allows the entire array to be allocated the first time an
 * explicit tcache is created without a disproportionate impact on memory usage.
 */
extern tcaches_t	*tcaches;

size_t	tcache_salloc(tsdn_t *tsdn, const void *ptr);
void	tcache_event_hard(tsd_t *tsd, tcache_t *tcache);
void	*tcache_alloc_small_hard(tsdn_t *tsdn, arena_t *arena, tcache_t *tcache,
    cache_bin_t *tbin, szind_t binind, bool *tcache_success);
void	tcache_bin_flush_small(tsd_t *tsd, tcache_t *tcache, cache_bin_t *tbin,
    szind_t binind, unsigned rem);
void	tcache_bin_flush_large(tsd_t *tsd, cache_bin_t *tbin, szind_t binind,
    unsigned rem, tcache_t *tcache);
void	tcache_arena_reassociate(tsdn_t *tsdn, tcache_t *tcache,
    arena_t *arena);
tcache_t *tcache_create_explicit(tsd_t *tsd);
void	tcache_cleanup(tsd_t *tsd);
void	tcache_stats_merge(tsdn_t *tsdn, tcache_t *tcache, arena_t *arena);
bool	tcaches_create(tsd_t *tsd, unsigned *r_ind);
void	tcaches_flush(tsd_t *tsd, unsigned ind);
void	tcaches_destroy(tsd_t *tsd, unsigned ind);
bool	tcache_boot(tsdn_t *tsdn);
void tcache_arena_associate(tsdn_t *tsdn, tcache_t *tcache, arena_t *arena);
void tcache_prefork(tsdn_t *tsdn);
void tcache_postfork_parent(tsdn_t *tsdn);
void tcache_postfork_child(tsdn_t *tsdn);
void tcache_flush(tsd_t *tsd);
bool tsd_tcache_data_init(tsd_t *tsd);
bool tsd_tcache_enabled_data_init(tsd_t *tsd);

#endif /* JEMALLOC_INTERNAL_TCACHE_EXTERNS_H */
