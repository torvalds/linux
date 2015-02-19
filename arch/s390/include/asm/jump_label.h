#ifndef _ASM_S390_JUMP_LABEL_H
#define _ASM_S390_JUMP_LABEL_H

#include <linux/types.h>

#define JUMP_LABEL_NOP_SIZE 6
#define JUMP_LABEL_NOP_OFFSET 2

#ifdef CONFIG_64BIT
#define ASM_PTR ".quad"
#define ASM_ALIGN ".balign 8"
#else
#define ASM_PTR ".long"
#define ASM_ALIGN ".balign 4"
#endif

/*
 * We use a brcl 0,2 instruction for jump labels at compile time so it
 * can be easily distinguished from a hotpatch generated instruction.
 */
static __always_inline bool arch_static_branch(struct static_key *key)
{
	asm_volatile_goto("0:	brcl 0,"__stringify(JUMP_LABEL_NOP_OFFSET)"\n"
		".pushsection __jump_table, \"aw\"\n"
		ASM_ALIGN "\n"
		ASM_PTR " 0b, %l[label], %0\n"
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

#endif
