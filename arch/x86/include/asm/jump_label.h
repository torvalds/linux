/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_JUMP_LABEL_H
#define _ASM_X86_JUMP_LABEL_H

#define HAVE_JUMP_LABEL_BATCH

#define JUMP_LABEL_NOP_SIZE 5

#ifdef CONFIG_X86_64
# define STATIC_KEY_INIT_NOP P6_NOP5_ATOMIC
#else
# define STATIC_KEY_INIT_NOP GENERIC_NOP5_ATOMIC
#endif

#include <asm/asm.h>
#include <asm/nops.h>

#ifndef __ASSEMBLY__

#include <linux/stringify.h>
#include <linux/types.h>

static __always_inline bool arch_static_branch(struct static_key * const key, const bool branch)
{
	asm_volatile_goto("1:"
		".byte " __stringify(STATIC_KEY_INIT_NOP) "\n\t"
		".pushsection __jump_table,  \"aw\" \n\t"
		_ASM_ALIGN "\n\t"
		".long 1b - ., %l[l_yes] - . \n\t"
		_ASM_PTR "%c0 + %c1 - .\n\t"
		".popsection \n\t"
		: :  "i" (key), "i" (branch) : : l_yes);

	return false;
l_yes:
	return true;
}

static __always_inline bool arch_static_branch_jump(struct static_key * const key, const bool branch)
{
	asm_volatile_goto("1:"
		".byte 0xe9\n\t .long %l[l_yes] - 2f\n\t"
		"2:\n\t"
		".pushsection __jump_table,  \"aw\" \n\t"
		_ASM_ALIGN "\n\t"
		".long 1b - ., %l[l_yes] - . \n\t"
		_ASM_PTR "%c0 + %c1 - .\n\t"
		".popsection \n\t"
		: :  "i" (key), "i" (branch) : : l_yes);

	return false;
l_yes:
	return true;
}

#else	/* __ASSEMBLY__ */

.macro STATIC_JUMP_IF_TRUE target, key, def
.Lstatic_jump_\@:
	.if \def
	/* Equivalent to "jmp.d32 \target" */
	.byte		0xe9
	.long		\target - .Lstatic_jump_after_\@
.Lstatic_jump_after_\@:
	.else
	.byte		STATIC_KEY_INIT_NOP
	.endif
	.pushsection __jump_table, "aw"
	_ASM_ALIGN
	.long		.Lstatic_jump_\@ - ., \target - .
	_ASM_PTR	\key - .
	.popsection
.endm

.macro STATIC_JUMP_IF_FALSE target, key, def
.Lstatic_jump_\@:
	.if \def
	.byte		STATIC_KEY_INIT_NOP
	.else
	/* Equivalent to "jmp.d32 \target" */
	.byte		0xe9
	.long		\target - .Lstatic_jump_after_\@
.Lstatic_jump_after_\@:
	.endif
	.pushsection __jump_table, "aw"
	_ASM_ALIGN
	.long		.Lstatic_jump_\@ - ., \target - .
	_ASM_PTR	\key + 1 - .
	.popsection
.endm

#endif	/* __ASSEMBLY__ */

#endif
