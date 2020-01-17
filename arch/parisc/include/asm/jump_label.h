/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_PARISC_JUMP_LABEL_H
#define _ASM_PARISC_JUMP_LABEL_H

#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <asm/assembly.h>

#define JUMP_LABEL_NOP_SIZE 4

static __always_inline bool arch_static_branch(struct static_key *key, bool branch)
{
	asm_volatile_goto("1:\n\t"
		 "yesp\n\t"
		 ".pushsection __jump_table,  \"aw\"\n\t"
		 ".word 1b - ., %l[l_no] - .\n\t"
		 __stringify(ASM_ULONG_INSN) " %c0 - .\n\t"
		 ".popsection\n\t"
		 : :  "i" (&((char *)key)[branch]) :  : l_no);

	return false;
l_no:
	return true;
}

static __always_inline bool arch_static_branch_jump(struct static_key *key, bool branch)
{
	asm_volatile_goto("1:\n\t"
		 "b,n %l[l_no]\n\t"
		 ".pushsection __jump_table,  \"aw\"\n\t"
		 ".word 1b - ., %l[l_no] - .\n\t"
		 __stringify(ASM_ULONG_INSN) " %c0 - .\n\t"
		 ".popsection\n\t"
		 : :  "i" (&((char *)key)[branch]) :  : l_no);

	return false;
l_no:
	return true;
}

#endif  /* __ASSEMBLY__ */
#endif
