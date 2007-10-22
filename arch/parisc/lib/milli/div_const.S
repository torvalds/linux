/* 32 and 64-bit millicode, original author Hewlett-Packard
   adapted for gcc by Paul Bame <bame@debian.org>
   and Alan Modra <alan@linuxcare.com.au>.

   Copyright 2001, 2002, 2003 Free Software Foundation, Inc.

   This file is part of GCC and is released under the terms of
   of the GNU General Public License as published by the Free Software
   Foundation; either version 2, or (at your option) any later version.
   See the file COPYING in the top-level GCC source directory for a copy
   of the license.  */

#include "milli.h"

#ifdef L_div_const
/* ROUTINE:	$$divI_2
   .		$$divI_3	$$divU_3
   .		$$divI_4
   .		$$divI_5	$$divU_5
   .		$$divI_6	$$divU_6
   .		$$divI_7	$$divU_7
   .		$$divI_8
   .		$$divI_9	$$divU_9
   .		$$divI_10	$$divU_10
   .
   .		$$divI_12	$$divU_12
   .
   .		$$divI_14	$$divU_14
   .		$$divI_15	$$divU_15
   .		$$divI_16
   .		$$divI_17	$$divU_17
   .
   .	Divide by selected constants for single precision binary integers.

   INPUT REGISTERS:
   .	arg0 ==	dividend
   .	mrp  == return pc
   .	sr0  == return space when called externally

   OUTPUT REGISTERS:
   .	arg0 =	undefined
   .	arg1 =	undefined
   .	ret1 =	quotient

   OTHER REGISTERS AFFECTED:
   .	r1   =	undefined

   SIDE EFFECTS:
   .	Causes a trap under the following conditions: NONE
   .	Changes memory at the following places:  NONE

   PERMISSIBLE CONTEXT:
   .	Unwindable.
   .	Does not create a stack frame.
   .	Suitable for internal or external millicode.
   .	Assumes the special millicode register conventions.

   DISCUSSION:
   .	Calls other millicode routines using mrp:  NONE
   .	Calls other millicode routines:  NONE  */


