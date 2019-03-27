#ifndef JEMALLOC_INTERNAL_SIZE_H
#define JEMALLOC_INTERNAL_SIZE_H

#include "jemalloc/internal/bit_util.h"
#include "jemalloc/internal/pages.h"
#include "jemalloc/internal/size_classes.h"
#include "jemalloc/internal/util.h"

/*
 * sz module: Size computations.
 *
 * Some abbreviations used here:
 *   p: Page
 *   ind: Index
 *   s, sz: Size
 *   u: Usable size
 *   a: Aligned
 *
 * These are not always used completely consistently, but should be enough to
 * interpret function names.  E.g. sz_psz2ind converts page size to page size
 * index; sz_sa2u converts a (size, alignment) allocation request to the usable
 * size that would result from such an allocation.
 */

/*
 * sz_pind2sz_tab encodes the same information as could be computed by
 * sz_pind2sz_compute().
 */
extern size_t const sz_pind2sz_tab[NPSIZES+1];
/*
 * sz_index2size_tab encodes the same information as could be computed (at
 * unacceptable cost in some code paths) by sz_index2size_compute().
 */
extern size_t const sz_index2size_tab[NSIZES];
/*
 * sz_size2index_tab is a compact lookup table that rounds request sizes up to
 * size classes.  In order to reduce cache footprint, the table is compressed,
 * and all accesses are via sz_size2index().
 */
extern uint8_t const sz_size2index_tab[];

static const size_t sz_large_pad =
#ifdef JEMALLOC_CACHE_OBLIVIOUS
    PAGE
#else
    0
#endif
    ;

JEMALLOC_ALWAYS_INLINE pszind_t
sz_psz2ind(size_t psz) {
	if (unlikely(psz > LARGE_MAXCLASS)) {
		return NPSIZES;
	}
	{
		pszind_t x = lg_floor((psz<<1)-1);
		pszind_t shift = (x < LG_SIZE_CLASS_GROUP + LG_PAGE) ? 0 : x -
		    (LG_SIZE_CLASS_GROUP + LG_PAGE);
		pszind_t grp = shift << LG_SIZE_CLASS_GROUP;

		pszind_t lg_delta = (x < LG_SIZE_CLASS_GROUP + LG_PAGE + 1) ?
		    LG_PAGE : x - LG_SIZE_CLASS_GROUP - 1;

		size_t delta_inverse_mask = ZU(-1) << lg_delta;
		pszind_t mod = ((((psz-1) & delta_inverse_mask) >> lg_delta)) &
		    ((ZU(1) << LG_SIZE_CLASS_GROUP) - 1);

		pszind_t ind = grp + mod;
		return ind;
	}
}

static inline size_t
sz_pind2sz_compute(pszind_t pind) {
	if (unlikely(pind == NPSIZES)) {
		return LARGE_MAXCLASS + PAGE;
	}
	{
		size_t grp = pind >> LG_SIZE_CLASS_GROUP;
		size_t mod = pind & ((ZU(1) << LG_SIZE_CLASS_GROUP) - 1);

		size_t grp_size_mask = ~((!!grp)-1);
		size_t grp_size = ((ZU(1) << (LG_PAGE +
		    (LG_SIZE_CLASS_GROUP-1))) << grp) & grp_size_mask;

		size_t shift = (grp == 0) ? 1 : grp;
		size_t lg_delta = shift + (LG_PAGE-1);
		size_t mod_size = (mod+1) << lg_delta;

		size_t sz = grp_size + mod_size;
		return sz;
	}
}

static inline size_t
sz_pind2sz_lookup(pszind_t pind) {
	size_t ret = (size_t)sz_pind2sz_tab[pind];
	assert(ret == sz_pind2sz_compute(pind));
	return ret;
}

static inline size_t
sz_pind2sz(pszind_t pind) {
	assert(pind < NPSIZES+1);
	return sz_pind2sz_lookup(pind);
}

static inline size_t
sz_psz2u(size_t psz) {
	if (unlikely(psz > LARGE_MAXCLASS)) {
		return LARGE_MAXCLASS + PAGE;
	}
	{
		size_t x = lg_floor((psz<<1)-1);
		size_t lg_delta = (x < LG_SIZE_CLASS_GROUP + LG_PAGE + 1) ?
		    LG_PAGE : x - LG_SIZE_CLASS_GROUP - 1;
		size_t delta = ZU(1) << lg_delta;
		size_t delta_mask = delta - 1;
		size_t usize = (psz + delta_mask) & ~delta_mask;
		return usize;
	}
}

static inline szind_t
sz_size2index_compute(size_t size) {
	if (unlikely(size > LARGE_MAXCLASS)) {
		return NSIZES;
	}
#if (NTBINS != 0)
	if (size <= (ZU(1) << LG_TINY_MAXCLASS)) {
		szind_t lg_tmin = LG_TINY_MAXCLASS - NTBINS + 1;
		szind_t lg_ceil = lg_floor(pow2_ceil_zu(size));
		return (lg_ceil < lg_tmin ? 0 : lg_ceil - lg_tmin);
	}
#endif
	{
		szind_t x = lg_floor((size<<1)-1);
		szind_t shift = (x < LG_SIZE_CLASS_GROUP + LG_QUANTUM) ? 0 :
		    x - (LG_SIZE_CLASS_GROUP + LG_QUANTUM);
		szind_t grp = shift << LG_SIZE_CLASS_GROUP;

		szind_t lg_delta = (x < LG_SIZE_CLASS_GROUP + LG_QUANTUM + 1)
		    ? LG_QUANTUM : x - LG_SIZE_CLASS_GROUP - 1;

		size_t delta_inverse_mask = ZU(-1) << lg_delta;
		szind_t mod = ((((size-1) & delta_inverse_mask) >> lg_delta)) &
		    ((ZU(1) << LG_SIZE_CLASS_GROUP) - 1);

		szind_t index = NTBINS + grp + mod;
		return index;
	}
}

