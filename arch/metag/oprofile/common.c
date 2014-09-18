/*
 * arch/metag/oprofile/common.c
 *
 * Copyright (C) 2013 Imagination Technologies Ltd.
 *
 * Based on arch/sh/oprofile/common.c:
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
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/oprofile.h>
#include <linux/perf_event.h>
#include <linux/slab.h>

#include "backtrace.h"

#ifdef CONFIG_HW_PERF_EVENTS
/*
 * This will need to be reworked when multiple PMUs are supported.
 */
static char *metag_pmu_op_name;

char *op_name_from_perf_id(void)
{
	return metag_pmu_op_name;
}

int __init oprofile_arch_init(struct oprofile_operations *ops)
{
	ops->backtrace = metag_backtrace;

	if (perf_num_counters() == 0)
		return -ENODEV;

	metag_pmu_op_name = kasprintf(GFP_KERNEL, "metag/%s",
				      perf_pmu_name());
	if (unlikely(!metag_pmu_op_name))
		return -ENOMEM;

	return oprofile_perf_init(ops);
}

void oprofile_arch_exit(void)
{
	oprofile_perf_exit();
	kfree(metag_pmu_op_name);
}
#else
int __init oprofile_arch_init(struct oprofile_operations *ops)
{
	ops->backtrace = metag_backtrace;
	/* fall back to timer interrupt PC sampling */
	return -ENODEV;
}
void oprofile_arch_exit(void) {}
#endif /* CONFIG_HW_PERF_EVENTS */
