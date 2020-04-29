/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  This file contains the power_save function for 6xx & 7xxx CPUs
 *  rewritten in assembler
 *
 *  Warning ! This code assumes that if your machine has a 750fx
 *  it will have PLL 1 set to low speed mode (used during NAP/DOZE).
 *  if this is not the case some additional changes will have to
 *  be done to check a runtime var (a bit like powersave-nap)
 */

#include <linux/threads.h>
#include <asm/reg.h>
#include <asm/page.h>
#include <asm/cputable.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/feature-fixups.h>

	.text

/*
 * Init idle, called at early CPU setup time from head.S for each CPU
 * Make sure no rest of NAP mode remains in HID0, save default
 * values for some CPU specific registers. Called with r24
 * containing CPU number and r3 reloc offset
 */
_GLOBAL(init_idle_6xx)
BEGIN_FTR_SECTION
	mfspr	r4,SPRN_HID0
	rlwinm	r4,r4,0,10,8	/* Clear NAP */
	mtspr	SPRN_HID0, r4
	b	1f
END_FTR_SECTION_IFSET(CPU_FTR_CAN_NAP)
	blr
1:
	slwi	r5,r24,2
	add	r5,r5,r3
BEGIN_FTR_SECTION
	mfspr	r4,SPRN_MSSCR0
	addis	r6,r5, nap_save_msscr0@ha
	stw	r4,nap_save_msscr0@l(r6)
END_FTR_SECTION_IFSET(CPU_FTR_NAP_DISABLE_L2_PR)
BEGIN_FTR_SECTION
	mfspr	r4,SPRN_HID1
	addis	r6,r5,nap_save_hid1@ha
	stw	r4,nap_save_hid1@l(r6)
END_FTR_SECTION_IFSET(CPU_FTR_DUAL_PLL_750FX)
	blr

/*
 * Here is the power_save_6xx function. This could eventually be
 * split into several functions & changing the function pointer
 * depending on the various features.
 */
_GLOBAL(ppc6xx_idle)
	/* Check if we can nap or doze, put HID0 mask in r3
	 */
	lis	r3, 0
BEGIN_FTR_SECTION
	lis	r3,HID0_DOZE@h
END_FTR_SECTION_IFSET(CPU_FTR_CAN_DOZE)
BEGIN_FTR_SECTION
	/* We must dynamically check for the NAP feature as it
	 * can be cleared by CPU init after the fixups are done
	 */
	lis	r4,cur_cpu_spec@ha
	lwz	r4,cur_cpu_spec@l(r4)
	lwz	r4,CPU_SPEC_FEATURES(r4)
	andi.	r0,r4,CPU_FTR_CAN_NAP
	beq	1f
	/* Now check if user or arch enabled NAP mode */
	lis	r4,powersave_nap@ha
	lwz	r4,powersave_nap@l(r4)
	cmpwi	0,r4,0
	beq	1f
	lis	r3,HID0_NAP@h
1:	
END_FTR_SECTION_IFSET(CPU_FTR_CAN_NAP)
	cmpwi	0,r3,0
	beqlr

	/* Some pre-nap cleanups needed on some CPUs */
	andis.	r0,r3,HID0_NAP@h
	beq	2f
BEGIN_FTR_SECTION
	/* Disable L2 prefetch on some 745x and try to ensure
	 * L2 prefetch engines are idle. As explained by errata
	 * text, we can't be sure they are, we just hope very hard
	 * that well be enough (sic !). At least I noticed Apple
	 * doesn't even bother doing the dcbf's here...
	 */
	mfspr	r4,SPRN_MSSCR0
	rlwinm	r4,r4,0,0,29
	sync
	mtspr	SPRN_MSSCR0,r4
	sync
	isync
	lis	r4,KERNELBASE@h
	dcbf	0,r4
	dcbf	0,r4
	dcbf	0,r4
	dcbf	0,r4
END_FTR_SECTION_IFSET(CPU_FTR_NAP_DISABLE_L2_PR)
2:
BEGIN_FTR_SECTION
	/* Go to low speed mode on some 750FX */
	lis	r4,powersave_lowspeed@ha
	lwz	r4,powersave_lowspeed@l(r4)
	cmpwi	0,r4,0
	beq	1f
	mfspr	r4,SPRN_HID1
	oris	r4,r4,0x0001
	mtspr	SPRN_HID1,r4
1:	
END_FTR_SECTION_IFSET(CPU_FTR_DUAL_PLL_750FX)

	/* Go to NAP or DOZE now */	
	mfspr	r4,SPRN_HID0
	lis	r5,(HID0_NAP|HID0_SLEEP)@h
BEGIN_FTR_SECTION
	oris	r5,r5,HID0_DOZE@h
END_FTR_SECTION_IFSET(CPU_FTR_CAN_DOZE)
	andc	r4,r4,r5
	or	r4,r4,r3
BEGIN_FTR_SECTION
	oris	r4,r4,HID0_DPM@h	/* that should be done once for all  */
END_FTR_SECTION_IFCLR(CPU_FTR_NO_DPM)
	mtspr	SPRN_HID0,r4
BEGIN_FTR_SECTION
	DSSALL
	sync
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
	lwz	r8,TI_LOCAL_FLAGS(r2)	/* set napping bit */
	ori	r8,r8,_TLF_NAPPING	/* so when we take an exception */
	stw	r8,TI_LOCAL_FLAGS(r2)	/* it will return to our caller */
	mfmsr	r7
	ori	r7,r7,MSR_EE
	oris	r7,r7,MSR_POW@h
1:	sync
	mtmsr	r7
	isync
	b	1b

/*
 * Return from NAP/DOZE mode, restore some CPU specific registers,
 * we are called with DR/IR still off and r2 containing physical
 * address of current.  R11 points to the exception frame (physical
 * address).  We have to preserve r10.
 */
_GLOBAL(power_save_ppc32_restore)
	lwz	r9,_LINK(r11)		/* interrupted in ppc6xx_idle: */
	stw	r9,_NIP(r11)		/* make it do a blr */

#ifdef CONFIG_SMP
	lwz	r11,TASK_CPU(r2)	/* get cpu number * 4 */
	slwi	r11,r11,2
#else
	li	r11,0
#endif
	/* Todo make sure all these are in the same page
	 * and load r11 (@ha part + CPU offset) only once
	 */
BEGIN_FTR_SECTION
	mfspr	r9,SPRN_HID0
	andis.	r9,r9,HID0_NAP@h
	beq	1f
#ifdef CONFIG_VMAP_STACK
	addis	r9, r11, nap_save_msscr0@ha
#else
	addis	r9,r11,(nap_save_msscr0-KERNELBASE)@ha
#endif
	lwz	r9,nap_save_msscr0@l(r9)
	mtspr	SPRN_MSSCR0, r9
	sync
	isync
1:
END_FTR_SECTION_IFSET(CPU_FTR_NAP_DISABLE_L2_PR)
BEGIN_FTR_SECTION
#ifdef CONFIG_VMAP_STACK
	addis	r9, r11, nap_save_hid1@ha
#else
	addis	r9,r11,(nap_save_hid1-KERNELBASE)@ha
#endif
	lwz	r9,nap_save_hid1@l(r9)
	mtspr	SPRN_HID1, r9
END_FTR_SECTION_IFSET(CPU_FTR_DUAL_PLL_750FX)
	b	transfer_to_handler_cont

	.data

_GLOBAL(nap_save_msscr0)
	.space	4*NR_CPUS

_GLOBAL(nap_save_hid1)
	.space	4*NR_CPUS

_GLOBAL(powersave_lowspeed)
	.long	0
