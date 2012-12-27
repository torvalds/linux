#include <linux/module.h>

#include "libgcc.h"

#define W_TYPE_SIZE 32

#define __ll_B ((unsigned long) 1 << (W_TYPE_SIZE / 2))
#define __ll_lowpart(t) ((unsigned long) (t) & (__ll_B - 1))
#define __ll_highpart(t) ((unsigned long) (t) >> (W_TYPE_SIZE / 2))

/* If we still don't have umul_ppmm, define it using plain C.  */
#if !defined(umul_ppmm)
#define umul_ppmm(w1, w0, u, v)						\
	do {								\
		unsigned long __x0, __x1, __x2, __x3;			\
		unsigned short __ul, __vl, __uh, __vh;			\
									\
		__ul = __ll_lowpart(u);					\
		__uh = __ll_highpart(u);				\
		__vl = __ll_lowpart(v);					\
		__vh = __ll_highpart(v);				\
									\
		__x0 = (unsigned long) __ul * __vl;			\
		__x1 = (unsigned long) __ul * __vh;			\
		__x2 = (unsigned long) __uh * __vl;			\
		__x3 = (unsigned long) __uh * __vh;			\
									\
		__x1 += __ll_highpart(__x0); /* this can't give carry */\
		__x1 += __x2; /* but this indeed can */			\
		if (__x1 < __x2) /* did we get it? */			\
		__x3 += __ll_B; /* yes, add it in the proper pos */	\
									\
		(w1) = __x3 + __ll_highpart(__x1);			\
		(w0) = __ll_lowpart(__x1) * __ll_B + __ll_lowpart(__x0);\
	} while (0)
#endif

#if !defined(__umulsidi3)
#define __umulsidi3(u, v) ({				\
	DWunion __w;					\
	umul_ppmm(__w.s.high, __w.s.low, u, v);		\
	__w.ll;						\
	})
#endif

long long __muldi3(long long u, long long v)
{
	const DWunion uu = {.ll = u};
	const DWunion vv = {.ll = v};
	DWunion w = {.ll = __umulsidi3(uu.s.low, vv.s.low)};

	w.s.high += ((unsigned long) uu.s.low * (unsigned long) vv.s.high
		+ (unsigned long) uu.s.high * (unsigned long) vv.s.low);

	return w.ll;
}
EXPORT_SYMBOL(__muldi3);
