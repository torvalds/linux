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

.macro DISABLE_INTERRUPTS
       mfmsr   r0
       rldicl  r0,r0,48,1
       rotldi  r0,r0,16
       mtmsrd  r0,1
.endm

#define VCPU_LOAD_NVGPRS(vcpu) \
	ld	r14, VCPU_GPR(r14)(vcpu); \
	ld	r15, VCPU_GPR(r15)(vcpu); \
	ld	r16, VCPU_GPR(r16)(vcpu); \
	ld	r17, VCPU_GPR(r17)(vcpu); \
	ld	r18, VCPU_GPR(r18)(vcpu); \
	ld	r19, VCPU_GPR(r19)(vcpu); \
	ld	r20, VCPU_GPR(r20)(vcpu); \
	ld	r21, VCPU_GPR(r21)(vcpu); \
	ld	r22, VCPU_GPR(r22)(vcpu); \
	ld	r23, VCPU_GPR(r23)(vcpu); \
	ld	r24, VCPU_GPR(r24)(vcpu); \
	ld	r25, VCPU_GPR(r25)(vcpu); \
	ld	r26, VCPU_GPR(r26)(vcpu); \
	ld	r27, VCPU_GPR(r27)(vcpu); \
	ld	r28, VCPU_GPR(r28)(vcpu); \
	ld	r29, VCPU_GPR(r29)(vcpu); \
	ld	r30, VCPU_GPR(r30)(vcpu); \
	ld	r31, VCPU_GPR(r31)(vcpu); \

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
	std	r0, _LINK(r1)

	/* Load non-volatile guest state from the vcpu */
	VCPU_LOAD_NVGPRS(r4)

	/* Save R1/R2 in the PACA */
	std	r1, PACA_KVM_HOST_R1(r13)
	std	r2, PACA_KVM_HOST_R2(r13)

	/* XXX swap in/out on load? */
	ld	r3, VCPU_HIGHMEM_HANDLER(r4)
	std	r3, PACA_KVM_VMHANDLER(r13)

kvm_start_lightweight:

	ld	r9, VCPU_PC(r4)			/* r9 = vcpu->arch.pc */
	ld	r10, VCPU_SHADOW_MSR(r4)	/* r10 = vcpu->arch.shadow_msr */

	/* Load some guest state in the respective registers */
	ld	r5, VCPU_CTR(r4)	/* r5 = vcpu->arch.ctr */
					/* will be swapped in by rmcall */

	ld	r3, VCPU_LR(r4)		/* r3 = vcpu->arch.lr */
	mtlr	r3			/* LR = r3 */

	DISABLE_INTERRUPTS

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

	ld	r6, VCPU_RMCALL(r4)
	mtctr	r6

	ld	r3, VCPU_TRAMPOLINE_ENTER(r4)
	LOAD_REG_IMMEDIATE(r4, MSR_KERNEL & ~(MSR_IR | MSR_DR))

	/* Jump to SLB patching handlder and into our guest */
	bctr

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
	 * R0         = guest last inst
	 * R1         = host R1
	 * R2         = host R2
	 * R3         = guest PC
	 * R4         = guest MSR
	 * R5         = guest DAR
	 * R6         = guest DSISR
	 * R13        = PACA
	 * PACA.KVM.* = guest *
	 *
	 */

	/* R7 = vcpu */
	ld	r7, GPR4(r1)

	/* Now save the guest state */

	stw	r0, VCPU_LAST_INST(r7)

	std	r3, VCPU_PC(r7)
	std	r4, VCPU_SHADOW_MSR(r7)
	std	r5, VCPU_FAULT_DEAR(r7)
	std	r6, VCPU_FAULT_DSISR(r7)

	ld	r5, VCPU_HFLAGS(r7)
	rldicl.	r5, r5, 0, 63		/* CR = ((r5 & 1) == 0) */
	beq	no_dcbz32_off

	li	r4, 0
	mfspr   r5,SPRN_HID5
	rldimi  r5,r4,6,56
	mtspr   SPRN_HID5,r5

