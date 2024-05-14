// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/extable.h>
#include <linux/spinlock.h>
#include <asm/branch.h>
#include <linux/uaccess.h>

int fixup_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *fixup;

	fixup = search_exception_tables(exception_era(regs));
	if (fixup) {
		regs->csr_era = fixup->fixup;

		return 1;
	}

	return 0;
}
