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

#define HAVE_JUMP_LABEL_BATCH

#define JUMP_LABEL_NOP_SIZE 4

#define JUMP_TABLE_ENTRY(key, label)			\
	".pushsection	__jump_table, \"aw\"	\n\t"	\
	".align		" RISCV_LGPTR "		\n\t"	\
	".long		1b - ., " label " - .	\n\t"	\
	"" RISCV_PTR "	" key " - .		\n\t"	\
	".popsection				\n\t"

/* This macro is also expanded on the Rust side. */
#define ARCH_STATIC_BRANCH_ASM(key, label)		\
	"	.align		2		\n\t"	\
	"	.option push			\n\t"	\
	"	.option norelax			\n\t"	\
	"	.option norvc			\n\t"	\
	"1:	nop				\n\t"	\
	"	.option pop			\n\t"	\
	JUMP_TABLE_ENTRY(key, label)

static __always_inline bool arch_static_branch(struct static_key * const key,
					       const bool branch)
{
	asm goto(
		ARCH_STATIC_BRANCH_ASM("%0", "%l[label]")
		:  :  "i"(&((char *)key)[branch]) :  : label);

	return false;
label:
	return true;
}

#define ARCH_STATIC_BRANCH_JUMP_ASM(key, label)		\
	"	.align		2		\n\t"	\
	"	.option push			\n\t"	\
	"	.option norelax			\n\t"	\
	"	.option norvc			\n\t"	\
	"1:	j	" label "		\n\t" \
	"	.option pop			\n\t"	\
	JUMP_TABLE_ENTRY(key, label)

static __always_inline bool arch_static_branch_jump(struct static_key * const key,
						    const bool branch)
{
	asm goto(
		ARCH_STATIC_BRANCH_JUMP_ASM("%0", "%l[label]")
		:  :  "i"(&((char *)key)[branch]) :  : label);

	return false;
label:
	return true;
}

#endif  /* __ASSEMBLY__ */
#endif	/* __ASM_JUMP_LABEL_H */
