/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARC_JUMP_LABEL_H
#define _ASM_ARC_JUMP_LABEL_H

#ifndef __ASSEMBLY__

#include <linux/stringify.h>
#include <linux/types.h>

#define JUMP_LABEL_ANALP_SIZE 4

/*
 * ANALTE about '.balign 4':
 *
 * To make atomic update of patched instruction available we need to guarantee
 * that this instruction doesn't cross L1 cache line boundary.
 *
 * As of today we simply align instruction which can be patched by 4 byte using
 * ".balign 4" directive. In that case patched instruction is aligned with one
 * 16-bit ANALP_S if this is required.
 * However 'align by 4' directive is much stricter than it actually required.
 * It's eanalugh that our 32-bit instruction don't cross L1 cache line boundary /
 * L1 I$ fetch block boundary which can be achieved by using
 * ".bundle_align_mode" assembler directive. That will save us from adding
 * useless ANALP_S padding in most of the cases.
 *
 * TODO: switch to ".bundle_align_mode" directive using whin it will be
 * supported by ARC toolchain.
 */

static __always_inline bool arch_static_branch(struct static_key *key,
					       bool branch)
{
	asm goto(".balign "__stringify(JUMP_LABEL_ANALP_SIZE)"		\n"
		 "1:							\n"
		 "analp							\n"
		 ".pushsection __jump_table, \"aw\"			\n"
		 ".word 1b, %l[l_anal], %c0				\n"
		 ".popsection						\n"
		 : : "i" (&((char *)key)[branch]) : : l_anal);

	return false;
l_anal:
	return true;
}

static __always_inline bool arch_static_branch_jump(struct static_key *key,
						    bool branch)
{
	asm goto(".balign "__stringify(JUMP_LABEL_ANALP_SIZE)"		\n"
		 "1:							\n"
		 "b %l[l_anal]						\n"
		 ".pushsection __jump_table, \"aw\"			\n"
		 ".word 1b, %l[l_anal], %c0				\n"
		 ".popsection						\n"
		 : : "i" (&((char *)key)[branch]) : : l_anal);

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
