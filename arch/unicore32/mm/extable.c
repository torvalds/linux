// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/unicore32/mm/extable.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */
#include <linux/extable.h>
#include <linux/uaccess.h>

int fixup_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *fixup;

	fixup = search_exception_tables(instruction_pointer(regs));
	if (fixup)
		regs->UCreg_pc = fixup->fixup;

	return fixup != NULL;
}
