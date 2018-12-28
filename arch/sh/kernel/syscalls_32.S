/* SPDX-License-Identifier: GPL-2.0
 *
 * arch/sh/kernel/syscalls.S
 *
 * System call table for SuperH
 *
 *  Copyright (C) 1999, 2000, 2002  Niibe Yutaka
 *  Copyright (C) 2003  Paul Mundt
 */
#include <linux/sys.h>
#include <linux/linkage.h>

#define __SYSCALL(nr, entry, nargs) .long entry
	.data
ENTRY(sys_call_table)
#include <asm/syscall_table.h>
#undef __SYSCALL
