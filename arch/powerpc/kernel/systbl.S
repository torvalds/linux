/*
 * This file contains the table of syscall-handling functions.
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 * Largely rewritten by Cort Dougan (cort@cs.nmt.edu)
 * and Paul Mackerras.
 *
 * Adapted for iSeries by Mike Corrigan (mikejc@us.ibm.com)
 * PPC64 updates by Dave Engebretsen (engebret@us.ibm.com) 
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <asm/ppc_asm.h>

.section .rodata,"a"

#ifdef CONFIG_PPC64
	.p2align	3
#endif

.globl sys_call_table
sys_call_table:
#ifdef CONFIG_PPC64
#define __SYSCALL(nr, entry)	.8byte DOTSYM(entry)
#include <asm/syscall_table_64.h>
#undef __SYSCALL
#else
#define __SYSCALL(nr, entry)	.long entry
#include <asm/syscall_table_32.h>
#undef __SYSCALL
#endif

#ifdef CONFIG_COMPAT
.globl compat_sys_call_table
compat_sys_call_table:
#define compat_sys_sigsuspend	sys_sigsuspend
#define __SYSCALL(nr, entry)	.8byte DOTSYM(entry)
#include <asm/syscall_table_c32.h>
#undef __SYSCALL
#endif
