#include <linux/linkage.h>
#include <asm/assembler.h>
/*
 * Function: v7_early_abort
 *
 * Params  : r2 = address of aborted instruction
 *         : r3 = saved SPSR
 *
 * Returns : r0 = address of abort
 *	   : r1 = FSR, bit 11 = write
 *	   : r2-r8 = corrupted
 *	   : r9 = preserved
 *	   : sp = pointer to registers
 *
 * Purpose : obtain information about current aborted instruction.
 */
	.align	5
ENTRY(v7_early_abort)
	/*
	 * The effect of data aborts on on the exclusive access monitor are
	 * UNPREDICTABLE. Do a CLREX to clear the state
	 */
	clrex

	mrc	p15, 0, r1, c5, c0, 0		@ get FSR
	mrc	p15, 0, r0, c6, c0, 0		@ get FAR

	/*
	 * V6 code adjusts the returned DFSR.
	 * New designs should not need to patch up faults.
	 */
	mov	pc, lr
ENDPROC(v7_early_abort)
