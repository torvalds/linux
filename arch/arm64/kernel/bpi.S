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

.macro hyp_ventry
	.align 7
1:	.rept 27
	nop
	.endr
	b	__kvm_hyp_vector + (1b - 0b)
	nop
	nop
	nop
	nop
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
	.rept 4
	generate_vectors
	.endr
ENTRY(__bp_harden_hyp_vecs_end)

	.popsection

ENTRY(__qcom_hyp_sanitize_link_stack_start)
	stp     x29, x30, [sp, #-16]!
	.rept	16
	bl	. + 4
	.endr
	ldp	x29, x30, [sp], #16
ENTRY(__qcom_hyp_sanitize_link_stack_end)

.macro smccc_workaround_1 inst
	sub	sp, sp, #(8 * 4)
	stp	x2, x3, [sp, #(8 * 0)]
	stp	x0, x1, [sp, #(8 * 2)]
	mov	w0, #ARM_SMCCC_ARCH_WORKAROUND_1
	\inst	#0
	ldp	x2, x3, [sp, #(8 * 0)]
	ldp	x0, x1, [sp, #(8 * 2)]
	add	sp, sp, #(8 * 4)
.endm

ENTRY(__smccc_workaround_1_smc_start)
	smccc_workaround_1	smc
ENTRY(__smccc_workaround_1_smc_end)

ENTRY(__smccc_workaround_1_hvc_start)
	smccc_workaround_1	hvc
ENTRY(__smccc_workaround_1_hvc_end)
