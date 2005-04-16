~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
MOTOROLA MICROPROCESSOR & MEMORY TECHNOLOGY GROUP
M68000 Hi-Performance Microprocessor Division
M68060 Software Package
Production Release P1.00 -- October 10, 1994

M68060 Software Package Copyright © 1993, 1994 Motorola Inc.  All rights reserved.

THE SOFTWARE is provided on an "AS IS" basis and without warranty.
To the maximum extent permitted by applicable law,
MOTOROLA DISCLAIMS ALL WARRANTIES WHETHER EXPRESS OR IMPLIED,
INCLUDING IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
and any warranty against infringement with regard to the SOFTWARE
(INCLUDING ANY MODIFIED VERSIONS THEREOF) and any accompanying written materials.

To the maximum extent permitted by applicable law,
IN NO EVENT SHALL MOTOROLA BE LIABLE FOR ANY DAMAGES WHATSOEVER
(INCLUDING WITHOUT LIMITATION, DAMAGES FOR LOSS OF BUSINESS PROFITS,
BUSINESS INTERRUPTION, LOSS OF BUSINESS INFORMATION, OR OTHER PECUNIARY LOSS)
ARISING OF THE USE OR INABILITY TO USE THE SOFTWARE.
Motorola assumes no responsibility for the maintenance and support of the SOFTWARE.

You are hereby granted a copyright license to use, modify, and distribute the SOFTWARE
so long as this entire notice is retained without alteration in any modified and/or
redistributed versions, and that such modified versions are clearly identified as such.
No licenses are granted by implication, estoppel or otherwise under any patents
or trademarks of Motorola, Inc.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
# litop.s:
#	This file is appended to the top of the 060FPLSP package
# and contains the entry points into the package. The user, in
# effect, branches to one of the branch table entries located here.
#

	bra.l	_060LSP__idivs64_
	short	0x0000
	bra.l	_060LSP__idivu64_
	short	0x0000

	bra.l	_060LSP__imuls64_
	short	0x0000
	bra.l	_060LSP__imulu64_
	short	0x0000

	bra.l	_060LSP__cmp2_Ab_
	short	0x0000
	bra.l	_060LSP__cmp2_Aw_
	short	0x0000
	bra.l	_060LSP__cmp2_Al_
	short	0x0000
	bra.l	_060LSP__cmp2_Db_
	short	0x0000
	bra.l	_060LSP__cmp2_Dw_
	short	0x0000
	bra.l	_060LSP__cmp2_Dl_
	short	0x0000

# leave room for future possible aditions.
	align	0x200

#########################################################################
# XDEF ****************************************************************	#
#	_060LSP__idivu64_(): Emulate 64-bit unsigned div instruction.	#
#	_060LSP__idivs64_(): Emulate 64-bit signed div instruction.	#
#									#
#	This is the library version which is accessed as a subroutine	#
#	and therefore does not work exactly like the 680X0 div{s,u}.l	#
#	64-bit divide instruction.					#
#									#
# XREF ****************************************************************	#
#	None.								#
#									#
# INPUT ***************************************************************	#
#	0x4(sp)  = divisor						#
#	0x8(sp)  = hi(dividend)						#
#	0xc(sp)  = lo(dividend)						#
#	0x10(sp) = pointer to location to place quotient/remainder	#
#									#
# OUTPUT **************************************************************	#
#	0x10(sp) = points to location of remainder/quotient.		#
#		   remainder is in first longword, quotient is in 2nd.	#
#									#
# ALGORITHM ***********************************************************	#
#	If the operands are signed, make them unsigned and save the	#
# sign info for later. Separate out special cases like divide-by-zero	#
# or 32-bit divides if possible. Else, use a special math algorithm	#
# to calculate the result.						#
#	Restore sign info if signed instruction. Set the condition	#
# codes before performing the final "rts". If the divisor was equal to	#
# zero, then perform a divide-by-zero using a 16-bit implemented	#
# divide instruction. This way, the operating system can record that	#
# the event occurred even though it may not point to the correct place.	#
#									#
#########################################################################

set	POSNEG,		-1
set	NDIVISOR,	-2
set	NDIVIDEND,	-3
set	DDSECOND,	-4
set	DDNORMAL,	-8
set	DDQUOTIENT,	-12
set	DIV64_CC,	-16

##########
# divs.l #
##########
	global		_060LSP__idivs64_
_060LSP__idivs64_:
# PROLOGUE BEGIN ########################################################
	link.w		%a6,&-16
	movm.l		&0x3f00,-(%sp)		# save d2-d7
#	fmovm.l		&0x0,-(%sp)		# save no fpregs
# PROLOGUE END ##########################################################

	mov.w		%cc,DIV64_CC(%a6)
	st		POSNEG(%a6)		# signed operation
	bra.b		ldiv64_cont

