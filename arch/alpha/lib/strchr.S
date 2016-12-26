/*
 * arch/alpha/lib/strchr.S
 * Contributed by Richard Henderson (rth@tamu.edu)
 *
 * Return the address of a given character within a null-terminated
 * string, or null if it is not found.
 */
#include <asm/export.h>
#include <asm/regdef.h>

	.set noreorder
	.set noat

	.align 3
	.globl strchr
	.ent strchr
strchr:
	.frame sp, 0, ra
	.prologue 0

	zapnot	a1, 1, a1	# e0    : zero extend the search character
	ldq_u   t0, 0(a0)	# .. e1 : load first quadword
	sll	a1, 8, t5	# e0    : replicate the search character
	andnot  a0, 7, v0	# .. e1 : align our loop pointer
	or	t5, a1, a1	# e0    :
	lda	t4, -1		# .. e1 : build garbage mask
	sll	a1, 16, t5	# e0    :
	cmpbge  zero, t0, t2	# .. e1 : bits set iff byte == zero
	mskqh	t4, a0, t4	# e0    :
	or	t5, a1, a1	# .. e1 :
	sll	a1, 32, t5	# e0    :
	cmpbge	zero, t4, t4	# .. e1 : bits set iff byte is garbage
	or	t5, a1, a1	# e0    :
	xor	t0, a1, t1	# .. e1 : make bytes == c zero
	cmpbge  zero, t1, t3	# e0    : bits set iff byte == c
	or	t2, t3, t0	# e1    : bits set iff char match or zero match
	andnot	t0, t4, t0	# e0    : clear garbage bits
	bne	t0, $found	# .. e1 (zdb)

$loop:	ldq	t0, 8(v0)	# e0    :
	addq	v0, 8, v0	# .. e1 :
	nop			# e0    :
	xor	t0, a1, t1	# .. e1 (ev5 data stall)
	cmpbge	zero, t0, t2	# e0    : bits set iff byte == 0
	cmpbge	zero, t1, t3	# .. e1 : bits set iff byte == c
	or	t2, t3, t0	# e0    :
	beq	t0, $loop	# .. e1 (zdb)

$found:	negq    t0, t1		# e0    : clear all but least set bit
	and     t0, t1, t0	# e1 (stall)

	and	t0, t3, t1	# e0    : bit set iff byte was the char
	beq	t1, $retnull	# .. e1 (zdb)

	and     t0, 0xf0, t2	# e0    : binary search for that set bit
	and	t0, 0xcc, t3	# .. e1 :
	and	t0, 0xaa, t4	# e0    :
	cmovne	t2, 4, t2	# .. e1 :
	cmovne	t3, 2, t3	# e0    :
	cmovne	t4, 1, t4	# .. e1 :
	addq	t2, t3, t2	# e0    :
	addq	v0, t4, v0	# .. e1 :
	addq	v0, t2, v0	# e0    :
	ret			# .. e1 :

$retnull:
	mov	zero, v0	# e0    :
	ret			# .. e1 :

	.end strchr
	EXPORT_SYMBOL(strchr)
