#include <linux/linkage.h>
#include <asm/assembler.h>

/*
 * Function: v6_pabort
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
ENTRY(v6_pabort)
	mov	r0, r4
	mrc	p15, 0, r1, c5, c0, 1		@ get IFSR
	b	do_PrefetchAbort
ENDPROC(v6_pabort)
