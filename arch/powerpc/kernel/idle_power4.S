/*
 *  This file contains the power_save function for 970-family CPUs.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/threads.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/cputable.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/irqflags.h>
#include <asm/hw_irq.h>

#undef DEBUG

	.text

_GLOBAL(power4_idle)
BEGIN_FTR_SECTION
	blr
END_FTR_SECTION_IFCLR(CPU_FTR_CAN_NAP)
	/* Now check if user or arch enabled NAP mode */
	LOAD_REG_ADDRBASE(r3,powersave_nap)
	lwz	r4,ADDROFF(powersave_nap)(r3)
	cmpwi	0,r4,0
	beqlr

	/* Hard disable interrupts */
	mfmsr	r7
	rldicl	r0,r7,48,1
	rotldi	r0,r0,16
	mtmsrd	r0,1

	/* Check if something happened while soft-disabled */
	lbz	r0,PACAIRQHAPPENED(r13)
	cmpwi	cr0,r0,0
	bnelr

	/* Soft-enable interrupts */
#ifdef CONFIG_TRACE_IRQFLAGS
	mflr	r0
	std	r0,16(r1)
	stdu    r1,-128(r1)
	bl	trace_hardirqs_on
	addi    r1,r1,128
	ld	r0,16(r1)
	mtlr	r0
	mfmsr	r7
#endif /* CONFIG_TRACE_IRQFLAGS */

	li	r0,IRQS_ENABLED
	stb	r0,PACAIRQSOFTMASK(r13)	/* we'll hard-enable shortly */
BEGIN_FTR_SECTION
	DSSALL
	sync
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
	CURRENT_THREAD_INFO(r9, r1)
	ld	r8,TI_LOCAL_FLAGS(r9)	/* set napping bit */
	ori	r8,r8,_TLF_NAPPING	/* so when we take an exception */
	std	r8,TI_LOCAL_FLAGS(r9)	/* it will return to our caller */
	ori	r7,r7,MSR_EE
	oris	r7,r7,MSR_POW@h
1:	sync
	isync
	mtmsrd	r7
	isync
	b	1b

