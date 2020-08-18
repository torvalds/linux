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
#define ARCH_DEFINE_STATIC_CALL_TRAMP(name, func)			\
	asm(".pushsection .static_call.text, \"ax\"		\n"	\
	    ".align 4						\n"	\
	    ".globl " STATIC_CALL_TRAMP_STR(name) "		\n"	\
	    STATIC_CALL_TRAMP_STR(name) ":			\n"	\
	    "	.byte 0xe9 # jmp.d32				\n"	\
	    "	.long " #func " - (. + 4)			\n"	\
	    ".type " STATIC_CALL_TRAMP_STR(name) ", @function	\n"	\
	    ".size " STATIC_CALL_TRAMP_STR(name) ", . - " STATIC_CALL_TRAMP_STR(name) " \n" \
	    ".popsection					\n")

#endif /* _ASM_STATIC_CALL_H */