##########
# divu.l #
##########
	global		_060LSP__idivu64_
_060LSP__idivu64_:
# PROLOGUE BEGIN ########################################################
	link.w		%a6,&-16
	movm.l		&0x3f00,-(%sp)		# save d2-d7
#	fmovm.l		&0x0,-(%sp)		# save no fpregs
# PROLOGUE END ##########################################################

	mov.w		%cc,DIV64_CC(%a6)
	sf		POSNEG(%a6)		# unsigned operation

ldiv64_cont:
	mov.l		0x8(%a6),%d7		# fetch divisor

	beq.w		ldiv64eq0		# divisor is = 0!!!

	mov.l		0xc(%a6), %d5		# get dividend hi
	mov.l		0x10(%a6), %d6		# get dividend lo

# separate signed and unsigned divide
	tst.b		POSNEG(%a6)		# signed or unsigned?
	beq.b		ldspecialcases		# use positive divide

# save the sign of the divisor
# make divisor unsigned if it's negative
	tst.l		%d7			# chk sign of divisor
	slt		NDIVISOR(%a6)		# save sign of divisor
	bpl.b		ldsgndividend
	neg.l		%d7			# complement negative divisor

# save the sign of the dividend
# make dividend unsigned if it's negative
ldsgndividend:
	tst.l		%d5			# chk sign of hi(dividend)
	slt		NDIVIDEND(%a6)		# save sign of dividend
	bpl.b		ldspecialcases

	mov.w		&0x0, %cc		# clear 'X' cc bit
	negx.l		%d6			# complement signed dividend
	negx.l		%d5

# extract some special cases:
#	- is (dividend == 0) ?
#	- is (hi(dividend) == 0 && (divisor <= lo(dividend))) ? (32-bit div)
ldspecialcases:
	tst.l		%d5			# is (hi(dividend) == 0)
	bne.b		ldnormaldivide		# no, so try it the long way

	tst.l		%d6			# is (lo(dividend) == 0), too
	beq.w		lddone			# yes, so (dividend == 0)

	cmp.l		%d7,%d6			# is (divisor <= lo(dividend))
	bls.b		ld32bitdivide		# yes, so use 32 bit divide

	exg		%d5,%d6			# q = 0, r = dividend
	bra.w		ldivfinish		# can't divide, we're done.

ld32bitdivide:
	tdivu.l		%d7, %d5:%d6		# it's only a 32/32 bit div!

	bra.b		ldivfinish

ldnormaldivide:
# last special case:
#	- is hi(dividend) >= divisor ? if yes, then overflow
	cmp.l		%d7,%d5
	bls.b		lddovf			# answer won't fit in 32 bits

# perform the divide algorithm:
	bsr.l		ldclassical		# do int divide

# separate into signed and unsigned finishes.
ldivfinish:
	tst.b		POSNEG(%a6)		# do divs, divu separately
	beq.b		lddone			# divu has no processing!!!

# it was a divs.l, so ccode setting is a little more complicated...
	tst.b		NDIVIDEND(%a6)		# remainder has same sign
	beq.b		ldcc			# as dividend.
	neg.l		%d5			# sgn(rem) = sgn(dividend)
ldcc:
	mov.b		NDIVISOR(%a6), %d0
	eor.b		%d0, NDIVIDEND(%a6)	# chk if quotient is negative
	beq.b		ldqpos			# branch to quot positive

# 0x80000000 is the largest number representable as a 32-bit negative
# number. the negative of 0x80000000 is 0x80000000.
	cmpi.l		%d6, &0x80000000	# will (-quot) fit in 32 bits?
	bhi.b		lddovf

	neg.l		%d6			# make (-quot) 2's comp

	bra.b		lddone

ldqpos:
	btst		&0x1f, %d6		# will (+quot) fit in 32 bits?
	bne.b		lddovf

lddone:
# if the register numbers are the same, only the quotient gets saved.
# so, if we always save the quotient second, we save ourselves a cmp&beq
	andi.w		&0x10,DIV64_CC(%a6)
	mov.w		DIV64_CC(%a6),%cc
	tst.l		%d6			# may set 'N' ccode bit

# here, the result is in d1 and d0. the current strategy is to save
# the values at the location pointed to by a0.
# use movm here to not disturb the condition codes.
ldexit:
	movm.l		&0x0060,([0x14,%a6])	# save result

# EPILOGUE BEGIN ########################################################
#	fmovm.l		(%sp)+,&0x0		# restore no fpregs
	movm.l		(%sp)+,&0x00fc		# restore d2-d7
	unlk		%a6
# EPILOGUE END ##########################################################

	rts

