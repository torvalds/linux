#define JEMALLOC_EXTENT_DSS_C_
#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/jemalloc_internal_includes.h"

#include "jemalloc/internal/assert.h"
#include "jemalloc/internal/extent_dss.h"
#include "jemalloc/internal/spin.h"

/******************************************************************************/
/* Data. */

const char	*opt_dss = DSS_DEFAULT;

const char	*dss_prec_names[] = {
	"disabled",
	"primary",
	"secondary",
	"N/A"
};

/*
 * Current dss precedence default, used when creating new arenas.  NB: This is
 * stored as unsigned rather than dss_prec_t because in principle there's no
 * guarantee that sizeof(dss_prec_t) is the same as sizeof(unsigned), and we use
 * atomic operations to synchronize the setting.
 */
static atomic_u_t	dss_prec_default = ATOMIC_INIT(
    (unsigned)DSS_PREC_DEFAULT);

/* Base address of the DSS. */
static void		*dss_base;
/* Atomic boolean indicating whether a thread is currently extending DSS. */
static atomic_b_t	dss_extending;
/* Atomic boolean indicating whether the DSS is exhausted. */
static atomic_b_t	dss_exhausted;
/* Atomic current upper limit on DSS addresses. */
static atomic_p_t	dss_max;

/******************************************************************************/

static void *
extent_dss_sbrk(intptr_t increment) {
#ifdef JEMALLOC_DSS
	return sbrk(increment);
#else
	not_implemented();
	return NULL;
#endif
}

dss_prec_t
extent_dss_prec_get(void) {
	dss_prec_t ret;

	if (!have_dss) {
		return dss_prec_disabled;
	}
	ret = (dss_prec_t)atomic_load_u(&dss_prec_default, ATOMIC_ACQUIRE);
	return ret;
}

bool
extent_dss_prec_set(dss_prec_t dss_prec) {
	if (!have_dss) {
		return (dss_prec != dss_prec_disabled);
	}
	atomic_store_u(&dss_prec_default, (unsigned)dss_prec, ATOMIC_RELEASE);
	return false;
}

static void
extent_dss_extending_start(void) {
	spin_t spinner = SPIN_INITIALIZER;
	while (true) {
		bool expected = false;
		if (atomic_compare_exchange_weak_b(&dss_extending, &expected,
		    true, ATOMIC_ACQ_REL, ATOMIC_RELAXED)) {
			break;
		}
		spin_adaptive(&spinner);
	}
}

static void
extent_dss_extending_finish(void) {
	assert(atomic_load_b(&dss_extending, ATOMIC_RELAXED));

	atomic_store_b(&dss_extending, false, ATOMIC_RELEASE);
}

static void *
extent_dss_max_update(void *new_addr) {
	/*
	 * Get the current end of the DSS as max_cur and assure that dss_max is
	 * up to date.
	 */
	void *max_cur = extent_dss_sbrk(0);
	if (max_cur == (void *)-1) {
		return NULL;
	}
	atomic_store_p(&dss_max, max_cur, ATOMIC_RELEASE);
	/* Fixed new_addr can only be supported if it is at the edge of DSS. */
	if (new_addr != NULL && max_cur != new_addr) {
		return NULL;
	}
	return max_cur;
}

