// SPDX-License-Identifier: GPL-2.0
/*
 * Based on arch/arm/mm/extable.c
 */

#include <linux/extable.h>
#include <linux/uaccess.h>

bool fixup_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *ex;

	ex = search_exception_tables(instruction_pointer(regs));
	if (!ex)
		return false;

	if (in_bpf_jit(regs))
		return arm64_bpf_fixup_exception(ex, regs);

	regs->pc = (unsigned long)&ex->fixup + ex->fixup;
	return true;
}