/* TRUNCATED DIVISION BY SMALL INTEGERS

   We are interested in q(x) = floor(x/y), where x >= 0 and y > 0
   (with y fixed).

   Let a = floor(z/y), for some choice of z.  Note that z will be
   chosen so that division by z is cheap.

   Let r be the remainder(z/y).  In other words, r = z - ay.

   Now, our method is to choose a value for b such that

   q'(x) = floor((ax+b)/z)

   is equal to q(x) over as large a range of x as possible.  If the
   two are equal over a sufficiently large range, and if it is easy to
   form the product (ax), and it is easy to divide by z, then we can
   perform the division much faster than the general division algorithm.

   So, we want the following to be true:

   .	For x in the following range:
   .
   .	    ky <= x < (k+1)y
   .
   .	implies that
   .
   .	    k <= (ax+b)/z < (k+1)

   We want to determine b such that this is true for all k in the
   range {0..K} for some maximum K.

   Since (ax+b) is an increasing function of x, we can take each
   bound separately to determine the "best" value for b.

   (ax+b)/z < (k+1)	       implies

   (a((k+1)y-1)+b < (k+1)z     implies

   b < a + (k+1)(z-ay)	       implies

   b < a + (k+1)r

   This needs to be true for all k in the range {0..K}.  In
   particular, it is true for k = 0 and this leads to a maximum
   acceptable value for b.

   b < a+r   or   b <= a+r-1

   Taking the other bound, we have

   k <= (ax+b)/z	       implies

   k <= (aky+b)/z	       implies

   k(z-ay) <= b		       implies

   kr <= b

   Clearly, the largest range for k will be achieved by maximizing b,
   when r is not zero.	When r is zero, then the simplest choice for b
   is 0.  When r is not 0, set

   .	b = a+r-1

   Now, by construction, q'(x) = floor((ax+b)/z) = q(x) = floor(x/y)
   for all x in the range:

   .	0 <= x < (K+1)y

   We need to determine what K is.  Of our two bounds,

   .	b < a+(k+1)r	is satisfied for all k >= 0, by construction.

   The other bound is

   .	kr <= b

   This is always true if r = 0.  If r is not 0 (the usual case), then
   K = floor((a+r-1)/r), is the maximum value for k.

   Therefore, the formula q'(x) = floor((ax+b)/z) yields the correct
   answer for q(x) = floor(x/y) when x is in the range

   (0,(K+1)y-1)	       K = floor((a+r-1)/r)

   To be most useful, we want (K+1)y-1 = (max x) >= 2**32-1 so that
   the formula for q'(x) yields the correct value of q(x) for all x
   representable by a single word in HPPA.

   We are also constrained in that computing the product (ax), adding
   b, and dividing by z must all be done quickly, otherwise we will be
   better off going through the general algorithm using the DS
   instruction, which uses approximately 70 cycles.

   For each y, there is a choice of z which satisfies the constraints
   for (K+1)y >= 2**32.  We may not, however, be able to satisfy the
   timing constraints for arbitrary y.	It seems that z being equal to
   a power of 2 or a power of 2 minus 1 is as good as we can do, since
   it minimizes the time to do division by z.  We want the choice of z
   to also result in a value for (a) that minimizes the computation of
   the product (ax).  This is best achieved if (a) has a regular bit
   pattern (so the multiplication can be done with shifts and adds).
   The value of (a) also needs to be less than 2**32 so the product is
   always guaranteed to fit in 2 words.

   In actual practice, the following should be done:

   1) For negative x, you should take the absolute value and remember
   .  the fact so that the result can be negated.  This obviously does
   .  not apply in the unsigned case.
   2) For even y, you should factor out the power of 2 that divides y
   .  and divide x by it.  You can then proceed by dividing by the
   .  odd factor of y.

   Here is a table of some odd values of y, and corresponding choices
   for z which are "good".

    y	  z	  r	 a (hex)     max x (hex)

    3	2**32	  1	55555555      100000001
    5	2**32	  1	33333333      100000003
    7  2**24-1	  0	  249249     (infinite)
    9  2**24-1	  0	  1c71c7     (infinite)
   11  2**20-1	  0	   1745d     (infinite)
   13  2**24-1	  0	  13b13b     (infinite)
   15	2**32	  1	11111111      10000000d
   17	2**32	  1	 f0f0f0f      10000000f

   If r is 1, then b = a+r-1 = a.  This simplifies the computation
   of (ax+b), since you can compute (x+1)(a) instead.  If r is 0,
   then b = 0 is ok to use which simplifies (ax+b).

   The bit patterns for 55555555, 33333333, and 11111111 are obviously
   very regular.  The bit patterns for the other values of a above are:

    y	   (hex)	  (binary)

    7	  249249  001001001001001001001001  << regular >>
    9	  1c71c7  000111000111000111000111  << regular >>
   11	   1745d  000000010111010001011101  << irregular >>
   13	  13b13b  000100111011000100111011  << irregular >>

   The bit patterns for (a) corresponding to (y) of 11 and 13 may be
   too irregular to warrant using this method.

   When z is a power of 2 minus 1, then the division by z is slightly
   more complicated, involving an iterative solution.

   The code presented here solves division by 1 through 17, except for
   11 and 13. There are algorithms for both signed and unsigned
   quantities given.

   TIMINGS (cycles)

   divisor  positive  negative	unsigned

   .   1	2	   2	     2
   .   2	4	   4	     2
   .   3       19	  21	    19
   .   4	4	   4	     2
   .   5       18	  22	    19
   .   6       19	  22	    19
   .   8	4	   4	     2
   .  10       18	  19	    17
   .  12       18	  20	    18
   .  15       16	  18	    16
   .  16	4	   4	     2
   .  17       16	  18	    16

   Now, the algorithm for 7, 9, and 14 is an iterative one.  That is,
   a loop body is executed until the tentative quotient is 0.  The
   number of times the loop body is executed varies depending on the
   dividend, but is never more than two times.	If the dividend is
   less than the divisor, then the loop body is not executed at all.
   Each iteration adds 4 cycles to the timings.

   divisor  positive  negative	unsigned

   .   7       19+4n	 20+4n	   20+4n    n = number of iterations
   .   9       21+4n	 22+4n	   21+4n
   .  14       21+4n	 22+4n	   20+4n

   To give an idea of how the number of iterations varies, here is a
   table of dividend versus number of iterations when dividing by 7.

   smallest	 largest       required
   dividend	dividend      iterations

   .	0	     6		    0
   .	7	 0x6ffffff	    1
   0x1000006	0xffffffff	    2

   There is some overlap in the range of numbers requiring 1 and 2
   iterations.	*/

