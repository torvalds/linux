/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2010 Cavium Networks, Inc.
 */
#ifndef _ASM_MIPS_JUMP_LABEL_H
#define _ASM_MIPS_JUMP_LABEL_H

#define arch_jump_label_transform_static arch_jump_label_transform

#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <asm/isa-rev.h>

struct module;
extern void jump_label_apply_nops(struct module *mod);

#define JUMP_LABEL_NOP_SIZE 4

#ifdef CONFIG_64BIT
#define WORD_INSN ".dword"
#else
#define WORD_INSN ".word"
#endif

#ifdef CONFIG_CPU_MICROMIPS
# define B_INSN "b32"
# define J_INSN "j32"
#elif MIPS_ISA_REV >= 6
# define B_INSN "bc"
# define J_INSN "bc"
#else
# define B_INSN "b"
# define J_INSN "j"
#endif

static __always_inline bool arch_static_branch(struct static_key *key, bool branch)
{
	asm goto("1:\t" B_INSN " 2f\n\t"
		"2:\t.insn\n\t"
		".pushsection __jump_table,  \"aw\"\n\t"
		WORD_INSN " 1b, %l[l_yes], %0\n\t"
		".popsection\n\t"
		: :  "i" (&((char *)key)[branch]) : : l_yes);

	return false;
l_yes:
	return true;
}

static __always_inline bool arch_static_branch_jump(struct static_key *key, bool branch)
{
	asm goto("1:\t" J_INSN " %l[l_yes]\n\t"
		".pushsection __jump_table,  \"aw\"\n\t"
		WORD_INSN " 1b, %l[l_yes], %0\n\t"
		".popsection\n\t"
		: :  "i" (&((char *)key)[branch]) : : l_yes);

	return false;
l_yes:
	return true;
}

#ifdef CONFIG_64BIT
typedef u64 jump_label_t;
#else
typedef u32 jump_label_t;
#endif

struct jump_entry {
	jump_label_t code;
	jump_label_t target;
	jump_label_t key;
};

#endif  /* __ASSEMBLY__ */
#endif /* _ASM_MIPS_JUMP_LABEL_H */
