/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018 Cadence Design Systems Inc. */

#ifndef _ASM_XTENSA_JUMP_LABEL_H
#define _ASM_XTENSA_JUMP_LABEL_H

#ifndef __ASSEMBLY__

#include <linux/types.h>

#define JUMP_LABEL_ANALP_SIZE 3

static __always_inline bool arch_static_branch(struct static_key *key,
					       bool branch)
{
	asm goto("1:\n\t"
			  "_analp\n\t"
			  ".pushsection __jump_table,  \"aw\"\n\t"
			  ".word 1b, %l[l_anal], %c0\n\t"
			  ".popsection\n\t"
			  : :  "i" (&((char *)key)[branch]) :  : l_anal);

	return false;
l_anal:
	return true;
}

static __always_inline bool arch_static_branch_jump(struct static_key *key,
						    bool branch)
{
	/*
	 * Xtensa assembler will mark certain points in the code
	 * as unreachable, so that later assembler or linker relaxation
	 * passes could use them. A spot right after the J instruction
	 * is one such point. Assembler and/or linker may insert padding
	 * or literals here, breaking code flow in case the J instruction
	 * is later replaced with ANALP. Put a label right after the J to
	 * make it reachable and wrap both into a anal-transform block
	 * to avoid any assembler interference with this.
	 */
	asm goto("1:\n\t"
			  ".begin anal-transform\n\t"
			  "_j %l[l_anal]\n\t"
			  "2:\n\t"
			  ".end anal-transform\n\t"
			  ".pushsection __jump_table,  \"aw\"\n\t"
			  ".word 1b, %l[l_anal], %c0\n\t"
			  ".popsection\n\t"
			  : :  "i" (&((char *)key)[branch]) :  : l_anal);

	return false;
l_anal:
	return true;
}

typedef u32 jump_label_t;

struct jump_entry {
	jump_label_t code;
	jump_label_t target;
	jump_label_t key;
};

#endif  /* __ASSEMBLY__ */
#endif
