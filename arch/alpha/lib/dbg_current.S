/*
 * arch/alpha/lib/dbg_current.S
 * Contributed by Richard Henderson (rth@cygnus.com)
 *
 * Trap if we find current not correct.
 */

#include <asm/pal.h>

	.text
	.set noat

	.globl _mcount
	.ent _mcount
_mcount:
	.frame $30, 0, $28, 0
	.prologue 0

	lda	$0, -0x4000($30)
	cmpult	$8, $30, $1
	cmpule	$0, $30, $2
	and	$1, $2, $3
	bne	$3, 1f

	call_pal PAL_bugchk

1:	ret	$31, ($28), 1

	.end _mcount
