/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_JUMP_LABEL_H
#define _ASM_POWERPC_JUMP_LABEL_H

/*
 * Copyright 2010 Michael Ellerman, IBM Corp.
 */

#ifndef __ASSEMBLY__
#include <linux/types.h>

#include <asm/feature-fixups.h>
#include <asm/asm-const.h>

#define JUMP_ENTRY_TYPE		stringify_in_c(FTR_ENTRY_LONG)
#define JUMP_LABEL_NOP_SIZE	4

static __always_inline bool arch_static_branch(struct static_key *key, bool branch)
{
	asm goto("1:\n\t"
		 "nop # arch_static_branch\n\t"
		 ".pushsection __jump_table,  \"aw\"\n\t"
		 ".long 1b - ., %l[l_yes] - .\n\t"
		 JUMP_ENTRY_TYPE "%c0 - .\n\t"
		 ".popsection \n\t"
		 : :  "i" (&((char *)key)[branch]) : : l_yes);

	return false;
l_yes:
	return true;
}

static __always_inline bool arch_static_branch_jump(struct static_key *key, bool branch)
{
	asm goto("1:\n\t"
		 "b %l[l_yes] # arch_static_branch_jump\n\t"
		 ".pushsection __jump_table,  \"aw\"\n\t"
		 ".long 1b - ., %l[l_yes] - .\n\t"
		 JUMP_ENTRY_TYPE "%c0 - .\n\t"
		 ".popsection \n\t"
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
