/*
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 *
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
 */

#include <linux/arm-smccc.h>
#include <linux/linkage.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_asm.h>

	.arch_extension     virt

	.text
	.pushsection	.hyp.text, "ax"

.macro load_vcpu	reg
	mrc	p15, 4, \reg, c13, c0, 2	@ HTPIDR
.endm

/********************************************************************
 * Hypervisor exception vector and handlers
 *
 *
 * The KVM/ARM Hypervisor ABI is defined as follows:
 *
 * Entry to Hyp mode from the host kernel will happen _only_ when an HVC
 * instruction is issued since all traps are disabled when running the host
 * kernel as per the Hyp-mode initialization at boot time.
 *
 * HVC instructions cause a trap to the vector page + offset 0x14 (see hyp_hvc
 * below) when the HVC instruction is called from SVC mode (i.e. a guest or the
 * host kernel) and they cause a trap to the vector page + offset 0x8 when HVC
 * instructions are called from within Hyp-mode.
 *
 * Hyp-ABI: Calling HYP-mode functions from host (in SVC mode):
 *    Switching to Hyp mode is done through a simple HVC #0 instruction. The
 *    exception vector code will check that the HVC comes from VMID==0.
 *    - r0 contains a pointer to a HYP function
 *    - r1, r2, and r3 contain arguments to the above function.
 *    - The HYP function will be called with its arguments in r0, r1 and r2.
 *    On HYP function return, we return directly to SVC.
 *
 * Note that the above is used to execute code in Hyp-mode from a host-kernel
 * point of view, and is a different concept from performing a world-switch and
 * executing guest code SVC mode (with a VMID != 0).
 */

	.align 5
__kvm_hyp_vector:
	.global __kvm_hyp_vector

	@ Hyp-mode exception vector
	W(b)	hyp_reset
	W(b)	hyp_undef
	W(b)	hyp_svc
	W(b)	hyp_pabt
	W(b)	hyp_dabt
	W(b)	hyp_hvc
	W(b)	hyp_irq
	W(b)	hyp_fiq

#ifdef CONFIG_HARDEN_BRANCH_PREDICTOR
	.align 5
__kvm_hyp_vector_ic_inv:
	.global __kvm_hyp_vector_ic_inv

	/*
	 * We encode the exception entry in the bottom 3 bits of
	 * SP, and we have to guarantee to be 8 bytes aligned.
	 */
	W(add)	sp, sp, #1	/* Reset 	  7 */
	W(add)	sp, sp, #1	/* Undef	  6 */
	W(add)	sp, sp, #1	/* Syscall	  5 */
	W(add)	sp, sp, #1	/* Prefetch abort 4 */
	W(add)	sp, sp, #1	/* Data abort	  3 */
	W(add)	sp, sp, #1	/* HVC		  2 */
	W(add)	sp, sp, #1	/* IRQ		  1 */
	W(nop)			/* FIQ		  0 */

	mcr	p15, 0, r0, c7, c5, 0	/* ICIALLU */
	isb

	b	decode_vectors

	.align 5
__kvm_hyp_vector_bp_inv:
	.global __kvm_hyp_vector_bp_inv

	/*
	 * We encode the exception entry in the bottom 3 bits of
	 * SP, and we have to guarantee to be 8 bytes aligned.
	 */
	W(add)	sp, sp, #1	/* Reset 	  7 */
	W(add)	sp, sp, #1	/* Undef	  6 */
	W(add)	sp, sp, #1	/* Syscall	  5 */
	W(add)	sp, sp, #1	/* Prefetch abort 4 */
	W(add)	sp, sp, #1	/* Data abort	  3 */
	W(add)	sp, sp, #1	/* HVC		  2 */
	W(add)	sp, sp, #1	/* IRQ		  1 */
	W(nop)			/* FIQ		  0 */

	mcr	p15, 0, r0, c7, c5, 6	/* BPIALL */
	isb

decode_vectors:

#ifdef CONFIG_THUMB2_KERNEL
	/*
	 * Yet another silly hack: Use VPIDR as a temp register.
	 * Thumb2 is really a pain, as SP cannot be used with most
	 * of the bitwise instructions. The vect_br macro ensures
	 * things gets cleaned-up.
	 */
	mcr	p15, 4, r0, c0, c0, 0	/* VPIDR */
	mov	r0, sp
	and	r0, r0, #7
	sub	sp, sp, r0
	push	{r1, r2}
	mov	r1, r0
	mrc	p15, 4, r0, c0, c0, 0	/* VPIDR */
	mrc	p15, 0, r2, c0, c0, 0	/* MIDR  */
	mcr	p15, 4, r2, c0, c0, 0	/* VPIDR */
#endif

