/*
 * arch/alpha/lib/ev67-strchr.S
 * 21264 version contributed by Rick Gorton <rick.gorton@alpha-processor.com>
 *
 * Return the address of a given character within a null-terminated
 * string, or null if it is not found.
 *
 * Much of the information about 21264 scheduling/coding comes from:
 *	Compiler Writer's Guide for the Alpha 21264
 *	abbreviated as 'CWG' in other comments here
 *	ftp.digital.com/pub/Digital/info/semiconductor/literature/dsc-library.html
 * Scheduling notation:
 *	E	- either cluster
 *	U	- upper subcluster; U0 - subcluster U0; U1 - subcluster U1
 *	L	- lower subcluster; L0 - subcluster L0; L1 - subcluster L1
 * Try not to change the actual algorithm if possible for consistency.
 */
#include <asm/export.h>
#include <asm/regdef.h>

	.set noreorder
	.set noat

	.align 4
	.globl strchr
	.ent strchr
strchr:
	.frame sp, 0, ra
	.prologue 0

	ldq_u   t0, 0(a0)	# L : load first quadword Latency=3
	and	a1, 0xff, t3	# E : 00000000000000ch
	insbl	a1, 1, t5	# U : 000000000000ch00
	insbl	a1, 7, a2	# U : ch00000000000000

	insbl	t3, 6, a3	# U : 00ch000000000000
	or	t5, t3, a1	# E : 000000000000chch
	andnot  a0, 7, v0	# E : align our loop pointer
	lda	t4, -1		# E : build garbage mask

	mskqh	t4, a0, t4	# U : only want relevant part of first quad
	or	a2, a3, a2	# E : chch000000000000
	inswl	a1, 2, t5	# E : 00000000chch0000
	inswl	a1, 4, a3	# E : 0000chch00000000

	or	a1, a2, a1	# E : chch00000000chch
	or	a3, t5, t5	# E : 0000chchchch0000
	cmpbge  zero, t0, t2	# E : bits set iff byte == zero
	cmpbge	zero, t4, t4	# E : bits set iff byte is garbage

	/* This quad is _very_ serialized.  Lots of stalling happens */
	or	t5, a1, a1	# E : chchchchchchchch
	xor	t0, a1, t1	# E : make bytes == c zero
	cmpbge  zero, t1, t3	# E : bits set iff byte == c
	or	t2, t3, t0	# E : bits set iff char match or zero match

	andnot	t0, t4, t0	# E : clear garbage bits
	cttz	t0, a2		# U0 : speculative (in case we get a match)
	nop			# E :
	bne	t0, $found	# U :

	/*
	 * Yuk.  This loop is going to stall like crazy waiting for the
	 * data to be loaded.  Not much can be done about it unless it's
	 * unrolled multiple times - is that safe to do in kernel space?
	 * Or would exception handling recovery code do the trick here?
	 */
$loop:	ldq	t0, 8(v0)	# L : Latency=3
	addq	v0, 8, v0	# E :
	xor	t0, a1, t1	# E :
	cmpbge	zero, t0, t2	# E : bits set iff byte == 0

	cmpbge	zero, t1, t3	# E : bits set iff byte == c
	or	t2, t3, t0	# E :
	cttz	t3, a2		# U0 : speculative (in case we get a match)
	beq	t0, $loop	# U :

$found:	negq    t0, t1		# E : clear all but least set bit
	and     t0, t1, t0	# E :
	and	t0, t3, t1	# E : bit set iff byte was the char
	addq	v0, a2, v0	# E : Add in the bit number from above

	cmoveq	t1, $31, v0	# E : Two mapping slots, latency = 2
	nop
	nop
	ret			# L0 :

	.end strchr
	EXPORT_SYMBOL(strchr)
