#include "libgcc.h"

u32 __udivmodsi4(u32 num, u32 den, u32 * rem_p)
{
	u32 quot = 0, qbit = 1;

	if (den == 0) {
		BUG();
	}

	/* Left-justify denominator and count shift */
	while ((s32) den >= 0) {
		den <<= 1;
		qbit <<= 1;
	}

	while (qbit) {
		if (den <= num) {
			num -= den;
			quot += qbit;
		}
		den >>= 1;
		qbit >>= 1;
	}

	if (rem_p)
		*rem_p = num;

	return quot;
}
EXPORT_SYMBOL(__udivmodsi4);
