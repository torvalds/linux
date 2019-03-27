#ifndef JEMALLOC_INTERNAL_LARGE_EXTERNS_H
#define JEMALLOC_INTERNAL_LARGE_EXTERNS_H

void *large_malloc(tsdn_t *tsdn, arena_t *arena, size_t usize, bool zero);
void *large_palloc(tsdn_t *tsdn, arena_t *arena, size_t usize, size_t alignment,
    bool zero);
bool large_ralloc_no_move(tsdn_t *tsdn, extent_t *extent, size_t usize_min,
    size_t usize_max, bool zero);
void *large_ralloc(tsdn_t *tsdn, arena_t *arena, extent_t *extent, size_t usize,
    size_t alignment, bool zero, tcache_t *tcache);

typedef void (large_dalloc_junk_t)(void *, size_t);
extern large_dalloc_junk_t *JET_MUTABLE large_dalloc_junk;

typedef void (large_dalloc_maybe_junk_t)(void *, size_t);
extern large_dalloc_maybe_junk_t *JET_MUTABLE large_dalloc_maybe_junk;

void large_dalloc_prep_junked_locked(tsdn_t *tsdn, extent_t *extent);
void large_dalloc_finish(tsdn_t *tsdn, extent_t *extent);
void large_dalloc(tsdn_t *tsdn, extent_t *extent);
size_t large_salloc(tsdn_t *tsdn, const extent_t *extent);
prof_tctx_t *large_prof_tctx_get(tsdn_t *tsdn, const extent_t *extent);
void large_prof_tctx_set(tsdn_t *tsdn, extent_t *extent, prof_tctx_t *tctx);
void large_prof_tctx_reset(tsdn_t *tsdn, extent_t *extent);

#endif /* JEMALLOC_INTERNAL_LARGE_EXTERNS_H */
