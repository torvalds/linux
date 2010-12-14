/*
 * arch/sh/oprofile/init.c
 *
 * Copyright (C) 2003 - 2010  Paul Mundt
 *
 * Based on arch/mips/oprofile/common.c:
 *
 *	Copyright (C) 2004, 2005 Ralf Baechle
 *	Copyright (C) 2005 MIPS Technologies, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/oprofile.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/smp.h>
#include <linux/perf_event.h>
#include <linux/slab.h>
#include <asm/processor.h>

extern void sh_backtrace(struct pt_regs * const regs, unsigned int depth);

#ifdef CONFIG_HW_PERF_EVENTS
/*
 * This will need to be reworked when multiple PMUs are supported.
 */
static char *sh_pmu_op_name;

char *op_name_from_perf_id(void)
{
	return sh_pmu_op_name;
}

int __init oprofile_arch_init(struct oprofile_operations *ops)
{
	ops->backtrace = sh_backtrace;

	if (perf_num_counters() == 0)
		return -ENODEV;

	sh_pmu_op_name = kasprintf(GFP_KERNEL, "%s/%s",
				   UTS_MACHINE, perf_pmu_name());
	if (unlikely(!sh_pmu_op_name))
		return -ENOMEM;

	return oprofile_perf_init(ops);
}

void __exit oprofile_arch_exit(void)
{
	oprofile_perf_exit();
	kfree(sh_pmu_op_name);
}
#else
int __init oprofile_arch_init(struct oprofile_operations *ops)
{
	ops->backtrace = sh_backtrace;
	return -ENODEV;
}
void __exit oprofile_arch_exit(void) {}
#endif /* CONFIG_HW_PERF_EVENTS */
