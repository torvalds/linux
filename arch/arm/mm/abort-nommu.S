#include <linux/linkage.h>
#include <asm/assembler.h>
/*
 * Function: nommu_early_abort
 *
 * Params  : r4 = aborted context pc
 *	   : r5 = aborted context psr
 *
 * Returns : r0 = 0 (abort address)
 *	   : r1 = 0 (FSR)
 *
 * Note: There is no FSR/FAR on !CPU_CP15_MMU cores.
 *       Just fill zero into the registers.
 */
	.align	5
ENTRY(nommu_early_abort)
	mov	r0, #0				@ clear r0, r1 (no FSR/FAR)
	mov	r1, #0
	mov	pc, lr
ENDPROC(nommu_early_abort)
