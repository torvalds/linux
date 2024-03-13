/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 ARM Ltd.
 */
#ifndef __ASM_KVM_MTE_H
#define __ASM_KVM_MTE_H

#ifdef __ASSEMBLY__

#include <asm/sysreg.h>

#ifdef CONFIG_ARM64_MTE

.macro mte_switch_to_guest g_ctxt, h_ctxt, reg1
alternative_if_not ARM64_MTE
	b	.L__skip_switch\@
alternative_else_nop_endif
	mrs	\reg1, hcr_el2
	tbz	\reg1, #(HCR_ATA_SHIFT), .L__skip_switch\@

	mrs_s	\reg1, SYS_RGSR_EL1
	str	\reg1, [\h_ctxt, #CPU_RGSR_EL1]
	mrs_s	\reg1, SYS_GCR_EL1
	str	\reg1, [\h_ctxt, #CPU_GCR_EL1]

	ldr	\reg1, [\g_ctxt, #CPU_RGSR_EL1]
	msr_s	SYS_RGSR_EL1, \reg1
	ldr	\reg1, [\g_ctxt, #CPU_GCR_EL1]
	msr_s	SYS_GCR_EL1, \reg1

.L__skip_switch\@:
.endm

.macro mte_switch_to_hyp g_ctxt, h_ctxt, reg1
alternative_if_not ARM64_MTE
	b	.L__skip_switch\@
alternative_else_nop_endif
	mrs	\reg1, hcr_el2
	tbz	\reg1, #(HCR_ATA_SHIFT), .L__skip_switch\@

	mrs_s	\reg1, SYS_RGSR_EL1
	str	\reg1, [\g_ctxt, #CPU_RGSR_EL1]
	mrs_s	\reg1, SYS_GCR_EL1
	str	\reg1, [\g_ctxt, #CPU_GCR_EL1]

	ldr	\reg1, [\h_ctxt, #CPU_RGSR_EL1]
	msr_s	SYS_RGSR_EL1, \reg1
	ldr	\reg1, [\h_ctxt, #CPU_GCR_EL1]
	msr_s	SYS_GCR_EL1, \reg1

	isb

.L__skip_switch\@:
.endm

#else /* !CONFIG_ARM64_MTE */

.macro mte_switch_to_guest g_ctxt, h_ctxt, reg1
.endm

.macro mte_switch_to_hyp g_ctxt, h_ctxt, reg1
.endm

#endif /* CONFIG_ARM64_MTE */
#endif /* __ASSEMBLY__ */
#endif /* __ASM_KVM_MTE_H */
