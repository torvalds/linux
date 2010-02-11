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

#define KVMPPC_HANDLE_EXIT .kvmppc_handle_exit
#define ULONG_SIZE 8
#define VCPU_GPR(n)     (VCPU_GPRS + (n * ULONG_SIZE))

.macro mfpaca tmp_reg, src_reg, offset, vcpu_reg
	ld	\tmp_reg, (PACA_EXMC+\offset)(r13)
	std	\tmp_reg, VCPU_GPR(\src_reg)(\vcpu_reg)
.endm

.macro DISABLE_INTERRUPTS
       mfmsr   r0
       rldicl  r0,r0,48,1
       rotldi  r0,r0,16
       mtmsrd  r0,1
.endm

/*****************************************************************************
 *                                                                           *
 *     Guest entry / exit code that is in kernel module memory (highmem)     *
 *                                                                           *
 ****************************************************************************/

/* Registers:
 *  r3: kvm_run pointer
 *  r4: vcpu pointer
 */
_GLOBAL(__kvmppc_vcpu_entry)

kvm_start_entry:
	/* Write correct stack frame */
	mflr    r0
	std     r0,16(r1)

	/* Save host state to the stack */
	stdu	r1, -SWITCH_FRAME_SIZE(r1)

	/* Save r3 (kvm_run) and r4 (vcpu) */
	SAVE_2GPRS(3, r1)

	/* Save non-volatile registers (r14 - r31) */
	SAVE_NVGPRS(r1)

	/* Save LR */
	mflr	r14
	std	r14, _LINK(r1)

/* XXX optimize non-volatile loading away */
kvm_start_lightweight:

	DISABLE_INTERRUPTS

	/* Save R1/R2 in the PACA */
	std	r1, PACAR1(r13)
	std	r2, (PACA_EXMC+EX_SRR0)(r13)
	ld	r3, VCPU_HIGHMEM_HANDLER(r4)
	std	r3, PACASAVEDMSR(r13)

	/* Load non-volatile guest state from the vcpu */
	ld	r14, VCPU_GPR(r14)(r4)
	ld	r15, VCPU_GPR(r15)(r4)
	ld	r16, VCPU_GPR(r16)(r4)
	ld	r17, VCPU_GPR(r17)(r4)
	ld	r18, VCPU_GPR(r18)(r4)
	ld	r19, VCPU_GPR(r19)(r4)
	ld	r20, VCPU_GPR(r20)(r4)
	ld	r21, VCPU_GPR(r21)(r4)
	ld	r22, VCPU_GPR(r22)(r4)
	ld	r23, VCPU_GPR(r23)(r4)
	ld	r24, VCPU_GPR(r24)(r4)
	ld	r25, VCPU_GPR(r25)(r4)
	ld	r26, VCPU_GPR(r26)(r4)
	ld	r27, VCPU_GPR(r27)(r4)
	ld	r28, VCPU_GPR(r28)(r4)
	ld	r29, VCPU_GPR(r29)(r4)
	ld	r30, VCPU_GPR(r30)(r4)
	ld	r31, VCPU_GPR(r31)(r4)

	ld	r9, VCPU_PC(r4)			/* r9 = vcpu->arch.pc */
	ld	r10, VCPU_SHADOW_MSR(r4)	/* r10 = vcpu->arch.shadow_msr */

	ld	r3, VCPU_TRAMPOLINE_ENTER(r4)
	mtsrr0	r3

	LOAD_REG_IMMEDIATE(r3, MSR_KERNEL & ~(MSR_IR | MSR_DR))
	mtsrr1	r3

	/* Load guest state in the respective registers */
	lwz	r3, VCPU_CR(r4)		/* r3 = vcpu->arch.cr */
	stw	r3, (PACA_EXMC + EX_CCR)(r13)

	ld	r3, VCPU_CTR(r4)	/* r3 = vcpu->arch.ctr */
	mtctr	r3			/* CTR = r3 */

	ld	r3, VCPU_LR(r4)		/* r3 = vcpu->arch.lr */
	mtlr	r3			/* LR = r3 */

	ld	r3, VCPU_XER(r4)	/* r3 = vcpu->arch.xer */
	std	r3, (PACA_EXMC + EX_R3)(r13)

	/* Some guests may need to have dcbz set to 32 byte length.
	 *
	 * Usually we ensure that by patching the guest's instructions
	 * to trap on dcbz and emulate it in the hypervisor.
	 *
	 * If we can, we should tell the CPU to use 32 byte dcbz though,
	 * because that's a lot faster.
	 */

	ld	r3, VCPU_HFLAGS(r4)
	rldicl.	r3, r3, 0, 63		/* CR = ((r3 & 1) == 0) */
	beq	no_dcbz32_on

	mfspr   r3,SPRN_HID5
	ori     r3, r3, 0x80		/* XXX HID5_dcbz32 = 0x80 */
	mtspr   SPRN_HID5,r3

