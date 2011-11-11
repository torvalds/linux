/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc. All rights reserved.
 * Dave Liu <daveliu@freescale.com>
 * copy from idle_6xx.S and modify for e500 based processor,
 * implement the power_save function in idle.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/threads.h>
#include <asm/reg.h>
#include <asm/page.h>
#include <asm/cputable.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>

	.text

_GLOBAL(e500_idle)
	rlwinm	r3,r1,0,0,31-THREAD_SHIFT	/* current thread_info */
	lwz	r4,TI_LOCAL_FLAGS(r3)	/* set napping bit */
	ori	r4,r4,_TLF_NAPPING	/* so when we take an exception */
	stw	r4,TI_LOCAL_FLAGS(r3)	/* it will return to our caller */

#ifdef CONFIG_PPC_E500MC
	wrteei	1
1:	wait

	/*
	 * Guard against spurious wakeups (e.g. from a hypervisor) --
	 * any real interrupt will cause us to return to LR due to
	 * _TLF_NAPPING.
	 */
	b	1b
#else
	/* Check if we can nap or doze, put HID0 mask in r3 */
	lis	r3,0
BEGIN_FTR_SECTION
	lis	r3,HID0_DOZE@h
END_FTR_SECTION_IFSET(CPU_FTR_CAN_DOZE)

BEGIN_FTR_SECTION
	/* Now check if user enabled NAP mode */
	lis	r4,powersave_nap@ha
	lwz	r4,powersave_nap@l(r4)
	cmpwi	0,r4,0
	beq	1f
	stwu	r1,-16(r1)
	mflr	r0
	stw	r0,20(r1)
	bl	flush_dcache_L1
	lwz	r0,20(r1)
	addi	r1,r1,16
	mtlr	r0
	lis	r3,HID0_NAP@h
END_FTR_SECTION_IFSET(CPU_FTR_CAN_NAP)
BEGIN_FTR_SECTION
	msync
	li	r7,L2CSR0_L2FL@l
	mtspr	SPRN_L2CSR0,r7
2:
	mfspr	r7,SPRN_L2CSR0
	andi.	r4,r7,L2CSR0_L2FL@l
	bne	2b
END_FTR_SECTION_IFSET(CPU_FTR_L2CSR|CPU_FTR_CAN_NAP)
1:
	/* Go to NAP or DOZE now */
	mfspr	r4,SPRN_HID0
	rlwinm	r4,r4,0,~(HID0_DOZE|HID0_NAP|HID0_SLEEP)
	or	r4,r4,r3
	isync
	mtspr	SPRN_HID0,r4
	isync

	mfmsr	r7
	oris	r7,r7,MSR_WE@h
	ori	r7,r7,MSR_EE
	msync
	mtmsr	r7
	isync
2:	b	2b
#endif /* !E500MC */

/*
 * Return from NAP/DOZE mode, restore some CPU specific registers,
 * r2 containing physical address of current.
 * r11 points to the exception frame (physical address).
 * We have to preserve r10.
 */
_GLOBAL(power_save_ppc32_restore)
	lwz	r9,_LINK(r11)		/* interrupted in e500_idle */
	stw	r9,_NIP(r11)		/* make it do a blr */

#ifdef CONFIG_SMP
	rlwinm	r12,r1,0,0,31-THREAD_SHIFT
	lwz	r11,TI_CPU(r12)		/* get cpu number * 4 */
	slwi	r11,r11,2
#else
	li	r11,0
#endif

	b	transfer_to_handler_cont
