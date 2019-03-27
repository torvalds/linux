#ifndef JEMALLOC_INTERNAL_WITNESS_H
#define JEMALLOC_INTERNAL_WITNESS_H

#include "jemalloc/internal/ql.h"

/******************************************************************************/
/* LOCK RANKS */
/******************************************************************************/

/*
 * Witnesses with rank WITNESS_RANK_OMIT are completely ignored by the witness
 * machinery.
 */

#define WITNESS_RANK_OMIT		0U

#define WITNESS_RANK_MIN		1U

#define WITNESS_RANK_INIT		1U
#define WITNESS_RANK_CTL		1U
#define WITNESS_RANK_TCACHES		2U
#define WITNESS_RANK_ARENAS		3U

#define WITNESS_RANK_BACKGROUND_THREAD_GLOBAL	4U

#define WITNESS_RANK_PROF_DUMP		5U
#define WITNESS_RANK_PROF_BT2GCTX	6U
#define WITNESS_RANK_PROF_TDATAS	7U
#define WITNESS_RANK_PROF_TDATA		8U
#define WITNESS_RANK_PROF_GCTX		9U

#define WITNESS_RANK_BACKGROUND_THREAD	10U

/*
 * Used as an argument to witness_assert_depth_to_rank() in order to validate
 * depth excluding non-core locks with lower ranks.  Since the rank argument to
 * witness_assert_depth_to_rank() is inclusive rather than exclusive, this
 * definition can have the same value as the minimally ranked core lock.
 */
#define WITNESS_RANK_CORE		11U

#define WITNESS_RANK_DECAY		11U
#define WITNESS_RANK_TCACHE_QL		12U
#define WITNESS_RANK_EXTENT_GROW	13U
#define WITNESS_RANK_EXTENTS		14U
#define WITNESS_RANK_EXTENT_AVAIL	15U

#define WITNESS_RANK_EXTENT_POOL	16U
#define WITNESS_RANK_RTREE		17U
#define WITNESS_RANK_BASE		18U
#define WITNESS_RANK_ARENA_LARGE	19U

#define WITNESS_RANK_LEAF		0xffffffffU
#define WITNESS_RANK_BIN		WITNESS_RANK_LEAF
#define WITNESS_RANK_ARENA_STATS	WITNESS_RANK_LEAF
#define WITNESS_RANK_DSS		WITNESS_RANK_LEAF
#define WITNESS_RANK_PROF_ACTIVE	WITNESS_RANK_LEAF
#define WITNESS_RANK_PROF_ACCUM		WITNESS_RANK_LEAF
#define WITNESS_RANK_PROF_DUMP_SEQ	WITNESS_RANK_LEAF
#define WITNESS_RANK_PROF_GDUMP		WITNESS_RANK_LEAF
#define WITNESS_RANK_PROF_NEXT_THR_UID	WITNESS_RANK_LEAF
#define WITNESS_RANK_PROF_THREAD_ACTIVE_INIT	WITNESS_RANK_LEAF

/******************************************************************************/
/* PER-WITNESS DATA */
/******************************************************************************/
#if defined(JEMALLOC_DEBUG)
#  define WITNESS_INITIALIZER(name, rank) {name, rank, NULL, NULL, {NULL, NULL}}
#else
#  define WITNESS_INITIALIZER(name, rank)
#endif

typedef struct witness_s witness_t;
typedef unsigned witness_rank_t;
typedef ql_head(witness_t) witness_list_t;
typedef int witness_comp_t (const witness_t *, void *, const witness_t *,
    void *);

struct witness_s {
	/* Name, used for printing lock order reversal messages. */
	const char		*name;

	/*
	 * Witness rank, where 0 is lowest and UINT_MAX is highest.  Witnesses
	 * must be acquired in order of increasing rank.
	 */
	witness_rank_t		rank;

	/*
	 * If two witnesses are of equal rank and they have the samp comp
	 * function pointer, it is called as a last attempt to differentiate
	 * between witnesses of equal rank.
	 */
	witness_comp_t		*comp;

