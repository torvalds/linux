#include <linux/linkage.h>
#include <asm/assembler.h>

/*
 * Function: v7_pabort
 *
 * Params  : r2 = pt_regs
 *	   : r4 = address of aborted instruction
 *	   : r5 = psr for parent context
 *
 * Returns : r4 - r11, r13 preserved
 *
 * Purpose : obtain information about current prefetch abort.
 */

	.align	5
ENTRY(v7_pabort)
	mrc	p15, 0, r0, c6, c0, 2		@ get IFAR
	mrc	p15, 0, r1, c5, c0, 1		@ get IFSR
	b	do_PrefetchAbort
ENDPROC(v7_pabort)
