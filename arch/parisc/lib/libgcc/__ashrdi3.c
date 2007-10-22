#include "libgcc.h"

u64 __ashrdi3(u64 v, int cnt)
{
	int c = cnt & 31;
	u32 vl = (u32) v;
	u32 vh = (u32) (v >> 32);

	if (cnt & 32) {
		vl = ((s32) vh >> c);
		vh = (s32) vh >> 31;
	} else {
		vl = (vl >> c) + (vh << (32 - c));
		vh = ((s32) vh >> c);
	}

	return ((u64) vh << 32) + vl;
}
EXPORT_SYMBOL(__ashrdi3);
