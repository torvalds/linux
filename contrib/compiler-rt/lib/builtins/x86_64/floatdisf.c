/* This file is distributed under the University of Illinois Open Source
 * License. See LICENSE.TXT for details.
 */

#if defined(__x86_64__) || defined(_M_X64)

#include "../int_lib.h"

float __floatdisf(int64_t a)
{
	return (float)a;
}

#endif /* __x86_64__ */
