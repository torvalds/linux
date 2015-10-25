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

#include <linux/linkage.h>

#include <asm/asm-offsets.h>
#include <asm/assembler.h>
#include <asm/fpsimdmacros.h>
#include <asm/kvm.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_mmu.h>

#define CPU_GP_REG_OFFSET(x)	(CPU_GP_REGS + x)
#define CPU_XREG_OFFSET(x)	CPU_GP_REG_OFFSET(CPU_USER_PT_REGS + 8*x)

	.text
	.pushsection	.hyp.text, "ax"

.macro save_callee_saved_regs ctxt
	stp	x19, x20, [\ctxt, #CPU_XREG_OFFSET(19)]
	stp	x21, x22, [\ctxt, #CPU_XREG_OFFSET(21)]
	stp	x23, x24, [\ctxt, #CPU_XREG_OFFSET(23)]
	stp	x25, x26, [\ctxt, #CPU_XREG_OFFSET(25)]
	stp	x27, x28, [\ctxt, #CPU_XREG_OFFSET(27)]
	stp	x29, lr,  [\ctxt, #CPU_XREG_OFFSET(29)]
.endm

.macro restore_callee_saved_regs ctxt
	ldp	x19, x20, [\ctxt, #CPU_XREG_OFFSET(19)]
	ldp	x21, x22, [\ctxt, #CPU_XREG_OFFSET(21)]
	ldp	x23, x24, [\ctxt, #CPU_XREG_OFFSET(23)]
	ldp	x25, x26, [\ctxt, #CPU_XREG_OFFSET(25)]
	ldp	x27, x28, [\ctxt, #CPU_XREG_OFFSET(27)]
	ldp	x29, lr,  [\ctxt, #CPU_XREG_OFFSET(29)]
.endm

/*
 * u64 __guest_enter(struct kvm_vcpu *vcpu,
 *		     struct kvm_cpu_context *host_ctxt);
 */
ENTRY(__guest_enter)
	// x0: vcpu
	// x1: host/guest context
	// x2-x18: clobbered by macros

	// Store the host regs
	save_callee_saved_regs x1

	// Preserve vcpu & host_ctxt for use at exit time
	stp	x0, x1, [sp, #-16]!

	add	x1, x0, #VCPU_CONTEXT

	// Prepare x0-x1 for later restore by pushing them onto the stack
	ldp	x2, x3, [x1, #CPU_XREG_OFFSET(0)]
	stp	x2, x3, [sp, #-16]!

	// x2-x18
	ldp	x2, x3,   [x1, #CPU_XREG_OFFSET(2)]
	ldp	x4, x5,   [x1, #CPU_XREG_OFFSET(4)]
	ldp	x6, x7,   [x1, #CPU_XREG_OFFSET(6)]
	ldp	x8, x9,   [x1, #CPU_XREG_OFFSET(8)]
	ldp	x10, x11, [x1, #CPU_XREG_OFFSET(10)]
	ldp	x12, x13, [x1, #CPU_XREG_OFFSET(12)]
	ldp	x14, x15, [x1, #CPU_XREG_OFFSET(14)]
	ldp	x16, x17, [x1, #CPU_XREG_OFFSET(16)]
	ldr	x18,      [x1, #CPU_XREG_OFFSET(18)]

	// x19-x29, lr
	restore_callee_saved_regs x1

	// Last bits of the 64bit state
	ldp	x0, x1, [sp], #16

	// Do not touch any register after this!
	eret
ENDPROC(__guest_enter)

ENTRY(__guest_exit)
	// x0: vcpu
	// x1: return code
	// x2-x3: free
	// x4-x29,lr: vcpu regs
	// vcpu x0-x3 on the stack

	add	x2, x0, #VCPU_CONTEXT

	stp	x4, x5,   [x2, #CPU_XREG_OFFSET(4)]
	stp	x6, x7,   [x2, #CPU_XREG_OFFSET(6)]
	stp	x8, x9,   [x2, #CPU_XREG_OFFSET(8)]
	stp	x10, x11, [x2, #CPU_XREG_OFFSET(10)]
	stp	x12, x13, [x2, #CPU_XREG_OFFSET(12)]
	stp	x14, x15, [x2, #CPU_XREG_OFFSET(14)]
	stp	x16, x17, [x2, #CPU_XREG_OFFSET(16)]
	str	x18,      [x2, #CPU_XREG_OFFSET(18)]

	ldp	x6, x7, [sp], #16	// x2, x3
	ldp	x4, x5, [sp], #16	// x0, x1

	stp	x4, x5, [x2, #CPU_XREG_OFFSET(0)]
	stp	x6, x7, [x2, #CPU_XREG_OFFSET(2)]

	save_callee_saved_regs x2

	// Restore vcpu & host_ctxt from the stack
	// (preserving return code in x1)
	ldp	x0, x2, [sp], #16
	// Now restore the host regs
	restore_callee_saved_regs x2

	mov	x0, x1
	ret
ENDPROC(__guest_exit)

ENTRY(__fpsimd_guest_restore)
	stp	x4, lr, [sp, #-16]!

	mrs	x2, cptr_el2
	bic	x2, x2, #CPTR_EL2_TFP
	msr	cptr_el2, x2
	isb

	mrs	x3, tpidr_el2

	ldr	x0, [x3, #VCPU_HOST_CONTEXT]
	kern_hyp_va x0
	add	x0, x0, #CPU_GP_REG_OFFSET(CPU_FP_REGS)
	bl	__fpsimd_save_state

	add	x2, x3, #VCPU_CONTEXT
	add	x0, x2, #CPU_GP_REG_OFFSET(CPU_FP_REGS)
	bl	__fpsimd_restore_state

	// Skip restoring fpexc32 for AArch64 guests
	mrs	x1, hcr_el2
	tbnz	x1, #HCR_RW_SHIFT, 1f
	ldr	x4, [x3, #VCPU_FPEXC32_EL2]
	msr	fpexc32_el2, x4
1:
	ldp	x4, lr, [sp], #16
	ldp	x2, x3, [sp], #16
	ldp	x0, x1, [sp], #16

	eret
ENDPROC(__fpsimd_guest_restore)