# the result should be the unchanged dividend
lddovf:
	mov.l		0xc(%a6), %d5		# get dividend hi
	mov.l		0x10(%a6), %d6		# get dividend lo

	andi.w		&0x1c,DIV64_CC(%a6)
	ori.w		&0x02,DIV64_CC(%a6)	# set 'V' ccode bit
	mov.w		DIV64_CC(%a6),%cc

	bra.b		ldexit

ldiv64eq0:
	mov.l		0xc(%a6),([0x14,%a6])
	mov.l		0x10(%a6),([0x14,%a6],0x4)

	mov.w		DIV64_CC(%a6),%cc

# EPILOGUE BEGIN ########################################################
#	fmovm.l		(%sp)+,&0x0		# restore no fpregs
	movm.l		(%sp)+,&0x00fc		# restore d2-d7
	unlk		%a6
# EPILOGUE END ##########################################################

	divu.w		&0x0,%d0		# force a divbyzero exception
	rts

###########################################################################
#########################################################################
# This routine uses the 'classical' Algorithm D from Donald Knuth's	#
# Art of Computer Programming, vol II, Seminumerical Algorithms.	#
# For this implementation b=2**16, and the target is U1U2U3U4/V1V2,	#
# where U,V are words of the quadword dividend and longword divisor,	#
# and U1, V1 are the most significant words.				#
#									#
# The most sig. longword of the 64 bit dividend must be in %d5, least	#
# in %d6. The divisor must be in the variable ddivisor, and the		#
# signed/unsigned flag ddusign must be set (0=unsigned,1=signed).	#
# The quotient is returned in %d6, remainder in %d5, unless the		#
# v (overflow) bit is set in the saved %ccr. If overflow, the dividend	#
# is unchanged.								#
#########################################################################
ldclassical:
# if the divisor msw is 0, use simpler algorithm then the full blown
# one at ddknuth:

	cmpi.l		%d7, &0xffff
	bhi.b		lddknuth		# go use D. Knuth algorithm

# Since the divisor is only a word (and larger than the mslw of the dividend),
# a simpler algorithm may be used :
# In the general case, four quotient words would be created by
# dividing the divisor word into each dividend word. In this case,
# the first two quotient words must be zero, or overflow would occur.
# Since we already checked this case above, we can treat the most significant
# longword of the dividend as (0) remainder (see Knuth) and merely complete
# the last two divisions to get a quotient longword and word remainder:

	clr.l		%d1
	swap		%d5			# same as r*b if previous step rqd
	swap		%d6			# get u3 to lsw position
	mov.w		%d6, %d5		# rb + u3

	divu.w		%d7, %d5

	mov.w		%d5, %d1		# first quotient word
	swap		%d6			# get u4
	mov.w		%d6, %d5		# rb + u4

	divu.w		%d7, %d5

	swap		%d1
	mov.w		%d5, %d1		# 2nd quotient 'digit'
	clr.w		%d5
	swap		%d5			# now remainder
	mov.l		%d1, %d6		# and quotient

	rts

lddknuth:
# In this algorithm, the divisor is treated as a 2 digit (word) number
# which is divided into a 3 digit (word) dividend to get one quotient
# digit (word). After subtraction, the dividend is shifted and the
# process repeated. Before beginning, the divisor and quotient are
# 'normalized' so that the process of estimating the quotient digit
# will yield verifiably correct results..

	clr.l		DDNORMAL(%a6)		# count of shifts for normalization
	clr.b		DDSECOND(%a6)		# clear flag for quotient digits
	clr.l		%d1			# %d1 will hold trial quotient
lddnchk:
	btst		&31, %d7		# must we normalize? first word of
	bne.b		lddnormalized		# divisor (V1) must be >= 65536/2
	addq.l		&0x1, DDNORMAL(%a6)	# count normalization shifts
	lsl.l		&0x1, %d7		# shift the divisor
	lsl.l		&0x1, %d6		# shift u4,u3 with overflow to u2
	roxl.l		&0x1, %d5		# shift u1,u2
	bra.w		lddnchk
lddnormalized:

# Now calculate an estimate of the quotient words (msw first, then lsw).
# The comments use subscripts for the first quotient digit determination.
	mov.l		%d7, %d3		# divisor
	mov.l		%d5, %d2		# dividend mslw
	swap		%d2
	swap		%d3
	cmp.w		%d2, %d3		# V1 = U1 ?
	bne.b		lddqcalc1
	mov.w		&0xffff, %d1		# use max trial quotient word
	bra.b		lddadj0
lddqcalc1:
	mov.l		%d5, %d1

	divu.w		%d3, %d1		# use quotient of mslw/msw

	andi.l		&0x0000ffff, %d1	# zero any remainder
