#include "libgcc.h"

u64 __udivmoddi4(u64 num, u64 den, u64 * rem_p)
{
	u64 quot = 0, qbit = 1;

	if (den == 0) {
		BUG();
	}

	/* Left-justify denominator and count shift */
	while ((s64) den >= 0) {
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
EXPORT_SYMBOL(__udivmoddi4);
