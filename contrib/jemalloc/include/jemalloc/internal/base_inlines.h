#ifndef JEMALLOC_INTERNAL_BASE_INLINES_H
#define JEMALLOC_INTERNAL_BASE_INLINES_H

static inline unsigned
base_ind_get(const base_t *base) {
	return base->ind;
}

static inline bool
metadata_thp_enabled(void) {
	return (opt_metadata_thp != metadata_thp_disabled);
}
#endif /* JEMALLOC_INTERNAL_BASE_INLINES_H */