RDEFINE(t2,r1)
RDEFINE(x2,arg0)	/*  r26 */
RDEFINE(t1,arg1)	/*  r25 */
RDEFINE(x1,ret1)	/*  r29 */

	SUBSPA_MILLI_DIV
	ATTR_MILLI

	.proc
	.callinfo	millicode
	.entry
/* NONE of these routines require a stack frame
   ALL of these routines are unwindable from millicode	*/

GSYM($$divide_by_constant)
	.export $$divide_by_constant,millicode
/*  Provides a "nice" label for the code covered by the unwind descriptor
    for things like gprof.  */

/* DIVISION BY 2 (shift by 1) */
GSYM($$divI_2)
	.export		$$divI_2,millicode
	comclr,>=	arg0,0,0
	addi		1,arg0,arg0
	MILLIRET
	extrs		arg0,30,31,ret1


/* DIVISION BY 4 (shift by 2) */
GSYM($$divI_4)
	.export		$$divI_4,millicode
	comclr,>=	arg0,0,0
	addi		3,arg0,arg0
	MILLIRET
	extrs		arg0,29,30,ret1


/* DIVISION BY 8 (shift by 3) */
GSYM($$divI_8)
	.export		$$divI_8,millicode
	comclr,>=	arg0,0,0
	addi		7,arg0,arg0
	MILLIRET
	extrs		arg0,28,29,ret1

/* DIVISION BY 16 (shift by 4) */
GSYM($$divI_16)
	.export		$$divI_16,millicode
	comclr,>=	arg0,0,0
	addi		15,arg0,arg0
	MILLIRET
	extrs		arg0,27,28,ret1

/****************************************************************************
*
*	DIVISION BY DIVISORS OF FFFFFFFF, and powers of 2 times these
*
*	includes 3,5,15,17 and also 6,10,12
*
****************************************************************************/

/* DIVISION BY 3 (use z = 2**32; a = 55555555) */

GSYM($$divI_3)
	.export		$$divI_3,millicode
	comb,<,N	x2,0,LREF(neg3)

	addi		1,x2,x2		/* this cannot overflow	*/
	extru		x2,1,2,x1	/* multiply by 5 to get started */
	sh2add		x2,x2,x2
	b		LREF(pos)
	addc		x1,0,x1

LSYM(neg3)
	subi		1,x2,x2		/* this cannot overflow	*/
	extru		x2,1,2,x1	/* multiply by 5 to get started */
	sh2add		x2,x2,x2
	b		LREF(neg)
	addc		x1,0,x1

GSYM($$divU_3)
	.export		$$divU_3,millicode
	addi		1,x2,x2		/* this CAN overflow */
	addc		0,0,x1
	shd		x1,x2,30,t1	/* multiply by 5 to get started */
	sh2add		x2,x2,x2
	b		LREF(pos)
	addc		x1,t1,x1

/* DIVISION BY 5 (use z = 2**32; a = 33333333) */

GSYM($$divI_5)
	.export		$$divI_5,millicode
	comb,<,N	x2,0,LREF(neg5)

	addi		3,x2,t1		/* this cannot overflow	*/
	sh1add		x2,t1,x2	/* multiply by 3 to get started */
	b		LREF(pos)
	addc		0,0,x1

