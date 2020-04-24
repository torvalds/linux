/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_ASM_POINTER_AUTH_H
#define __ASM_ASM_POINTER_AUTH_H

#include <asm/alternative.h>
#include <asm/asm-offsets.h>
#include <asm/cpufeature.h>
#include <asm/sysreg.h>

#ifdef CONFIG_ARM64_PTR_AUTH
/*
 * thread.keys_user.ap* as offset exceeds the #imm offset range
 * so use the base value of ldp as thread.keys_user and offset as
 * thread.keys_user.ap*.
 */
	.macro ptrauth_keys_install_user tsk, tmp1, tmp2, tmp3
	mov	\tmp1, #THREAD_KEYS_USER
	add	\tmp1, \tsk, \tmp1
alternative_if_not ARM64_HAS_ADDRESS_AUTH
	b	.Laddr_auth_skip_\@
alternative_else_nop_endif
	ldp	\tmp2, \tmp3, [\tmp1, #PTRAUTH_USER_KEY_APIA]
	msr_s	SYS_APIAKEYLO_EL1, \tmp2
	msr_s	SYS_APIAKEYHI_EL1, \tmp3
	ldp	\tmp2, \tmp3, [\tmp1, #PTRAUTH_USER_KEY_APIB]
	msr_s	SYS_APIBKEYLO_EL1, \tmp2
	msr_s	SYS_APIBKEYHI_EL1, \tmp3
	ldp	\tmp2, \tmp3, [\tmp1, #PTRAUTH_USER_KEY_APDA]
	msr_s	SYS_APDAKEYLO_EL1, \tmp2
	msr_s	SYS_APDAKEYHI_EL1, \tmp3
	ldp	\tmp2, \tmp3, [\tmp1, #PTRAUTH_USER_KEY_APDB]
	msr_s	SYS_APDBKEYLO_EL1, \tmp2
	msr_s	SYS_APDBKEYHI_EL1, \tmp3
.Laddr_auth_skip_\@:
alternative_if ARM64_HAS_GENERIC_AUTH
	ldp	\tmp2, \tmp3, [\tmp1, #PTRAUTH_USER_KEY_APGA]
	msr_s	SYS_APGAKEYLO_EL1, \tmp2
	msr_s	SYS_APGAKEYHI_EL1, \tmp3
alternative_else_nop_endif
	.endm

	.macro ptrauth_keys_install_kernel tsk, sync, tmp1, tmp2, tmp3
alternative_if ARM64_HAS_ADDRESS_AUTH
	mov	\tmp1, #THREAD_KEYS_KERNEL
	add	\tmp1, \tsk, \tmp1
	ldp	\tmp2, \tmp3, [\tmp1, #PTRAUTH_KERNEL_KEY_APIA]
	msr_s	SYS_APIAKEYLO_EL1, \tmp2
	msr_s	SYS_APIAKEYHI_EL1, \tmp3
	.if     \sync == 1
	isb
	.endif
alternative_else_nop_endif
	.endm

#else /* CONFIG_ARM64_PTR_AUTH */

	.macro ptrauth_keys_install_user tsk, tmp1, tmp2, tmp3
	.endm

	.macro ptrauth_keys_install_kernel tsk, sync, tmp1, tmp2, tmp3
	.endm

#endif /* CONFIG_ARM64_PTR_AUTH */

#endif /* __ASM_ASM_POINTER_AUTH_H */
