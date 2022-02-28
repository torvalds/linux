// SPDX-License-Identifier: GPL-2.0

#include <linux/extable.h>
#include <asm/extable.h>

const struct exception_table_entry *s390_search_extables(unsigned long addr)
{
	const struct exception_table_entry *fixup;
	size_t num;

	fixup = search_exception_tables(addr);
	if (fixup)
		return fixup;
	num = __stop_amode31_ex_table - __start_amode31_ex_table;
	return search_extable(__start_amode31_ex_table, num, addr);
}

bool fixup_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *ex;
	ex_handler_t handler;

	ex = s390_search_extables(instruction_pointer(regs));
	if (!ex)
		return false;
	handler = ex_fixup_handler(ex);
	if (unlikely(handler))
		return handler(ex, regs);
	regs->psw.addr = extable_fixup(ex);
	return true;
}
