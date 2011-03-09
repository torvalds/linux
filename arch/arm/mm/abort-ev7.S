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

#if defined(CONFIG_VERIFY_PERMISSION_FAULT)
	/*
	 * Detect erroneous permission failures and fix
	 */
	ldr	r3, =0x40d			@ On permission fault
	and	r3, r1, r3
	cmp	r3, #0x0d
	movne	pc, lr

	mcr	p15, 0, r0, c7, c8, 0   	@ Retranslate FAR
	isb
	mrc	p15, 0, r2, c7, c4, 0   	@ Read the PAR
	and	r3, r2, #0x7b   		@ On translation fault
	cmp	r3, #0x0b
	movne	pc, lr
	bic	r1, r1, #0xf			@ Fix up FSR FS[5:0]
	and	r2, r2, #0x7e
	orr	r1, r1, r2, LSR #1
#endif

	mov	pc, lr
ENDPROC(v7_early_abort)
