#ifndef JEMALLOC_INTERNAL_EXTENT_DSS_H
#define JEMALLOC_INTERNAL_EXTENT_DSS_H

typedef enum {
	dss_prec_disabled  = 0,
	dss_prec_primary   = 1,
	dss_prec_secondary = 2,

	dss_prec_limit     = 3
} dss_prec_t;
#define DSS_PREC_DEFAULT dss_prec_secondary
#define DSS_DEFAULT "secondary"

extern const char *dss_prec_names[];

extern const char *opt_dss;

dss_prec_t extent_dss_prec_get(void);
bool extent_dss_prec_set(dss_prec_t dss_prec);
void *extent_alloc_dss(tsdn_t *tsdn, arena_t *arena, void *new_addr,
    size_t size, size_t alignment, bool *zero, bool *commit);
bool extent_in_dss(void *addr);
bool extent_dss_mergeable(void *addr_a, void *addr_b);
void extent_dss_boot(void);

#endif /* JEMALLOC_INTERNAL_EXTENT_DSS_H */
