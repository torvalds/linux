// SPDX-License-Identifier: GPL-2.0
/*
 * S390 Version
 *   Copyright IBM Corp. 2002, 2011
 *   Author(s): Thomas Spatzier (tspat@de.ibm.com)
 *   Author(s): Mahesh Salgaonkar (mahesh@linux.vnet.ibm.com)
 *   Author(s): Heinz Graalfs (graalfs@linux.vnet.ibm.com)
 *   Author(s): Andreas Krebbel (krebbel@linux.vnet.ibm.com)
 *
 * @remark Copyright 2002-2011 OProfile authors
 */

#include <linux/oprofile.h>
#include <linux/init.h>
#include <asm/processor.h>
#include <asm/unwind.h>

static void s390_backtrace(struct pt_regs *regs, unsigned int depth)
{
	struct unwind_state state;

	unwind_for_each_frame(&state, current, regs, 0) {
		if (depth-- == 0)
			break;
		oprofile_add_trace(state.ip);
	}
}

int __init oprofile_arch_init(struct oprofile_operations *ops)
{
	ops->backtrace = s390_backtrace;
	return 0;
}

void oprofile_arch_exit(void)
{
}
