#ifndef JEMALLOC_INTERNAL_BITMAP_H
#define JEMALLOC_INTERNAL_BITMAP_H

#include "jemalloc/internal/arena_types.h"
#include "jemalloc/internal/bit_util.h"
#include "jemalloc/internal/size_classes.h"

typedef unsigned long bitmap_t;
#define LG_SIZEOF_BITMAP	LG_SIZEOF_LONG

/* Maximum bitmap bit count is 2^LG_BITMAP_MAXBITS. */
#if LG_SLAB_MAXREGS > LG_CEIL_NSIZES
/* Maximum bitmap bit count is determined by maximum regions per slab. */
#  define LG_BITMAP_MAXBITS	LG_SLAB_MAXREGS
#else
/* Maximum bitmap bit count is determined by number of extent size classes. */
#  define LG_BITMAP_MAXBITS	LG_CEIL_NSIZES
#endif
#define BITMAP_MAXBITS		(ZU(1) << LG_BITMAP_MAXBITS)

/* Number of bits per group. */
#define LG_BITMAP_GROUP_NBITS		(LG_SIZEOF_BITMAP + 3)
#define BITMAP_GROUP_NBITS		(1U << LG_BITMAP_GROUP_NBITS)
#define BITMAP_GROUP_NBITS_MASK		(BITMAP_GROUP_NBITS-1)

/*
 * Do some analysis on how big the bitmap is before we use a tree.  For a brute
 * force linear search, if we would have to call ffs_lu() more than 2^3 times,
 * use a tree instead.
 */
#if LG_BITMAP_MAXBITS - LG_BITMAP_GROUP_NBITS > 3
#  define BITMAP_USE_TREE
#endif

/* Number of groups required to store a given number of bits. */
#define BITMAP_BITS2GROUPS(nbits)					\
    (((nbits) + BITMAP_GROUP_NBITS_MASK) >> LG_BITMAP_GROUP_NBITS)

/*
 * Number of groups required at a particular level for a given number of bits.
 */
#define BITMAP_GROUPS_L0(nbits)						\
    BITMAP_BITS2GROUPS(nbits)
#define BITMAP_GROUPS_L1(nbits)						\
    BITMAP_BITS2GROUPS(BITMAP_BITS2GROUPS(nbits))
#define BITMAP_GROUPS_L2(nbits)						\
    BITMAP_BITS2GROUPS(BITMAP_BITS2GROUPS(BITMAP_BITS2GROUPS((nbits))))
#define BITMAP_GROUPS_L3(nbits)						\
    BITMAP_BITS2GROUPS(BITMAP_BITS2GROUPS(BITMAP_BITS2GROUPS(		\
	BITMAP_BITS2GROUPS((nbits)))))
#define BITMAP_GROUPS_L4(nbits)						\
    BITMAP_BITS2GROUPS(BITMAP_BITS2GROUPS(BITMAP_BITS2GROUPS(		\
	BITMAP_BITS2GROUPS(BITMAP_BITS2GROUPS((nbits))))))

/*
 * Assuming the number of levels, number of groups required for a given number
 * of bits.
 */
#define BITMAP_GROUPS_1_LEVEL(nbits)					\
    BITMAP_GROUPS_L0(nbits)
#define BITMAP_GROUPS_2_LEVEL(nbits)					\
    (BITMAP_GROUPS_1_LEVEL(nbits) + BITMAP_GROUPS_L1(nbits))
#define BITMAP_GROUPS_3_LEVEL(nbits)					\
    (BITMAP_GROUPS_2_LEVEL(nbits) + BITMAP_GROUPS_L2(nbits))
#define BITMAP_GROUPS_4_LEVEL(nbits)					\
    (BITMAP_GROUPS_3_LEVEL(nbits) + BITMAP_GROUPS_L3(nbits))
#define BITMAP_GROUPS_5_LEVEL(nbits)					\
    (BITMAP_GROUPS_4_LEVEL(nbits) + BITMAP_GROUPS_L4(nbits))

/*
 * Maximum number of groups required to support LG_BITMAP_MAXBITS.
 */
#ifdef BITMAP_USE_TREE

