// SPDX-License-Identifier: GPL-2.0

#include <linux/extable.h>
#include <linux/errno.h>
#include <linux/panic.h>
#include <asm/asm-extable.h>
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

static bool ex_handler_fixup(const struct exception_table_entry *ex, struct pt_regs *regs)
{
	regs->psw.addr = extable_fixup(ex);
	return true;
}

static bool ex_handler_uaccess(const struct exception_table_entry *ex, struct pt_regs *regs)
{
	regs->gprs[ex->data] = -EFAULT;
	regs->psw.addr = extable_fixup(ex);
	return true;
}

bool fixup_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *ex;

	ex = s390_search_extables(instruction_pointer(regs));
	if (!ex)
		return false;
	switch (ex->type) {
	case EX_TYPE_FIXUP:
		return ex_handler_fixup(ex, regs);
	case EX_TYPE_BPF:
		return ex_handler_bpf(ex, regs);
	case EX_TYPE_UACCESS:
		return ex_handler_uaccess(ex, regs);
	}
	panic("invalid exception table entry");
}