	/* Opaque data, passed to comp(). */
	void			*opaque;

	/* Linkage for thread's currently owned locks. */
	ql_elm(witness_t)	link;
};

/******************************************************************************/
/* PER-THREAD DATA */
/******************************************************************************/
typedef struct witness_tsd_s witness_tsd_t;
struct witness_tsd_s {
	witness_list_t witnesses;
	bool forking;
};

#define WITNESS_TSD_INITIALIZER { ql_head_initializer(witnesses), false }
#define WITNESS_TSDN_NULL ((witness_tsdn_t *)0)

/******************************************************************************/
/* (PER-THREAD) NULLABILITY HELPERS */
/******************************************************************************/
typedef struct witness_tsdn_s witness_tsdn_t;
struct witness_tsdn_s {
	witness_tsd_t witness_tsd;
};

JEMALLOC_ALWAYS_INLINE witness_tsdn_t *
witness_tsd_tsdn(witness_tsd_t *witness_tsd) {
	return (witness_tsdn_t *)witness_tsd;
}

JEMALLOC_ALWAYS_INLINE bool
witness_tsdn_null(witness_tsdn_t *witness_tsdn) {
	return witness_tsdn == NULL;
}

JEMALLOC_ALWAYS_INLINE witness_tsd_t *
witness_tsdn_tsd(witness_tsdn_t *witness_tsdn) {
	assert(!witness_tsdn_null(witness_tsdn));
	return &witness_tsdn->witness_tsd;
}

/******************************************************************************/
/* API */
/******************************************************************************/
void witness_init(witness_t *witness, const char *name, witness_rank_t rank,
    witness_comp_t *comp, void *opaque);

typedef void (witness_lock_error_t)(const witness_list_t *, const witness_t *);
extern witness_lock_error_t *JET_MUTABLE witness_lock_error;

typedef void (witness_owner_error_t)(const witness_t *);
extern witness_owner_error_t *JET_MUTABLE witness_owner_error;

typedef void (witness_not_owner_error_t)(const witness_t *);
extern witness_not_owner_error_t *JET_MUTABLE witness_not_owner_error;

typedef void (witness_depth_error_t)(const witness_list_t *,
    witness_rank_t rank_inclusive, unsigned depth);
extern witness_depth_error_t *JET_MUTABLE witness_depth_error;

void witnesses_cleanup(witness_tsd_t *witness_tsd);
void witness_prefork(witness_tsd_t *witness_tsd);
void witness_postfork_parent(witness_tsd_t *witness_tsd);
void witness_postfork_child(witness_tsd_t *witness_tsd);

/* Helper, not intended for direct use. */
static inline bool
witness_owner(witness_tsd_t *witness_tsd, const witness_t *witness) {
	witness_list_t *witnesses;
	witness_t *w;

	cassert(config_debug);

	witnesses = &witness_tsd->witnesses;
	ql_foreach(w, witnesses, link) {
		if (w == witness) {
			return true;
		}
	}

	return false;
}

static inline void
witness_assert_owner(witness_tsdn_t *witness_tsdn, const witness_t *witness) {
	witness_tsd_t *witness_tsd;

	if (!config_debug) {
		return;
	}

	if (witness_tsdn_null(witness_tsdn)) {
		return;
	}
	witness_tsd = witness_tsdn_tsd(witness_tsdn);
	if (witness->rank == WITNESS_RANK_OMIT) {
		return;
	}

	if (witness_owner(witness_tsd, witness)) {
		return;
	}
	witness_owner_error(witness);
}

