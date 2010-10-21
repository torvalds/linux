/*
 * arch/sh/oprofile/init.c
 *
 * Copyright (C) 2003 - 2008  Paul Mundt
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
#include <asm/processor.h>

#ifdef CONFIG_HW_PERF_EVENTS
extern void sh_backtrace(struct pt_regs * const regs, unsigned int depth);

char *op_name_from_perf_id(void)
{
	const char *pmu;
	char buf[20];
	int size;

	pmu = perf_pmu_name();
	if (!pmu)
		return NULL;

	size = snprintf(buf, sizeof(buf), "sh/%s", pmu);
	if (size > -1 && size < sizeof(buf))
		return buf;

	return NULL;
}

int __init oprofile_arch_init(struct oprofile_operations *ops)
{
	ops->backtrace = sh_backtrace;

	return oprofile_perf_init(ops);
}

void __exit oprofile_arch_exit(void)
{
	oprofile_perf_exit();
}
#else
int __init oprofile_arch_init(struct oprofile_operations *ops)
{
	pr_info("oprofile: hardware counters not available\n");
	return -ENODEV;
}
void __exit oprofile_arch_exit(void) {}
#endif /* CONFIG_HW_PERF_EVENTS */
