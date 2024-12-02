/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/alpha/lib/dbg_stackkill.S
 * Contributed by Richard Henderson (rth@cygnus.com)
 *
 * Clobber the balance of the kernel stack, hoping to catch
 * uninitialized local variables in the act.
 */

#include <asm/asm-offsets.h>

	.text
	.set noat

	.align 5
	.globl _mcount
	.ent _mcount
_mcount:
	.frame $30, 0, $28, 0
	.prologue 0

	ldi	$0, 0xdeadbeef
	lda	$2, -STACK_SIZE
	sll	$0, 32, $1
	and	$30, $2, $2
	or	$0, $1, $0
	lda	$2, TASK_SIZE($2)
	cmpult	$2, $30, $1
	beq	$1, 2f
1:	stq	$0, 0($2)
	addq	$2, 8, $2
	cmpult	$2, $30, $1
	bne	$1, 1b
2:	ret	($28)

	.end _mcount