JEMALLOC_ALWAYS_INLINE szind_t
sz_size2index_lookup(size_t size) {
	assert(size <= LOOKUP_MAXCLASS);
	{
		szind_t ret = (sz_size2index_tab[(size-1) >> LG_TINY_MIN]);
		assert(ret == sz_size2index_compute(size));
		return ret;
	}
}

JEMALLOC_ALWAYS_INLINE szind_t
sz_size2index(size_t size) {
	assert(size > 0);
	if (likely(size <= LOOKUP_MAXCLASS)) {
		return sz_size2index_lookup(size);
	}
	return sz_size2index_compute(size);
}

static inline size_t
sz_index2size_compute(szind_t index) {
#if (NTBINS > 0)
	if (index < NTBINS) {
		return (ZU(1) << (LG_TINY_MAXCLASS - NTBINS + 1 + index));
	}
#endif
	{
		size_t reduced_index = index - NTBINS;
		size_t grp = reduced_index >> LG_SIZE_CLASS_GROUP;
		size_t mod = reduced_index & ((ZU(1) << LG_SIZE_CLASS_GROUP) -
		    1);

		size_t grp_size_mask = ~((!!grp)-1);
		size_t grp_size = ((ZU(1) << (LG_QUANTUM +
		    (LG_SIZE_CLASS_GROUP-1))) << grp) & grp_size_mask;

		size_t shift = (grp == 0) ? 1 : grp;
		size_t lg_delta = shift + (LG_QUANTUM-1);
		size_t mod_size = (mod+1) << lg_delta;

		size_t usize = grp_size + mod_size;
		return usize;
	}
}

JEMALLOC_ALWAYS_INLINE size_t
sz_index2size_lookup(szind_t index) {
	size_t ret = (size_t)sz_index2size_tab[index];
	assert(ret == sz_index2size_compute(index));
	return ret;
}

JEMALLOC_ALWAYS_INLINE size_t
sz_index2size(szind_t index) {
	assert(index < NSIZES);
	return sz_index2size_lookup(index);
}

JEMALLOC_ALWAYS_INLINE size_t
sz_s2u_compute(size_t size) {
	if (unlikely(size > LARGE_MAXCLASS)) {
		return 0;
	}
#if (NTBINS > 0)
	if (size <= (ZU(1) << LG_TINY_MAXCLASS)) {
		size_t lg_tmin = LG_TINY_MAXCLASS - NTBINS + 1;
		size_t lg_ceil = lg_floor(pow2_ceil_zu(size));
		return (lg_ceil < lg_tmin ? (ZU(1) << lg_tmin) :
		    (ZU(1) << lg_ceil));
	}
#endif
	{
		size_t x = lg_floor((size<<1)-1);
		size_t lg_delta = (x < LG_SIZE_CLASS_GROUP + LG_QUANTUM + 1)
		    ?  LG_QUANTUM : x - LG_SIZE_CLASS_GROUP - 1;
		size_t delta = ZU(1) << lg_delta;
		size_t delta_mask = delta - 1;
		size_t usize = (size + delta_mask) & ~delta_mask;
		return usize;
	}
}

JEMALLOC_ALWAYS_INLINE size_t
sz_s2u_lookup(size_t size) {
	size_t ret = sz_index2size_lookup(sz_size2index_lookup(size));

	assert(ret == sz_s2u_compute(size));
	return ret;
}

/*
 * Compute usable size that would result from allocating an object with the
 * specified size.
 */
JEMALLOC_ALWAYS_INLINE size_t
sz_s2u(size_t size) {
	assert(size > 0);
	if (likely(size <= LOOKUP_MAXCLASS)) {
		return sz_s2u_lookup(size);
	}
	return sz_s2u_compute(size);
}

/*
 * Compute usable size that would result from allocating an object with the
 * specified size and alignment.
 */
JEMALLOC_ALWAYS_INLINE size_t
sz_sa2u(size_t size, size_t alignment) {
	size_t usize;

	assert(alignment != 0 && ((alignment - 1) & alignment) == 0);

	/* Try for a small size class. */
	if (size <= SMALL_MAXCLASS && alignment < PAGE) {
		/*
		 * Round size up to the nearest multiple of alignment.
		 *
		 * This done, we can take advantage of the fact that for each
		 * small size class, every object is aligned at the smallest
		 * power of two that is non-zero in the base two representation
		 * of the size.  For example:
		 *
		 *   Size |   Base 2 | Minimum alignment
		 *   -----+----------+------------------
		 *     96 |  1100000 |  32
		 *    144 | 10100000 |  32
		 *    192 | 11000000 |  64
		 */
		usize = sz_s2u(ALIGNMENT_CEILING(size, alignment));
		if (usize < LARGE_MINCLASS) {
			return usize;
		}
	}

	/* Large size class.  Beware of overflow. */

	if (unlikely(alignment > LARGE_MAXCLASS)) {
		return 0;
	}

	/* Make sure result is a large size class. */
	if (size <= LARGE_MINCLASS) {
		usize = LARGE_MINCLASS;
	} else {
		usize = sz_s2u(size);
		if (usize < size) {
			/* size_t overflow. */
			return 0;
		}
	}

	/*
	 * Calculate the multi-page mapping that large_palloc() would need in
	 * order to guarantee the alignment.
	 */
	if (usize + sz_large_pad + PAGE_CEILING(alignment) - PAGE < usize) {
		/* size_t overflow. */
		return 0;
	}
	return usize;
}

#endif /* JEMALLOC_INTERNAL_SIZE_H */
