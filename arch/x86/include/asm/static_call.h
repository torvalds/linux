/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_STATIC_CALL_H
#define _ASM_STATIC_CALL_H

#include <asm/text-patching.h>

/*
 * For CONFIG_HAVE_STATIC_CALL_INLINE, this is a temporary trampoline which
 * uses the current value of the key->func pointer to do an indirect jump to
 * the function.  This trampoline is only used during boot, before the call
 * sites get patched by static_call_update().  The name of this trampoline has
 * a magical aspect: objtool uses it to find static call sites so it can create
 * the .static_call_sites section.
 *
 * For CONFIG_HAVE_STATIC_CALL, this is a permanent trampoline which
 * does a direct jump to the function.  The direct jump gets patched by
 * static_call_update().
 *
 * Having the trampoline in a special section forces GCC to emit a JMP.d32 when
 * it does tail-call optimization on the call; since you cannot compute the
 * relative displacement across sections.
 */

/*
 * The trampoline is 8 bytes and of the general form:
 *
 *   jmp.d32 \func
 *   ud1 %esp, %ecx
 *
 * That trailing #UD provides both a speculation stop and serves as a unique
 * 3 byte signature identifying static call trampolines. Also see tramp_ud[]
 * and __static_call_fixup().
 */
#define __ARCH_DEFINE_STATIC_CALL_TRAMP(name, insns)			\
	asm(".pushsection .static_call.text, \"ax\"		\n"	\
	    ".align 4						\n"	\
	    ".globl " STATIC_CALL_TRAMP_STR(name) "		\n"	\
	    STATIC_CALL_TRAMP_STR(name) ":			\n"	\
	    ANNOTATE_NOENDBR						\
	    insns "						\n"	\
	    ".byte 0x0f, 0xb9, 0xcc				\n"	\
	    ".type " STATIC_CALL_TRAMP_STR(name) ", @function	\n"	\
	    ".size " STATIC_CALL_TRAMP_STR(name) ", . - " STATIC_CALL_TRAMP_STR(name) " \n" \
	    ".popsection					\n")

#define ARCH_DEFINE_STATIC_CALL_TRAMP(name, func)			\
	__ARCH_DEFINE_STATIC_CALL_TRAMP(name, ".byte 0xe9; .long " #func " - (. + 4)")

#ifdef CONFIG_MITIGATION_RETHUNK
#define ARCH_DEFINE_STATIC_CALL_NULL_TRAMP(name)			\
	__ARCH_DEFINE_STATIC_CALL_TRAMP(name, "jmp __x86_return_thunk")
#else
#define ARCH_DEFINE_STATIC_CALL_NULL_TRAMP(name)			\
	__ARCH_DEFINE_STATIC_CALL_TRAMP(name, "ret; int3; nop; nop; nop")
#endif

#define ARCH_DEFINE_STATIC_CALL_RET0_TRAMP(name)			\
	ARCH_DEFINE_STATIC_CALL_TRAMP(name, __static_call_return0)

#define ARCH_ADD_TRAMP_KEY(name)					\
	asm(".pushsection .static_call_tramp_key, \"a\"		\n"	\
	    ".long " STATIC_CALL_TRAMP_STR(name) " - .		\n"	\
	    ".long " STATIC_CALL_KEY_STR(name) " - .		\n"	\
	    ".popsection					\n")

extern bool __static_call_fixup(void *tramp, u8 op, void *dest);

extern void __static_call_update_early(void *tramp, void *func);

#define static_call_update_early(name, _func)				\
({									\
	typeof(&STATIC_CALL_TRAMP(name)) __F = (_func);			\
	if (static_call_initialized) {					\
		__static_call_update(&STATIC_CALL_KEY(name),		\
				     STATIC_CALL_TRAMP_ADDR(name), __F);\
	} else {							\
		WRITE_ONCE(STATIC_CALL_KEY(name).func, _func);		\
		__static_call_update_early(STATIC_CALL_TRAMP_ADDR(name),\
					   __F);			\
	}								\
})

#endif /* _ASM_STATIC_CALL_H */
