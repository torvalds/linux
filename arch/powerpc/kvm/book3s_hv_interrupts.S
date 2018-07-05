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
#include <asm/asm-compat.h>
#include <asm/feature-fixups.h>

/*****************************************************************************
 *                                                                           *
 *     Guest entry / exit code that is in kernel module memory (vmalloc)     *
 *                                                                           *
 ****************************************************************************/

/* Registers:
 *  none
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
	mfspr	r3, SPRN_DSCR
	std	r3, HSTATE_DSCR(r13)

BEGIN_FTR_SECTION
	/* Save host DABR */
	mfspr	r3, SPRN_DABR
	std	r3, HSTATE_DABR(r13)
END_FTR_SECTION_IFCLR(CPU_FTR_ARCH_207S)

	/* Save host PMU registers */
BEGIN_FTR_SECTION
	/* Work around P8 PMAE bug */
	li	r3, -1
	clrrdi	r3, r3, 10
	mfspr	r8, SPRN_MMCR2
	mtspr	SPRN_MMCR2, r3		/* freeze all counters using MMCR2 */
	isync
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_207S)
	li	r3, 1
	sldi	r3, r3, 31		/* MMCR0_FC (freeze counters) bit */
	mfspr	r7, SPRN_MMCR0		/* save MMCR0 */
	mtspr	SPRN_MMCR0, r3		/* freeze all counters, disable interrupts */
	mfspr	r6, SPRN_MMCRA
	/* Clear MMCRA in order to disable SDAR updates */
	li	r5, 0
	mtspr	SPRN_MMCRA, r5
	isync
	lbz	r5, PACA_PMCINUSE(r13)	/* is the host using the PMU? */
	cmpwi	r5, 0
	beq	31f			/* skip if not */
	mfspr	r5, SPRN_MMCR1
	mfspr	r9, SPRN_SIAR
	mfspr	r10, SPRN_SDAR
	std	r7, HSTATE_MMCR0(r13)
	std	r5, HSTATE_MMCR1(r13)
	std	r6, HSTATE_MMCRA(r13)
	std	r9, HSTATE_SIAR(r13)
	std	r10, HSTATE_SDAR(r13)
BEGIN_FTR_SECTION
	mfspr	r9, SPRN_SIER
	std	r8, HSTATE_MMCR2(r13)
	std	r9, HSTATE_SIER(r13)
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_207S)
	mfspr	r3, SPRN_PMC1
	mfspr	r5, SPRN_PMC2
	mfspr	r6, SPRN_PMC3
	mfspr	r7, SPRN_PMC4
	mfspr	r8, SPRN_PMC5
	mfspr	r9, SPRN_PMC6
	stw	r3, HSTATE_PMC1(r13)
	stw	r5, HSTATE_PMC2(r13)
	stw	r6, HSTATE_PMC3(r13)
	stw	r7, HSTATE_PMC4(r13)
	stw	r8, HSTATE_PMC5(r13)
	stw	r9, HSTATE_PMC6(r13)
31:

	/*
	 * Put whatever is in the decrementer into the
	 * hypervisor decrementer.
	 */
BEGIN_FTR_SECTION
	ld	r5, HSTATE_KVM_VCORE(r13)
	ld	r6, VCORE_KVM(r5)
	ld	r9, KVM_HOST_LPCR(r6)
	andis.	r9, r9, LPCR_LD@h
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_300)
	mfspr	r8,SPRN_DEC
	mftb	r7
BEGIN_FTR_SECTION
	/* On POWER9, don't sign-extend if host LPCR[LD] bit is set */
	bne	32f
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_300)
	extsw	r8,r8
32:	mtspr	SPRN_HDEC,r8
	add	r8,r8,r7
	std	r8,HSTATE_DECEXP(r13)

	/* Jump to partition switch code */
	bl	kvmppc_hv_entry_trampoline
	nop

/*
 * We return here in virtual mode after the guest exits
 * with something that we can't handle in real mode.
 * Interrupts are still hard-disabled.
 */

	/*
	 * Register usage at this point:
	 *
	 * R1       = host R1
	 * R2       = host R2
	 * R3       = trap number on this thread
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