#if LG_BITMAP_MAXBITS <= LG_BITMAP_GROUP_NBITS
#  define BITMAP_GROUPS(nbits)	BITMAP_GROUPS_1_LEVEL(nbits)
#  define BITMAP_GROUPS_MAX	BITMAP_GROUPS_1_LEVEL(BITMAP_MAXBITS)
#elif LG_BITMAP_MAXBITS <= LG_BITMAP_GROUP_NBITS * 2
#  define BITMAP_GROUPS(nbits)	BITMAP_GROUPS_2_LEVEL(nbits)
#  define BITMAP_GROUPS_MAX	BITMAP_GROUPS_2_LEVEL(BITMAP_MAXBITS)
#elif LG_BITMAP_MAXBITS <= LG_BITMAP_GROUP_NBITS * 3
#  define BITMAP_GROUPS(nbits)	BITMAP_GROUPS_3_LEVEL(nbits)
#  define BITMAP_GROUPS_MAX	BITMAP_GROUPS_3_LEVEL(BITMAP_MAXBITS)
#elif LG_BITMAP_MAXBITS <= LG_BITMAP_GROUP_NBITS * 4
#  define BITMAP_GROUPS(nbits)	BITMAP_GROUPS_4_LEVEL(nbits)
#  define BITMAP_GROUPS_MAX	BITMAP_GROUPS_4_LEVEL(BITMAP_MAXBITS)
#elif LG_BITMAP_MAXBITS <= LG_BITMAP_GROUP_NBITS * 5
#  define BITMAP_GROUPS(nbits)	BITMAP_GROUPS_5_LEVEL(nbits)
#  define BITMAP_GROUPS_MAX	BITMAP_GROUPS_5_LEVEL(BITMAP_MAXBITS)
#else
#  error "Unsupported bitmap size"
#endif

/*
 * Maximum number of levels possible.  This could be statically computed based
 * on LG_BITMAP_MAXBITS:
 *
 * #define BITMAP_MAX_LEVELS \
 *     (LG_BITMAP_MAXBITS / LG_SIZEOF_BITMAP) \
 *     + !!(LG_BITMAP_MAXBITS % LG_SIZEOF_BITMAP)
 *
 * However, that would not allow the generic BITMAP_INFO_INITIALIZER() macro, so
 * instead hardcode BITMAP_MAX_LEVELS to the largest number supported by the
 * various cascading macros.  The only additional cost this incurs is some
 * unused trailing entries in bitmap_info_t structures; the bitmaps themselves
 * are not impacted.
 */
#define BITMAP_MAX_LEVELS	5

#define BITMAP_INFO_INITIALIZER(nbits) {				\
	/* nbits. */							\
	nbits,								\
	/* nlevels. */							\
	(BITMAP_GROUPS_L0(nbits) > BITMAP_GROUPS_L1(nbits)) +		\
	    (BITMAP_GROUPS_L1(nbits) > BITMAP_GROUPS_L2(nbits)) +	\
	    (BITMAP_GROUPS_L2(nbits) > BITMAP_GROUPS_L3(nbits)) +	\
	    (BITMAP_GROUPS_L3(nbits) > BITMAP_GROUPS_L4(nbits)) + 1,	\
	/* levels. */							\
	{								\
		{0},							\
		{BITMAP_GROUPS_L0(nbits)},				\
		{BITMAP_GROUPS_L1(nbits) + BITMAP_GROUPS_L0(nbits)},	\
		{BITMAP_GROUPS_L2(nbits) + BITMAP_GROUPS_L1(nbits) +	\
		    BITMAP_GROUPS_L0(nbits)},				\
		{BITMAP_GROUPS_L3(nbits) + BITMAP_GROUPS_L2(nbits) +	\
		    BITMAP_GROUPS_L1(nbits) + BITMAP_GROUPS_L0(nbits)},	\
		{BITMAP_GROUPS_L4(nbits) + BITMAP_GROUPS_L3(nbits) +	\
		     BITMAP_GROUPS_L2(nbits) + BITMAP_GROUPS_L1(nbits)	\
		     + BITMAP_GROUPS_L0(nbits)}				\
	}								\
}

#else /* BITMAP_USE_TREE */

#define BITMAP_GROUPS(nbits)	BITMAP_BITS2GROUPS(nbits)
#define BITMAP_GROUPS_MAX	BITMAP_BITS2GROUPS(BITMAP_MAXBITS)

