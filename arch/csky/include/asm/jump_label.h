/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __ASM_CSKY_JUMP_LABEL_H
#define __ASM_CSKY_JUMP_LABEL_H

#ifndef __ASSEMBLY__

#include <linux/types.h>

#define JUMP_LABEL_NOP_SIZE 4

static __always_inline bool arch_static_branch(struct static_key *key,
					       bool branch)
{
	asm_volatile_goto(
		"1:	nop32					\n"
		"	.pushsection	__jump_table, \"aw\"	\n"
		"	.align		2			\n"
		"	.long		1b - ., %l[label] - .	\n"
		"	.long		%0 - .			\n"
		"	.popsection				\n"
		:  :  "i"(&((char *)key)[branch]) :  : label);

	return false;
label:
	return true;
}

static __always_inline bool arch_static_branch_jump(struct static_key *key,
						    bool branch)
{
	asm_volatile_goto(
		"1:	bsr32		%l[label]		\n"
		"	.pushsection	__jump_table, \"aw\"	\n"
		"	.align		2			\n"
		"	.long		1b - ., %l[label] - .	\n"
		"	.long		%0 - .			\n"
		"	.popsection				\n"
		:  :  "i"(&((char *)key)[branch]) :  : label);

	return false;
label:
	return true;
}

#endif  /* __ASSEMBLY__ */
#endif	/* __ASM_CSKY_JUMP_LABEL_H */