no_dcbz32_off:

	std	r14, VCPU_GPR(r14)(r7)
	std	r15, VCPU_GPR(r15)(r7)
	std	r16, VCPU_GPR(r16)(r7)
	std	r17, VCPU_GPR(r17)(r7)
	std	r18, VCPU_GPR(r18)(r7)
	std	r19, VCPU_GPR(r19)(r7)
	std	r20, VCPU_GPR(r20)(r7)
	std	r21, VCPU_GPR(r21)(r7)
	std	r22, VCPU_GPR(r22)(r7)
	std	r23, VCPU_GPR(r23)(r7)
	std	r24, VCPU_GPR(r24)(r7)
	std	r25, VCPU_GPR(r25)(r7)
	std	r26, VCPU_GPR(r26)(r7)
	std	r27, VCPU_GPR(r27)(r7)
	std	r28, VCPU_GPR(r28)(r7)
	std	r29, VCPU_GPR(r29)(r7)
	std	r30, VCPU_GPR(r30)(r7)
	std	r31, VCPU_GPR(r31)(r7)

	/* Save guest CTR */
	mfctr	r5
	std	r5, VCPU_CTR(r7)

	/* Save guest LR */
	mflr	r5
	std	r5, VCPU_LR(r7)

	/* Restore host msr -> SRR1 */
	ld	r6, VCPU_HOST_MSR(r7)

	/*
	 * For some interrupts, we need to call the real Linux
	 * handler, so it can do work for us. This has to happen
	 * as if the interrupt arrived from the kernel though,
	 * so let's fake it here where most state is restored.
	 *
	 * Call Linux for hardware interrupts/decrementer
	 * r3 = address of interrupt handler (exit reason)
	 */

	cmpwi	r12, BOOK3S_INTERRUPT_EXTERNAL
	beq	call_linux_handler
	cmpwi	r12, BOOK3S_INTERRUPT_DECREMENTER
	beq	call_linux_handler

	/* Back to EE=1 */
	mtmsr	r6
	b	kvm_return_point

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
	 * R5 VCPU_HOST_RETIP and
	 * R6 VCPU_HOST_MSR
	 */

	/* Restore host IP -> SRR0 */
	ld	r5, VCPU_HOST_RETIP(r7)

	/* XXX Better move to a safe function?
	 *     What if we get an HTAB flush in between mtsrr0 and mtsrr1? */

	mtlr	r12

	ld	r4, VCPU_TRAMPOLINE_LOWMEM(r7)
	mtsrr0	r4
	LOAD_REG_IMMEDIATE(r3, MSR_KERNEL & ~(MSR_IR | MSR_DR))
	mtsrr1	r3

	RFI

.global kvm_return_point
kvm_return_point:

	/* Jump back to lightweight entry if we're supposed to */
	/* go back into the guest */

	/* Pass the exit number as 3rd argument to kvmppc_handle_exit */
	mr	r5, r12

	/* Restore r3 (kvm_run) and r4 (vcpu) */
	REST_2GPRS(3, r1)
	bl	KVMPPC_HANDLE_EXIT

	/* If RESUME_GUEST, get back in the loop */
	cmpwi	r3, RESUME_GUEST
	beq	kvm_loop_lightweight

	cmpwi	r3, RESUME_GUEST_NV
	beq	kvm_loop_heavyweight

kvm_exit_loop:

	ld	r4, _LINK(r1)
	mtlr	r4

	/* Restore non-volatile host registers (r14 - r31) */
	REST_NVGPRS(r1)

	addi    r1, r1, SWITCH_FRAME_SIZE
	blr

kvm_loop_heavyweight:

	ld	r4, _LINK(r1)
	std     r4, (16 + SWITCH_FRAME_SIZE)(r1)

	/* Load vcpu and cpu_run */
	REST_2GPRS(3, r1)

	/* Load non-volatile guest state from the vcpu */
	VCPU_LOAD_NVGPRS(r4)

	/* Jump back into the beginning of this function */
	b	kvm_start_lightweight

kvm_loop_lightweight:

	/* We'll need the vcpu pointer */
	REST_GPR(4, r1)

	/* Jump back into the beginning of this function */
	b	kvm_start_lightweight

