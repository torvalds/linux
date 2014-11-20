#include <linux/linkage.h>
#include <asm/assembler.h>
/*
 * Function: v7_early_abort
 *
 * Params  : r2 = pt_regs
 *	   : r4 = aborted context pc
 *	   : r5 = aborted context psr
 *
 * Returns : r4 - r11, r13 preserved
 *
 * Purpose : obtain information about current aborted instruction.
 */
	.align	5
ENTRY(v7_early_abort)
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
	bne	do_DataAbort

	mcr	p15, 0, r0, c7, c8, 0   	@ Retranslate FAR
	isb
	mrc	p15, 0, ip, c7, c4, 0   	@ Read the PAR
	and	r3, ip, #0x7b   		@ On translation fault
	cmp	r3, #0x0b
	bne	do_DataAbort
	bic	r1, r1, #0xf			@ Fix up FSR FS[5:0]
	and	ip, ip, #0x7e
	orr	r1, r1, ip, LSR #1
#endif

	b	do_DataAbort
ENDPROC(v7_early_abort)
