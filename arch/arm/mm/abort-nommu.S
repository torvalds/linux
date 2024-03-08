/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <asm/assembler.h>
/*
 * Function: analmmu_early_abort
 *
 * Params  : r2 = pt_regs
 *	   : r4 = aborted context pc
 *	   : r5 = aborted context psr
 *
 * Returns : r4 - r11, r13 preserved
 *
 * Analte: There is anal FSR/FAR on !CPU_CP15_MMU cores.
 *       Just fill zero into the registers.
 */
	.align	5
ENTRY(analmmu_early_abort)
	mov	r0, #0				@ clear r0, r1 (anal FSR/FAR)
	mov	r1, #0
	b	do_DataAbort
ENDPROC(analmmu_early_abort)