#define BITMAP_INFO_INITIALIZER(nbits) {				\
	/* nbits. */							\
	nbits,								\
	/* ngroups. */							\
	BITMAP_BITS2GROUPS(nbits)					\
}

#endif /* BITMAP_USE_TREE */

typedef struct bitmap_level_s {
	/* Offset of this level's groups within the array of groups. */
	size_t group_offset;
} bitmap_level_t;

typedef struct bitmap_info_s {
	/* Logical number of bits in bitmap (stored at bottom level). */
	size_t nbits;

#ifdef BITMAP_USE_TREE
	/* Number of levels necessary for nbits. */
	unsigned nlevels;

	/*
	 * Only the first (nlevels+1) elements are used, and levels are ordered
	 * bottom to top (e.g. the bottom level is stored in levels[0]).
	 */
	bitmap_level_t levels[BITMAP_MAX_LEVELS+1];
#else /* BITMAP_USE_TREE */
	/* Number of groups necessary for nbits. */
	size_t ngroups;
#endif /* BITMAP_USE_TREE */
} bitmap_info_t;

void bitmap_info_init(bitmap_info_t *binfo, size_t nbits);
void bitmap_init(bitmap_t *bitmap, const bitmap_info_t *binfo, bool fill);
size_t bitmap_size(const bitmap_info_t *binfo);

static inline bool
bitmap_full(bitmap_t *bitmap, const bitmap_info_t *binfo) {
#ifdef BITMAP_USE_TREE
	size_t rgoff = binfo->levels[binfo->nlevels].group_offset - 1;
	bitmap_t rg = bitmap[rgoff];
	/* The bitmap is full iff the root group is 0. */
	return (rg == 0);
#else
	size_t i;

	for (i = 0; i < binfo->ngroups; i++) {
		if (bitmap[i] != 0) {
			return false;
		}
	}
	return true;
#endif
}

static inline bool
bitmap_get(bitmap_t *bitmap, const bitmap_info_t *binfo, size_t bit) {
	size_t goff;
	bitmap_t g;

	assert(bit < binfo->nbits);
	goff = bit >> LG_BITMAP_GROUP_NBITS;
	g = bitmap[goff];
	return !(g & (ZU(1) << (bit & BITMAP_GROUP_NBITS_MASK)));
}

static inline void
bitmap_set(bitmap_t *bitmap, const bitmap_info_t *binfo, size_t bit) {
	size_t goff;
	bitmap_t *gp;
	bitmap_t g;

	assert(bit < binfo->nbits);
	assert(!bitmap_get(bitmap, binfo, bit));
	goff = bit >> LG_BITMAP_GROUP_NBITS;
	gp = &bitmap[goff];
	g = *gp;
	assert(g & (ZU(1) << (bit & BITMAP_GROUP_NBITS_MASK)));
	g ^= ZU(1) << (bit & BITMAP_GROUP_NBITS_MASK);
	*gp = g;
	assert(bitmap_get(bitmap, binfo, bit));
#ifdef BITMAP_USE_TREE
	/* Propagate group state transitions up the tree. */
	if (g == 0) {
		unsigned i;
		for (i = 1; i < binfo->nlevels; i++) {
			bit = goff;
			goff = bit >> LG_BITMAP_GROUP_NBITS;
			gp = &bitmap[binfo->levels[i].group_offset + goff];
			g = *gp;
			assert(g & (ZU(1) << (bit & BITMAP_GROUP_NBITS_MASK)));
			g ^= ZU(1) << (bit & BITMAP_GROUP_NBITS_MASK);
			*gp = g;
			if (g != 0) {
				break;
			}
		}
	}
#endif
}