void *
extent_alloc_dss(tsdn_t *tsdn, arena_t *arena, void *new_addr, size_t size,
    size_t alignment, bool *zero, bool *commit) {
	extent_t *gap;

	cassert(have_dss);
	assert(size > 0);
	assert(alignment > 0);

	/*
	 * sbrk() uses a signed increment argument, so take care not to
	 * interpret a large allocation request as a negative increment.
	 */
	if ((intptr_t)size < 0) {
		return NULL;
	}

	gap = extent_alloc(tsdn, arena);
	if (gap == NULL) {
		return NULL;
	}

	extent_dss_extending_start();
	if (!atomic_load_b(&dss_exhausted, ATOMIC_ACQUIRE)) {
		/*
		 * The loop is necessary to recover from races with other
		 * threads that are using the DSS for something other than
		 * malloc.
		 */
		while (true) {
			void *max_cur = extent_dss_max_update(new_addr);
			if (max_cur == NULL) {
				goto label_oom;
			}

			/*
			 * Compute how much page-aligned gap space (if any) is
			 * necessary to satisfy alignment.  This space can be
			 * recycled for later use.
			 */
			void *gap_addr_page = (void *)(PAGE_CEILING(
			    (uintptr_t)max_cur));
			void *ret = (void *)ALIGNMENT_CEILING(
			    (uintptr_t)gap_addr_page, alignment);
			size_t gap_size_page = (uintptr_t)ret -
			    (uintptr_t)gap_addr_page;
			if (gap_size_page != 0) {
				extent_init(gap, arena, gap_addr_page,
				    gap_size_page, false, NSIZES,
				    arena_extent_sn_next(arena),
				    extent_state_active, false, true, true);
			}
			/*
			 * Compute the address just past the end of the desired
			 * allocation space.
			 */
			void *dss_next = (void *)((uintptr_t)ret + size);
			if ((uintptr_t)ret < (uintptr_t)max_cur ||
			    (uintptr_t)dss_next < (uintptr_t)max_cur) {
				goto label_oom; /* Wrap-around. */
			}
			/* Compute the increment, including subpage bytes. */
			void *gap_addr_subpage = max_cur;
			size_t gap_size_subpage = (uintptr_t)ret -
			    (uintptr_t)gap_addr_subpage;
			intptr_t incr = gap_size_subpage + size;

			assert((uintptr_t)max_cur + incr == (uintptr_t)ret +
			    size);

			/* Try to allocate. */
			void *dss_prev = extent_dss_sbrk(incr);
			if (dss_prev == max_cur) {
				/* Success. */
				atomic_store_p(&dss_max, dss_next,
				    ATOMIC_RELEASE);
				extent_dss_extending_finish();

				if (gap_size_page != 0) {
					extent_dalloc_gap(tsdn, arena, gap);
				} else {
					extent_dalloc(tsdn, arena, gap);
				}
				if (!*commit) {
					*commit = pages_decommit(ret, size);
				}
				if (*zero && *commit) {
					extent_hooks_t *extent_hooks =
					    EXTENT_HOOKS_INITIALIZER;
					extent_t extent;

					extent_init(&extent, arena, ret, size,
					    size, false, NSIZES,
					    extent_state_active, false, true,
					    true);
					if (extent_purge_forced_wrapper(tsdn,
					    arena, &extent_hooks, &extent, 0,
					    size)) {
						memset(ret, 0, size);
					}
				}
				return ret;
			}
			/*
			 * Failure, whether due to OOM or a race with a raw
			 * sbrk() call from outside the allocator.
			 */
			if (dss_prev == (void *)-1) {
				/* OOM. */
				atomic_store_b(&dss_exhausted, true,
				    ATOMIC_RELEASE);
				goto label_oom;
			}
		}
	}
label_oom:
	extent_dss_extending_finish();
	extent_dalloc(tsdn, arena, gap);
	return NULL;
}

static bool
extent_in_dss_helper(void *addr, void *max) {
	return ((uintptr_t)addr >= (uintptr_t)dss_base && (uintptr_t)addr <
	    (uintptr_t)max);
}

bool
extent_in_dss(void *addr) {
	cassert(have_dss);

	return extent_in_dss_helper(addr, atomic_load_p(&dss_max,
	    ATOMIC_ACQUIRE));
}

bool
extent_dss_mergeable(void *addr_a, void *addr_b) {
	void *max;

	cassert(have_dss);

	if ((uintptr_t)addr_a < (uintptr_t)dss_base && (uintptr_t)addr_b <
	    (uintptr_t)dss_base) {
		return true;
	}

	max = atomic_load_p(&dss_max, ATOMIC_ACQUIRE);
	return (extent_in_dss_helper(addr_a, max) ==
	    extent_in_dss_helper(addr_b, max));
}

void
extent_dss_boot(void) {
	cassert(have_dss);

	dss_base = extent_dss_sbrk(0);
	atomic_store_b(&dss_extending, false, ATOMIC_RELAXED);
	atomic_store_b(&dss_exhausted, dss_base == (void *)-1, ATOMIC_RELAXED);
	atomic_store_p(&dss_max, dss_base, ATOMIC_RELAXED);
}

/******************************************************************************/
