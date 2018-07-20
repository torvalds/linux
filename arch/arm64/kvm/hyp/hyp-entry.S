/*
 * Copyright (C) 2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/arm-smccc.h>
#include <linux/linkage.h>

#include <asm/alternative.h>
#include <asm/assembler.h>
#include <asm/cpufeature.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_mmu.h>

	.text
	.pushsection	.hyp.text, "ax"

.macro do_el2_call
	/*
	 * Shuffle the parameters before calling the function
	 * pointed to in x0. Assumes parameters in x[1,2,3].
	 */
	str	lr, [sp, #-16]!
	mov	lr, x0
	mov	x0, x1
	mov	x1, x2
	mov	x2, x3
	blr	lr
	ldr	lr, [sp], #16
.endm

ENTRY(__vhe_hyp_call)
	do_el2_call
	/*
	 * We used to rely on having an exception return to get
	 * an implicit isb. In the E2H case, we don't have it anymore.
	 * rather than changing all the leaf functions, just do it here
	 * before returning to the rest of the kernel.
	 */
	isb
	ret
ENDPROC(__vhe_hyp_call)

el1_sync:				// Guest trapped into EL2
	stp	x0, x1, [sp, #-16]!

alternative_if_not ARM64_HAS_VIRT_HOST_EXTN
	mrs	x1, esr_el2
alternative_else
	mrs	x1, esr_el1
alternative_endif
	lsr	x0, x1, #ESR_ELx_EC_SHIFT

	cmp	x0, #ESR_ELx_EC_HVC64
	ccmp	x0, #ESR_ELx_EC_HVC32, #4, ne
	b.ne	el1_trap

	mrs	x1, vttbr_el2		// If vttbr is valid, the guest
	cbnz	x1, el1_hvc_guest	// called HVC

	/* Here, we're pretty sure the host called HVC. */
	ldp	x0, x1, [sp], #16

	/* Check for a stub HVC call */
	cmp	x0, #HVC_STUB_HCALL_NR
	b.hs	1f

	/*
	 * Compute the idmap address of __kvm_handle_stub_hvc and
	 * jump there. Since we use kimage_voffset, do not use the
	 * HYP VA for __kvm_handle_stub_hvc, but the kernel VA instead
	 * (by loading it from the constant pool).
	 *
	 * Preserve x0-x4, which may contain stub parameters.
	 */
	ldr	x5, =__kvm_handle_stub_hvc
	ldr_l	x6, kimage_voffset

	/* x5 = __pa(x5) */
	sub	x5, x5, x6
	br	x5

1:
	/*
	 * Perform the EL2 call
	 */
	kern_hyp_va	x0
	do_el2_call

	eret

el1_hvc_guest:
	/*
	 * Fastest possible path for ARM_SMCCC_ARCH_WORKAROUND_1.
	 * The workaround has already been applied on the host,
	 * so let's quickly get back to the guest. We don't bother
	 * restoring x1, as it can be clobbered anyway.
	 */
	ldr	x1, [sp]				// Guest's x0
	eor	w1, w1, #ARM_SMCCC_ARCH_WORKAROUND_1
	cbnz	w1, el1_trap
	mov	x0, x1
	add	sp, sp, #16
	eret

el1_trap:
	/*
	 * x0: ESR_EC
	 */
	ldr	x1, [sp, #16 + 8]	// vcpu stored by __guest_enter

	/*
	 * We trap the first access to the FP/SIMD to save the host context
	 * and restore the guest context lazily.
	 * If FP/SIMD is not implemented, handle the trap and inject an
	 * undefined instruction exception to the guest.
	 */
alternative_if_not ARM64_HAS_NO_FPSIMD
	cmp	x0, #ESR_ELx_EC_FP_ASIMD
	b.eq	__fpsimd_guest_restore
alternative_else_nop_endif

	mov	x0, #ARM_EXCEPTION_TRAP
	b	__guest_exit

el1_irq:
	stp     x0, x1, [sp, #-16]!
	ldr	x1, [sp, #16 + 8]
	mov	x0, #ARM_EXCEPTION_IRQ
	b	__guest_exit

el1_error:
	stp     x0, x1, [sp, #-16]!
	ldr	x1, [sp, #16 + 8]
	mov	x0, #ARM_EXCEPTION_EL1_SERROR
	b	__guest_exit

el2_error:
	/*
	 * Only two possibilities:
	 * 1) Either we come from the exit path, having just unmasked
	 *    PSTATE.A: change the return code to an EL2 fault, and
	 *    carry on, as we're already in a sane state to handle it.
	 * 2) Or we come from anywhere else, and that's a bug: we panic.
	 *
	 * For (1), x0 contains the original return code and x1 doesn't
	 * contain anything meaningful at that stage. We can reuse them
	 * as temp registers.
	 * For (2), who cares?
	 */
	mrs	x0, elr_el2
	adr	x1, abort_guest_exit_start
	cmp	x0, x1
	adr	x1, abort_guest_exit_end
	ccmp	x0, x1, #4, ne
	b.ne	__hyp_panic
	mov	x0, #(1 << ARM_EXIT_WITH_SERROR_BIT)
	eret

ENTRY(__hyp_do_panic)
	mov	lr, #(PSR_F_BIT | PSR_I_BIT | PSR_A_BIT | PSR_D_BIT |\
		      PSR_MODE_EL1h)
	msr	spsr_el2, lr
	ldr	lr, =panic
	msr	elr_el2, lr
	eret
ENDPROC(__hyp_do_panic)

.macro invalid_vector	label, target = __hyp_panic
	.align	2
\label:
	b \target
ENDPROC(\label)
.endm

	/* None of these should ever happen */
	invalid_vector	el2t_sync_invalid
	invalid_vector	el2t_irq_invalid
	invalid_vector	el2t_fiq_invalid
	invalid_vector	el2t_error_invalid
	invalid_vector	el2h_sync_invalid
	invalid_vector	el2h_irq_invalid
	invalid_vector	el2h_fiq_invalid
	invalid_vector	el1_sync_invalid
	invalid_vector	el1_irq_invalid
	invalid_vector	el1_fiq_invalid

	.ltorg

	.align 11

ENTRY(__kvm_hyp_vector)
	ventry	el2t_sync_invalid		// Synchronous EL2t
	ventry	el2t_irq_invalid		// IRQ EL2t
	ventry	el2t_fiq_invalid		// FIQ EL2t
	ventry	el2t_error_invalid		// Error EL2t

	ventry	el2h_sync_invalid		// Synchronous EL2h
	ventry	el2h_irq_invalid		// IRQ EL2h
	ventry	el2h_fiq_invalid		// FIQ EL2h
	ventry	el2_error			// Error EL2h

	ventry	el1_sync			// Synchronous 64-bit EL1
	ventry	el1_irq				// IRQ 64-bit EL1
	ventry	el1_fiq_invalid			// FIQ 64-bit EL1
	ventry	el1_error			// Error 64-bit EL1

	ventry	el1_sync			// Synchronous 32-bit EL1
	ventry	el1_irq				// IRQ 32-bit EL1
	ventry	el1_fiq_invalid			// FIQ 32-bit EL1
	ventry	el1_error			// Error 32-bit EL1
ENDPROC(__kvm_hyp_vector)
