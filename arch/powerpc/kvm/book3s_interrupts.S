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

#if defined(CONFIG_PPC_BOOK3S_64)

#define ULONG_SIZE 		8
#define FUNC(name) 		GLUE(.,name)

#define GET_SHADOW_VCPU_R13

#define DISABLE_INTERRUPTS	\
	mfmsr   r0;		\
	rldicl  r0,r0,48,1;	\
	rotldi  r0,r0,16;	\
	mtmsrd  r0,1;		\

#elif defined(CONFIG_PPC_BOOK3S_32)

#define ULONG_SIZE              4
#define FUNC(name)		name

#define GET_SHADOW_VCPU_R13	\
	lwz	r13, (THREAD + THREAD_KVM_SVCPU)(r2)

#define DISABLE_INTERRUPTS	\
	mfmsr   r0;		\
	rlwinm  r0,r0,0,17,15;	\
	mtmsr   r0;		\

#endif /* CONFIG_PPC_BOOK3S_XX */


#define VCPU_GPR(n)		(VCPU_GPRS + (n * ULONG_SIZE))
#define VCPU_LOAD_NVGPRS(vcpu) \
	PPC_LL	r14, VCPU_GPR(r14)(vcpu); \
	PPC_LL	r15, VCPU_GPR(r15)(vcpu); \
	PPC_LL	r16, VCPU_GPR(r16)(vcpu); \
	PPC_LL	r17, VCPU_GPR(r17)(vcpu); \
	PPC_LL	r18, VCPU_GPR(r18)(vcpu); \
	PPC_LL	r19, VCPU_GPR(r19)(vcpu); \
	PPC_LL	r20, VCPU_GPR(r20)(vcpu); \
	PPC_LL	r21, VCPU_GPR(r21)(vcpu); \
	PPC_LL	r22, VCPU_GPR(r22)(vcpu); \
	PPC_LL	r23, VCPU_GPR(r23)(vcpu); \
	PPC_LL	r24, VCPU_GPR(r24)(vcpu); \
	PPC_LL	r25, VCPU_GPR(r25)(vcpu); \
	PPC_LL	r26, VCPU_GPR(r26)(vcpu); \
	PPC_LL	r27, VCPU_GPR(r27)(vcpu); \
	PPC_LL	r28, VCPU_GPR(r28)(vcpu); \
	PPC_LL	r29, VCPU_GPR(r29)(vcpu); \
	PPC_LL	r30, VCPU_GPR(r30)(vcpu); \
	PPC_LL	r31, VCPU_GPR(r31)(vcpu); \

/*****************************************************************************
 *                                                                           *
 *     Guest entry / exit code that is in kernel module memory (highmem)     *
 *                                                                           *
 ****************************************************************************/

/* Registers:
 *  r3: kvm_run pointer
 *  r4: vcpu pointer
 */
_GLOBAL(__kvmppc_vcpu_run)

kvm_start_entry:
	/* Write correct stack frame */
	mflr	r0
	PPC_STL	r0,PPC_LR_STKOFF(r1)

	/* Save host state to the stack */
	PPC_STLU r1, -SWITCH_FRAME_SIZE(r1)

	/* Save r3 (kvm_run) and r4 (vcpu) */
	SAVE_2GPRS(3, r1)

	/* Save non-volatile registers (r14 - r31) */
	SAVE_NVGPRS(r1)

	/* Save LR */
	PPC_STL	r0, _LINK(r1)

	/* Load non-volatile guest state from the vcpu */
	VCPU_LOAD_NVGPRS(r4)

kvm_start_lightweight:

	GET_SHADOW_VCPU_R13
	PPC_LL	r3, VCPU_HIGHMEM_HANDLER(r4)
	PPC_STL	r3, HSTATE_VMHANDLER(r13)

	PPC_LL	r10, VCPU_SHADOW_MSR(r4)	/* r10 = vcpu->arch.shadow_msr */

	DISABLE_INTERRUPTS

