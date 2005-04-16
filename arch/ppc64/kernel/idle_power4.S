/*
 *  This file contains the power_save function for 6xx & 7xxx CPUs
 *  rewritten in assembler
 *
 *  Warning ! This code assumes that if your machine has a 750fx
 *  it will have PLL 1 set to low speed mode (used during NAP/DOZE).
 *  if this is not the case some additional changes will have to
 *  be done to check a runtime var (a bit like powersave-nap)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/threads.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/cputable.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/offsets.h>

#undef DEBUG

	.text

/*
 * Here is the power_save_6xx function. This could eventually be
 * split into several functions & changing the function pointer
 * depending on the various features.
 */
_GLOBAL(power4_idle)
BEGIN_FTR_SECTION
	blr
END_FTR_SECTION_IFCLR(CPU_FTR_CAN_NAP)
	/* We must dynamically check for the NAP feature as it
	 * can be cleared by CPU init after the fixups are done
	 */
	LOADBASE(r3,cur_cpu_spec)
	ld	r4,cur_cpu_spec@l(r3)
	ld	r4,CPU_SPEC_FEATURES(r4)
	andi.	r0,r4,CPU_FTR_CAN_NAP
	beqlr
	/* Now check if user or arch enabled NAP mode */
	LOADBASE(r3,powersave_nap)
	lwz	r4,powersave_nap@l(r3)
	cmpwi	0,r4,0
	beqlr

	/* Clear MSR:EE */
	mfmsr	r7
	li	r4,0
	ori	r4,r4,MSR_EE
	andc	r0,r7,r4
	mtmsrd	r0

	/* Check current_thread_info()->flags */
	clrrdi	r4,r1,THREAD_SHIFT
	ld	r4,TI_FLAGS(r4)
	andi.	r0,r4,_TIF_NEED_RESCHED
	beq	1f
	mtmsrd	r7	/* out of line this ? */
	blr
1:	
	/* Go to NAP now */	
BEGIN_FTR_SECTION
	DSSALL
	sync
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
	oris	r7,r7,MSR_POW@h
	sync
	isync
	mtmsrd	r7
	isync
	sync
	blr
	
