/****************************************************************

The author of this software is David M. Gay.

Copyright (C) 2005 by David M. Gay
All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that the copyright notice and this permission notice and warranty
disclaimer appear in supporting documentation, and that the name of
the author or any of his current or former employers not be used in
advertising or publicity pertaining to distribution of the software
without specific, written prior permission.

THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.  IN
NO EVENT SHALL THE AUTHOR OR ANY OF HIS CURRENT OR FORMER EMPLOYERS BE
LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY
DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

****************************************************************/

/* Please send bug reports to David M. Gay (dmg at acm dot org,
 * with " at " changed at "@" and " dot " changed to ".").	*/

/* Program to compute quiet NaNs of various precisions (float,	*/
/* double, and perhaps long double) on the current system,	*/
/* provided the system uses binary IEEE (P754) arithmetic.	*/
/* Note that one system's quiet NaN may be a signaling NaN on	*/
/* another system.  The IEEE arithmetic standards (P754, P854)	*/
/* do not specify how to distinguish signaling NaNs from quiet	*/
/* ones, and this detail varies across systems.	 The computed	*/
/* NaN values are encoded in #defines for values for an		*/
/* unsigned 32-bit integer type, called Ulong below, and	*/
/* (for long double) perhaps as unsigned short values.  Once	*/
/* upon a time, there were PC compilers for Intel CPUs that	*/
/* had sizeof(long double) = 10.  Are such compilers still	*/
/* distributed?							*/

#include <stdio.h>
#include "arith.h"

#ifndef Long
#define Long long
#endif

typedef unsigned Long Ulong;

#undef HAVE_IEEE
#ifdef IEEE_8087
#define _0 1
#define _1 0
#define HAVE_IEEE
#endif
#ifdef IEEE_MC68k
#define _0 0
#define _1 1
#define HAVE_IEEE
#endif

#define UL (unsigned long)

 int
main(void)
{
#ifdef HAVE_IEEE
	typedef union {
		float f;
		double d;
		Ulong L[4];
#ifndef NO_LONG_LONG
		unsigned short u[5];
		long double D;
#endif
		} U;
	U a, b, c;
	int i;

	a.L[0] = b.L[0] = 0x7f800000;
	c.f = a.f - b.f;
	printf("#define f_QNAN 0x%lx\n", UL c.L[0]);
	a.L[_0] = b.L[_0] = 0x7ff00000;
	a.L[_1] = b.L[_1] = 0;
	c.d = a.d - b.d;	/* quiet NaN */
	printf("#define d_QNAN0 0x%lx\n", UL c.L[0]);
	printf("#define d_QNAN1 0x%lx\n", UL c.L[1]);
#ifdef NO_LONG_LONG
	for(i = 0; i < 4; i++)
		printf("#define ld_QNAN%d 0xffffffff\n", i);
	for(i = 0; i < 5; i++)
		printf("#define ldus_QNAN%d 0xffff\n", i);
#else
	b.D = c.D = a.d;
	if (printf("") < 0)
		c.D = 37;	/* never executed; just defeat optimization */
	a.L[2] = a.L[3] = 0;
	a.D = b.D - c.D;
	for(i = 0; i < 4; i++)
		printf("#define ld_QNAN%d 0x%lx\n", i, UL a.L[i]);
	for(i = 0; i < 5; i++)
		printf("#define ldus_QNAN%d 0x%x\n", i, a.u[i]);
#endif
#endif /* HAVE_IEEE */
	return 0;
	}
