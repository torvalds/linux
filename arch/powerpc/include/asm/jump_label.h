/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_JUMP_LABEL_H
#define _ASM_POWERPC_JUMP_LABEL_H

/*
 * Copyright 2010 Michael Ellerman, IBM Corp.
 */

#ifndef __ASSEMBLER__
#include <linux/types.h>

#include <asm/feature-fixups.h>
#include <asm/asm-const.h>

#define JUMP_ENTRY_TYPE		stringify_in_c(FTR_ENTRY_LONG)
#define JUMP_LABEL_NOP_SIZE	4

#define JUMP_TABLE_ENTRY(key, label)			\
	".pushsection __jump_table,  \"aw\"	\n\t"	\
	".long 1b - ., " label " - .		\n\t"	\
	JUMP_ENTRY_TYPE key " - .		\n\t"	\
	".popsection 				\n\t"

#define ARCH_STATIC_BRANCH_ASM(key, label)		\
	"1:	nop				\n\t"	\
	JUMP_TABLE_ENTRY(key, label)

static __always_inline bool arch_static_branch(struct static_key *key, bool branch)
{
	asm goto(
		 ARCH_STATIC_BRANCH_ASM("%c0", "%l[l_yes]")
		 : :  "i" (&((char *)key)[branch]) : : l_yes);

	return false;
l_yes:
	return true;
}

static __always_inline bool arch_static_branch_jump(struct static_key *key, bool branch)
{
	asm goto("1:\n\t"
		 "b %l[l_yes] # arch_static_branch_jump\n\t"
		 JUMP_TABLE_ENTRY("%c0", "%l[l_yes]")
		 : :  "i" (&((char *)key)[branch]) : : l_yes);

	return false;
l_yes:
	return true;
}

#else
#define ARCH_STATIC_BRANCH(LABEL, KEY)		\
1098:	nop;					\
	.pushsection __jump_table, "aw";	\
	.long 1098b - ., LABEL - .;		\
	FTR_ENTRY_LONG KEY - .;			\
	.popsection
#endif

#endif /* _ASM_POWERPC_JUMP_LABEL_H */
