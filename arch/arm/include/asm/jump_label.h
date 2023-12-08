/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARM_JUMP_LABEL_H
#define _ASM_ARM_JUMP_LABEL_H

#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <asm/unified.h>

#define JUMP_LABEL_NOP_SIZE 4

static __always_inline bool arch_static_branch(struct static_key *key, bool branch)
{
	asm_volatile_goto("1:\n\t"
		 WASM(nop) "\n\t"
		 ".pushsection __jump_table,  \"aw\"\n\t"
		 ".word 1b, %l[l_yes], %c0\n\t"
		 ".popsection\n\t"
		 : :  "i" (&((char *)key)[branch]) :  : l_yes);

	return false;
l_yes:
	return true;
}

static __always_inline bool arch_static_branch_jump(struct static_key *key, bool branch)
{
	asm_volatile_goto("1:\n\t"
		 WASM(b) " %l[l_yes]\n\t"
		 ".pushsection __jump_table,  \"aw\"\n\t"
		 ".word 1b, %l[l_yes], %c0\n\t"
		 ".popsection\n\t"
		 : :  "i" (&((char *)key)[branch]) :  : l_yes);

	return false;
l_yes:
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
