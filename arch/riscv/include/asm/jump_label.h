/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Emil Renner Berthing
 *
 * Based on arch/arm64/include/asm/jump_label.h
 */
#ifndef __ASM_JUMP_LABEL_H
#define __ASM_JUMP_LABEL_H

#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <asm/asm.h>

#define JUMP_LABEL_NOP_SIZE 4

static __always_inline bool arch_static_branch(struct static_key *key,
					       bool branch)
{
	asm_volatile_goto(
		"	.option push				\n\t"
		"	.option norelax				\n\t"
		"	.option norvc				\n\t"
		"1:	nop					\n\t"
		"	.option pop				\n\t"
		"	.pushsection	__jump_table, \"aw\"	\n\t"
		"	.align		" RISCV_LGPTR "		\n\t"
		"	.long		1b - ., %l[label] - .	\n\t"
		"	" RISCV_PTR "	%0 - .			\n\t"
		"	.popsection				\n\t"
		:  :  "i"(&((char *)key)[branch]) :  : label);

	return false;
label:
	return true;
}

static __always_inline bool arch_static_branch_jump(struct static_key *key,
						    bool branch)
{
	asm_volatile_goto(
		"	.option push				\n\t"
		"	.option norelax				\n\t"
		"	.option norvc				\n\t"
		"1:	jal		zero, %l[label]		\n\t"
		"	.option pop				\n\t"
		"	.pushsection	__jump_table, \"aw\"	\n\t"
		"	.align		" RISCV_LGPTR "		\n\t"
		"	.long		1b - ., %l[label] - .	\n\t"
		"	" RISCV_PTR "	%0 - .			\n\t"
		"	.popsection				\n\t"
		:  :  "i"(&((char *)key)[branch]) :  : label);

	return false;
label:
	return true;
}

#endif  /* __ASSEMBLY__ */
#endif	/* __ASM_JUMP_LABEL_H */
