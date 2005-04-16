/* $Id: umul.S,v 1.4 1996/09/30 02:22:39 davem Exp $
 * umul.S:      This routine was taken from glibc-1.09 and is covered
 *              by the GNU Library General Public License Version 2.
 */


/*
 * Unsigned multiply.  Returns %o0 * %o1 in %o1%o0 (i.e., %o1 holds the
 * upper 32 bits of the 64-bit product).
 *
 * This code optimizes short (less than 13-bit) multiplies.  Short
 * multiplies require 25 instruction cycles, and long ones require
 * 45 instruction cycles.
 *
 * On return, overflow has occurred (%o1 is not zero) if and only if
 * the Z condition code is clear, allowing, e.g., the following:
 *
 *	call	.umul
 *	nop
 *	bnz	overflow	(or tnz)
 */

	.globl .umul
.umul:
	or	%o0, %o1, %o4
	mov	%o0, %y		! multiplier -> Y

	andncc	%o4, 0xfff, %g0	! test bits 12..31 of *both* args
	be	Lmul_shortway	! if zero, can do it the short way
	 andcc	%g0, %g0, %o4	! zero the partial product and clear N and V

	/*
	 * Long multiply.  32 steps, followed by a final shift step.
	 */
	mulscc	%o4, %o1, %o4	! 1
	mulscc	%o4, %o1, %o4	! 2
	mulscc	%o4, %o1, %o4	! 3
	mulscc	%o4, %o1, %o4	! 4
	mulscc	%o4, %o1, %o4	! 5
	mulscc	%o4, %o1, %o4	! 6
	mulscc	%o4, %o1, %o4	! 7
	mulscc	%o4, %o1, %o4	! 8
	mulscc	%o4, %o1, %o4	! 9
	mulscc	%o4, %o1, %o4	! 10
	mulscc	%o4, %o1, %o4	! 11
	mulscc	%o4, %o1, %o4	! 12
	mulscc	%o4, %o1, %o4	! 13
	mulscc	%o4, %o1, %o4	! 14
	mulscc	%o4, %o1, %o4	! 15
	mulscc	%o4, %o1, %o4	! 16
	mulscc	%o4, %o1, %o4	! 17
	mulscc	%o4, %o1, %o4	! 18
	mulscc	%o4, %o1, %o4	! 19
	mulscc	%o4, %o1, %o4	! 20
	mulscc	%o4, %o1, %o4	! 21
	mulscc	%o4, %o1, %o4	! 22
	mulscc	%o4, %o1, %o4	! 23
	mulscc	%o4, %o1, %o4	! 24
	mulscc	%o4, %o1, %o4	! 25
	mulscc	%o4, %o1, %o4	! 26
	mulscc	%o4, %o1, %o4	! 27
	mulscc	%o4, %o1, %o4	! 28
	mulscc	%o4, %o1, %o4	! 29
	mulscc	%o4, %o1, %o4	! 30
	mulscc	%o4, %o1, %o4	! 31
	mulscc	%o4, %o1, %o4	! 32
	mulscc	%o4, %g0, %o4	! final shift


	/*
	 * Normally, with the shift-and-add approach, if both numbers are
	 * positive you get the correct result.  With 32-bit two's-complement
	 * numbers, -x is represented as
	 *
	 *		  x		    32
	 *	( 2  -  ------ ) mod 2  *  2
	 *		   32
	 *		  2
	 *
	 * (the `mod 2' subtracts 1 from 1.bbbb).  To avoid lots of 2^32s,
	 * we can treat this as if the radix point were just to the left
	 * of the sign bit (multiply by 2^32), and get
	 *
	 *	-x  =  (2 - x) mod 2
	 *
	 * Then, ignoring the `mod 2's for convenience:
	 *
	 *   x *  y	= xy
	 *  -x *  y	= 2y - xy
	 *   x * -y	= 2x - xy
	 *  -x * -y	= 4 - 2x - 2y + xy
	 *
	 * For signed multiplies, we subtract (x << 32) from the partial
	 * product to fix this problem for negative multipliers (see mul.s).
	 * Because of the way the shift into the partial product is calculated
	 * (N xor V), this term is automatically removed for the multiplicand,
	 * so we don't have to adjust.
	 *
	 * But for unsigned multiplies, the high order bit wasn't a sign bit,
	 * and the correction is wrong.  So for unsigned multiplies where the
	 * high order bit is one, we end up with xy - (y << 32).  To fix it
	 * we add y << 32.
	 */
#if 0
	tst	%o1
	bl,a	1f		! if %o1 < 0 (high order bit = 1),
	 add	%o4, %o0, %o4	! %o4 += %o0 (add y to upper half)

1:
	rd	%y, %o0		! get lower half of product
	retl
	 addcc	%o4, %g0, %o1	! put upper half in place and set Z for %o1==0
#else
	/* Faster code from tege@sics.se.  */
	sra	%o1, 31, %o2	! make mask from sign bit
	and	%o0, %o2, %o2	! %o2 = 0 or %o0, depending on sign of %o1
	rd	%y, %o0		! get lower half of product
	retl
	 addcc	%o4, %o2, %o1	! add compensation and put upper half in place
#endif

Lmul_shortway:
	/*
	 * Short multiply.  12 steps, followed by a final shift step.
	 * The resulting bits are off by 12 and (32-12) = 20 bit positions,
	 * but there is no problem with %o0 being negative (unlike above),
	 * and overflow is impossible (the answer is at most 24 bits long).
	 */
	mulscc	%o4, %o1, %o4	! 1
	mulscc	%o4, %o1, %o4	! 2
	mulscc	%o4, %o1, %o4	! 3
	mulscc	%o4, %o1, %o4	! 4
	mulscc	%o4, %o1, %o4	! 5
	mulscc	%o4, %o1, %o4	! 6
	mulscc	%o4, %o1, %o4	! 7
	mulscc	%o4, %o1, %o4	! 8
	mulscc	%o4, %o1, %o4	! 9
	mulscc	%o4, %o1, %o4	! 10
	mulscc	%o4, %o1, %o4	! 11
	mulscc	%o4, %o1, %o4	! 12
	mulscc	%o4, %g0, %o4	! final shift

	/*
	 * %o4 has 20 of the bits that should be in the result; %y has
	 * the bottom 12 (as %y's top 12).  That is:
	 *
	 *	  %o4		    %y
	 * +----------------+----------------+
	 * | -12- |   -20-  | -12- |   -20-  |
	 * +------(---------+------)---------+
	 *	   -----result-----
	 *
	 * The 12 bits of %o4 left of the `result' area are all zero;
	 * in fact, all top 20 bits of %o4 are zero.
	 */

	rd	%y, %o5
	sll	%o4, 12, %o0	! shift middle bits left 12
	srl	%o5, 20, %o5	! shift low bits right 20
	or	%o5, %o0, %o0
	retl
	 addcc	%g0, %g0, %o1	! %o1 = zero, and set Z

	.globl	.umul_patch
.umul_patch:
	umul	%o0, %o1, %o0
	retl
	 rd	%y, %o1
	nop
