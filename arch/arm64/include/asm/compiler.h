/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_COMPILER_H
#define __ASM_COMPILER_H

#ifdef ARM64_ASM_ARCH
#define ARM64_ASM_PREAMBLE ".arch " ARM64_ASM_ARCH "\n"
#else
#define ARM64_ASM_PREAMBLE
#endif

#define xpaclri(ptr)							\
({									\
	register unsigned long __xpaclri_ptr asm("x30") = (ptr);	\
									\
	asm(								\
	ARM64_ASM_PREAMBLE						\
	"	hint	#7\n"						\
	: "+r" (__xpaclri_ptr));					\
									\
	__xpaclri_ptr;							\
})

#ifdef CONFIG_ARM64_PTR_AUTH_KERNEL
#define ptrauth_strip_kernel_insn_pac(ptr)	xpaclri(ptr)
#else
#define ptrauth_strip_kernel_insn_pac(ptr)	(ptr)
#endif

#ifdef CONFIG_ARM64_PTR_AUTH
#define ptrauth_strip_user_insn_pac(ptr)	xpaclri(ptr)
#else
#define ptrauth_strip_user_insn_pac(ptr)	(ptr)
#endif

#if !defined(CONFIG_BUILTIN_RETURN_ADDRESS_STRIPS_PAC)
#define __builtin_return_address(val)					\
	(void *)(ptrauth_strip_kernel_insn_pac((unsigned long)__builtin_return_address(val)))
#endif

#endif /* __ASM_COMPILER_H */