no_dcbz32_on:
	/*	Load guest GPRs */

	ld	r3, VCPU_GPR(r9)(r4)
	std	r3, (PACA_EXMC + EX_R9)(r13)
	ld	r3, VCPU_GPR(r10)(r4)
	std	r3, (PACA_EXMC + EX_R10)(r13)
	ld	r3, VCPU_GPR(r11)(r4)
	std	r3, (PACA_EXMC + EX_R11)(r13)
	ld	r3, VCPU_GPR(r12)(r4)
	std	r3, (PACA_EXMC + EX_R12)(r13)
	ld	r3, VCPU_GPR(r13)(r4)
	std	r3, (PACA_EXMC + EX_R13)(r13)

	ld	r0, VCPU_GPR(r0)(r4)
	ld	r1, VCPU_GPR(r1)(r4)
	ld	r2, VCPU_GPR(r2)(r4)
	ld	r3, VCPU_GPR(r3)(r4)
	ld	r5, VCPU_GPR(r5)(r4)
	ld	r6, VCPU_GPR(r6)(r4)
	ld	r7, VCPU_GPR(r7)(r4)
	ld	r8, VCPU_GPR(r8)(r4)
	ld	r4, VCPU_GPR(r4)(r4)

	/* This sets the Magic value for the trampoline */

	li	r11, 1
	stb	r11, PACA_KVM_IN_GUEST(r13)

	/* Jump to SLB patching handlder and into our guest */
	RFI

/*
 * This is the handler in module memory. It gets jumped at from the
 * lowmem trampoline code, so it's basically the guest exit code.
 *
 */

.global kvmppc_handler_highmem
kvmppc_handler_highmem:

	/*
	 * Register usage at this point:
	 *
	 * R00   = guest R13
	 * R01   = host R1
	 * R02   = host R2
	 * R10   = guest PC
	 * R11   = guest MSR
	 * R12   = exit handler id
	 * R13   = PACA
	 * PACA.exmc.R9    = guest R1
	 * PACA.exmc.R10   = guest R10
	 * PACA.exmc.R11   = guest R11
	 * PACA.exmc.R12   = guest R12
	 * PACA.exmc.R13   = guest R2
	 * PACA.exmc.DAR   = guest DAR
	 * PACA.exmc.DSISR = guest DSISR
	 * PACA.exmc.LR    = guest instruction
	 * PACA.exmc.CCR   = guest CR
	 * PACA.exmc.SRR0  = guest R0
	 *
	 */

	std	r3, (PACA_EXMC+EX_R3)(r13)

	/* save the exit id in R3 */
	mr	r3, r12

	/* R12 = vcpu */
	ld	r12, GPR4(r1)

	/* Now save the guest state */

	std	r0, VCPU_GPR(r13)(r12)
	std	r4, VCPU_GPR(r4)(r12)
	std	r5, VCPU_GPR(r5)(r12)
	std	r6, VCPU_GPR(r6)(r12)
	std	r7, VCPU_GPR(r7)(r12)
	std	r8, VCPU_GPR(r8)(r12)
	std	r9, VCPU_GPR(r9)(r12)

	/* get registers from PACA */
	mfpaca	r5, r0, EX_SRR0, r12
	mfpaca	r5, r3, EX_R3, r12
	mfpaca	r5, r1, EX_R9, r12
	mfpaca	r5, r10, EX_R10, r12
	mfpaca	r5, r11, EX_R11, r12
	mfpaca	r5, r12, EX_R12, r12
	mfpaca	r5, r2, EX_R13, r12

	lwz	r5, (PACA_EXMC+EX_LR)(r13)
	stw	r5, VCPU_LAST_INST(r12)

	lwz	r5, (PACA_EXMC+EX_CCR)(r13)
	stw	r5, VCPU_CR(r12)

	ld	r5, VCPU_HFLAGS(r12)
	rldicl.	r5, r5, 0, 63		/* CR = ((r5 & 1) == 0) */
	beq	no_dcbz32_off

	mfspr   r5,SPRN_HID5
	rldimi  r5,r5,6,56
	mtspr   SPRN_HID5,r5

