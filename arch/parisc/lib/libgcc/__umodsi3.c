#include "libgcc.h"

u32 __umodsi3(u32 num, u32 den)
{
	u32 v;

	(void)__udivmodsi4(num, den, &v);
	return v;
}
EXPORT_SYMBOL(__umodsi3);
