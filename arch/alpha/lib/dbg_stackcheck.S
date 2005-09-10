/*
 * arch/alpha/lib/stackcheck.S
 * Contributed by Richard Henderson (rth@tamu.edu)
 *
 * Verify that we have not overflowed the stack.  Oops if we have.
 */

#include <asm/asm-offsets.h>

	.text
	.set noat

	.align 3
	.globl _mcount
	.ent _mcount
_mcount:
	.frame $30, 0, $28, 0
	.prologue 0

	lda	$0, TASK_SIZE($8)
	cmpult	$30, $0, $0
	bne	$0, 1f
	ret	($28)
1:	stq	$31, -8($31)	# oops me, damn it.
	br	1b

	.end _mcount
