// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/arch/arm/mm/extable.c
 */
#include <linux/extable.h>
#include <linux/uaccess.h>

int fixup_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *fixup;

	fixup = search_exception_tables(instruction_pointer(regs));
	if (fixup) {
		regs->ARM_pc = fixup->fixup;
#ifdef CONFIG_THUMB2_KERNEL
		/* Clear the IT state to avoid nasty surprises in the fixup */
		regs->ARM_cpsr &= ~PSR_IT_MASK;
#endif
	}

	return fixup != NULL;
}
