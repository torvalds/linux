#include "libgcc.h"

s32 __divsi3(s32 num, s32 den)
{
	int minus = 0;
	s32 v;

	if (num < 0) {
		num = -num;
		minus = 1;
	}
	if (den < 0) {
		den = -den;
		minus ^= 1;
	}

	v = __udivmodsi4(num, den, NULL);
	if (minus)
		v = -v;

	return v;
}
EXPORT_SYMBOL(__divsi3);
