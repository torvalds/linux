#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/sz.h"

JEMALLOC_ALIGNED(CACHELINE)
const size_t sz_pind2sz_tab[NPSIZES+1] = {
#define PSZ_yes(lg_grp, ndelta, lg_delta)				\
	(((ZU(1)<<lg_grp) + (ZU(ndelta)<<lg_delta))),
#define PSZ_no(lg_grp, ndelta, lg_delta)
#define SC(index, lg_grp, lg_delta, ndelta, psz, bin, pgs, lg_delta_lookup) \
	PSZ_##psz(lg_grp, ndelta, lg_delta)
	SIZE_CLASSES
#undef PSZ_yes
#undef PSZ_no
#undef SC
	(LARGE_MAXCLASS + PAGE)
};

JEMALLOC_ALIGNED(CACHELINE)
const size_t sz_index2size_tab[NSIZES] = {
#define SC(index, lg_grp, lg_delta, ndelta, psz, bin, pgs, lg_delta_lookup) \
	((ZU(1)<<lg_grp) + (ZU(ndelta)<<lg_delta)),
	SIZE_CLASSES
#undef SC
};

JEMALLOC_ALIGNED(CACHELINE)
const uint8_t sz_size2index_tab[] = {
#if LG_TINY_MIN == 0
/* The div module doesn't support division by 1. */
#error "Unsupported LG_TINY_MIN"
#define S2B_0(i)	i,
#elif LG_TINY_MIN == 1
#warning "Dangerous LG_TINY_MIN"
#define S2B_1(i)	i,
#elif LG_TINY_MIN == 2
#warning "Dangerous LG_TINY_MIN"
#define S2B_2(i)	i,
#elif LG_TINY_MIN == 3
#define S2B_3(i)	i,
#elif LG_TINY_MIN == 4
#define S2B_4(i)	i,
#elif LG_TINY_MIN == 5
#define S2B_5(i)	i,
#elif LG_TINY_MIN == 6
#define S2B_6(i)	i,
#elif LG_TINY_MIN == 7
#define S2B_7(i)	i,
#elif LG_TINY_MIN == 8
#define S2B_8(i)	i,
#elif LG_TINY_MIN == 9
#define S2B_9(i)	i,
#elif LG_TINY_MIN == 10
#define S2B_10(i)	i,
#elif LG_TINY_MIN == 11
#define S2B_11(i)	i,
#else
#error "Unsupported LG_TINY_MIN"
#endif
#if LG_TINY_MIN < 1
#define S2B_1(i)	S2B_0(i) S2B_0(i)
#endif
#if LG_TINY_MIN < 2
#define S2B_2(i)	S2B_1(i) S2B_1(i)
#endif
#if LG_TINY_MIN < 3
#define S2B_3(i)	S2B_2(i) S2B_2(i)
#endif
#if LG_TINY_MIN < 4
#define S2B_4(i)	S2B_3(i) S2B_3(i)
#endif
#if LG_TINY_MIN < 5
#define S2B_5(i)	S2B_4(i) S2B_4(i)
#endif
#if LG_TINY_MIN < 6
#define S2B_6(i)	S2B_5(i) S2B_5(i)
#endif
#if LG_TINY_MIN < 7
#define S2B_7(i)	S2B_6(i) S2B_6(i)
#endif
#if LG_TINY_MIN < 8
#define S2B_8(i)	S2B_7(i) S2B_7(i)
#endif
#if LG_TINY_MIN < 9
#define S2B_9(i)	S2B_8(i) S2B_8(i)
#endif
#if LG_TINY_MIN < 10
#define S2B_10(i)	S2B_9(i) S2B_9(i)
#endif
#if LG_TINY_MIN < 11
#define S2B_11(i)	S2B_10(i) S2B_10(i)
#endif
#define S2B_no(i)
#define SC(index, lg_grp, lg_delta, ndelta, psz, bin, pgs, lg_delta_lookup) \
	S2B_##lg_delta_lookup(index)
	SIZE_CLASSES
#undef S2B_3
#undef S2B_4
#undef S2B_5
#undef S2B_6
#undef S2B_7
#undef S2B_8
#undef S2B_9
#undef S2B_10
#undef S2B_11
#undef S2B_no
#undef SC
};
