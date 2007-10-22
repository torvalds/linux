#include "libgcc.h"

s64 __moddi3(s64 num, s64 den)
{
	int minus = 0;
	s64 v;

	if (num < 0) {
		num = -num;
		minus = 1;
	}
	if (den < 0) {
		den = -den;
		minus ^= 1;
	}

	(void)__udivmoddi4(num, den, (u64 *) & v);
	if (minus)
		v = -v;

	return v;
}
EXPORT_SYMBOL(__moddi3);
