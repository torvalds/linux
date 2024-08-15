/* SPDX-License-Identifier: GPL-2.0 */
/* systbls.S: System call entry point tables for OS compatibility.
 *            The native Linux system call table lives here also.
 *
 * Copyright (C) 1995, 2007 David S. Miller (davem@davemloft.net)
 *
 * Based upon preliminary work which is:
 *
 * Copyright (C) 1995 Adrian M. Rodriguez (adrian@remus.rutgers.edu)
 */

#define __SYSCALL_WITH_COMPAT(nr, native, compat)	__SYSCALL(nr, native)
#define __SYSCALL(nr, entry) .long entry
	.data
	.align 4
	.globl sys_call_table
sys_call_table:
#include <asm/syscall_table_32.h>	/* 32-bit native syscalls */
