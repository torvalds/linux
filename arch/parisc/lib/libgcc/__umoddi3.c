#include "libgcc.h"

u64 __umoddi3(u64 num, u64 den)
{
	u64 v;

	(void)__udivmoddi4(num, den, &v);
	return v;
}
EXPORT_SYMBOL(__umoddi3);
