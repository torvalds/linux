/*
 * Generic __div64_32 wrapper for __xdiv64_32.
 */

#include <linux/types.h>

extern u64 __xdiv64_32(u64 n, u32 d);

u64 __div64_32(u64 *xp, u32 y)
{
	u64 rem;
	u64 q = __xdiv64_32(*xp, y);

	rem = *xp - q * y;
	*xp = q;

	return rem;
}

