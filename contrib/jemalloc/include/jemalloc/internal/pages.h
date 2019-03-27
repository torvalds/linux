#ifndef JEMALLOC_INTERNAL_PAGES_EXTERNS_H
#define JEMALLOC_INTERNAL_PAGES_EXTERNS_H

/* Page size.  LG_PAGE is determined by the configure script. */
#ifdef PAGE_MASK
#  undef PAGE_MASK
#endif
#define PAGE		((size_t)(1U << LG_PAGE))
#define PAGE_MASK	((size_t)(PAGE - 1))
/* Return the page base address for the page containing address a. */
#define PAGE_ADDR2BASE(a)						\
	((void *)((uintptr_t)(a) & ~PAGE_MASK))
/* Return the smallest pagesize multiple that is >= s. */
#define PAGE_CEILING(s)							\
	(((s) + PAGE_MASK) & ~PAGE_MASK)

/* Huge page size.  LG_HUGEPAGE is determined by the configure script. */
#define HUGEPAGE	((size_t)(1U << LG_HUGEPAGE))
#define HUGEPAGE_MASK	((size_t)(HUGEPAGE - 1))
/* Return the huge page base address for the huge page containing address a. */
#define HUGEPAGE_ADDR2BASE(a)						\
	((void *)((uintptr_t)(a) & ~HUGEPAGE_MASK))
/* Return the smallest pagesize multiple that is >= s. */
#define HUGEPAGE_CEILING(s)						\
	(((s) + HUGEPAGE_MASK) & ~HUGEPAGE_MASK)

/* PAGES_CAN_PURGE_LAZY is defined if lazy purging is supported. */
#if defined(_WIN32) || defined(JEMALLOC_PURGE_MADVISE_FREE)
#  define PAGES_CAN_PURGE_LAZY
#endif
/*
 * PAGES_CAN_PURGE_FORCED is defined if forced purging is supported.
 *
 * The only supported way to hard-purge on Windows is to decommit and then
 * re-commit, but doing so is racy, and if re-commit fails it's a pain to
 * propagate the "poisoned" memory state.  Since we typically decommit as the
 * next step after purging on Windows anyway, there's no point in adding such
 * complexity.
 */
#if !defined(_WIN32) && ((defined(JEMALLOC_PURGE_MADVISE_DONTNEED) && \
    defined(JEMALLOC_PURGE_MADVISE_DONTNEED_ZEROS)) || \
    defined(JEMALLOC_MAPS_COALESCE))
#  define PAGES_CAN_PURGE_FORCED
#endif

static const bool pages_can_purge_lazy =
#ifdef PAGES_CAN_PURGE_LAZY
    true
#else
    false
#endif
    ;
static const bool pages_can_purge_forced =
#ifdef PAGES_CAN_PURGE_FORCED
    true
#else
    false
#endif
    ;

typedef enum {
	thp_mode_default       = 0, /* Do not change hugepage settings. */
	thp_mode_always        = 1, /* Always set MADV_HUGEPAGE. */
	thp_mode_never         = 2, /* Always set MADV_NOHUGEPAGE. */

	thp_mode_names_limit   = 3, /* Used for option processing. */
	thp_mode_not_supported = 3  /* No THP support detected. */
} thp_mode_t;

#define THP_MODE_DEFAULT thp_mode_default
extern thp_mode_t opt_thp;
extern thp_mode_t init_system_thp_mode; /* Initial system wide state. */
extern const char *thp_mode_names[];

void *pages_map(void *addr, size_t size, size_t alignment, bool *commit);
void pages_unmap(void *addr, size_t size);
bool pages_commit(void *addr, size_t size);
bool pages_decommit(void *addr, size_t size);
bool pages_purge_lazy(void *addr, size_t size);
bool pages_purge_forced(void *addr, size_t size);
bool pages_huge(void *addr, size_t size);
bool pages_nohuge(void *addr, size_t size);
bool pages_dontdump(void *addr, size_t size);
bool pages_dodump(void *addr, size_t size);
bool pages_boot(void);
void pages_set_thp_state (void *ptr, size_t size);

#endif /* JEMALLOC_INTERNAL_PAGES_EXTERNS_H */
