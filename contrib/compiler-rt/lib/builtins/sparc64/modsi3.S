/*
 * This m4 code has been taken from The SPARC Architecture Manual Version 8.
 */
/*
 * Division/Remainder
 *
 * Input is:
 *   dividend -- the thing being divided
 *   divisor -- how many ways to divide it
 * Important parameters:
 *   N -- how many bits per iteration we try to get
 *        as our current guess:
 *   WORDSIZE -- how many bits altogether we're talking about:
 *               obviously:
 * A derived constant:
 *   TOPBITS -- how many bits are in the top "decade" of a number:
 *
 * Important variables are:
 *   Q -- the partial quotient under development -- initially 0
 *   R -- the remainder so far -- initially == the dividend
 *   ITER -- number of iterations of the main division loop which will
 *           be required. Equal to CEIL( lg2(quotient)/4 )
 *           Note that this is log_base_(2ˆ4) of the quotient.
 *   V -- the current comparand -- initially divisor*2ˆ(ITER*4-1)
 * Cost:
 *   current estimate for non-large dividend is
 *        CEIL( lg2(quotient) / 4 ) x ( 10 + 74/2 ) + C
 *   a large dividend is one greater than 2ˆ(31-4 ) and takes a
 *   different path, as the upper bits of the quotient must be developed
 *   one bit at a time.
 *   This uses the m4 and cpp macro preprocessors.
 */
/*
 * This is the recursive definition of how we develop quotient digits.
 * It takes three important parameters:
 *   $1 -- the current depth, 1<=$1<=4
 *   $2 -- the current accumulation of quotient bits
 *   4 -- max depth
 * We add a new bit to $2 and either recurse or insert the bits in the quotient.
 * Dynamic input:
 *   %o3 -- current remainder
 *   %o2 -- current quotient
 *   %o5 -- current comparand
 *   cc -- set on current value of %o3
 * Dynamic output:
 *   %o3', %o2', %o5', cc'
 */
#include "../assembly.h"
.text
	.align 32
DEFINE_COMPILERRT_FUNCTION(__umodsi3)
	b	divide
	mov	0,%g3			! result always nonnegative
.text
	.align 32
DEFINE_COMPILERRT_FUNCTION(__modsi3)
	orcc	%o1,%o0,%g0	! are either %o0 or %o1 negative
	bge	divide			! if not, skip this junk
	mov	%o0,%g3		! record sign of result in sign of %g3
	tst	%o1
	bge	2f
	tst	%o0
	! %o1 < 0
	bge	divide
	neg	%o1
	2:
	! %o0 < 0
	neg	%o0
	! FALL THROUGH
divide:
	! Compute size of quotient, scale comparand.
	orcc	%o1,%g0,%o5		! movcc %o1,%o5
	te	2			! if %o1 = 0
	mov	%o0,%o3
	mov	0,%o2
	sethi	%hi(1<<(32-4 -1)),%g1
	cmp	%o3,%g1
	blu	not_really_big
	mov	0,%o4
	!
	! Here, the %o0 is >= 2ˆ(31-4) or so. We must be careful here,
	! as our usual 4-at-a-shot divide step will cause overflow and havoc.
	! The total number of bits in the result here is 4*%o4+%g2, where
	! %g2 <= 4.
	! Compute %o4 in an unorthodox manner: know we need to Shift %o5 into
! the top decade: so don't even bother to compare to %o3.
1:
	cmp	%o5,%g1
	bgeu	3f
	mov	1,%g2
	sll	%o5,4,%o5
	b	1b
	inc	%o4
! Now compute %g2
2:	addcc	%o5,%o5,%o5
	bcc	not_too_big
	add	%g2,1,%g2
		! We're here if the %o1 overflowed when Shifting.
		! This means that %o3 has the high-order bit set.
		! Restore %o5 and subtract from %o3.
		sll	%g1,4 ,%g1	! high order bit
		srl	%o5,1,%o5		! rest of %o5
		add	%o5,%g1,%o5
		b	do_single_div
		dec	%g2
not_too_big:
3:	cmp	%o5,%o3
	blu	2b
	nop
	be	do_single_div
	nop
