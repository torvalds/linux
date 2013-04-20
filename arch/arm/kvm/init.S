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
#include <asm/unified.h>
#include <asm/asm-offsets.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_arm.h>

/********************************************************************
 * Hypervisor initialization
 *   - should be called with:
 *       r0,r1 = Hypervisor pgd pointer
 *       r2 = top of Hyp stack (kernel VA)
 *       r3 = pointer to hyp vectors
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
	@ Set the HTTBR to point to the hypervisor PGD pointer passed
	mcrr	p15, 4, r0, r1, c2

	@ Set the HTCR and VTCR to the same shareability and cacheability
	@ settings as the non-secure TTBCR and with T0SZ == 0.
	mrc	p15, 4, r0, c2, c0, 2	@ HTCR
	ldr	r12, =HTCR_MASK
	bic	r0, r0, r12
	mrc	p15, 0, r1, c2, c0, 2	@ TTBCR
	and	r1, r1, #(HTCR_MASK & ~TTBCR_T0SZ)
	orr	r0, r0, r1
	mcr	p15, 4, r0, c2, c0, 2	@ HTCR

	mrc	p15, 4, r1, c2, c1, 2	@ VTCR
	ldr	r12, =VTCR_MASK
	bic	r1, r1, r12
	bic	r0, r0, #(~VTCR_HTCR_SH)	@ clear non-reusable HTCR bits
	orr	r1, r0, r1
	orr	r1, r1, #(KVM_VTCR_SL0 | KVM_VTCR_T0SZ | KVM_VTCR_S)
	mcr	p15, 4, r1, c2, c1, 2	@ VTCR

	@ Use the same memory attributes for hyp. accesses as the kernel
	@ (copy MAIRx ro HMAIRx).
	mrc	p15, 0, r0, c10, c2, 0
	mcr	p15, 4, r0, c10, c2, 0
	mrc	p15, 0, r0, c10, c2, 1
	mcr	p15, 4, r0, c10, c2, 1

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
	ldr	r12, =HSCTLR_MASK
	bic	r0, r0, r12
	mrc	p15, 0, r1, c1, c0, 0	@ SCTLR
	ldr	r12, =(HSCTLR_EE | HSCTLR_FI | HSCTLR_I | HSCTLR_C)
	and	r1, r1, r12
 ARM(	ldr	r12, =(HSCTLR_M | HSCTLR_A)			)
 THUMB(	ldr	r12, =(HSCTLR_M | HSCTLR_A | HSCTLR_TE)		)
	orr	r1, r1, r12
	orr	r0, r0, r1
	isb
	mcr	p15, 4, r0, c1, c0, 0	@ HSCR
	isb

	@ Set stack pointer and return to the kernel
	mov	sp, r2

	@ Set HVBAR to point to the HYP vectors
	mcr	p15, 4, r3, c12, c0, 0	@ HVBAR

	eret

	.ltorg

	.globl __kvm_hyp_init_end
__kvm_hyp_init_end:

	.popsection
