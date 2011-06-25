#include <linux/linkage.h>
#include <asm/assembler.h>

/*
 * Function: v6_pabort
 *
 * Params  : r4 = address of aborted instruction
 *
 * Returns : r0 = address of abort
 *	   : r1 = IFSR
 *
 * Purpose : obtain information about current prefetch abort.
 */

	.align	5
ENTRY(v7_pabort)
	mrc	p15, 0, r0, c6, c0, 2		@ get IFAR
	mrc	p15, 0, r1, c5, c0, 1		@ get IFSR
	mov	pc, lr
ENDPROC(v7_pabort)
