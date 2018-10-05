/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_JUMP_LABEL_H
#define _ASM_X86_JUMP_LABEL_H

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

static __always_inline bool arch_static_branch(struct static_key *key, bool branch)
{
	asm_volatile_goto("STATIC_BRANCH_NOP l_yes=\"%l[l_yes]\" key=\"%c0\" "
			  "branch=\"%c1\""
			: :  "i" (key), "i" (branch) : : l_yes);
	return false;
l_yes:
	return true;
}

static __always_inline bool arch_static_branch_jump(struct static_key *key, bool branch)
{
	asm_volatile_goto("STATIC_BRANCH_JMP l_yes=\"%l[l_yes]\" key=\"%c0\" "
			  "branch=\"%c1\""
		: :  "i" (key), "i" (branch) : : l_yes);

	return false;
l_yes:
	return true;
}

#else	/* __ASSEMBLY__ */

.macro STATIC_BRANCH_NOP l_yes:req key:req branch:req
.Lstatic_branch_nop_\@:
	.byte STATIC_KEY_INIT_NOP
.Lstatic_branch_no_after_\@:
	.pushsection __jump_table, "aw"
	_ASM_ALIGN
	.long		.Lstatic_branch_nop_\@ - ., \l_yes - .
	_ASM_PTR        \key + \branch - .
	.popsection
.endm

.macro STATIC_BRANCH_JMP l_yes:req key:req branch:req
.Lstatic_branch_jmp_\@:
	.byte 0xe9
	.long \l_yes - .Lstatic_branch_jmp_after_\@
.Lstatic_branch_jmp_after_\@:
	.pushsection __jump_table, "aw"
	_ASM_ALIGN
	.long		.Lstatic_branch_jmp_\@ - ., \l_yes - .
	_ASM_PTR	\key + \branch - .
	.popsection
.endm

#endif	/* __ASSEMBLY__ */

#endif
