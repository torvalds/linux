/*
 * Contains CPU specific branch predictor invalidation sequences
 *
 * Copyright (C) 2018 ARM Ltd.
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
#include <linux/arm-smccc.h>

#include <asm/alternative.h>
#include <asm/mmu.h>

.macro hyp_ventry
	.align 7
1:	.rept 27
	nop
	.endr
/*
 * The default sequence is to directly branch to the KVM vectors,
 * using the computed offset. This applies for VHE as well as
 * !ARM64_HARDEN_EL2_VECTORS.
 *
 * For ARM64_HARDEN_EL2_VECTORS configurations, this gets replaced
 * with:
 *
 * stp	x0, x1, [sp, #-16]!
 * movz	x0, #(addr & 0xffff)
 * movk	x0, #((addr >> 16) & 0xffff), lsl #16
 * movk	x0, #((addr >> 32) & 0xffff), lsl #32
 * br	x0
 *
 * Where addr = kern_hyp_va(__kvm_hyp_vector) + vector-offset + 4.
 * See kvm_patch_vector_branch for details.
 */
alternative_cb	kvm_patch_vector_branch
	b	__kvm_hyp_vector + (1b - 0b)
	nop
	nop
	nop
	nop
alternative_cb_end
.endm

.macro generate_vectors
0:
	.rept 16
	hyp_ventry
	.endr
	.org 0b + SZ_2K		// Safety measure
.endm


	.text
	.pushsection	.hyp.text, "ax"

	.align	11
ENTRY(__bp_harden_hyp_vecs_start)
	.rept BP_HARDEN_EL2_SLOTS
	generate_vectors
	.endr
ENTRY(__bp_harden_hyp_vecs_end)

	.popsection

ENTRY(__smccc_workaround_1_smc_start)
	sub	sp, sp, #(8 * 4)
	stp	x2, x3, [sp, #(8 * 0)]
	stp	x0, x1, [sp, #(8 * 2)]
	mov	w0, #ARM_SMCCC_ARCH_WORKAROUND_1
	smc	#0
	ldp	x2, x3, [sp, #(8 * 0)]
	ldp	x0, x1, [sp, #(8 * 2)]
	add	sp, sp, #(8 * 4)
ENTRY(__smccc_workaround_1_smc_end)