lddadj0:

# now test the trial quotient and adjust. This step plus the
# normalization assures (according to Knuth) that the trial
# quotient will be at worst 1 too large.
	mov.l		%d6, -(%sp)
	clr.w		%d6			# word u3 left
	swap		%d6			# in lsw position
lddadj1: mov.l		%d7, %d3
	mov.l		%d1, %d2
	mulu.w		%d7, %d2		# V2q
	swap		%d3
	mulu.w		%d1, %d3		# V1q
	mov.l		%d5, %d4		# U1U2
	sub.l		%d3, %d4		# U1U2 - V1q

	swap		%d4

	mov.w		%d4,%d0
	mov.w		%d6,%d4			# insert lower word (U3)

	tst.w		%d0			# is upper word set?
	bne.w		lddadjd1

#	add.l		%d6, %d4		# (U1U2 - V1q) + U3

	cmp.l		%d2, %d4
	bls.b		lddadjd1		# is V2q > (U1U2-V1q) + U3 ?
	subq.l		&0x1, %d1		# yes, decrement and recheck
	bra.b		lddadj1
lddadjd1:
# now test the word by multiplying it by the divisor (V1V2) and comparing
# the 3 digit (word) result with the current dividend words
	mov.l		%d5, -(%sp)		# save %d5 (%d6 already saved)
	mov.l		%d1, %d6
	swap		%d6			# shift answer to ms 3 words
	mov.l		%d7, %d5
	bsr.l		ldmm2
	mov.l		%d5, %d2		# now %d2,%d3 are trial*divisor
	mov.l		%d6, %d3
	mov.l		(%sp)+, %d5		# restore dividend
	mov.l		(%sp)+, %d6
	sub.l		%d3, %d6
	subx.l		%d2, %d5		# subtract double precision
	bcc		ldd2nd			# no carry, do next quotient digit
	subq.l		&0x1, %d1		# q is one too large
# need to add back divisor longword to current ms 3 digits of dividend
# - according to Knuth, this is done only 2 out of 65536 times for random
# divisor, dividend selection.
	clr.l		%d2
	mov.l		%d7, %d3
	swap		%d3
	clr.w		%d3			# %d3 now ls word of divisor
	add.l		%d3, %d6		# aligned with 3rd word of dividend
	addx.l		%d2, %d5
	mov.l		%d7, %d3
	clr.w		%d3			# %d3 now ms word of divisor
	swap		%d3			# aligned with 2nd word of dividend
	add.l		%d3, %d5
ldd2nd:
	tst.b		DDSECOND(%a6)	# both q words done?
	bne.b		lddremain
# first quotient digit now correct. store digit and shift the
# (subtracted) dividend
	mov.w		%d1, DDQUOTIENT(%a6)
	clr.l		%d1
	swap		%d5
	swap		%d6
	mov.w		%d6, %d5
	clr.w		%d6
	st		DDSECOND(%a6)		# second digit
	bra.w		lddnormalized
lddremain:
# add 2nd word to quotient, get the remainder.
	mov.w		%d1, DDQUOTIENT+2(%a6)
# shift down one word/digit to renormalize remainder.
	mov.w		%d5, %d6
	swap		%d6
	swap		%d5
	mov.l		DDNORMAL(%a6), %d7	# get norm shift count
	beq.b		lddrn
	subq.l		&0x1, %d7		# set for loop count
lddnlp:
	lsr.l		&0x1, %d5		# shift into %d6
	roxr.l		&0x1, %d6
	dbf		%d7, lddnlp
lddrn:
	mov.l		%d6, %d5		# remainder
	mov.l		DDQUOTIENT(%a6), %d6	# quotient

	rts
ldmm2:
# factors for the 32X32->64 multiplication are in %d5 and %d6.
# returns 64 bit result in %d5 (hi) %d6(lo).
# destroys %d2,%d3,%d4.

# multiply hi,lo words of each factor to get 4 intermediate products
	mov.l		%d6, %d2
	mov.l		%d6, %d3
	mov.l		%d5, %d4
	swap		%d3
	swap		%d4
	mulu.w		%d5, %d6		# %d6 <- lsw*lsw
	mulu.w		%d3, %d5		# %d5 <- msw-dest*lsw-source
	mulu.w		%d4, %d2		# %d2 <- msw-source*lsw-dest
	mulu.w		%d4, %d3		# %d3 <- msw*msw
