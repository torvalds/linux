/* $Id: urem.S,v 1.4 1996/09/30 02:22:42 davem Exp $
 * urem.S:      This routine was taken from glibc-1.09 and is covered
 *              by the GNU Library General Public License Version 2.
 */

/* This file is generated from divrem.m4; DO NOT EDIT! */
/*
 * Division and remainder, from Appendix E of the Sparc Version 8
 * Architecture Manual, with fixes from Gordon Irlam.
 */

/*
 * Input: dividend and divisor in %o0 and %o1 respectively.
 *
 * m4 parameters:
 *  .urem	name of function to generate
 *  rem		rem=div => %o0 / %o1; rem=rem => %o0 % %o1
 *  false		false=true => signed; false=false => unsigned
 *
 * Algorithm parameters:
 *  N		how many bits per iteration we try to get (4)
 *  WORDSIZE	total number of bits (32)
 *
 * Derived constants:
 *  TOPBITS	number of bits in the top decade of a number
 *
 * Important variables:
 *  Q		the partial quotient under development (initially 0)
 *  R		the remainder so far, initially the dividend
 *  ITER	number of main division loop iterations required;
 *		equal to ceil(log2(quotient) / N).  Note that this
 *		is the log base (2^N) of the quotient.
 *  V		the current comparand, initially divisor*2^(ITER*N-1)
 *
 * Cost:
 *  Current estimate for non-large dividend is
 *	ceil(log2(quotient) / N) * (10 + 7N/2) + C
 *  A large dividend is one greater than 2^(31-TOPBITS) and takes a
 *  different path, as the upper bits of the quotient must be developed
 *  one bit at a time.
 */

	.globl .urem
	.globl _Urem
.urem:
_Urem:	/* needed for export */

	! Ready to divide.  Compute size of quotient; scale comparand.
	orcc	%o1, %g0, %o5
	bne	1f
	 mov	%o0, %o3

		! Divide by zero trap.  If it returns, return 0 (about as
		! wrong as possible, but that is what SunOS does...).
		ta	ST_DIV0
		retl
		 clr	%o0

1:
	cmp	%o3, %o5			! if %o1 exceeds %o0, done
	blu	Lgot_result		! (and algorithm fails otherwise)
	 clr	%o2

	sethi	%hi(1 << (32 - 4 - 1)), %g1

	cmp	%o3, %g1
	blu	Lnot_really_big
	 clr	%o4

	! Here the dividend is >= 2**(31-N) or so.  We must be careful here,
	! as our usual N-at-a-shot divide step will cause overflow and havoc.
	! The number of bits in the result here is N*ITER+SC, where SC <= N.
	! Compute ITER in an unorthodox manner: know we need to shift V into
	! the top decade: so do not even bother to compare to R.
	1:
		cmp	%o5, %g1
		bgeu	3f
		 mov	1, %g7

		sll	%o5, 4, %o5

		b	1b
		 add	%o4, 1, %o4

	! Now compute %g7.
	2:
		addcc	%o5, %o5, %o5
		bcc	Lnot_too_big
		 add	%g7, 1, %g7

		! We get here if the %o1 overflowed while shifting.
		! This means that %o3 has the high-order bit set.
		! Restore %o5 and subtract from %o3.
		sll	%g1, 4, %g1	! high order bit
		srl	%o5, 1, %o5		! rest of %o5
		add	%o5, %g1, %o5

		b	Ldo_single_div
		 sub	%g7, 1, %g7

	Lnot_too_big:
	3:
		cmp	%o5, %o3
		blu	2b
		 nop

		be	Ldo_single_div
		 nop
	/* NB: these are commented out in the V8-Sparc manual as well */
	/* (I do not understand this) */
	! %o5 > %o3: went too far: back up 1 step
	!	srl	%o5, 1, %o5
	!	dec	%g7
	! do single-bit divide steps
	!
	! We have to be careful here.  We know that %o3 >= %o5, so we can do the
	! first divide step without thinking.  BUT, the others are conditional,
	! and are only done if %o3 >= 0.  Because both %o3 and %o5 may have the high-
	! order bit set in the first step, just falling into the regular
	! division loop will mess up the first time around.
	! So we unroll slightly...
	Ldo_single_div:
		subcc	%g7, 1, %g7
		bl	Lend_regular_divide
		 nop

		sub	%o3, %o5, %o3
		mov	1, %o2

		b	Lend_single_divloop
		 nop
	Lsingle_divloop:
		sll	%o2, 1, %o2
		bl	1f
		 srl	%o5, 1, %o5
		! %o3 >= 0
		sub	%o3, %o5, %o3
		b	2f
		 add	%o2, 1, %o2
	1:	! %o3 < 0
		add	%o3, %o5, %o3
		sub	%o2, 1, %o2
	2:
	Lend_single_divloop:
		subcc	%g7, 1, %g7
		bge	Lsingle_divloop
		 tst	%o3

		b,a	Lend_regular_divide

