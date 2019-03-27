#ifndef JEMALLOC_INTERNAL_BASE_EXTERNS_H
#define JEMALLOC_INTERNAL_BASE_EXTERNS_H

extern metadata_thp_mode_t opt_metadata_thp;
extern const char *metadata_thp_mode_names[];

base_t *b0get(void);
base_t *base_new(tsdn_t *tsdn, unsigned ind, extent_hooks_t *extent_hooks);
void base_delete(tsdn_t *tsdn, base_t *base);
extent_hooks_t *base_extent_hooks_get(base_t *base);
extent_hooks_t *base_extent_hooks_set(base_t *base,
    extent_hooks_t *extent_hooks);
void *base_alloc(tsdn_t *tsdn, base_t *base, size_t size, size_t alignment);
extent_t *base_alloc_extent(tsdn_t *tsdn, base_t *base);
void base_stats_get(tsdn_t *tsdn, base_t *base, size_t *allocated,
    size_t *resident, size_t *mapped, size_t *n_thp);
void base_prefork(tsdn_t *tsdn, base_t *base);
void base_postfork_parent(tsdn_t *tsdn, base_t *base);
void base_postfork_child(tsdn_t *tsdn, base_t *base);
bool base_boot(tsdn_t *tsdn);

#endif /* JEMALLOC_INTERNAL_BASE_EXTERNS_H */
