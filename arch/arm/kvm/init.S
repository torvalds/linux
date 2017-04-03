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

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/unified.h>
#include <asm/asm-offsets.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_mmu.h>
#include <asm/virt.h>

/********************************************************************
 * Hypervisor initialization
 *   - should be called with:
 *       r0 = top of Hyp stack (kernel VA)
 *       r1 = pointer to hyp vectors
 *       r2,r3 = Hypervisor pgd pointer
 *
 * The init scenario is:
 * - We jump in HYP with 3 parameters: runtime HYP pgd, runtime stack,
 *   runtime vectors
 * - Invalidate TLBs
 * - Set stack and vectors
 * - Setup the page tables
 * - Enable the MMU
 * - Profit! (or eret, if you only care about the code).
 *
 * Another possibility is to get a HYP stub hypercall.
 * We discriminate between the two by checking if r0 contains a value
 * that is less than HVC_STUB_HCALL_NR.
 */

	.text
	.pushsection    .hyp.idmap.text,"ax"
	.align 5
__kvm_hyp_init:
	.globl __kvm_hyp_init

	@ Hyp-mode exception vector
	W(b)	.
	W(b)	.
	W(b)	.
	W(b)	.
	W(b)	.
	W(b)	__do_hyp_init
	W(b)	.
	W(b)	.

__do_hyp_init:
	@ Check for a stub hypercall
	cmp	r0, #HVC_STUB_HCALL_NR
	blo	__kvm_handle_stub_hvc

	@ Set stack pointer
	mov	sp, r0

	@ Set HVBAR to point to the HYP vectors
	mcr	p15, 4, r1, c12, c0, 0	@ HVBAR

	@ Set the HTTBR to point to the hypervisor PGD pointer passed
	mcrr	p15, 4, rr_lo_hi(r2, r3), c2

	@ Set the HTCR and VTCR to the same shareability and cacheability
	@ settings as the non-secure TTBCR and with T0SZ == 0.
	mrc	p15, 4, r0, c2, c0, 2	@ HTCR
	ldr	r2, =HTCR_MASK
	bic	r0, r0, r2
	mrc	p15, 0, r1, c2, c0, 2	@ TTBCR
	and	r1, r1, #(HTCR_MASK & ~TTBCR_T0SZ)
	orr	r0, r0, r1
	mcr	p15, 4, r0, c2, c0, 2	@ HTCR

	@ Use the same memory attributes for hyp. accesses as the kernel
	@ (copy MAIRx ro HMAIRx).
	mrc	p15, 0, r0, c10, c2, 0
	mcr	p15, 4, r0, c10, c2, 0
	mrc	p15, 0, r0, c10, c2, 1
	mcr	p15, 4, r0, c10, c2, 1

	@ Invalidate the stale TLBs from Bootloader
	mcr	p15, 4, r0, c8, c7, 0	@ TLBIALLH
	dsb	ish

	@ Set the HSCTLR to:
	@  - ARM/THUMB exceptions: Kernel config (Thumb-2 kernel)
	@  - Endianness: Kernel config
	@  - Fast Interrupt Features: Kernel config
	@  - Write permission implies XN: disabled
	@  - Instruction cache: enabled
	@  - Data/Unified cache: enabled
	@  - Memory alignment checks: enabled
	@  - MMU: enabled (this code must be run from an identity mapping)
	mrc	p15, 4, r0, c1, c0, 0	@ HSCR
	ldr	r2, =HSCTLR_MASK
	bic	r0, r0, r2
	mrc	p15, 0, r1, c1, c0, 0	@ SCTLR
	ldr	r2, =(HSCTLR_EE | HSCTLR_FI | HSCTLR_I | HSCTLR_C)
	and	r1, r1, r2
 ARM(	ldr	r2, =(HSCTLR_M | HSCTLR_A)			)
 THUMB(	ldr	r2, =(HSCTLR_M | HSCTLR_A | HSCTLR_TE)		)
	orr	r1, r1, r2
	orr	r0, r0, r1
	mcr	p15, 4, r0, c1, c0, 0	@ HSCR
	isb

	eret

ENTRY(__kvm_handle_stub_hvc)
	cmp	r0, #HVC_SOFT_RESTART
	bne	1f

	/* The target is expected in r1 */
	msr	ELR_hyp, r1
	mrs	r0, cpsr
	bic	r0, r0, #MODE_MASK
	orr	r0, r0, #HYP_MODE
THUMB(	orr	r0, r0, #PSR_T_BIT	)
	msr	spsr_cxsf, r0
	b	reset

1:	cmp	r0, #HVC_RESET_VECTORS
	bne	1f

reset:
	/* We're now in idmap, disable MMU */
	mrc	p15, 4, r1, c1, c0, 0	@ HSCTLR
	ldr	r0, =(HSCTLR_M | HSCTLR_A | HSCTLR_C | HSCTLR_I)
	bic	r1, r1, r0
	mcr	p15, 4, r1, c1, c0, 0	@ HSCTLR

	/*
	 * Install stub vectors, using ardb's VA->PA trick.
	 */
0:	adr	r0, 0b					@ PA(0)
	movw	r1, #:lower16:__hyp_stub_vectors - 0b   @ VA(stub) - VA(0)
	movt	r1, #:upper16:__hyp_stub_vectors - 0b
	add	r1, r1, r0				@ PA(stub)
	mcr	p15, 4, r1, c12, c0, 0	@ HVBAR
	b	exit

1:	ldr	r0, =HVC_STUB_ERR
	eret

exit:
	mov	r0, #0
	eret
ENDPROC(__kvm_handle_stub_hvc)

	.ltorg

	.globl __kvm_hyp_init_end
__kvm_hyp_init_end:

	.popsection