LSYM(neg5)
	sub		0,x2,x2		/* negate x2			*/
	addi		1,x2,x2		/* this cannot overflow	*/
	shd		0,x2,31,x1	/* get top bit (can be 1)	*/
	sh1add		x2,x2,x2	/* multiply by 3 to get started */
	b		LREF(neg)
	addc		x1,0,x1

GSYM($$divU_5)
	.export		$$divU_5,millicode
	addi		1,x2,x2		/* this CAN overflow */
	addc		0,0,x1
	shd		x1,x2,31,t1	/* multiply by 3 to get started */
	sh1add		x2,x2,x2
	b		LREF(pos)
	addc		t1,x1,x1

/* DIVISION BY	6 (shift to divide by 2 then divide by 3) */
GSYM($$divI_6)
	.export		$$divI_6,millicode
	comb,<,N	x2,0,LREF(neg6)
	extru		x2,30,31,x2	/* divide by 2			*/
	addi		5,x2,t1		/* compute 5*(x2+1) = 5*x2+5	*/
	sh2add		x2,t1,x2	/* multiply by 5 to get started */
	b		LREF(pos)
	addc		0,0,x1

LSYM(neg6)
	subi		2,x2,x2		/* negate, divide by 2, and add 1 */
					/* negation and adding 1 are done */
					/* at the same time by the SUBI   */
	extru		x2,30,31,x2
	shd		0,x2,30,x1
	sh2add		x2,x2,x2	/* multiply by 5 to get started */
	b		LREF(neg)
	addc		x1,0,x1

GSYM($$divU_6)
	.export		$$divU_6,millicode
	extru		x2,30,31,x2	/* divide by 2 */
	addi		1,x2,x2		/* cannot carry */
	shd		0,x2,30,x1	/* multiply by 5 to get started */
	sh2add		x2,x2,x2
	b		LREF(pos)
	addc		x1,0,x1

/* DIVISION BY 10 (shift to divide by 2 then divide by 5) */
GSYM($$divU_10)
	.export		$$divU_10,millicode
	extru		x2,30,31,x2	/* divide by 2 */
	addi		3,x2,t1		/* compute 3*(x2+1) = (3*x2)+3	*/
	sh1add		x2,t1,x2	/* multiply by 3 to get started */
	addc		0,0,x1
LSYM(pos)
	shd		x1,x2,28,t1	/* multiply by 0x11 */
	shd		x2,0,28,t2
	add		x2,t2,x2
	addc		x1,t1,x1
LSYM(pos_for_17)
	shd		x1,x2,24,t1	/* multiply by 0x101 */
	shd		x2,0,24,t2
	add		x2,t2,x2
	addc		x1,t1,x1

	shd		x1,x2,16,t1	/* multiply by 0x10001 */
	shd		x2,0,16,t2
	add		x2,t2,x2
	MILLIRET
	addc		x1,t1,x1

GSYM($$divI_10)
	.export		$$divI_10,millicode
	comb,<		x2,0,LREF(neg10)
	copy		0,x1
	extru		x2,30,31,x2	/* divide by 2 */
	addib,TR	1,x2,LREF(pos)	/* add 1 (cannot overflow)     */
	sh1add		x2,x2,x2	/* multiply by 3 to get started */

LSYM(neg10)
	subi		2,x2,x2		/* negate, divide by 2, and add 1 */
					/* negation and adding 1 are done */
					/* at the same time by the SUBI   */
	extru		x2,30,31,x2
	sh1add		x2,x2,x2	/* multiply by 3 to get started */
LSYM(neg)
	shd		x1,x2,28,t1	/* multiply by 0x11 */
	shd		x2,0,28,t2
	add		x2,t2,x2
	addc		x1,t1,x1
LSYM(neg_for_17)
	shd		x1,x2,24,t1	/* multiply by 0x101 */
	shd		x2,0,24,t2
	add		x2,t2,x2
	addc		x1,t1,x1

	shd		x1,x2,16,t1	/* multiply by 0x10001 */
	shd		x2,0,16,t2
	add		x2,t2,x2
	addc		x1,t1,x1
	MILLIRET
	sub		0,x1,x1

