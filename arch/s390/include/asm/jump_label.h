#ifndef _ASM_S390_JUMP_LABEL_H
#define _ASM_S390_JUMP_LABEL_H

#ifndef __ASSEMBLY__

#include <linux/types.h>

#define JUMP_LABEL_NOP_SIZE 6
#define JUMP_LABEL_NOP_OFFSET 2

/*
 * We use a brcl 0,2 instruction for jump labels at compile time so it
 * can be easily distinguished from a hotpatch generated instruction.
 */
static __always_inline bool arch_static_branch(struct static_key *key)
{
	asm_volatile_goto("0:	brcl 0,"__stringify(JUMP_LABEL_NOP_OFFSET)"\n"
		".pushsection __jump_table, \"aw\"\n"
		".balign 8\n"
		".quad 0b, %l[label], %0\n"
		".popsection\n"
		: : "X" (key) : : label);
	return false;
label:
	return true;
}

typedef unsigned long jump_label_t;

struct jump_entry {
	jump_label_t code;
	jump_label_t target;
	jump_label_t key;
};

#endif  /* __ASSEMBLY__ */
#endif
