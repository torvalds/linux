/*
 * linux/arch/i386/mm/extable.c
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>

int fixup_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *fixup;

	fixup = search_exception_tables(regs->bpc);
	if (fixup) {
		regs->bpc = fixup->fixup;
		return 1;
	}

	return 0;
}