/* DIVISION BY 12 (shift to divide by 4 then divide by 3) */
GSYM($$divI_12)
	.export		$$divI_12,millicode
	comb,<		x2,0,LREF(neg12)
	copy		0,x1
	extru		x2,29,30,x2	/* divide by 4			*/
	addib,tr	1,x2,LREF(pos)	/* compute 5*(x2+1) = 5*x2+5    */
	sh2add		x2,x2,x2	/* multiply by 5 to get started */

LSYM(neg12)
	subi		4,x2,x2		/* negate, divide by 4, and add 1 */
					/* negation and adding 1 are done */
					/* at the same time by the SUBI   */
	extru		x2,29,30,x2
	b		LREF(neg)
	sh2add		x2,x2,x2	/* multiply by 5 to get started */

GSYM($$divU_12)
	.export		$$divU_12,millicode
	extru		x2,29,30,x2	/* divide by 4   */
	addi		5,x2,t1		/* cannot carry */
	sh2add		x2,t1,x2	/* multiply by 5 to get started */
	b		LREF(pos)
	addc		0,0,x1

/* DIVISION BY 15 (use z = 2**32; a = 11111111) */
GSYM($$divI_15)
	.export		$$divI_15,millicode
	comb,<		x2,0,LREF(neg15)
	copy		0,x1
	addib,tr	1,x2,LREF(pos)+4
	shd		x1,x2,28,t1

LSYM(neg15)
	b		LREF(neg)
	subi		1,x2,x2

GSYM($$divU_15)
	.export		$$divU_15,millicode
	addi		1,x2,x2		/* this CAN overflow */
	b		LREF(pos)
	addc		0,0,x1

/* DIVISION BY 17 (use z = 2**32; a =  f0f0f0f) */
GSYM($$divI_17)
	.export		$$divI_17,millicode
	comb,<,n	x2,0,LREF(neg17)
	addi		1,x2,x2		/* this cannot overflow */
	shd		0,x2,28,t1	/* multiply by 0xf to get started */
	shd		x2,0,28,t2
	sub		t2,x2,x2
	b		LREF(pos_for_17)
	subb		t1,0,x1

LSYM(neg17)
	subi		1,x2,x2		/* this cannot overflow */
	shd		0,x2,28,t1	/* multiply by 0xf to get started */
	shd		x2,0,28,t2
	sub		t2,x2,x2
	b		LREF(neg_for_17)
	subb		t1,0,x1

GSYM($$divU_17)
	.export		$$divU_17,millicode
	addi		1,x2,x2		/* this CAN overflow */
	addc		0,0,x1
	shd		x1,x2,28,t1	/* multiply by 0xf to get started */
LSYM(u17)
	shd		x2,0,28,t2
	sub		t2,x2,x2
	b		LREF(pos_for_17)
	subb		t1,x1,x1


/* DIVISION BY DIVISORS OF FFFFFF, and powers of 2 times these
   includes 7,9 and also 14


   z = 2**24-1
   r = z mod x = 0

   so choose b = 0

   Also, in order to divide by z = 2**24-1, we approximate by dividing
   by (z+1) = 2**24 (which is easy), and then correcting.

   (ax) = (z+1)q' + r
   .	= zq' + (q'+r)

   So to compute (ax)/z, compute q' = (ax)/(z+1) and r = (ax) mod (z+1)
   Then the true remainder of (ax)/z is (q'+r).  Repeat the process
   with this new remainder, adding the tentative quotients together,
   until a tentative quotient is 0 (and then we are done).  There is
   one last correction to be done.  It is possible that (q'+r) = z.
   If so, then (q'+r)/(z+1) = 0 and it looks like we are done.	But,
   in fact, we need to add 1 more to the quotient.  Now, it turns
   out that this happens if and only if the original value x is
   an exact multiple of y.  So, to avoid a three instruction test at
   the end, instead use 1 instruction to add 1 to x at the beginning.  */

/* DIVISION BY 7 (use z = 2**24-1; a = 249249) */
GSYM($$divI_7)
	.export		$$divI_7,millicode
	comb,<,n	x2,0,LREF(neg7)