# now use swap and addx to consolidate to two longwords
	clr.l		%d4
	swap		%d6
	add.w		%d5, %d6		# add msw of l*l to lsw of m*l product
	addx.w		%d4, %d3		# add any carry to m*m product
	add.w		%d2, %d6		# add in lsw of other m*l product
	addx.w		%d4, %d3		# add any carry to m*m product
	swap		%d6			# %d6 is low 32 bits of final product
	clr.w		%d5
	clr.w		%d2			# lsw of two mixed products used,
	swap		%d5			# now use msws of longwords
	swap		%d2
	add.l		%d2, %d5
	add.l		%d3, %d5	# %d5 now ms 32 bits of final product
	rts

#########################################################################
# XDEF ****************************************************************	#
#	_060LSP__imulu64_(): Emulate 64-bit unsigned mul instruction	#
#	_060LSP__imuls64_(): Emulate 64-bit signed mul instruction.	#
#									#
#	This is the library version which is accessed as a subroutine	#
#	and therefore does not work exactly like the 680X0 mul{s,u}.l	#
#	64-bit multiply instruction.					#
#									#
# XREF ****************************************************************	#
#	None								#
#									#
# INPUT ***************************************************************	#
#	0x4(sp) = multiplier						#
#	0x8(sp) = multiplicand						#
#	0xc(sp) = pointer to location to place 64-bit result		#
#									#
# OUTPUT **************************************************************	#
#	0xc(sp) = points to location of 64-bit result			#
#									#
# ALGORITHM ***********************************************************	#
#	Perform the multiply in pieces using 16x16->32 unsigned		#
# multiplies and "add" instructions.					#
#	Set the condition codes as appropriate before performing an	#
# "rts".								#
#									#
#########################################################################

set MUL64_CC, -4

	global		_060LSP__imulu64_
_060LSP__imulu64_:

# PROLOGUE BEGIN ########################################################
	link.w		%a6,&-4
	movm.l		&0x3800,-(%sp)		# save d2-d4
#	fmovm.l		&0x0,-(%sp)		# save no fpregs
# PROLOGUE END ##########################################################

	mov.w		%cc,MUL64_CC(%a6)	# save incoming ccodes

	mov.l		0x8(%a6),%d0		# store multiplier in d0
	beq.w		mulu64_zero		# handle zero separately

	mov.l		0xc(%a6),%d1		# get multiplicand in d1
	beq.w		mulu64_zero		# handle zero separately

#########################################################################
#	63			   32				0	#
#	----------------------------					#
#	| hi(mplier) * hi(mplicand)|					#
#	----------------------------					#
#		     -----------------------------			#
#		     | hi(mplier) * lo(mplicand) |			#
#		     -----------------------------			#
#		     -----------------------------			#
#		     | lo(mplier) * hi(mplicand) |			#
#		     -----------------------------			#
#	  |			   -----------------------------	#
#	--|--			   | lo(mplier) * lo(mplicand) |	#
#	  |			   -----------------------------	#
#	========================================================	#
#	--------------------------------------------------------	#
#	|	hi(result)	   |	    lo(result)         |	#
#	--------------------------------------------------------	#
#########################################################################
mulu64_alg:
# load temp registers with operands
	mov.l		%d0,%d2			# mr in d2
	mov.l		%d0,%d3			# mr in d3
	mov.l		%d1,%d4			# md in d4
	swap		%d3			# hi(mr) in lo d3
	swap		%d4			# hi(md) in lo d4

# complete necessary multiplies:
	mulu.w		%d1,%d0			# [1] lo(mr) * lo(md)
	mulu.w		%d3,%d1			# [2] hi(mr) * lo(md)
	mulu.w		%d4,%d2			# [3] lo(mr) * hi(md)
	mulu.w		%d4,%d3			# [4] hi(mr) * hi(md)

# add lo portions of [2],[3] to hi portion of [1].
# add carries produced from these adds to [4].
# lo([1]) is the final lo 16 bits of the result.
	clr.l		%d4			# load d4 w/ zero value
	swap		%d0			# hi([1]) <==> lo([1])
	add.w		%d1,%d0			# hi([1]) + lo([2])
	addx.l		%d4,%d3			#    [4]  + carry
	add.w		%d2,%d0			# hi([1]) + lo([3])
	addx.l		%d4,%d3			#    [4]  + carry
	swap		%d0			# lo([1]) <==> hi([1])

# lo portions of [2],[3] have been added in to final result.
# now, clear lo, put hi in lo reg, and add to [4]
	clr.w		%d1			# clear lo([2])
	clr.w		%d2			# clear hi([3])
	swap		%d1			# hi([2]) in lo d1
	swap		%d2			# hi([3]) in lo d2
	add.l		%d2,%d1			#    [4]  + hi([2])
	add.l		%d3,%d1			#    [4]  + hi([3])