no_dcbz32_off:

	/* XXX maybe skip on lightweight? */
	std	r14, VCPU_GPR(r14)(r12)
	std	r15, VCPU_GPR(r15)(r12)
	std	r16, VCPU_GPR(r16)(r12)
	std	r17, VCPU_GPR(r17)(r12)
	std	r18, VCPU_GPR(r18)(r12)
	std	r19, VCPU_GPR(r19)(r12)
	std	r20, VCPU_GPR(r20)(r12)
	std	r21, VCPU_GPR(r21)(r12)
	std	r22, VCPU_GPR(r22)(r12)
	std	r23, VCPU_GPR(r23)(r12)
	std	r24, VCPU_GPR(r24)(r12)
	std	r25, VCPU_GPR(r25)(r12)
	std	r26, VCPU_GPR(r26)(r12)
	std	r27, VCPU_GPR(r27)(r12)
	std	r28, VCPU_GPR(r28)(r12)
	std	r29, VCPU_GPR(r29)(r12)
	std	r30, VCPU_GPR(r30)(r12)
	std	r31, VCPU_GPR(r31)(r12)

	/* Restore non-volatile host registers (r14 - r31) */
	REST_NVGPRS(r1)

	/* Save guest PC (R10) */
	std	r10, VCPU_PC(r12)

	/* Save guest msr (R11) */
	std	r11, VCPU_SHADOW_MSR(r12)

	/* Save guest CTR (in R12) */
	mfctr	r5
	std	r5, VCPU_CTR(r12)

	/* Save guest LR */
	mflr	r5
	std	r5, VCPU_LR(r12)

	/* Save guest XER */
	mfxer	r5
	std	r5, VCPU_XER(r12)

	/* Save guest DAR */
	ld	r5, (PACA_EXMC+EX_DAR)(r13)
	std	r5, VCPU_FAULT_DEAR(r12)

	/* Save guest DSISR */
	lwz	r5, (PACA_EXMC+EX_DSISR)(r13)
	std	r5, VCPU_FAULT_DSISR(r12)

	/* Restore host msr -> SRR1 */
	ld	r7, VCPU_HOST_MSR(r12)
	mtsrr1	r7

	/* Restore host IP -> SRR0 */
	ld	r6, VCPU_HOST_RETIP(r12)
	mtsrr0	r6

	/*
	 * For some interrupts, we need to call the real Linux
	 * handler, so it can do work for us. This has to happen
	 * as if the interrupt arrived from the kernel though,
	 * so let's fake it here where most state is restored.
	 *
	 * Call Linux for hardware interrupts/decrementer
	 * r3 = address of interrupt handler (exit reason)
	 */

	cmpwi	r3, BOOK3S_INTERRUPT_EXTERNAL
	beq	call_linux_handler
	cmpwi	r3, BOOK3S_INTERRUPT_DECREMENTER
	beq	call_linux_handler

	/* Back to Interruptable Mode! (goto kvm_return_point) */
	RFI

call_linux_handler:

	/*
	 * If we land here we need to jump back to the handler we
	 * came from.
	 *
	 * We have a page that we can access from real mode, so let's
	 * jump back to that and use it as a trampoline to get back into the
	 * interrupt handler!
	 *
	 * R3 still contains the exit code,
	 * R6 VCPU_HOST_RETIP and
	 * R7 VCPU_HOST_MSR
	 */

	mtlr	r3

	ld	r5, VCPU_TRAMPOLINE_LOWMEM(r12)
	mtsrr0	r5
	LOAD_REG_IMMEDIATE(r5, MSR_KERNEL & ~(MSR_IR | MSR_DR))
	mtsrr1	r5

	RFI

.global kvm_return_point
kvm_return_point:

	/* Jump back to lightweight entry if we're supposed to */
	/* go back into the guest */
	mr	r5, r3
	/* Restore r3 (kvm_run) and r4 (vcpu) */
	REST_2GPRS(3, r1)
	bl	KVMPPC_HANDLE_EXIT

#if 0 /* XXX get lightweight exits back */
	cmpwi	r3, RESUME_GUEST
	bne	kvm_exit_heavyweight

	/* put VCPU and KVM_RUN back into place and roll again! */
	REST_2GPRS(3, r1)
	b	kvm_start_lightweight

kvm_exit_heavyweight:
	/* Restore non-volatile host registers */
	ld	r14, _LINK(r1)
	mtlr	r14
	REST_NVGPRS(r1)

	addi    r1, r1, SWITCH_FRAME_SIZE
#else
	ld	r4, _LINK(r1)
	mtlr	r4

	cmpwi	r3, RESUME_GUEST
	bne	kvm_exit_heavyweight

	REST_2GPRS(3, r1)

	addi    r1, r1, SWITCH_FRAME_SIZE

	b	kvm_start_entry

kvm_exit_heavyweight:

	addi    r1, r1, SWITCH_FRAME_SIZE
#endif

	blr
