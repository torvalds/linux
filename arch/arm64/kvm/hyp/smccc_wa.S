/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015-2018 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <linux/arm-smccc.h>
#include <linux/linkage.h>

#include <asm/kvm_asm.h>
#include <asm/kvm_mmu.h>

	/*
	 * This is not executed directly and is instead copied into the vectors
	 * by install_bp_hardening_cb().
	 */
	.data
	.pushsection	.rodata
	.global		__smccc_workaround_1_smc
SYM_DATA_START(__smccc_workaround_1_smc)
	esb
	sub	sp, sp, #(8 * 4)
	stp	x2, x3, [sp, #(8 * 0)]
	stp	x0, x1, [sp, #(8 * 2)]
	mov	w0, #ARM_SMCCC_ARCH_WORKAROUND_1
	smc	#0
	ldp	x2, x3, [sp, #(8 * 0)]
	ldp	x0, x1, [sp, #(8 * 2)]
	add	sp, sp, #(8 * 4)
1:	.org __smccc_workaround_1_smc + __SMCCC_WORKAROUND_1_SMC_SZ
	.org 1b
SYM_DATA_END(__smccc_workaround_1_smc)

	.global		__smccc_workaround_3_smc
SYM_DATA_START(__smccc_workaround_3_smc)
	esb
	sub	sp, sp, #(8 * 4)
	stp	x2, x3, [sp, #(8 * 0)]
	stp	x0, x1, [sp, #(8 * 2)]
	mov	w0, #ARM_SMCCC_ARCH_WORKAROUND_3
	smc	#0
	ldp	x2, x3, [sp, #(8 * 0)]
	ldp	x0, x1, [sp, #(8 * 2)]
	add	sp, sp, #(8 * 4)
1:	.org __smccc_workaround_3_smc + __SMCCC_WORKAROUND_3_SMC_SZ
	.org 1b
SYM_DATA_END(__smccc_workaround_3_smc)

	.global	__spectre_bhb_loop_k8
SYM_DATA_START(__spectre_bhb_loop_k8)
	esb
	sub	sp, sp, #(8 * 2)
	stp	x0, x1, [sp, #(8 * 0)]
	mov	x0, #8
2:	b	. + 4
	subs	x0, x0, #1
	b.ne	2b
	dsb	nsh
	isb
	ldp	x0, x1, [sp, #(8 * 0)]
	add	sp, sp, #(8 * 2)
1:	.org __spectre_bhb_loop_k8 + __SPECTRE_BHB_LOOP_SZ
	.org 1b
SYM_DATA_END(__spectre_bhb_loop_k8)

	.global	__spectre_bhb_loop_k24
SYM_DATA_START(__spectre_bhb_loop_k24)
	esb
	sub	sp, sp, #(8 * 2)
	stp	x0, x1, [sp, #(8 * 0)]
	mov	x0, #8
2:	b	. + 4
	subs	x0, x0, #1
	b.ne	2b
	dsb	nsh
	isb
	ldp	x0, x1, [sp, #(8 * 0)]
	add	sp, sp, #(8 * 2)
1:	.org __spectre_bhb_loop_k24 + __SPECTRE_BHB_LOOP_SZ
	.org 1b
SYM_DATA_END(__spectre_bhb_loop_k24)

	.global	__spectre_bhb_loop_k32
SYM_DATA_START(__spectre_bhb_loop_k32)
	esb
	sub	sp, sp, #(8 * 2)
	stp	x0, x1, [sp, #(8 * 0)]
	mov	x0, #8
2:	b	. + 4
	subs	x0, x0, #1
	b.ne	2b
	dsb	nsh
	isb
	ldp	x0, x1, [sp, #(8 * 0)]
	add	sp, sp, #(8 * 2)
1:	.org __spectre_bhb_loop_k32 + __SPECTRE_BHB_LOOP_SZ
	.org 1b
SYM_DATA_END(__spectre_bhb_loop_k32)

	.global	__spectre_bhb_clearbhb
SYM_DATA_START(__spectre_bhb_clearbhb)
	esb
	clearbhb
	isb
1:	.org __spectre_bhb_clearbhb + __SPECTRE_BHB_CLEARBHB_SZ
	.org 1b
SYM_DATA_END(__spectre_bhb_clearbhb)
