/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARC_JUMP_LABEL_H
#define _ASM_ARC_JUMP_LABEL_H

#ifndef __ASSEMBLY__

#include <linux/stringify.h>
#include <linux/types.h>

#define JUMP_LABEL_NOP_SIZE 4

/*
 * NOTE about '.balign 4':
 *
 * To make atomic update of patched instruction available we need to guarantee
 * that this instruction doesn't cross L1 cache line boundary.
 *
 * As of today we simply align instruction which can be patched by 4 byte using
 * ".balign 4" directive. In that case patched instruction is aligned with one
 * 16-bit NOP_S if this is required.
 * However 'align by 4' directive is much stricter than it actually required.
 * It's enough that our 32-bit instruction don't cross L1 cache line boundary /
 * L1 I$ fetch block boundary which can be achieved by using
 * ".bundle_align_mode" assembler directive. That will save us from adding
 * useless NOP_S padding in most of the cases.
 *
 * TODO: switch to ".bundle_align_mode" directive using whin it will be
 * supported by ARC toolchain.
 */

static __always_inline bool arch_static_branch(struct static_key *key,
					       bool branch)
{
	asm_volatile_goto(".balign "__stringify(JUMP_LABEL_NOP_SIZE)"	\n"
		 "1:							\n"
		 "nop							\n"
		 ".pushsection __jump_table, \"aw\"			\n"
		 ".word 1b, %l[l_yes], %c0				\n"
		 ".popsection						\n"
		 : : "i" (&((char *)key)[branch]) : : l_yes);

	return false;
l_yes:
	return true;
}

static __always_inline bool arch_static_branch_jump(struct static_key *key,
						    bool branch)
{
	asm_volatile_goto(".balign "__stringify(JUMP_LABEL_NOP_SIZE)"	\n"
		 "1:							\n"
		 "b %l[l_yes]						\n"
		 ".pushsection __jump_table, \"aw\"			\n"
		 ".word 1b, %l[l_yes], %c0				\n"
		 ".popsection						\n"
		 : : "i" (&((char *)key)[branch]) : : l_yes);

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
