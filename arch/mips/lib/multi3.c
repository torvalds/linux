// SPDX-License-Identifier: GPL-2.0
#include <linux/export.h>

#include "libgcc.h"

/*
 * GCC 7 & older can suboptimally generate __multi3 calls for mips64r6, so for
 * that specific case only we implement that intrinsic here.
 *
 * See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=82981
 */
#if defined(CONFIG_64BIT) && defined(CONFIG_CPU_MIPSR6) && (__GNUC__ < 8)

/* multiply 64-bit values, low 64-bits returned */
static inline long long notrace dmulu(long long a, long long b)
{
	long long res;

	asm ("dmulu %0,%1,%2" : "=r" (res) : "r" (a), "r" (b));
	return res;
}

/* multiply 64-bit unsigned values, high 64-bits of 128-bit result returned */
static inline long long notrace dmuhu(long long a, long long b)
{
	long long res;

	asm ("dmuhu %0,%1,%2" : "=r" (res) : "r" (a), "r" (b));
	return res;
}

/* multiply 128-bit values, low 128-bits returned */
ti_type notrace __multi3(ti_type a, ti_type b)
{
	TWunion res, aa, bb;

	aa.ti = a;
	bb.ti = b;

	/*
	 * a * b =           (a.lo * b.lo)
	 *         + 2^64  * (a.hi * b.lo + a.lo * b.hi)
	 *        [+ 2^128 * (a.hi * b.hi)]
	 */
	res.s.low = dmulu(aa.s.low, bb.s.low);
	res.s.high = dmuhu(aa.s.low, bb.s.low);
	res.s.high += dmulu(aa.s.high, bb.s.low);
	res.s.high += dmulu(aa.s.low, bb.s.high);

	return res.ti;
}
EXPORT_SYMBOL(__multi3);

#endif /* 64BIT && CPU_MIPSR6 && GCC7 */
