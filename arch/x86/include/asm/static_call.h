/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_STATIC_CALL_H
#define _ASM_STATIC_CALL_H

#include <asm/text-patching.h>

/*
 * For CONFIG_HAVE_STATIC_CALL, this is a permanent trampoline which
 * does a direct jump to the function.  The direct jump gets patched by
 * static_call_update().
 */
#define ARCH_DEFINE_STATIC_CALL_TRAMP(name, func)			\
	asm(".pushsection .text, \"ax\"				\n"	\
	    ".align 4						\n"	\
	    ".globl " STATIC_CALL_TRAMP_STR(name) "		\n"	\
	    STATIC_CALL_TRAMP_STR(name) ":			\n"	\
	    "	.byte 0xe9 # jmp.d32				\n"	\
	    "	.long " #func " - (. + 4)			\n"	\
	    ".type " STATIC_CALL_TRAMP_STR(name) ", @function	\n"	\
	    ".size " STATIC_CALL_TRAMP_STR(name) ", . - " STATIC_CALL_TRAMP_STR(name) " \n" \
	    ".popsection					\n")

#endif /* _ASM_STATIC_CALL_H */
