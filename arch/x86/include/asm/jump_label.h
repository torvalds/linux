/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_JUMP_LABEL_H
#define _ASM_X86_JUMP_LABEL_H

#define HAVE_JUMP_LABEL_BATCH

#include <asm/asm.h>
#include <asm/nops.h>

#ifndef __ASSEMBLER__

#include <linux/stringify.h>
#include <linux/types.h>

#define JUMP_TABLE_ENTRY(key, label)			\
	".pushsection __jump_table,  \"aw\" \n\t"	\
	_ASM_ALIGN "\n\t"				\
	".long 1b - . \n\t"				\
	".long " label " - . \n\t"			\
	_ASM_PTR " " key " - . \n\t"			\
	".popsection \n\t"

/* This macro is also expanded on the Rust side. */
#ifdef CONFIG_HAVE_JUMP_LABEL_HACK
#define ARCH_STATIC_BRANCH_ASM(key, label)		\
	"1: jmp " label " # objtool NOPs this \n\t"	\
	JUMP_TABLE_ENTRY(key " + 2", label)
#else /* !CONFIG_HAVE_JUMP_LABEL_HACK */
#define ARCH_STATIC_BRANCH_ASM(key, label)		\
	"1: .byte " __stringify(BYTES_NOP5) "\n\t"	\
	JUMP_TABLE_ENTRY(key, label)
#endif /* CONFIG_HAVE_JUMP_LABEL_HACK */

static __always_inline bool arch_static_branch(struct static_key * const key, const bool branch)
{
	asm goto(ARCH_STATIC_BRANCH_ASM("%c0 + %c1", "%l[l_yes]")
		: :  "i" (key), "i" (branch) : : l_yes);

	return false;
l_yes:
	return true;
}

static __always_inline bool arch_static_branch_jump(struct static_key * const key, const bool branch)
{
	asm goto("1:"
		"jmp %l[l_yes]\n\t"
		JUMP_TABLE_ENTRY("%c0 + %c1", "%l[l_yes]")
		: :  "i" (key), "i" (branch) : : l_yes);

	return false;
l_yes:
	return true;
}

extern int arch_jump_entry_size(struct jump_entry *entry);

#endif	/* __ASSEMBLER__ */

#endif
