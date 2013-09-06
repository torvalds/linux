/*
 * Based on swsusp_32.S, modified for FSL BookE by
 * Anton Vorontsov <avorontsov@ru.mvista.com>
 * Copyright (c) 2009-2010 MontaVista Software, LLC.
 */

#include <linux/threads.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/cputable.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/mmu.h>

/*
 * Structure for storing CPU registers on the save area.
 */
#define SL_SP		0
#define SL_PC		4
#define SL_MSR		8
#define SL_TCR		0xc
#define SL_SPRG0	0x10
#define SL_SPRG1	0x14
#define SL_SPRG2	0x18
#define SL_SPRG3	0x1c
#define SL_SPRG4	0x20
#define SL_SPRG5	0x24
#define SL_SPRG6	0x28
#define SL_SPRG7	0x2c
#define SL_TBU		0x30
#define SL_TBL		0x34
#define SL_R2		0x38
#define SL_CR		0x3c
#define SL_LR		0x40
#define SL_R12		0x44	/* r12 to r31 */
#define SL_SIZE		(SL_R12 + 80)

	.section .data
	.align	5

_GLOBAL(swsusp_save_area)
	.space	SL_SIZE


	.section .text
	.align	5

_GLOBAL(swsusp_arch_suspend)
	lis	r11,swsusp_save_area@h
	ori	r11,r11,swsusp_save_area@l

	mflr	r0
	stw	r0,SL_LR(r11)
	mfcr	r0
	stw	r0,SL_CR(r11)
	stw	r1,SL_SP(r11)
	stw	r2,SL_R2(r11)
	stmw	r12,SL_R12(r11)

	/* Save MSR & TCR */
	mfmsr	r4
	stw	r4,SL_MSR(r11)
	mfspr	r4,SPRN_TCR
	stw	r4,SL_TCR(r11)

	/* Get a stable timebase and save it */
1:	mfspr	r4,SPRN_TBRU
	stw	r4,SL_TBU(r11)
	mfspr	r5,SPRN_TBRL
	stw	r5,SL_TBL(r11)
	mfspr	r3,SPRN_TBRU
	cmpw	r3,r4
	bne	1b

	/* Save SPRGs */
	mfsprg	r4,0
	stw	r4,SL_SPRG0(r11)
	mfsprg	r4,1
	stw	r4,SL_SPRG1(r11)
	mfsprg	r4,2
	stw	r4,SL_SPRG2(r11)
	mfsprg	r4,3
	stw	r4,SL_SPRG3(r11)
	mfsprg	r4,4
	stw	r4,SL_SPRG4(r11)
	mfsprg	r4,5
	stw	r4,SL_SPRG5(r11)
	mfsprg	r4,6
	stw	r4,SL_SPRG6(r11)
	mfsprg	r4,7
	stw	r4,SL_SPRG7(r11)

	/* Call the low level suspend stuff (we should probably have made
	 * a stackframe...
	 */
	bl	swsusp_save

	/* Restore LR from the save area */
	lis	r11,swsusp_save_area@h
	ori	r11,r11,swsusp_save_area@l
	lwz	r0,SL_LR(r11)
	mtlr	r0

	blr

_GLOBAL(swsusp_arch_resume)
	sync

	/* Load ptr the list of pages to copy in r3 */
	lis	r11,(restore_pblist)@h
	ori	r11,r11,restore_pblist@l
	lwz	r3,0(r11)

	/* Copy the pages. This is a very basic implementation, to
	 * be replaced by something more cache efficient */
1:
	li	r0,256
	mtctr	r0
	lwz	r5,pbe_address(r3)	/* source */
	lwz	r6,pbe_orig_address(r3)	/* destination */
2:
	lwz	r8,0(r5)
	lwz	r9,4(r5)
	lwz	r10,8(r5)
	lwz	r11,12(r5)
	addi	r5,r5,16
	stw	r8,0(r6)
	stw	r9,4(r6)
	stw	r10,8(r6)
	stw	r11,12(r6)
	addi	r6,r6,16
	bdnz	2b
	lwz	r3,pbe_next(r3)
	cmpwi	0,r3,0
	bne	1b

	bl flush_dcache_L1
	bl flush_instruction_cache

	lis	r11,swsusp_save_area@h
	ori	r11,r11,swsusp_save_area@l

	/*
	 * Mappings from virtual addresses to physical addresses may be
	 * different than they were prior to restoring hibernation state. 
	 * Invalidate the TLB so that the boot CPU is using the new
	 * mappings.
	 */
	bl	_tlbil_all

	lwz	r4,SL_SPRG0(r11)
	mtsprg	0,r4
	lwz	r4,SL_SPRG1(r11)
	mtsprg	1,r4
	lwz	r4,SL_SPRG2(r11)
	mtsprg	2,r4
	lwz	r4,SL_SPRG3(r11)
	mtsprg	3,r4
	lwz	r4,SL_SPRG4(r11)
	mtsprg	4,r4
	lwz	r4,SL_SPRG5(r11)
	mtsprg	5,r4
	lwz	r4,SL_SPRG6(r11)
	mtsprg	6,r4
	lwz	r4,SL_SPRG7(r11)
	mtsprg	7,r4

	/* restore the MSR */
	lwz	r3,SL_MSR(r11)
	mtmsr	r3

	/* Restore TB */
	li	r3,0
	mtspr	SPRN_TBWL,r3
	lwz	r3,SL_TBU(r11)
	lwz	r4,SL_TBL(r11)
	mtspr	SPRN_TBWU,r3
	mtspr	SPRN_TBWL,r4

	/* Restore TCR and clear any pending bits in TSR. */
	lwz	r4,SL_TCR(r11)
	mtspr	SPRN_TCR,r4
	lis	r4, (TSR_ENW | TSR_WIS | TSR_DIS | TSR_FIS)@h
	mtspr	SPRN_TSR,r4

	/* Kick decrementer */
	li	r0,1
	mtdec	r0

	/* Restore the callee-saved registers and return */
	lwz	r0,SL_CR(r11)
	mtcr	r0
	lwz	r2,SL_R2(r11)
	lmw	r12,SL_R12(r11)
	lwz	r1,SL_SP(r11)
	lwz	r0,SL_LR(r11)
	mtlr	r0

	li	r3,0
	blr
