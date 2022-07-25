/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_ASM_POINTER_AUTH_H
#define __ASM_ASM_POINTER_AUTH_H

#include <asm/alternative.h>
#include <asm/asm-offsets.h>
#include <asm/cpufeature.h>
#include <asm/sysreg.h>

#ifdef CONFIG_ARM64_PTR_AUTH_KERNEL

	.macro __ptrauth_keys_install_kernel_nosync tsk, tmp1, tmp2, tmp3
	mov	\tmp1, #THREAD_KEYS_KERNEL
	add	\tmp1, \tsk, \tmp1
	ldp	\tmp2, \tmp3, [\tmp1, #PTRAUTH_KERNEL_KEY_APIA]
	msr_s	SYS_APIAKEYLO_EL1, \tmp2
	msr_s	SYS_APIAKEYHI_EL1, \tmp3
	.endm

	.macro ptrauth_keys_install_kernel_nosync tsk, tmp1, tmp2, tmp3
alternative_if ARM64_HAS_ADDRESS_AUTH
	__ptrauth_keys_install_kernel_nosync \tsk, \tmp1, \tmp2, \tmp3
alternative_else_nop_endif
	.endm

	.macro ptrauth_keys_install_kernel tsk, tmp1, tmp2, tmp3
alternative_if ARM64_HAS_ADDRESS_AUTH
	__ptrauth_keys_install_kernel_nosync \tsk, \tmp1, \tmp2, \tmp3
	isb
alternative_else_nop_endif
	.endm

#else /* CONFIG_ARM64_PTR_AUTH_KERNEL */

	.macro __ptrauth_keys_install_kernel_nosync tsk, tmp1, tmp2, tmp3
	.endm

	.macro ptrauth_keys_install_kernel_nosync tsk, tmp1, tmp2, tmp3
	.endm

	.macro ptrauth_keys_install_kernel tsk, tmp1, tmp2, tmp3
	.endm

#endif /* CONFIG_ARM64_PTR_AUTH_KERNEL */

#ifdef CONFIG_ARM64_PTR_AUTH
/*
 * thread.keys_user.ap* as offset exceeds the #imm offset range
 * so use the base value of ldp as thread.keys_user and offset as
 * thread.keys_user.ap*.
 */
	.macro __ptrauth_keys_install_user tsk, tmp1, tmp2, tmp3
	mov	\tmp1, #THREAD_KEYS_USER
	add	\tmp1, \tsk, \tmp1
	ldp	\tmp2, \tmp3, [\tmp1, #PTRAUTH_USER_KEY_APIA]
	msr_s	SYS_APIAKEYLO_EL1, \tmp2
	msr_s	SYS_APIAKEYHI_EL1, \tmp3
	.endm

	.macro __ptrauth_keys_init_cpu tsk, tmp1, tmp2, tmp3
	mrs	\tmp1, id_aa64isar1_el1
	ubfx	\tmp1, \tmp1, #ID_AA64ISAR1_EL1_APA_SHIFT, #8
	mrs_s	\tmp2, SYS_ID_AA64ISAR2_EL1
	ubfx	\tmp2, \tmp2, #ID_AA64ISAR2_EL1_APA3_SHIFT, #4
	orr	\tmp1, \tmp1, \tmp2
	cbz	\tmp1, .Lno_addr_auth\@
	mov_q	\tmp1, (SCTLR_ELx_ENIA | SCTLR_ELx_ENIB | \
			SCTLR_ELx_ENDA | SCTLR_ELx_ENDB)
	mrs	\tmp2, sctlr_el1
	orr	\tmp2, \tmp2, \tmp1
	msr	sctlr_el1, \tmp2
	__ptrauth_keys_install_kernel_nosync \tsk, \tmp1, \tmp2, \tmp3
	isb
.Lno_addr_auth\@:
	.endm

	.macro ptrauth_keys_init_cpu tsk, tmp1, tmp2, tmp3
alternative_if_not ARM64_HAS_ADDRESS_AUTH
	b	.Lno_addr_auth\@
alternative_else_nop_endif
	__ptrauth_keys_init_cpu \tsk, \tmp1, \tmp2, \tmp3
.Lno_addr_auth\@:
	.endm

#else /* !CONFIG_ARM64_PTR_AUTH */

	.macro ptrauth_keys_install_user tsk, tmp1, tmp2, tmp3
	.endm

#endif /* CONFIG_ARM64_PTR_AUTH */

#endif /* __ASM_ASM_POINTER_AUTH_H */
