/*
 * arch/sh/kernel/syscalls.S
 *
 * System call table for SuperH
 *
 *  Copyright (C) 1999, 2000, 2002  Niibe Yutaka
 *  Copyright (C) 2003  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */
#include <linux/sys.h>
#include <linux/linkage.h>

#define __SYSCALL(nr, entry, nargs) .long entry
	.data
ENTRY(sys_call_table)
#include <asm/syscall_table.h>
#undef __SYSCALL
