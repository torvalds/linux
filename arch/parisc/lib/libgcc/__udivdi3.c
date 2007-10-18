#include "libgcc.h"

u64 __udivdi3(u64 num, u64 den)
{
	return __udivmoddi4(num, den, NULL);
}
EXPORT_SYMBOL(__udivdi3);
