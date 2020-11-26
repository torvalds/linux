// SPDX-License-Identifier: GPL-2.0
/*
 * Based on arch/arm/mm/extable.c
 */

#include <linux/extable.h>
#include <linux/uaccess.h>

int fixup_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *fixup;

	fixup = search_exception_tables(instruction_pointer(regs));
	if (!fixup)
		return 0;

	if (in_bpf_jit(regs))
		return arm64_bpf_fixup_exception(fixup, regs);

	regs->pc = (unsigned long)&fixup->fixup + fixup->fixup;
	return 1;
}
