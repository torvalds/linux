/* This file is distributed under the University of Illinois Open Source
 * License. See LICENSE.TXT for details.
 */

/* long double __floatdixf(di_int a); */

#ifdef __x86_64__

#include "../int_lib.h"

long double __floatdixf(int64_t a)
{
	return (long double)a;
}

#endif /* __i386__ */
