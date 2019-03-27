#ifndef JEMALLOC_INTERNAL_ARENA_EXTERNS_H
#define JEMALLOC_INTERNAL_ARENA_EXTERNS_H

#include "jemalloc/internal/bin.h"
#include "jemalloc/internal/extent_dss.h"
#include "jemalloc/internal/pages.h"
#include "jemalloc/internal/size_classes.h"
#include "jemalloc/internal/stats.h"

extern ssize_t opt_dirty_decay_ms;
extern ssize_t opt_muzzy_decay_ms;

extern percpu_arena_mode_t opt_percpu_arena;
extern const char *percpu_arena_mode_names[];

extern const uint64_t h_steps[SMOOTHSTEP_NSTEPS];
extern malloc_mutex_t arenas_lock;

void arena_basic_stats_merge(tsdn_t *tsdn, arena_t *arena,
    unsigned *nthreads, const char **dss, ssize_t *dirty_decay_ms,
    ssize_t *muzzy_decay_ms, size_t *nactive, size_t *ndirty, size_t *nmuzzy);
void arena_stats_merge(tsdn_t *tsdn, arena_t *arena, unsigned *nthreads,
    const char **dss, ssize_t *dirty_decay_ms, ssize_t *muzzy_decay_ms,
    size_t *nactive, size_t *ndirty, size_t *nmuzzy, arena_stats_t *astats,
    bin_stats_t *bstats, arena_stats_large_t *lstats);
void arena_extents_dirty_dalloc(tsdn_t *tsdn, arena_t *arena,
    extent_hooks_t **r_extent_hooks, extent_t *extent);
#ifdef JEMALLOC_JET
size_t arena_slab_regind(extent_t *slab, szind_t binind, const void *ptr);
#endif
extent_t *arena_extent_alloc_large(tsdn_t *tsdn, arena_t *arena,
    size_t usize, size_t alignment, bool *zero);
void arena_extent_dalloc_large_prep(tsdn_t *tsdn, arena_t *arena,
    extent_t *extent);
void arena_extent_ralloc_large_shrink(tsdn_t *tsdn, arena_t *arena,
    extent_t *extent, size_t oldsize);
void arena_extent_ralloc_large_expand(tsdn_t *tsdn, arena_t *arena,
    extent_t *extent, size_t oldsize);
ssize_t arena_dirty_decay_ms_get(arena_t *arena);
bool arena_dirty_decay_ms_set(tsdn_t *tsdn, arena_t *arena, ssize_t decay_ms);
ssize_t arena_muzzy_decay_ms_get(arena_t *arena);
bool arena_muzzy_decay_ms_set(tsdn_t *tsdn, arena_t *arena, ssize_t decay_ms);
void arena_decay(tsdn_t *tsdn, arena_t *arena, bool is_background_thread,
    bool all);
void arena_reset(tsd_t *tsd, arena_t *arena);
void arena_destroy(tsd_t *tsd, arena_t *arena);
void arena_tcache_fill_small(tsdn_t *tsdn, arena_t *arena, tcache_t *tcache,
    cache_bin_t *tbin, szind_t binind, uint64_t prof_accumbytes);
void arena_alloc_junk_small(void *ptr, const bin_info_t *bin_info,
    bool zero);

typedef void (arena_dalloc_junk_small_t)(void *, const bin_info_t *);
extern arena_dalloc_junk_small_t *JET_MUTABLE arena_dalloc_junk_small;

void *arena_malloc_hard(tsdn_t *tsdn, arena_t *arena, size_t size,
    szind_t ind, bool zero);
void *arena_palloc(tsdn_t *tsdn, arena_t *arena, size_t usize,
    size_t alignment, bool zero, tcache_t *tcache);
void arena_prof_promote(tsdn_t *tsdn, const void *ptr, size_t usize);
void arena_dalloc_promoted(tsdn_t *tsdn, void *ptr, tcache_t *tcache,
    bool slow_path);
void arena_dalloc_bin_junked_locked(tsdn_t *tsdn, arena_t *arena,
    extent_t *extent, void *ptr);
void arena_dalloc_small(tsdn_t *tsdn, void *ptr);
bool arena_ralloc_no_move(tsdn_t *tsdn, void *ptr, size_t oldsize, size_t size,
    size_t extra, bool zero);
void *arena_ralloc(tsdn_t *tsdn, arena_t *arena, void *ptr, size_t oldsize,
    size_t size, size_t alignment, bool zero, tcache_t *tcache);
dss_prec_t arena_dss_prec_get(arena_t *arena);
bool arena_dss_prec_set(arena_t *arena, dss_prec_t dss_prec);
ssize_t arena_dirty_decay_ms_default_get(void);
bool arena_dirty_decay_ms_default_set(ssize_t decay_ms);
ssize_t arena_muzzy_decay_ms_default_get(void);
bool arena_muzzy_decay_ms_default_set(ssize_t decay_ms);
bool arena_retain_grow_limit_get_set(tsd_t *tsd, arena_t *arena,
    size_t *old_limit, size_t *new_limit);
unsigned arena_nthreads_get(arena_t *arena, bool internal);
void arena_nthreads_inc(arena_t *arena, bool internal);
void arena_nthreads_dec(arena_t *arena, bool internal);
size_t arena_extent_sn_next(arena_t *arena);
arena_t *arena_new(tsdn_t *tsdn, unsigned ind, extent_hooks_t *extent_hooks);
void arena_boot(void);
void arena_prefork0(tsdn_t *tsdn, arena_t *arena);
void arena_prefork1(tsdn_t *tsdn, arena_t *arena);
void arena_prefork2(tsdn_t *tsdn, arena_t *arena);
void arena_prefork3(tsdn_t *tsdn, arena_t *arena);
void arena_prefork4(tsdn_t *tsdn, arena_t *arena);
void arena_prefork5(tsdn_t *tsdn, arena_t *arena);
void arena_prefork6(tsdn_t *tsdn, arena_t *arena);
void arena_prefork7(tsdn_t *tsdn, arena_t *arena);
void arena_postfork_parent(tsdn_t *tsdn, arena_t *arena);
void arena_postfork_child(tsdn_t *tsdn, arena_t *arena);

#endif /* JEMALLOC_INTERNAL_ARENA_EXTERNS_H */
