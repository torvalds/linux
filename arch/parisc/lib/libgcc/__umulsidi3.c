#include "libgcc.h"

#define __ll_B ((u32) 1 << (32 / 2))
#define __ll_lowpart(t) ((u32) (t) & (__ll_B - 1))
#define __ll_highpart(t) ((u32) (t) >> 16)

#define umul_ppmm(w1, w0, u, v)						\
  do {									\
    u32 __x0, __x1, __x2, __x3;						\
    u16 __ul, __vl, __uh, __vh;						\
									\
    __ul = __ll_lowpart (u);						\
    __uh = __ll_highpart (u);						\
    __vl = __ll_lowpart (v);						\
    __vh = __ll_highpart (v);						\
									\
    __x0 = (u32) __ul * __vl;						\
    __x1 = (u32) __ul * __vh;						\
    __x2 = (u32) __uh * __vl;						\
    __x3 = (u32) __uh * __vh;						\
									\
    __x1 += __ll_highpart (__x0);/* this can't give carry */		\
    __x1 += __x2;		 /* but this indeed can */		\
    if (__x1 < __x2)		 /* did we get it? */			\
      __x3 += __ll_B;		 /* yes, add it in the proper pos.  */	\
									\
    (w1) = __x3 + __ll_highpart (__x1);					\
    (w0) = __ll_lowpart (__x1) * __ll_B + __ll_lowpart (__x0);		\
  } while (0)

union DWunion {
	struct {
		s32 high;
		s32 low;
	} s;
	s64 ll;
};

u64 __umulsidi3(u32 u, u32 v)
{
	union DWunion __w;

	umul_ppmm(__w.s.high, __w.s.low, u, v);

	return __w.ll;
}
