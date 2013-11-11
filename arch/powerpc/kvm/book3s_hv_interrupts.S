/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright 2011 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 *
 * Derived from book3s_interrupts.S, which is:
 * Copyright SUSE Linux Products GmbH 2009
 *
 * Authors: Alexander Graf <agraf@suse.de>
 */

#include <asm/ppc_asm.h>
#include <asm/kvm_asm.h>
#include <asm/reg.h>
#include <asm/page.h>
#include <asm/asm-offsets.h>
#include <asm/exception-64s.h>
#include <asm/ppc-opcode.h>

/*****************************************************************************
 *                                                                           *
 *     Guest entry / exit code that is in kernel module memory (vmalloc)     *
 *                                                                           *
 ****************************************************************************/

/* Registers:
 *  r4: vcpu pointer
 */
_GLOBAL(__kvmppc_vcore_entry)

	/* Write correct stack frame */
	mflr	r0
	std	r0,PPC_LR_STKOFF(r1)

	/* Save host state to the stack */
	stdu	r1, -SWITCH_FRAME_SIZE(r1)

	/* Save non-volatile registers (r14 - r31) and CR */
	SAVE_NVGPRS(r1)
	mfcr	r3
	std	r3, _CCR(r1)

	/* Save host DSCR */
BEGIN_FTR_SECTION
	mfspr	r3, SPRN_DSCR
	std	r3, HSTATE_DSCR(r13)
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_206)

	/* Save host DABR */
	mfspr	r3, SPRN_DABR
	std	r3, HSTATE_DABR(r13)

	/* Hard-disable interrupts */
	mfmsr   r10
	std	r10, HSTATE_HOST_MSR(r13)
	rldicl  r10,r10,48,1
	rotldi  r10,r10,16
	mtmsrd  r10,1

	/* Save host PMU registers */
	/* R4 is live here (vcpu pointer) but not r3 or r5 */
	li	r3, 1
	sldi	r3, r3, 31		/* MMCR0_FC (freeze counters) bit */
	mfspr	r7, SPRN_MMCR0		/* save MMCR0 */
	mtspr	SPRN_MMCR0, r3		/* freeze all counters, disable interrupts */
	mfspr	r6, SPRN_MMCRA
BEGIN_FTR_SECTION
	/* On P7, clear MMCRA in order to disable SDAR updates */
	li	r5, 0
	mtspr	SPRN_MMCRA, r5
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_206)
	isync
	ld	r3, PACALPPACAPTR(r13)	/* is the host using the PMU? */
	lbz	r5, LPPACA_PMCINUSE(r3)
	cmpwi	r5, 0
	beq	31f			/* skip if not */
	mfspr	r5, SPRN_MMCR1
	std	r7, HSTATE_MMCR(r13)
	std	r5, HSTATE_MMCR + 8(r13)
	std	r6, HSTATE_MMCR + 16(r13)
	mfspr	r3, SPRN_PMC1
	mfspr	r5, SPRN_PMC2
	mfspr	r6, SPRN_PMC3
	mfspr	r7, SPRN_PMC4
	mfspr	r8, SPRN_PMC5
	mfspr	r9, SPRN_PMC6
BEGIN_FTR_SECTION
	mfspr	r10, SPRN_PMC7
	mfspr	r11, SPRN_PMC8
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_201)
	stw	r3, HSTATE_PMC(r13)
	stw	r5, HSTATE_PMC + 4(r13)
	stw	r6, HSTATE_PMC + 8(r13)
	stw	r7, HSTATE_PMC + 12(r13)
	stw	r8, HSTATE_PMC + 16(r13)
	stw	r9, HSTATE_PMC + 20(r13)
BEGIN_FTR_SECTION
	stw	r10, HSTATE_PMC + 24(r13)
	stw	r11, HSTATE_PMC + 28(r13)
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_201)
31:

	/*
	 * Put whatever is in the decrementer into the
	 * hypervisor decrementer.
	 */
	mfspr	r8,SPRN_DEC
	mftb	r7
	mtspr	SPRN_HDEC,r8
	extsw	r8,r8
	add	r8,r8,r7
	std	r8,HSTATE_DECEXP(r13)

#ifdef CONFIG_SMP
	/*
	 * On PPC970, if the guest vcpu has an external interrupt pending,
	 * send ourselves an IPI so as to interrupt the guest once it
	 * enables interrupts.  (It must have interrupts disabled,
	 * otherwise we would already have delivered the interrupt.)
	 *
	 * XXX If this is a UP build, smp_send_reschedule is not available,
	 * so the interrupt will be delayed until the next time the vcpu
	 * enters the guest with interrupts enabled.
	 */
BEGIN_FTR_SECTION
	ld	r0, VCPU_PENDING_EXC(r4)
	li	r7, (1 << BOOK3S_IRQPRIO_EXTERNAL)
	oris	r7, r7, (1 << BOOK3S_IRQPRIO_EXTERNAL_LEVEL)@h
	and.	r0, r0, r7
	beq	32f
	mr	r31, r4
	lhz	r3, PACAPACAINDEX(r13)
	bl	smp_send_reschedule
	nop
	mr	r4, r31
32:
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_201)
#endif /* CONFIG_SMP */

	/* Jump to partition switch code */
	bl	.kvmppc_hv_entry_trampoline
	nop

/*
 * We return here in virtual mode after the guest exits
 * with something that we can't handle in real mode.
 * Interrupts are enabled again at this point.
 */

.global kvmppc_handler_highmem
kvmppc_handler_highmem:

	/*
	 * Register usage at this point:
	 *
	 * R1       = host R1
	 * R2       = host R2
	 * R12      = exit handler id
	 * R13      = PACA
	 */

	/* Restore non-volatile host registers (r14 - r31) and CR */
	REST_NVGPRS(r1)
	ld	r4, _CCR(r1)
	mtcr	r4

	addi    r1, r1, SWITCH_FRAME_SIZE
	ld	r0, PPC_LR_STKOFF(r1)
	mtlr	r0
	blr