LSYM(7)
	addi		1,x2,x2		/* cannot overflow */
	shd		0,x2,29,x1
	sh3add		x2,x2,x2
	addc		x1,0,x1
LSYM(pos7)
	shd		x1,x2,26,t1
	shd		x2,0,26,t2
	add		x2,t2,x2
	addc		x1,t1,x1

	shd		x1,x2,20,t1
	shd		x2,0,20,t2
	add		x2,t2,x2
	addc		x1,t1,t1

	/* computed <t1,x2>.  Now divide it by (2**24 - 1)	*/

	copy		0,x1
	shd,=		t1,x2,24,t1	/* tentative quotient  */
LSYM(1)
	addb,tr		t1,x1,LREF(2)	/* add to previous quotient   */
	extru		x2,31,24,x2	/* new remainder (unadjusted) */

	MILLIRETN

LSYM(2)
	addb,tr		t1,x2,LREF(1)	/* adjust remainder */
	extru,=		x2,7,8,t1	/* new quotient     */

LSYM(neg7)
	subi		1,x2,x2		/* negate x2 and add 1 */
LSYM(8)
	shd		0,x2,29,x1
	sh3add		x2,x2,x2
	addc		x1,0,x1

LSYM(neg7_shift)
	shd		x1,x2,26,t1
	shd		x2,0,26,t2
	add		x2,t2,x2
	addc		x1,t1,x1

	shd		x1,x2,20,t1
	shd		x2,0,20,t2
	add		x2,t2,x2
	addc		x1,t1,t1

	/* computed <t1,x2>.  Now divide it by (2**24 - 1)	*/

	copy		0,x1
	shd,=		t1,x2,24,t1	/* tentative quotient  */
LSYM(3)
	addb,tr		t1,x1,LREF(4)	/* add to previous quotient   */
	extru		x2,31,24,x2	/* new remainder (unadjusted) */

	MILLIRET
	sub		0,x1,x1		/* negate result    */

LSYM(4)
	addb,tr		t1,x2,LREF(3)	/* adjust remainder */
	extru,=		x2,7,8,t1	/* new quotient     */

GSYM($$divU_7)
	.export		$$divU_7,millicode
	addi		1,x2,x2		/* can carry */
	addc		0,0,x1
	shd		x1,x2,29,t1
	sh3add		x2,x2,x2
	b		LREF(pos7)
	addc		t1,x1,x1

/* DIVISION BY 9 (use z = 2**24-1; a = 1c71c7) */
GSYM($$divI_9)
	.export		$$divI_9,millicode
	comb,<,n	x2,0,LREF(neg9)
	addi		1,x2,x2		/* cannot overflow */
	shd		0,x2,29,t1
	shd		x2,0,29,t2
	sub		t2,x2,x2
	b		LREF(pos7)
	subb		t1,0,x1

LSYM(neg9)
	subi		1,x2,x2		/* negate and add 1 */
	shd		0,x2,29,t1
	shd		x2,0,29,t2
	sub		t2,x2,x2
	b		LREF(neg7_shift)
	subb		t1,0,x1

GSYM($$divU_9)
	.export		$$divU_9,millicode
	addi		1,x2,x2		/* can carry */
	addc		0,0,x1
	shd		x1,x2,29,t1
	shd		x2,0,29,t2
	sub		t2,x2,x2
	b		LREF(pos7)
	subb		t1,x1,x1

/* DIVISION BY 14 (shift to divide by 2 then divide by 7) */
GSYM($$divI_14)
	.export		$$divI_14,millicode
	comb,<,n	x2,0,LREF(neg14)
GSYM($$divU_14)
	.export		$$divU_14,millicode
	b		LREF(7)		/* go to 7 case */
	extru		x2,30,31,x2	/* divide by 2  */

LSYM(neg14)
	subi		2,x2,x2		/* negate (and add 2) */
	b		LREF(8)
	extru		x2,30,31,x2	/* divide by 2	      */
	.exit
	.procend
	.end
#endif
