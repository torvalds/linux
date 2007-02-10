# S/390 __udiv_qrnnd

# r2 : &__r
# r3 : upper half of 64 bit word n
# r4 : lower half of 64 bit word n
# r5 : divisor d
# the reminder r of the division is to be stored to &__r and
# the quotient q is to be returned

	.text
	.globl __udiv_qrnnd
__udiv_qrnnd:
	st    %r2,24(%r15)	  # store pointer to reminder for later
	lr    %r0,%r3		  # reload n
	lr    %r1,%r4
	ltr   %r2,%r5		  # reload and test divisor
	jp    5f
	# divisor >= 0x80000000
	srdl  %r0,2		  # n/4
	srl   %r2,1		  # d/2
	slr   %r1,%r2		  # special case if last bit of d is set
	brc   3,0f		  #  (n/4) div (n/2) can overflow by 1
	ahi   %r0,-1		  #  trick: subtract n/2, then divide
0:	dr    %r0,%r2		  # signed division
	ahi   %r1,1		  #  trick part 2: add 1 to the quotient
	# now (n >> 2) = (d >> 1) * %r1 + %r0
	lhi   %r3,1
	nr    %r3,%r1		  # test last bit of q
	jz    1f
	alr   %r0,%r2		  # add (d>>1) to r
1:	srl   %r1,1		  # q >>= 1
	# now (n >> 2) = (d&-2) * %r1 + %r0
	lhi   %r3,1
	nr    %r3,%r5		  # test last bit of d
	jz    2f
	slr   %r0,%r1		  # r -= q
	brc   3,2f		  # borrow ?
	alr   %r0,%r5		  # r += d
	ahi   %r1,-1
2:	# now (n >> 2) = d * %r1 + %r0
	alr   %r1,%r1		  # q <<= 1
	alr   %r0,%r0		  # r <<= 1
	brc   12,3f		  # overflow on r ?
	slr   %r0,%r5		  # r -= d
	ahi   %r1,1		  # q += 1
3:	lhi   %r3,2
	nr    %r3,%r4		  # test next to last bit of n
	jz    4f
	ahi   %r0,1		  # r += 1
4:	clr   %r0,%r5		  # r >= d ?
	jl    6f
	slr   %r0,%r5		  # r -= d
	ahi   %r1,1		  # q += 1
	# now (n >> 1) = d * %r1 + %r0
	j     6f
5:	# divisor < 0x80000000
	srdl  %r0,1
	dr    %r0,%r2		  # signed division
	# now (n >> 1) = d * %r1 + %r0
6:	alr   %r1,%r1		  # q <<= 1
	alr   %r0,%r0		  # r <<= 1
	brc   12,7f		  # overflow on r ?
	slr   %r0,%r5		  # r -= d
	ahi   %r1,1		  # q += 1
7:	lhi   %r3,1
	nr    %r3,%r4		  # isolate last bit of n
	alr   %r0,%r3		  # r += (n & 1)
	clr   %r0,%r5		  # r >= d ?
	jl    8f
	slr   %r0,%r5		  # r -= d
	ahi   %r1,1		  # q += 1
8:	# now n = d * %r1 + %r0
	l     %r2,24(%r15)
	st    %r0,0(%r2)
	lr    %r2,%r1
	br    %r14
	.end	__udiv_qrnnd
