// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * Based on orig code from @author John Levon <levon@movementarian.org>
 */

#include <linux/oprofile.h>
#include <linux/perf_event.h>

int __init oprofile_arch_init(struct oprofile_operations *ops)
{
	/*
	 * A failure here, forces oprofile core to switch to Timer based PC
	 * sampling, which will happen if say perf is not enabled/available
	 */
	return oprofile_perf_init(ops);
}

void oprofile_arch_exit(void)
{
	oprofile_perf_exit();
}
