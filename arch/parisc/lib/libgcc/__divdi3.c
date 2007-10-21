#include "libgcc.h"

s64 __divdi3(s64 num, s64 den)
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

	v = __udivmoddi4(num, den, NULL);
	if (minus)
		v = -v;

	return v;
}
EXPORT_SYMBOL(__divdi3);
