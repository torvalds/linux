/*
 * Copyright (C) 2016 - ARM Ltd
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
#include <asm/kvm_arm.h>
#include <asm/kvm_asm.h>

	.arch_extension     virt

	.text
	.pushsection	.hyp.text, "ax"

#define USR_REGS_OFFSET		(CPU_CTXT_GP_REGS + GP_REGS_USR)

/* int __guest_enter(struct kvm_vcpu *vcpu, struct kvm_cpu_context *host) */
ENTRY(__guest_enter)
	@ Save host registers
	add	r1, r1, #(USR_REGS_OFFSET + S_R4)
	stm	r1!, {r4-r12}
	str	lr, [r1, #4]	@ Skip SP_usr (already saved)

	@ Restore guest registers
	add	r0, r0,  #(VCPU_GUEST_CTXT + USR_REGS_OFFSET + S_R0)
	ldr	lr, [r0, #S_LR]
	ldm	r0, {r0-r12}

	clrex
	eret
ENDPROC(__guest_enter)

ENTRY(__guest_exit)
	/*
	 * return convention:
	 * guest r0, r1, r2 saved on the stack
	 * r0: vcpu pointer
	 * r1: exception code
	 */

	add	r2, r0, #(VCPU_GUEST_CTXT + USR_REGS_OFFSET + S_R3)
	stm	r2!, {r3-r12}
	str	lr, [r2, #4]
	add	r2, r0, #(VCPU_GUEST_CTXT + USR_REGS_OFFSET + S_R0)
	pop	{r3, r4, r5}		@ r0, r1, r2
	stm	r2, {r3-r5}

	ldr	r0, [r0, #VCPU_HOST_CTXT]
	add	r0, r0, #(USR_REGS_OFFSET + S_R4)
	ldm	r0!, {r4-r12}
	ldr	lr, [r0, #4]

	mov	r0, r1
	mrs	r1, SPSR
	mrs	r2, ELR_hyp
	mrc	p15, 4, r3, c5, c2, 0	@ HSR

	/*
	 * Force loads and stores to complete before unmasking aborts
	 * and forcing the delivery of the exception. This gives us a
	 * single instruction window, which the handler will try to
	 * match.
	 */
	dsb	sy
	cpsie	a

	.global	abort_guest_exit_start
abort_guest_exit_start:

	isb

	.global	abort_guest_exit_end
abort_guest_exit_end:

	/*
	 * If we took an abort, r0[31] will be set, and cmp will set
	 * the N bit in PSTATE.
	 */
	cmp	r0, #0
	msrmi	SPSR_cxsf, r1
	msrmi	ELR_hyp, r2
	mcrmi	p15, 4, r3, c5, c2, 0	@ HSR

	bx	lr
ENDPROC(__guest_exit)

/*
 * If VFPv3 support is not available, then we will not switch the VFP
 * registers; however cp10 and cp11 accesses will still trap and fallback
 * to the regular coprocessor emulation code, which currently will
 * inject an undefined exception to the guest.
 */
#ifdef CONFIG_VFPv3
ENTRY(__vfp_guest_restore)
	push	{r3, r4, lr}

	@ NEON/VFP used.  Turn on VFP access.
	mrc	p15, 4, r1, c1, c1, 2		@ HCPTR
	bic	r1, r1, #(HCPTR_TCP(10) | HCPTR_TCP(11))
	mcr	p15, 4, r1, c1, c1, 2		@ HCPTR
	isb

	@ Switch VFP/NEON hardware state to the guest's
	mov	r4, r0
	ldr	r0, [r0, #VCPU_HOST_CTXT]
	add	r0, r0, #CPU_CTXT_VFP
	bl	__vfp_save_state
	add	r0, r4, #(VCPU_GUEST_CTXT + CPU_CTXT_VFP)
	bl	__vfp_restore_state

	pop	{r3, r4, lr}
	pop	{r0, r1, r2}
	clrex
	eret
ENDPROC(__vfp_guest_restore)
#endif

	.popsection