! %o5 > %o3: went too far: back up 1 step
!     srl %o5,1,%o5
!      dec %g2
! do single-bit divide steps
!
! We have to be careful here. We know that %o3 >= %o5, so we can do the
! first divide step without thinking. BUT, the others are conditional,
! and are only done if %o3 >= 0. Because both %o3 and %o5 may have the high-
! order bit set in the first step, just falling into the regular
! division loop will mess up the first time around.
! So we unroll slightly...
do_single_div:
	deccc	%g2
	bl	end_regular_divide
	nop
	sub	%o3,%o5,%o3
	mov	1,%o2
	b,a	end_single_divloop
	! EMPTY
single_divloop:
	sll	%o2,1,%o2
	bl	1f
	srl	%o5,1,%o5
	! %o3 >= 0
		sub	%o3,%o5,%o3
		b	2f
		inc	%o2
	1:	! %o3 < 0
		add	%o3,%o5,%o3
		dec	%o2
	2:
	end_single_divloop:
		deccc	%g2
		bge	single_divloop
		tst	%o3
		b,a	end_regular_divide
		! EMPTY
not_really_big:
1:
	sll	%o5,4,%o5
	cmp	%o5,%o3
	bleu	1b
	inccc	%o4
	be	got_result
	dec	%o4
do_regular_divide:
	! Do the main division iteration
	tst	%o3
	! Fall through into divide loop
divloop:
	sll	%o2,4,%o2
		!depth 1, accumulated bits 0
	bl	L.1.16
	srl	%o5,1,%o5
	! remainder is nonnegative
	subcc	%o3,%o5,%o3
	 	!depth 2, accumulated bits 1
	bl	L.2.17
	srl	%o5,1,%o5
	! remainder is nonnegative
	subcc	%o3,%o5,%o3
	 	!depth 3, accumulated bits 3
	bl	L.3.19
	srl	%o5,1,%o5
	! remainder is nonnegative
	subcc	%o3,%o5,%o3
	 	!depth 4, accumulated bits 7
	bl	L.4.23
	srl	%o5,1,%o5
	! remainder is nonnegative
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
	 	!depth 4, accumulated bits 5
	bl	L.4.21
	srl	%o5,1,%o5
	! remainder is nonnegative
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
	 	!depth 3, accumulated bits 1
	bl	L.3.17
	srl	%o5,1,%o5
	! remainder is nonnegative
	subcc	%o3,%o5,%o3
	 	!depth 4, accumulated bits 3
	bl	L.4.19
	srl	%o5,1,%o5
	! remainder is nonnegative
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
	 	!depth 4, accumulated bits 1
	bl	L.4.17
	srl	%o5,1,%o5
	! remainder is nonnegative
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
	 	!depth 2, accumulated bits -1
	bl	L.2.15
	srl	%o5,1,%o5
	! remainder is nonnegative
	subcc	%o3,%o5,%o3
	 	!depth 3, accumulated bits -1
	bl	L.3.15
	srl	%o5,1,%o5
	! remainder is nonnegative
	subcc	%o3,%o5,%o3
	 	!depth 4, accumulated bits -1
	bl	L.4.15
	srl	%o5,1,%o5
	! remainder is nonnegative
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
	 	!depth 4, accumulated bits -3
	bl	L.4.13
	srl	%o5,1,%o5
	! remainder is nonnegative
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
	 	!depth 3, accumulated bits -3
	bl	L.3.13
	srl	%o5,1,%o5
	! remainder is nonnegative
	subcc	%o3,%o5,%o3
	 	!depth 4, accumulated bits -5
	bl	L.4.11
	srl	%o5,1,%o5
	! remainder is nonnegative
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
	 	!depth 4, accumulated bits -7
	bl	L.4.9
	srl	%o5,1,%o5
	! remainder is nonnegative
	subcc	%o3,%o5,%o3
		b	9f
		add	%o2, (-7*2+1), %o2
L.4.9:
	! remainder is negative
	addcc	%o3,%o5,%o3
		b	9f
		add	%o2, (-7*2-1), %o2
	9:
end_regular_divide:
	deccc	%o4
	bge	divloop
	tst	%o3
	bl,a	got_result
	! non-restoring fixup if remainder < 0, otherwise annulled
	add	%o3,%o1,%o3
got_result:
	tst	%g3
	bl,a	1f
	! negate for answer < 0, otherwise annulled
	neg	%o3,%o3 		! %o3 <- -%o3
1:
	retl				! leaf-routine return
	mov	%o3,%o0			! remainder <- %o3
