/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 *
 * Based on arch/arm64/include/asm/jump_label.h
 */
#ifndef __ASM_JUMP_LABEL_H
#define __ASM_JUMP_LABEL_H

#ifndef __ASSEMBLY__

#include <linux/types.h>

#define JUMP_LABEL_NOP_SIZE	4

#define JUMP_TABLE_ENTRY				\
	 ".pushsection	__jump_table, \"aw\"	\n\t"	\
	 ".align	3			\n\t"	\
	 ".long		1b - ., %l[l_yes] - .	\n\t"	\
	 ".quad		%0 - .			\n\t"	\
	 ".popsection				\n\t"

static __always_inline bool arch_static_branch(struct static_key * const key, const bool branch)
{
	asm goto(
		"1:	nop			\n\t"
		JUMP_TABLE_ENTRY
		:  :  "i"(&((char *)key)[branch]) :  : l_yes);

	return false;

l_yes:
	return true;
}

static __always_inline bool arch_static_branch_jump(struct static_key * const key, const bool branch)
{
	asm goto(
		"1:	b	%l[l_yes]	\n\t"
		JUMP_TABLE_ENTRY
		:  :  "i"(&((char *)key)[branch]) :  : l_yes);

	return false;

l_yes:
	return true;
}

#endif  /* __ASSEMBLY__ */
#endif	/* __ASM_JUMP_LABEL_H */