/* ffu: find first unset >= bit. */
static inline size_t
bitmap_ffu(const bitmap_t *bitmap, const bitmap_info_t *binfo, size_t min_bit) {
	assert(min_bit < binfo->nbits);

#ifdef BITMAP_USE_TREE
	size_t bit = 0;
	for (unsigned level = binfo->nlevels; level--;) {
		size_t lg_bits_per_group = (LG_BITMAP_GROUP_NBITS * (level +
		    1));
		bitmap_t group = bitmap[binfo->levels[level].group_offset + (bit
		    >> lg_bits_per_group)];
		unsigned group_nmask = (unsigned)(((min_bit > bit) ? (min_bit -
		    bit) : 0) >> (lg_bits_per_group - LG_BITMAP_GROUP_NBITS));
		assert(group_nmask <= BITMAP_GROUP_NBITS);
		bitmap_t group_mask = ~((1LU << group_nmask) - 1);
		bitmap_t group_masked = group & group_mask;
		if (group_masked == 0LU) {
			if (group == 0LU) {
				return binfo->nbits;
			}
			/*
			 * min_bit was preceded by one or more unset bits in
			 * this group, but there are no other unset bits in this
			 * group.  Try again starting at the first bit of the
			 * next sibling.  This will recurse at most once per
			 * non-root level.
			 */
			size_t sib_base = bit + (ZU(1) << lg_bits_per_group);
			assert(sib_base > min_bit);
			assert(sib_base > bit);
			if (sib_base >= binfo->nbits) {
				return binfo->nbits;
			}
			return bitmap_ffu(bitmap, binfo, sib_base);
		}
		bit += ((size_t)(ffs_lu(group_masked) - 1)) <<
		    (lg_bits_per_group - LG_BITMAP_GROUP_NBITS);
	}
	assert(bit >= min_bit);
	assert(bit < binfo->nbits);
	return bit;
#else
	size_t i = min_bit >> LG_BITMAP_GROUP_NBITS;
	bitmap_t g = bitmap[i] & ~((1LU << (min_bit & BITMAP_GROUP_NBITS_MASK))
	    - 1);
	size_t bit;
	do {
		bit = ffs_lu(g);
		if (bit != 0) {
			return (i << LG_BITMAP_GROUP_NBITS) + (bit - 1);
		}
		i++;
		g = bitmap[i];
	} while (i < binfo->ngroups);
	return binfo->nbits;
#endif
}

/* sfu: set first unset. */
static inline size_t
bitmap_sfu(bitmap_t *bitmap, const bitmap_info_t *binfo) {
	size_t bit;
	bitmap_t g;
	unsigned i;

	assert(!bitmap_full(bitmap, binfo));

#ifdef BITMAP_USE_TREE
	i = binfo->nlevels - 1;
	g = bitmap[binfo->levels[i].group_offset];
	bit = ffs_lu(g) - 1;
	while (i > 0) {
		i--;
		g = bitmap[binfo->levels[i].group_offset + bit];
		bit = (bit << LG_BITMAP_GROUP_NBITS) + (ffs_lu(g) - 1);
	}
#else
	i = 0;
	g = bitmap[0];
	while ((bit = ffs_lu(g)) == 0) {
		i++;
		g = bitmap[i];
	}
	bit = (i << LG_BITMAP_GROUP_NBITS) + (bit - 1);
#endif
	bitmap_set(bitmap, binfo, bit);
	return bit;
}

static inline void
bitmap_unset(bitmap_t *bitmap, const bitmap_info_t *binfo, size_t bit) {
	size_t goff;
	bitmap_t *gp;
	bitmap_t g;
	UNUSED bool propagate;

	assert(bit < binfo->nbits);
	assert(bitmap_get(bitmap, binfo, bit));
	goff = bit >> LG_BITMAP_GROUP_NBITS;
	gp = &bitmap[goff];
	g = *gp;
	propagate = (g == 0);
	assert((g & (ZU(1) << (bit & BITMAP_GROUP_NBITS_MASK))) == 0);
	g ^= ZU(1) << (bit & BITMAP_GROUP_NBITS_MASK);
	*gp = g;
	assert(!bitmap_get(bitmap, binfo, bit));
#ifdef BITMAP_USE_TREE
	/* Propagate group state transitions up the tree. */
	if (propagate) {
		unsigned i;
		for (i = 1; i < binfo->nlevels; i++) {
			bit = goff;
			goff = bit >> LG_BITMAP_GROUP_NBITS;
			gp = &bitmap[binfo->levels[i].group_offset + goff];
			g = *gp;
			propagate = (g == 0);
			assert((g & (ZU(1) << (bit & BITMAP_GROUP_NBITS_MASK)))
			    == 0);
			g ^= ZU(1) << (bit & BITMAP_GROUP_NBITS_MASK);
			*gp = g;
			if (!propagate) {
				break;
			}
		}
	}
#endif /* BITMAP_USE_TREE */
}

#endif /* JEMALLOC_INTERNAL_BITMAP_H */
