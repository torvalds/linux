/*
 * Copyright (C) 2013 Huawei Ltd.
 * Author: Jiang Liu <liuj97@gmail.com>
 *
 * Based on arch/arm/include/asm/jump_label.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_JUMP_LABEL_H
#define __ASM_JUMP_LABEL_H

#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <asm/insn.h>

#define JUMP_LABEL_NOP_SIZE		AARCH64_INSN_SIZE

static __always_inline bool arch_static_branch(struct static_key *key, bool branch)
{
	asm goto("1: nop\n\t"
		 ".pushsection __jump_table,  \"aw\"\n\t"
		 ".align 3\n\t"
		 ".quad 1b, %l[l_yes], %c0\n\t"
		 ".popsection\n\t"
		 :  :  "i"(&((char *)key)[branch]) :  : l_yes);

	return false;
l_yes:
	return true;
}

static __always_inline bool arch_static_branch_jump(struct static_key *key, bool branch)
{
	asm goto("1: b %l[l_yes]\n\t"
		 ".pushsection __jump_table,  \"aw\"\n\t"
		 ".align 3\n\t"
		 ".quad 1b, %l[l_yes], %c0\n\t"
		 ".popsection\n\t"
		 :  :  "i"(&((char *)key)[branch]) :  : l_yes);

	return false;
l_yes:
	return true;
}

typedef u64 jump_label_t;

struct jump_entry {
	jump_label_t code;
	jump_label_t target;
	jump_label_t key;
};

#endif  /* __ASSEMBLY__ */
#endif	/* __ASM_JUMP_LABEL_H */