static inline void
witness_assert_not_owner(witness_tsdn_t *witness_tsdn,
    const witness_t *witness) {
	witness_tsd_t *witness_tsd;
	witness_list_t *witnesses;
	witness_t *w;

	if (!config_debug) {
		return;
	}

	if (witness_tsdn_null(witness_tsdn)) {
		return;
	}
	witness_tsd = witness_tsdn_tsd(witness_tsdn);
	if (witness->rank == WITNESS_RANK_OMIT) {
		return;
	}

	witnesses = &witness_tsd->witnesses;
	ql_foreach(w, witnesses, link) {
		if (w == witness) {
			witness_not_owner_error(witness);
		}
	}
}

static inline void
witness_assert_depth_to_rank(witness_tsdn_t *witness_tsdn,
    witness_rank_t rank_inclusive, unsigned depth) {
	witness_tsd_t *witness_tsd;
	unsigned d;
	witness_list_t *witnesses;
	witness_t *w;

	if (!config_debug) {
		return;
	}

	if (witness_tsdn_null(witness_tsdn)) {
		return;
	}
	witness_tsd = witness_tsdn_tsd(witness_tsdn);

	d = 0;
	witnesses = &witness_tsd->witnesses;
	w = ql_last(witnesses, link);
	if (w != NULL) {
		ql_reverse_foreach(w, witnesses, link) {
			if (w->rank < rank_inclusive) {
				break;
			}
			d++;
		}
	}
	if (d != depth) {
		witness_depth_error(witnesses, rank_inclusive, depth);
	}
}

static inline void
witness_assert_depth(witness_tsdn_t *witness_tsdn, unsigned depth) {
	witness_assert_depth_to_rank(witness_tsdn, WITNESS_RANK_MIN, depth);
}

static inline void
witness_assert_lockless(witness_tsdn_t *witness_tsdn) {
	witness_assert_depth(witness_tsdn, 0);
}

static inline void
witness_lock(witness_tsdn_t *witness_tsdn, witness_t *witness) {
	witness_tsd_t *witness_tsd;
	witness_list_t *witnesses;
	witness_t *w;

	if (!config_debug) {
		return;
	}

	if (witness_tsdn_null(witness_tsdn)) {
		return;
	}
	witness_tsd = witness_tsdn_tsd(witness_tsdn);
	if (witness->rank == WITNESS_RANK_OMIT) {
		return;
	}

	witness_assert_not_owner(witness_tsdn, witness);

	witnesses = &witness_tsd->witnesses;
	w = ql_last(witnesses, link);
	if (w == NULL) {
		/* No other locks; do nothing. */
	} else if (witness_tsd->forking && w->rank <= witness->rank) {
		/* Forking, and relaxed ranking satisfied. */
	} else if (w->rank > witness->rank) {
		/* Not forking, rank order reversal. */
		witness_lock_error(witnesses, witness);
	} else if (w->rank == witness->rank && (w->comp == NULL || w->comp !=
	    witness->comp || w->comp(w, w->opaque, witness, witness->opaque) >
	    0)) {
		/*
		 * Missing/incompatible comparison function, or comparison
		 * function indicates rank order reversal.
		 */
		witness_lock_error(witnesses, witness);
	}

	ql_elm_new(witness, link);
	ql_tail_insert(witnesses, witness, link);
}

static inline void
witness_unlock(witness_tsdn_t *witness_tsdn, witness_t *witness) {
	witness_tsd_t *witness_tsd;
	witness_list_t *witnesses;

	if (!config_debug) {
		return;
	}

	if (witness_tsdn_null(witness_tsdn)) {
		return;
	}
	witness_tsd = witness_tsdn_tsd(witness_tsdn);
	if (witness->rank == WITNESS_RANK_OMIT) {
		return;
	}

	/*
	 * Check whether owner before removal, rather than relying on
	 * witness_assert_owner() to abort, so that unit tests can test this
	 * function's failure mode without causing undefined behavior.
	 */
	if (witness_owner(witness_tsd, witness)) {
		witnesses = &witness_tsd->witnesses;
		ql_remove(witnesses, witness, link);
	} else {
		witness_assert_owner(witness_tsdn, witness);
	}
}

#endif /* JEMALLOC_INTERNAL_WITNESS_H */
