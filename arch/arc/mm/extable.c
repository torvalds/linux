// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * Borrowed heavily from MIPS
 */

#include <linux/export.h>
#include <linux/extable.h>
#include <linux/uaccess.h>

int fixup_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *fixup;

	fixup = search_exception_tables(instruction_pointer(regs));
	if (fixup) {
		regs->ret = fixup->fixup;

		return 1;
	}

	return 0;
}

#ifdef CONFIG_CC_OPTIMIZE_FOR_SIZE

unsigned long arc_clear_user_noinline(void __user *to,
		unsigned long n)
{
	return __arc_clear_user(to, n);
}
EXPORT_SYMBOL(arc_clear_user_noinline);

#endif
