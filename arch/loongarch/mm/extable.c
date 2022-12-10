// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/extable.h>
#include <linux/uaccess.h>
#include <asm/asm-extable.h>
#include <asm/branch.h>

static inline unsigned long
get_ex_fixup(const struct exception_table_entry *ex)
{
	return ((unsigned long)&ex->fixup + ex->fixup);
}

static bool ex_handler_fixup(const struct exception_table_entry *ex,
			     struct pt_regs *regs)
{
	regs->csr_era = get_ex_fixup(ex);

	return true;
}


bool fixup_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *ex;

	ex = search_exception_tables(exception_era(regs));
	if (!ex)
		return false;

	return ex_handler_fixup(ex, regs);
}