#ifdef CONFIG_PPC_BOOK3S_64
	/* Some guests may need to have dcbz set to 32 byte length.
	 *
	 * Usually we ensure that by patching the guest's instructions
	 * to trap on dcbz and emulate it in the hypervisor.
	 *
	 * If we can, we should tell the CPU to use 32 byte dcbz though,
	 * because that's a lot faster.
	 */

	PPC_LL	r3, VCPU_HFLAGS(r4)
	rldicl.	r3, r3, 0, 63		/* CR = ((r3 & 1) == 0) */
	beq	no_dcbz32_on

	mfspr   r3,SPRN_HID5
	ori     r3, r3, 0x80		/* XXX HID5_dcbz32 = 0x80 */
	mtspr   SPRN_HID5,r3

no_dcbz32_on:

#endif /* CONFIG_PPC_BOOK3S_64 */

	PPC_LL	r6, VCPU_RMCALL(r4)
	mtctr	r6

	PPC_LL	r3, VCPU_TRAMPOLINE_ENTER(r4)
	LOAD_REG_IMMEDIATE(r4, MSR_KERNEL & ~(MSR_IR | MSR_DR))

	/* Jump to segment patching handler and into our guest */
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
	 * R1       = host R1
	 * R2       = host R2
	 * R12      = exit handler id
	 * R13      = PACA
	 * SVCPU.*  = guest *
	 *
	 */

	/* R7 = vcpu */
	PPC_LL	r7, GPR4(r1)

#ifdef CONFIG_PPC_BOOK3S_64

	PPC_LL	r5, VCPU_HFLAGS(r7)
	rldicl.	r5, r5, 0, 63		/* CR = ((r5 & 1) == 0) */
	beq	no_dcbz32_off

	li	r4, 0
	mfspr   r5,SPRN_HID5
	rldimi  r5,r4,6,56
	mtspr   SPRN_HID5,r5

no_dcbz32_off:

#endif /* CONFIG_PPC_BOOK3S_64 */

	PPC_STL	r14, VCPU_GPR(r14)(r7)
	PPC_STL	r15, VCPU_GPR(r15)(r7)
	PPC_STL	r16, VCPU_GPR(r16)(r7)
	PPC_STL	r17, VCPU_GPR(r17)(r7)
	PPC_STL	r18, VCPU_GPR(r18)(r7)
	PPC_STL	r19, VCPU_GPR(r19)(r7)
	PPC_STL	r20, VCPU_GPR(r20)(r7)
	PPC_STL	r21, VCPU_GPR(r21)(r7)
	PPC_STL	r22, VCPU_GPR(r22)(r7)
	PPC_STL	r23, VCPU_GPR(r23)(r7)
	PPC_STL	r24, VCPU_GPR(r24)(r7)
	PPC_STL	r25, VCPU_GPR(r25)(r7)
	PPC_STL	r26, VCPU_GPR(r26)(r7)
	PPC_STL	r27, VCPU_GPR(r27)(r7)
	PPC_STL	r28, VCPU_GPR(r28)(r7)
	PPC_STL	r29, VCPU_GPR(r29)(r7)
	PPC_STL	r30, VCPU_GPR(r30)(r7)
	PPC_STL	r31, VCPU_GPR(r31)(r7)

	/* Restore host msr -> SRR1 */
	PPC_LL	r6, VCPU_HOST_MSR(r7)

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
	cmpwi	r12, BOOK3S_INTERRUPT_PERFMON
	beq	call_linux_handler

	/* Back to EE=1 */
	mtmsr	r6
	sync
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
	PPC_LL	r5, VCPU_HOST_RETIP(r7)

	/* XXX Better move to a safe function?
	 *     What if we get an HTAB flush in between mtsrr0 and mtsrr1? */

	mtlr	r12

	PPC_LL	r4, VCPU_TRAMPOLINE_LOWMEM(r7)
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
	bl	FUNC(kvmppc_handle_exit)

	/* If RESUME_GUEST, get back in the loop */
	cmpwi	r3, RESUME_GUEST
	beq	kvm_loop_lightweight

	cmpwi	r3, RESUME_GUEST_NV
	beq	kvm_loop_heavyweight

kvm_exit_loop:

	PPC_LL	r4, _LINK(r1)
	mtlr	r4

	/* Restore non-volatile host registers (r14 - r31) */
	REST_NVGPRS(r1)

	addi    r1, r1, SWITCH_FRAME_SIZE
	blr

kvm_loop_heavyweight:

	PPC_LL	r4, _LINK(r1)
	PPC_STL r4, (PPC_LR_STKOFF + SWITCH_FRAME_SIZE)(r1)

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
