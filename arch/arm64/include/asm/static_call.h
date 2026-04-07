/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_STATIC_CALL_H
#define _ASM_STATIC_CALL_H

#define __ARCH_DEFINE_STATIC_CALL_TRAMP(name, target)		    \
	asm("	.pushsection .static_call.text, \"ax\"		\n" \
	    "	.align	4					\n" \
	    "	.globl	" name "				\n" \
	    name ":						\n" \
	    "	hint	34	/* BTI C */			\n" \
	    "	adrp	x16, 1f					\n" \
	    "	ldr	x16, [x16, :lo12:1f]			\n" \
	    "	br	x16					\n" \
	    "	.type	" name ", %function			\n" \
	    "	.size	" name ", . - " name "			\n" \
	    "	.popsection					\n" \
	    "	.pushsection .rodata, \"a\"			\n" \
	    "	.align	3					\n" \
	    "1:	.quad	" target "				\n" \
	    "	.popsection					\n")

#define ARCH_DEFINE_STATIC_CALL_TRAMP(name, func)			\
	__ARCH_DEFINE_STATIC_CALL_TRAMP(STATIC_CALL_TRAMP_STR(name), #func)

#define ARCH_DEFINE_STATIC_CALL_NULL_TRAMP(name)			\
	ARCH_DEFINE_STATIC_CALL_TRAMP(name, __static_call_return0)

#define ARCH_DEFINE_STATIC_CALL_RET0_TRAMP(name)			\
	ARCH_DEFINE_STATIC_CALL_TRAMP(name, __static_call_return0)

#endif /* _ASM_STATIC_CALL_H */
