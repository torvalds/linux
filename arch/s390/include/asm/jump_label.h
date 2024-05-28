/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_JUMP_LABEL_H
#define _ASM_S390_JUMP_LABEL_H

#define HAVE_JUMP_LABEL_BATCH

#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <linux/stringify.h>

#define JUMP_LABEL_NOP_SIZE 6

#ifdef CONFIG_CC_IS_CLANG
#define JUMP_LABEL_STATIC_KEY_CONSTRAINT "i"
#elif __GNUC__ < 9
#define JUMP_LABEL_STATIC_KEY_CONSTRAINT "X"
#else
#define JUMP_LABEL_STATIC_KEY_CONSTRAINT "jdd"
#endif

/*
 * We use a brcl 0,<offset> instruction for jump labels so it
 * can be easily distinguished from a hotpatch generated instruction.
 */
static __always_inline bool arch_static_branch(struct static_key *key, bool branch)
{
	asm goto("0:	brcl 0,%l[label]\n"
			  ".pushsection __jump_table,\"aw\"\n"
			  ".balign	8\n"
			  ".long	0b-.,%l[label]-.\n"
			  ".quad	%0+%1-.\n"
			  ".popsection\n"
			  : : JUMP_LABEL_STATIC_KEY_CONSTRAINT (key), "i" (branch) : : label);
	return false;
label:
	return true;
}

static __always_inline bool arch_static_branch_jump(struct static_key *key, bool branch)
{
	asm goto("0:	brcl 15,%l[label]\n"
			  ".pushsection __jump_table,\"aw\"\n"
			  ".balign	8\n"
			  ".long	0b-.,%l[label]-.\n"
			  ".quad	%0+%1-.\n"
			  ".popsection\n"
			  : : JUMP_LABEL_STATIC_KEY_CONSTRAINT (key), "i" (branch) : : label);
	return false;
label:
	return true;
}

#endif  /* __ASSEMBLY__ */
#endif
