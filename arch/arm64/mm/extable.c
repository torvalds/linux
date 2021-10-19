// SPDX-License-Identifier: GPL-2.0
/*
 * Based on arch/arm/mm/extable.c
 */

#include <linux/bitfield.h>
#include <linux/extable.h>
#include <linux/uaccess.h>

#include <asm/asm-extable.h>
#include <asm/ptrace.h>

typedef bool (*ex_handler_t)(const struct exception_table_entry *,
			     struct pt_regs *);

static inline unsigned long
get_ex_fixup(const struct exception_table_entry *ex)
{
	return ((unsigned long)&ex->fixup + ex->fixup);
}

static bool ex_handler_fixup(const struct exception_table_entry *ex,
			     struct pt_regs *regs)
{
	regs->pc = get_ex_fixup(ex);
	return true;
}

static bool ex_handler_uaccess_err_zero(const struct exception_table_entry *ex,
					struct pt_regs *regs)
{
	int reg_err = FIELD_GET(EX_DATA_REG_ERR, ex->data);
	int reg_zero = FIELD_GET(EX_DATA_REG_ZERO, ex->data);

	pt_regs_write_reg(regs, reg_err, -EFAULT);
	pt_regs_write_reg(regs, reg_zero, 0);

	regs->pc = get_ex_fixup(ex);
	return true;
}

bool fixup_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *ex;

	ex = search_exception_tables(instruction_pointer(regs));
	if (!ex)
		return false;

	switch (ex->type) {
	case EX_TYPE_FIXUP:
		return ex_handler_fixup(ex, regs);
	case EX_TYPE_BPF:
		return ex_handler_bpf(ex, regs);
	case EX_TYPE_UACCESS_ERR_ZERO:
		return ex_handler_uaccess_err_zero(ex, regs);
	}

	BUG();
}
