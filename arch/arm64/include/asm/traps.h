/*
 * Based on arch/arm/include/asm/traps.h
 *
 * Copyright (C) 2012 ARM Ltd.
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
#ifndef __ASM_TRAP_H
#define __ASM_TRAP_H

#include <linux/list.h>

struct undef_hook {
	struct list_head node;
	u32 instr_mask;
	u32 instr_val;
	u32 pstate_mask;
	u32 pstate_val;
	int (*fn)(struct pt_regs *regs, unsigned int instr);
};

void register_undef_hook(struct undef_hook *hook);

static inline int in_exception_text(unsigned long ptr)
{
	extern char __exception_text_start[];
	extern char __exception_text_end[];

	return ptr >= (unsigned long)&__exception_text_start &&
	       ptr < (unsigned long)&__exception_text_end;
}

#endif
