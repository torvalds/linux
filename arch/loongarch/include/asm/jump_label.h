/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 *
 * Based on arch/arm64/include/asm/jump_label.h
 */
#ifndef __ASM_JUMP_LABEL_H
#define __ASM_JUMP_LABEL_H

#ifndef __ASSEMBLER__

#include <linux/types.h>

#define JUMP_LABEL_NOP_SIZE	4

/* This macro is also expanded on the Rust side. */
#define JUMP_TABLE_ENTRY(key, label)			\
	 ".pushsection	__jump_table, \"aw\"	\n\t"	\
	 ".align	3			\n\t"	\
	 ".long		1b - ., " label " - .	\n\t"	\
	 ".quad		" key " - .		\n\t"	\
	 ".popsection				\n\t"

#define ARCH_STATIC_BRANCH_ASM(key, label)		\
	"1:	nop				\n\t"	\
	JUMP_TABLE_ENTRY(key, label)

static __always_inline bool arch_static_branch(struct static_key * const key, const bool branch)
{
	asm goto(
		ARCH_STATIC_BRANCH_ASM("%0", "%l[l_yes]")
		:  :  "i"(&((char *)key)[branch]) :  : l_yes);

	return false;

l_yes:
	return true;
}

static __always_inline bool arch_static_branch_jump(struct static_key * const key, const bool branch)
{
	asm goto(
		"1:	b	%l[l_yes]	\n\t"
		JUMP_TABLE_ENTRY("%0", "%l[l_yes]")
		:  :  "i"(&((char *)key)[branch]) :  : l_yes);

	return false;

l_yes:
	return true;
}

#endif  /* __ASSEMBLER__ */
#endif	/* __ASM_JUMP_LABEL_H */