.macro vect_br val, targ
ARM(	eor	sp, sp, #\val	)
ARM(	tst	sp, #7		)
ARM(	eorne	sp, sp, #\val	)

THUMB(	cmp	r1, #\val	)
THUMB(	popeq	{r1, r2}	)

	beq	\targ
.endm

	vect_br	0, hyp_fiq
	vect_br	1, hyp_irq
	vect_br	2, hyp_hvc
	vect_br	3, hyp_dabt
	vect_br	4, hyp_pabt
	vect_br	5, hyp_svc
	vect_br	6, hyp_undef
	vect_br	7, hyp_reset
#endif

.macro invalid_vector label, cause
	.align
\label:	mov	r0, #\cause
	b	__hyp_panic
.endm

	invalid_vector	hyp_reset	ARM_EXCEPTION_RESET
	invalid_vector	hyp_undef	ARM_EXCEPTION_UNDEFINED
	invalid_vector	hyp_svc		ARM_EXCEPTION_SOFTWARE
	invalid_vector	hyp_pabt	ARM_EXCEPTION_PREF_ABORT
	invalid_vector	hyp_fiq		ARM_EXCEPTION_FIQ

ENTRY(__hyp_do_panic)
	mrs	lr, cpsr
	bic	lr, lr, #MODE_MASK
	orr	lr, lr, #SVC_MODE
THUMB(	orr	lr, lr, #PSR_T_BIT	)
	msr	spsr_cxsf, lr
	ldr	lr, =panic
	msr	ELR_hyp, lr
	ldr	lr, =kvm_call_hyp
	clrex
	eret
ENDPROC(__hyp_do_panic)

hyp_hvc:
	/*
	 * Getting here is either because of a trap from a guest,
	 * or from executing HVC from the host kernel, which means
	 * "do something in Hyp mode".
	 */
	push	{r0, r1, r2}

	@ Check syndrome register
	mrc	p15, 4, r1, c5, c2, 0	@ HSR
	lsr	r0, r1, #HSR_EC_SHIFT
	cmp	r0, #HSR_EC_HVC
	bne	guest_trap		@ Not HVC instr.

	/*
	 * Let's check if the HVC came from VMID 0 and allow simple
	 * switch to Hyp mode
	 */
	mrrc    p15, 6, r0, r2, c2
	lsr     r2, r2, #16
	and     r2, r2, #0xff
	cmp     r2, #0
	bne	guest_hvc_trap		@ Guest called HVC

	/*
	 * Getting here means host called HVC, we shift parameters and branch
	 * to Hyp function.
	 */
	pop	{r0, r1, r2}

	/*
	 * Check if we have a kernel function, which is guaranteed to be
	 * bigger than the maximum hyp stub hypercall
	 */
	cmp	r0, #HVC_STUB_HCALL_NR
	bhs	1f

	/*
	 * Not a kernel function, treat it as a stub hypercall.
	 * Compute the physical address for __kvm_handle_stub_hvc
	 * (as the code lives in the idmaped page) and branch there.
	 * We hijack ip (r12) as a tmp register.
	 */
	push	{r1}
	ldr	r1, =kimage_voffset
	ldr	r1, [r1]
	ldr	ip, =__kvm_handle_stub_hvc
	sub	ip, ip, r1
	pop	{r1}

	bx	ip

1:
	/*
	 * Pushing r2 here is just a way of keeping the stack aligned to
	 * 8 bytes on any path that can trigger a HYP exception. Here,
	 * we may well be about to jump into the guest, and the guest
	 * exit would otherwise be badly decoded by our fancy
	 * "decode-exception-without-a-branch" code...
	 */
	push	{r2, lr}

	mov	lr, r0
	mov	r0, r1
	mov	r1, r2
	mov	r2, r3

THUMB(	orr	lr, #1)
	blx	lr			@ Call the HYP function

	pop	{r2, lr}
	eret

guest_hvc_trap:
	movw	r2, #:lower16:ARM_SMCCC_ARCH_WORKAROUND_1
	movt	r2, #:upper16:ARM_SMCCC_ARCH_WORKAROUND_1
	ldr	r0, [sp]		@ Guest's r0
	teq	r0, r2
	bne	guest_trap
	add	sp, sp, #12
	@ Returns:
	@ r0 = 0
	@ r1 = HSR value (perfectly predictable)
	@ r2 = ARM_SMCCC_ARCH_WORKAROUND_1
	mov	r0, #0
	eret

guest_trap:
	load_vcpu r0			@ Load VCPU pointer to r0

#ifdef CONFIG_VFPv3
	@ Check for a VFP access
	lsr	r1, r1, #HSR_EC_SHIFT
	cmp	r1, #HSR_EC_CP_0_13
	beq	__vfp_guest_restore
#endif

	mov	r1, #ARM_EXCEPTION_HVC
	b	__guest_exit

hyp_irq:
	push	{r0, r1, r2}
	mov	r1, #ARM_EXCEPTION_IRQ
	load_vcpu r0			@ Load VCPU pointer to r0
	b	__guest_exit

hyp_dabt:
	push	{r0, r1}
	mrs	r0, ELR_hyp
	ldr	r1, =abort_guest_exit_start
THUMB(	add	r1, r1, #1)
	cmp	r0, r1
	ldrne	r1, =abort_guest_exit_end
THUMB(	addne	r1, r1, #1)
	cmpne	r0, r1
	pop	{r0, r1}
	bne	__hyp_panic

	orr	r0, r0, #(1 << ARM_EXIT_WITH_ABORT_BIT)
	eret

	.ltorg

	.popsection
