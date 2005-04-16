/*
 * arch/alpha/lib/ev67-strrchr.S
 * 21264 version by Rick Gorton <rick.gorton@alpha-processor.com>
 *
 * Finds length of a 0-terminated string.  Optimized for the
 * Alpha architecture:
 *
 *	- memory accessed as aligned quadwords only
 *	- uses bcmpge to compare 8 bytes in parallel
 *
 * Much of the information about 21264 scheduling/coding comes from:
 *	Compiler Writer's Guide for the Alpha 21264
 *	abbreviated as 'CWG' in other comments here
 *	ftp.digital.com/pub/Digital/info/semiconductor/literature/dsc-library.html
 * Scheduling notation:
 *	E	- either cluster
 *	U	- upper subcluster; U0 - subcluster U0; U1 - subcluster U1
 *	L	- lower subcluster; L0 - subcluster L0; L1 - subcluster L1
 */


#include <asm/regdef.h>

	.set noreorder
	.set noat

	.align 4
	.ent strrchr
	.globl strrchr
strrchr:
	.frame sp, 0, ra
	.prologue 0

	and	a1, 0xff, t2	# E : 00000000000000ch
	insbl	a1, 1, t4	# U : 000000000000ch00
	insbl	a1, 2, t5	# U : 0000000000ch0000
	ldq_u   t0, 0(a0)	# L : load first quadword Latency=3

	mov	zero, t6	# E : t6 is last match aligned addr
	or	t2, t4, a1	# E : 000000000000chch
	sll	t5, 8, t3	# U : 00000000ch000000
	mov	zero, t8	# E : t8 is last match byte compare mask

	andnot  a0, 7, v0	# E : align source addr
	or	t5, t3, t3	# E : 00000000chch0000
	sll	a1, 32, t2	# U : 0000chch00000000
	sll	a1, 48, t4	# U : chch000000000000

	or	t4, a1, a1	# E : chch00000000chch
	or	t2, t3, t2	# E : 0000chchchch0000
	or	a1, t2, a1	# E : chchchchchchchch
	lda	t5, -1		# E : build garbage mask

	cmpbge  zero, t0, t1	# E : bits set iff byte == zero
	mskqh	t5, a0, t4	# E : Complete garbage mask
	xor	t0, a1, t2	# E : make bytes == c zero
	cmpbge	zero, t4, t4	# E : bits set iff byte is garbage

	cmpbge  zero, t2, t3	# E : bits set iff byte == c
	andnot	t1, t4, t1	# E : clear garbage from null test
	andnot	t3, t4, t3	# E : clear garbage from char test
	bne	t1, $eos	# U : did we already hit the terminator?

	/* Character search main loop */
$loop:
	ldq	t0, 8(v0)	# L : load next quadword
	cmovne	t3, v0, t6	# E : save previous comparisons match
	nop			#   : Latency=2, extra map slot (keep nop with cmov)
	nop

	cmovne	t3, t3, t8	# E : Latency=2, extra map slot
	nop			#   : keep with cmovne
	addq	v0, 8, v0	# E :
	xor	t0, a1, t2	# E :

	cmpbge	zero, t0, t1	# E : bits set iff byte == zero
	cmpbge	zero, t2, t3	# E : bits set iff byte == c
	beq	t1, $loop	# U : if we havnt seen a null, loop
	nop

	/* Mask out character matches after terminator */
$eos:
	negq	t1, t4		# E : isolate first null byte match
	and	t1, t4, t4	# E :
	subq	t4, 1, t5	# E : build a mask of the bytes upto...
	or	t4, t5, t4	# E : ... and including the null

	and	t3, t4, t3	# E : mask out char matches after null
	cmovne	t3, t3, t8	# E : save it, if match found Latency=2, extra map slot
	nop			#   : Keep with cmovne
	nop

	cmovne	t3, v0, t6	# E :
	nop			#   : Keep with cmovne
	/* Locate the address of the last matched character */
	ctlz	t8, t2		# U0 : Latency=3 (0x40 for t8=0)
	nop

	cmoveq	t8, 0x3f, t2	# E : Compensate for case when no match is seen
	nop			# E : hide the cmov latency (2) behind ctlz latency
	lda	t5, 0x3f($31)	# E :
	subq	t5, t2, t5	# E : Normalize leading zero count

	addq	t6, t5, v0	# E : and add to quadword address
	ret			# L0 : Latency=3
	nop
	nop

	.end strrchr
