#include "libgcc.h"

u64 __lshrdi3(u64 v, int cnt)
{
	int c = cnt & 31;
	u32 vl = (u32) v;
	u32 vh = (u32) (v >> 32);

	if (cnt & 32) {
		vl = (vh >> c);
		vh = 0;
	} else {
		vl = (vl >> c) + (vh << (32 - c));
		vh = (vh >> c);
	}

	return ((u64) vh << 32) + vl;
}
EXPORT_SYMBOL(__lshrdi3);