# now, grab the condition codes. only one that can be set is 'N'.
# 'N' CAN be set if the operation is unsigned if bit 63 is set.
	mov.w		MUL64_CC(%a6),%d4
	andi.b		&0x10,%d4		# keep old 'X' bit
	tst.l		%d1			# may set 'N' bit
	bpl.b		mulu64_ddone
	ori.b		&0x8,%d4		# set 'N' bit
mulu64_ddone:
	mov.w		%d4,%cc

# here, the result is in d1 and d0. the current strategy is to save
# the values at the location pointed to by a0.
# use movm here to not disturb the condition codes.
mulu64_end:
	exg		%d1,%d0
	movm.l		&0x0003,([0x10,%a6])		# save result

# EPILOGUE BEGIN ########################################################
#	fmovm.l		(%sp)+,&0x0		# restore no fpregs
	movm.l		(%sp)+,&0x001c		# restore d2-d4
	unlk		%a6
# EPILOGUE END ##########################################################

	rts

# one or both of the operands is zero so the result is also zero.
# save the zero result to the register file and set the 'Z' ccode bit.
mulu64_zero:
	clr.l		%d0
	clr.l		%d1

	mov.w		MUL64_CC(%a6),%d4
	andi.b		&0x10,%d4
	ori.b		&0x4,%d4
	mov.w		%d4,%cc			# set 'Z' ccode bit

	bra.b		mulu64_end

##########
# muls.l #
##########
	global		_060LSP__imuls64_
_060LSP__imuls64_:

# PROLOGUE BEGIN ########################################################
	link.w		%a6,&-4
	movm.l		&0x3c00,-(%sp)		# save d2-d5
#	fmovm.l		&0x0,-(%sp)		# save no fpregs
# PROLOGUE END ##########################################################

	mov.w		%cc,MUL64_CC(%a6)	# save incoming ccodes

	mov.l		0x8(%a6),%d0		# store multiplier in d0
	beq.b		mulu64_zero		# handle zero separately

	mov.l		0xc(%a6),%d1		# get multiplicand in d1
	beq.b		mulu64_zero		# handle zero separately

	clr.b		%d5			# clear sign tag
	tst.l		%d0			# is multiplier negative?
	bge.b		muls64_chk_md_sgn	# no
	neg.l		%d0			# make multiplier positive

	ori.b		&0x1,%d5		# save multiplier sgn

# the result sign is the exclusive or of the operand sign bits.
muls64_chk_md_sgn:
	tst.l		%d1			# is multiplicand negative?
	bge.b		muls64_alg		# no
	neg.l		%d1			# make multiplicand positive

	eori.b		&0x1,%d5		# calculate correct sign

#########################################################################
#	63			   32				0	#
#	----------------------------					#
#	| hi(mplier) * hi(mplicand)|					#
#	----------------------------					#
#		     -----------------------------			#
#		     | hi(mplier) * lo(mplicand) |			#
#		     -----------------------------			#
#		     -----------------------------			#
#		     | lo(mplier) * hi(mplicand) |			#
#		     -----------------------------			#
#	  |			   -----------------------------	#
#	--|--			   | lo(mplier) * lo(mplicand) |	#
#	  |			   -----------------------------	#
#	========================================================	#
#	--------------------------------------------------------	#
#	|	hi(result)	   |	    lo(result)         |	#
#	--------------------------------------------------------	#
#########################################################################
muls64_alg:
# load temp registers with operands
	mov.l		%d0,%d2			# mr in d2
	mov.l		%d0,%d3			# mr in d3
	mov.l		%d1,%d4			# md in d4
	swap		%d3			# hi(mr) in lo d3
	swap		%d4			# hi(md) in lo d4

# complete necessary multiplies:
	mulu.w		%d1,%d0			# [1] lo(mr) * lo(md)
	mulu.w		%d3,%d1			# [2] hi(mr) * lo(md)
	mulu.w		%d4,%d2			# [3] lo(mr) * hi(md)
	mulu.w		%d4,%d3			# [4] hi(mr) * hi(md)

# add lo portions of [2],[3] to hi portion of [1].
# add carries produced from these adds to [4].
# lo([1]) is the final lo 16 bits of the result.
	clr.l		%d4			# load d4 w/ zero value
	swap		%d0			# hi([1]) <==> lo([1])
	add.w		%d1,%d0			# hi([1]) + lo([2])
	addx.l		%d4,%d3			#    [4]  + carry
	add.w		%d2,%d0			# hi([1]) + lo([3])
	addx.l		%d4,%d3			#    [4]  + carry
	swap		%d0			# lo([1]) <==> hi([1])

