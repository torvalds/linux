// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OpenRISC sys_call_table.c
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * Modifications for the OpenRISC architecture:
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 */

#include <linux/syscalls.h>
#include <linux/signal.h>
#include <linux/unistd.h>

#include <asm/syscalls.h>

#define __SYSCALL(nr, call) [nr] = (call),
#define __SYSCALL_WITH_COMPAT(nr, native, compat) __SYSCALL(nr, native)

#define sys_mmap2 sys_mmap_pgoff
#define sys_clone __sys_clone
#define sys_clone3 __sys_clone3
#define sys_fork __sys_fork

void *sys_call_table[__NR_syscalls] = {
#include <asm/syscall_table_32.h>
};
