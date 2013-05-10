#include <linux/linkage.h>
#include <asm/assembler.h>
/*
 * Function: nommu_early_abort
 *
 * Params  : r2 = address of aborted instruction
 *         : r3 = saved SPSR
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
