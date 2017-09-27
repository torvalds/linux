#include <linux/extable.h>
#include <linux/uaccess.h>

int fixup_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *fixup;
	unsigned long pc = instruction_pointer(regs);

	fixup = search_exception_tables(pc);
	if (fixup)
		regs->ctx.CurrPC = fixup->fixup;

	return fixup != NULL;
}
