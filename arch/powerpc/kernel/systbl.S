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

#include <asm/ppc_asm.h>

.section .rodata,"a"

#ifdef CONFIG_PPC64
	.p2align	3
#define __SYSCALL(nr, entry)	.8byte entry
#else
#define __SYSCALL(nr, entry)	.long entry
#endif

#define __SYSCALL_WITH_COMPAT(nr, native, compat)	__SYSCALL(nr, native)
.globl sys_call_table
sys_call_table:
#ifdef CONFIG_PPC64
#include <asm/syscall_table_64.h>
#else
#include <asm/syscall_table_32.h>
#endif

#ifdef CONFIG_COMPAT
#undef __SYSCALL_WITH_COMPAT
#define __SYSCALL_WITH_COMPAT(nr, native, compat)	__SYSCALL(nr, compat)
.globl compat_sys_call_table
compat_sys_call_table:
#define compat_sys_sigsuspend	sys_sigsuspend
#include <asm/syscall_table_32.h>
#endif
