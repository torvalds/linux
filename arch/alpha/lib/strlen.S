/* SPDX-License-Identifier: GPL-2.0 */
/*
 * strlen.S (c) 1995 David Mosberger (davidm@cs.arizona.edu)
 *
 * Finds length of a 0-terminated string.  Optimized for the
 * Alpha architecture:
 *
 *	- memory accessed as aligned quadwords only
 *	- uses bcmpge to compare 8 bytes in parallel
 *	- does binary search to find 0 byte in last
 *	  quadword (HAKMEM needed 12 instructions to
 *	  do this instead of the 9 instructions that
 *	  binary search needs).
 */
#include <asm/export.h>
	.set noreorder
	.set noat

	.align 3

	.globl	strlen
	.ent	strlen

strlen:
	ldq_u	$1, 0($16)	# load first quadword ($16  may be misaligned)
	lda	$2, -1($31)
	insqh	$2, $16, $2
	andnot	$16, 7, $0
	or	$2, $1, $1
	cmpbge	$31, $1, $2	# $2  <- bitmask: bit i == 1 <==> i-th byte == 0
	bne	$2, found

loop:	ldq	$1, 8($0)
	addq	$0, 8, $0	# addr += 8
	nop			# helps dual issue last two insns
	cmpbge	$31, $1, $2
	beq	$2, loop

found:	blbs	$2, done	# make aligned case fast
	negq	$2, $3
	and	$2, $3, $2

	and	$2, 0x0f, $1
	addq	$0, 4, $3
	cmoveq	$1, $3, $0

	and	$2, 0x33, $1
	addq	$0, 2, $3
	cmoveq	$1, $3, $0

	and	$2, 0x55, $1
	addq	$0, 1, $3
	cmoveq	$1, $3, $0

done:	subq	$0, $16, $0
	ret	$31, ($26)

	.end	strlen
	EXPORT_SYMBOL(strlen)
