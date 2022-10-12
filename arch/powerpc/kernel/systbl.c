/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * This file contains the table of syscall-handling functions.
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 * Largely rewritten by Cort Dougan (cort@cs.nmt.edu)
 * and Paul Mackerras.
 *
 * Adapted for iSeries by Mike Corrigan (mikejc@us.ibm.com)
 * PPC64 updates by Dave Engebretsen (engebret@us.ibm.com) 
 */

#include <linux/syscalls.h>
#include <linux/compat.h>
#include <asm/unistd.h>
#include <asm/syscalls.h>

#undef __SYSCALL_WITH_COMPAT
#define __SYSCALL_WITH_COMPAT(nr, entry, compat) __SYSCALL(nr, entry)

#undef __SYSCALL
#ifdef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
#define __SYSCALL(nr, entry) [nr] = entry,
#else
/*
 * Coerce syscall handlers with arbitrary parameters to common type
 * requires cast to void* to avoid -Wcast-function-type.
 */
#define __SYSCALL(nr, entry) [nr] = (void *) entry,
#endif

const syscall_fn sys_call_table[] = {
#ifdef CONFIG_PPC64
#include <asm/syscall_table_64.h>
#else
#include <asm/syscall_table_32.h>
#endif
};

#ifdef CONFIG_COMPAT
#undef __SYSCALL_WITH_COMPAT
#define __SYSCALL_WITH_COMPAT(nr, native, compat)	__SYSCALL(nr, compat)
const syscall_fn compat_sys_call_table[] = {
#include <asm/syscall_table_32.h>
};
#endif /* CONFIG_COMPAT */