# lo portions of [2],[3] have been added in to final result.
# now, clear lo, put hi in lo reg, and add to [4]
	clr.w		%d1			# clear lo([2])
	clr.w		%d2			# clear hi([3])
	swap		%d1			# hi([2]) in lo d1
	swap		%d2			# hi([3]) in lo d2
	add.l		%d2,%d1			#    [4]  + hi([2])
	add.l		%d3,%d1			#    [4]  + hi([3])

	tst.b		%d5			# should result be signed?
	beq.b		muls64_done		# no

# result should be a signed negative number.
# compute 2's complement of the unsigned number:
#   -negate all bits and add 1
muls64_neg:
	not.l		%d0			# negate lo(result) bits
	not.l		%d1			# negate hi(result) bits
	addq.l		&1,%d0			# add 1 to lo(result)
	addx.l		%d4,%d1			# add carry to hi(result)

muls64_done:
	mov.w		MUL64_CC(%a6),%d4
	andi.b		&0x10,%d4		# keep old 'X' bit
	tst.l		%d1			# may set 'N' bit
	bpl.b		muls64_ddone
	ori.b		&0x8,%d4		# set 'N' bit
muls64_ddone:
	mov.w		%d4,%cc

# here, the result is in d1 and d0. the current strategy is to save
# the values at the location pointed to by a0.
# use movm here to not disturb the condition codes.
muls64_end:
	exg		%d1,%d0
	movm.l		&0x0003,([0x10,%a6])	# save result at (a0)

# EPILOGUE BEGIN ########################################################
#	fmovm.l		(%sp)+,&0x0		# restore no fpregs
	movm.l		(%sp)+,&0x003c		# restore d2-d5
	unlk		%a6
# EPILOGUE END ##########################################################

	rts

# one or both of the operands is zero so the result is also zero.
# save the zero result to the register file and set the 'Z' ccode bit.
muls64_zero:
	clr.l		%d0
	clr.l		%d1

	mov.w		MUL64_CC(%a6),%d4
	andi.b		&0x10,%d4
	ori.b		&0x4,%d4
	mov.w		%d4,%cc			# set 'Z' ccode bit

	bra.b		muls64_end

#########################################################################
# XDEF ****************************************************************	#
#	_060LSP__cmp2_Ab_(): Emulate "cmp2.b An,<ea>".			#
#	_060LSP__cmp2_Aw_(): Emulate "cmp2.w An,<ea>".			#
#	_060LSP__cmp2_Al_(): Emulate "cmp2.l An,<ea>".			#
#	_060LSP__cmp2_Db_(): Emulate "cmp2.b Dn,<ea>".			#
#	_060LSP__cmp2_Dw_(): Emulate "cmp2.w Dn,<ea>".			#
#	_060LSP__cmp2_Dl_(): Emulate "cmp2.l Dn,<ea>".			#
#									#
#	This is the library version which is accessed as a subroutine	#
#	and therefore does not work exactly like the 680X0 "cmp2"	#
#	instruction.							#
#									#
# XREF ****************************************************************	#
#	None								#
#									#
# INPUT ***************************************************************	#
#	0x4(sp) = Rn							#
#	0x8(sp) = pointer to boundary pair				#
#									#
# OUTPUT **************************************************************	#
#	cc = condition codes are set correctly				#
#									#
# ALGORITHM ***********************************************************	#
#	In the interest of simplicity, all operands are converted to	#
# longword size whether the operation is byte, word, or long. The	#
# bounds are sign extended accordingly. If Rn is a data regsiter, Rn is #
# also sign extended. If Rn is an address register, it need not be sign #
# extended since the full register is always used.			#
#	The condition codes are set correctly before the final "rts".	#
#									#
#########################################################################

set	CMP2_CC,	-4

	global		_060LSP__cmp2_Ab_
_060LSP__cmp2_Ab_:

# PROLOGUE BEGIN ########################################################
	link.w		%a6,&-4
	movm.l		&0x3800,-(%sp)		# save d2-d4
#	fmovm.l		&0x0,-(%sp)		# save no fpregs
# PROLOGUE END ##########################################################

	mov.w		%cc,CMP2_CC(%a6)
	mov.l		0x8(%a6), %d2		# get regval

	mov.b		([0xc,%a6],0x0),%d0
	mov.b		([0xc,%a6],0x1),%d1

	extb.l		%d0			# sign extend lo bnd
	extb.l		%d1			# sign extend hi bnd
	bra.w		l_cmp2_cmp		# go do the compare emulation

	global		_060LSP__cmp2_Aw_
_060LSP__cmp2_Aw_:

# PROLOGUE BEGIN ########################################################
	link.w		%a6,&-4
	movm.l		&0x3800,-(%sp)		# save d2-d4
