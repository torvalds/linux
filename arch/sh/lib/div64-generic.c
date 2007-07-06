/*
 * Generic __div64_32 wrapper for __xdiv64_32.
 */

#include <linux/types.h>

extern uint32_t __xdiv64_32(u64 n, u32 d);

uint32_t __div64_32(u64 *xp, u32 y)
{
	uint32_t rem;
	uint32_t q = __xdiv64_32(*xp, y);

	rem = *xp - q * y;
	*xp = q;

	return rem;
}