Lnot_really_big:
1:
	sll	%o5, 4, %o5

	cmp	%o5, %o3
	bleu	1b
	 addcc	%o4, 1, %o4

	be	Lgot_result
	 sub	%o4, 1, %o4

	tst	%o3	! set up for initial iteration
Ldivloop:
	sll	%o2, 4, %o2
		! depth 1, accumulated bits 0
	bl	L.1.16
	 srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
			! depth 2, accumulated bits 1
	bl	L.2.17
	 srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
			! depth 3, accumulated bits 3
	bl	L.3.19
	 srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
			! depth 4, accumulated bits 7
	bl	L.4.23
	 srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	b	9f
	 add	%o2, (7*2+1), %o2

L.4.23:
	! remainder is negative
	addcc	%o3,%o5,%o3
	b	9f
	 add	%o2, (7*2-1), %o2

L.3.19:
	! remainder is negative
	addcc	%o3,%o5,%o3
			! depth 4, accumulated bits 5
	bl	L.4.21
	 srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	b	9f
	 add	%o2, (5*2+1), %o2

L.4.21:
	! remainder is negative
	addcc	%o3,%o5,%o3
	b	9f
	 add	%o2, (5*2-1), %o2

L.2.17:
	! remainder is negative
	addcc	%o3,%o5,%o3
			! depth 3, accumulated bits 1
	bl	L.3.17
	 srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
			! depth 4, accumulated bits 3
	bl	L.4.19
	 srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	b	9f
	 add	%o2, (3*2+1), %o2

L.4.19:
	! remainder is negative
	addcc	%o3,%o5,%o3
	b	9f
	 add	%o2, (3*2-1), %o2

L.3.17:
	! remainder is negative
	addcc	%o3,%o5,%o3
			! depth 4, accumulated bits 1
	bl	L.4.17
	 srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	b	9f
	 add	%o2, (1*2+1), %o2
	
L.4.17:
	! remainder is negative
	addcc	%o3,%o5,%o3
	b	9f
	 add	%o2, (1*2-1), %o2

L.1.16:
	! remainder is negative
	addcc	%o3,%o5,%o3
			! depth 2, accumulated bits -1
	bl	L.2.15
	 srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
			! depth 3, accumulated bits -1
	bl	L.3.15
	 srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
			! depth 4, accumulated bits -1
	bl	L.4.15
	 srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	b	9f
	 add	%o2, (-1*2+1), %o2

L.4.15:
	! remainder is negative
	addcc	%o3,%o5,%o3
	b	9f
	 add	%o2, (-1*2-1), %o2

L.3.15:
	! remainder is negative
	addcc	%o3,%o5,%o3
			! depth 4, accumulated bits -3
	bl	L.4.13
	 srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	b	9f
	 add	%o2, (-3*2+1), %o2

L.4.13:
	! remainder is negative
	addcc	%o3,%o5,%o3
	b	9f
	 add	%o2, (-3*2-1), %o2

L.2.15:
	! remainder is negative
	addcc	%o3,%o5,%o3
			! depth 3, accumulated bits -3
	bl	L.3.13
	 srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
			! depth 4, accumulated bits -5
	bl	L.4.11
	 srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	b	9f
	 add	%o2, (-5*2+1), %o2
	
L.4.11:
	! remainder is negative
	addcc	%o3,%o5,%o3
	b	9f
	 add	%o2, (-5*2-1), %o2

L.3.13:
	! remainder is negative
	addcc	%o3,%o5,%o3
			! depth 4, accumulated bits -7
	bl	L.4.9
	 srl	%o5,1,%o5
	! remainder is positive
	subcc	%o3,%o5,%o3
	b	9f
	 add	%o2, (-7*2+1), %o2

L.4.9:
	! remainder is negative
	addcc	%o3,%o5,%o3
	b	9f
	 add	%o2, (-7*2-1), %o2

	9:
Lend_regular_divide:
	subcc	%o4, 1, %o4
	bge	Ldivloop
	 tst	%o3

	bl,a	Lgot_result
	! non-restoring fixup here (one instruction only!)
	add	%o3, %o1, %o3

Lgot_result:

	retl
	 mov %o3, %o0

	.globl	.urem_patch
.urem_patch:
	wr	%g0, 0x0, %y
	nop
	nop
	nop
	udiv	%o0, %o1, %o2
	umul	%o2, %o1, %o2
	retl
	 sub	%o0, %o2, %o0
