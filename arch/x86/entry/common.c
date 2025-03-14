// SPDX-License-Identifier: GPL-2.0-only
/*
 * common.c - C code for kernel entry and exit
 * Copyright (c) 2015 Andrew Lutomirski
 *
 * Based on asm and ptrace code by many authors.  The code here originated
 * in ptrace.c and signal.c.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/entry-common.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/export.h>
#include <linux/nospec.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/init.h>

#include <asm/apic.h>
#include <asm/desc.h>
#include <asm/traps.h>
#include <asm/vdso.h>
#include <asm/cpufeature.h>
#include <asm/fpu/api.h>
#include <asm/nospec-branch.h>
#include <asm/io_bitmap.h>
#include <asm/syscall.h>
#include <asm/irq_stack.h>

SYSCALL_DEFINE0(ni_syscall)
{
	return -ENOSYS;
}
