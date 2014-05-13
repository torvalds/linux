#ifndef _ASM_POWERPC_JUMP_LABEL_H
#define _ASM_POWERPC_JUMP_LABEL_H

/*
 * Copyright 2010 Michael Ellerman, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/types.h>

#include <asm/feature-fixups.h>

#define JUMP_ENTRY_TYPE		stringify_in_c(FTR_ENTRY_LONG)
#define JUMP_LABEL_NOP_SIZE	4

static __always_inline bool arch_static_branch(struct static_key *key)
{
	asm_volatile_goto("1:\n\t"
		 "nop\n\t"
		 ".pushsection __jump_table,  \"aw\"\n\t"
		 JUMP_ENTRY_TYPE "1b, %l[l_yes], %c0\n\t"
		 ".popsection \n\t"
		 : :  "i" (key) : : l_yes);
	return false;
l_yes:
	return true;
}

#ifdef CONFIG_PPC64
typedef u64 jump_label_t;
#else
typedef u32 jump_label_t;
#endif

struct jump_entry {
	jump_label_t code;
	jump_label_t target;
	jump_label_t key;
};

#endif /* _ASM_POWERPC_JUMP_LABEL_H */