#	fmovm.l		&0x0,-(%sp)		# save no fpregs
# PROLOGUE END ##########################################################

	mov.w		%cc,CMP2_CC(%a6)
	mov.l		0x8(%a6), %d2		# get regval

	mov.w		([0xc,%a6],0x0),%d0
	mov.w		([0xc,%a6],0x2),%d1

	ext.l		%d0			# sign extend lo bnd
	ext.l		%d1			# sign extend hi bnd
	bra.w		l_cmp2_cmp		# go do the compare emulation

	global		_060LSP__cmp2_Al_
_060LSP__cmp2_Al_:

# PROLOGUE BEGIN ########################################################
	link.w		%a6,&-4
	movm.l		&0x3800,-(%sp)		# save d2-d4
#	fmovm.l		&0x0,-(%sp)		# save no fpregs
# PROLOGUE END ##########################################################

	mov.w		%cc,CMP2_CC(%a6)
	mov.l		0x8(%a6), %d2		# get regval

	mov.l		([0xc,%a6],0x0),%d0
	mov.l		([0xc,%a6],0x4),%d1
	bra.w		l_cmp2_cmp		# go do the compare emulation

	global		_060LSP__cmp2_Db_
_060LSP__cmp2_Db_:

# PROLOGUE BEGIN ########################################################
	link.w		%a6,&-4
	movm.l		&0x3800,-(%sp)		# save d2-d4
#	fmovm.l		&0x0,-(%sp)		# save no fpregs
# PROLOGUE END ##########################################################

	mov.w		%cc,CMP2_CC(%a6)
	mov.l		0x8(%a6), %d2		# get regval

	mov.b		([0xc,%a6],0x0),%d0
	mov.b		([0xc,%a6],0x1),%d1

	extb.l		%d0			# sign extend lo bnd
	extb.l		%d1			# sign extend hi bnd

# operation is a data register compare.
# sign extend byte to long so we can do simple longword compares.
	extb.l		%d2			# sign extend data byte
	bra.w		l_cmp2_cmp		# go do the compare emulation

	global		_060LSP__cmp2_Dw_
_060LSP__cmp2_Dw_:

# PROLOGUE BEGIN ########################################################
	link.w		%a6,&-4
	movm.l		&0x3800,-(%sp)		# save d2-d4
#	fmovm.l		&0x0,-(%sp)		# save no fpregs
# PROLOGUE END ##########################################################

	mov.w		%cc,CMP2_CC(%a6)
	mov.l		0x8(%a6), %d2		# get regval

	mov.w		([0xc,%a6],0x0),%d0
	mov.w		([0xc,%a6],0x2),%d1

	ext.l		%d0			# sign extend lo bnd
	ext.l		%d1			# sign extend hi bnd

# operation is a data register compare.
# sign extend word to long so we can do simple longword compares.
	ext.l		%d2			# sign extend data word
	bra.w		l_cmp2_cmp		# go emulate compare

	global		_060LSP__cmp2_Dl_
_060LSP__cmp2_Dl_:

# PROLOGUE BEGIN ########################################################
	link.w		%a6,&-4
	movm.l		&0x3800,-(%sp)		# save d2-d4
#	fmovm.l		&0x0,-(%sp)		# save no fpregs
# PROLOGUE END ##########################################################

	mov.w		%cc,CMP2_CC(%a6)
	mov.l		0x8(%a6), %d2		# get regval

	mov.l		([0xc,%a6],0x0),%d0
	mov.l		([0xc,%a6],0x4),%d1

#
# To set the ccodes correctly:
#	(1) save 'Z' bit from (Rn - lo)
#	(2) save 'Z' and 'N' bits from ((hi - lo) - (Rn - hi))
#	(3) keep 'X', 'N', and 'V' from before instruction
#	(4) combine ccodes
#
l_cmp2_cmp:
	sub.l		%d0, %d2		# (Rn - lo)
	mov.w		%cc, %d3		# fetch resulting ccodes
	andi.b		&0x4, %d3		# keep 'Z' bit
	sub.l		%d0, %d1		# (hi - lo)
	cmp.l		%d1,%d2			# ((hi - lo) - (Rn - hi))

	mov.w		%cc, %d4		# fetch resulting ccodes
	or.b		%d4, %d3		# combine w/ earlier ccodes
	andi.b		&0x5, %d3		# keep 'Z' and 'N'

	mov.w		CMP2_CC(%a6), %d4	# fetch old ccodes
	andi.b		&0x1a, %d4		# keep 'X','N','V' bits
	or.b		%d3, %d4		# insert new ccodes
	mov.w		%d4,%cc			# save new ccodes

# EPILOGUE BEGIN ########################################################
#	fmovm.l		(%sp)+,&0x0		# restore no fpregs
	movm.l		(%sp)+,&0x001c		# restore d2-d4
	unlk		%a6
# EPILOGUE END ##########################################################

	rts
