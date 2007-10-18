#include "libgcc.h"

union DWunion {
	struct {
		s32 high;
		s32 low;
	} s;
	s64 ll;
};

s64 __muldi3(s64 u, s64 v)
{
	const union DWunion uu = { .ll = u };
	const union DWunion vv = { .ll = v };
	union DWunion w = { .ll = __umulsidi3(uu.s.low, vv.s.low) };

	w.s.high += ((u32)uu.s.low * (u32)vv.s.high
		+ (u32)uu.s.high * (u32)vv.s.low);

	return w.ll;
}
EXPORT_SYMBOL(__muldi3);
