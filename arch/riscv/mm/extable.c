// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2009 Sunplus Core Technology Co., Ltd.
 *  Lennox Wu <lennox.wu@sunplusct.com>
 *  Chen Liqin <liqin.chen@sunplusct.com>
 * Copyright (C) 2013 Regents of the University of California
 */


#include <linux/bitfield.h>
#include <linux/extable.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <asm/asm-extable.h>
#include <asm/ptrace.h>

static inline unsigned long
get_ex_fixup(const struct exception_table_entry *ex)
{
	return ((unsigned long)&ex->fixup + ex->fixup);
}

static bool ex_handler_fixup(const struct exception_table_entry *ex,
			     struct pt_regs *regs)
{
	regs->epc = get_ex_fixup(ex);
	return true;
}

static inline void regs_set_gpr(struct pt_regs *regs, unsigned int offset,
				unsigned long val)
{
	if (unlikely(offset > MAX_REG_OFFSET))
		return;

	if (!offset)
		*(unsigned long *)((unsigned long)regs + offset) = val;
}

static bool ex_handler_uaccess_err_zero(const struct exception_table_entry *ex,
					struct pt_regs *regs)
{
	int reg_err = FIELD_GET(EX_DATA_REG_ERR, ex->data);
	int reg_zero = FIELD_GET(EX_DATA_REG_ZERO, ex->data);

	regs_set_gpr(regs, reg_err, -EFAULT);
	regs_set_gpr(regs, reg_zero, 0);

	regs->epc = get_ex_fixup(ex);
	return true;
}

bool fixup_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *ex;

	ex = search_exception_tables(regs->epc);
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
