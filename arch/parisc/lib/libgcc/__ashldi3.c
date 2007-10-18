#include "libgcc.h"

u64 __ashldi3(u64 v, int cnt)
{
	int c = cnt & 31;
	u32 vl = (u32) v;
	u32 vh = (u32) (v >> 32);

	if (cnt & 32) {
		vh = (vl << c);
		vl = 0;
	} else {
		vh = (vh << c) + (vl >> (32 - c));
		vl = (vl << c);
	}

	return ((u64) vh << 32) + vl;
}
EXPORT_SYMBOL(__ashldi3);
