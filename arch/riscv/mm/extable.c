// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2009 Sunplus Core Technology Co., Ltd.
 *  Lennox Wu <lennox.wu@sunplusct.com>
 *  Chen Liqin <liqin.chen@sunplusct.com>
 * Copyright (C) 2013 Regents of the University of California
 */


#include <linux/extable.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#ifdef CONFIG_BPF_JIT
int rv_bpf_fixup_exception(const struct exception_table_entry *ex, struct pt_regs *regs);
#endif

int fixup_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *fixup;

	fixup = search_exception_tables(regs->epc);
	if (!fixup)
		return 0;

#ifdef CONFIG_BPF_JIT
	if (regs->epc >= BPF_JIT_REGION_START && regs->epc < BPF_JIT_REGION_END)
		return rv_bpf_fixup_exception(fixup, regs);
#endif

	regs->epc = fixup->fixup;
	return 1;
}
