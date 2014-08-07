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

#ifdef CONFIG_PPC64
#define SYSCALL(func)		.llong	DOTSYM(sys_##func),DOTSYM(sys_##func)
#define COMPAT_SYS(func)	.llong	DOTSYM(sys_##func),DOTSYM(compat_sys_##func)
#define PPC_SYS(func)		.llong	DOTSYM(ppc_##func),DOTSYM(ppc_##func)
#define OLDSYS(func)		.llong	DOTSYM(sys_ni_syscall),DOTSYM(sys_ni_syscall)
#define SYS32ONLY(func)		.llong	DOTSYM(sys_ni_syscall),DOTSYM(compat_sys_##func)
#define SYSX(f, f3264, f32)	.llong	DOTSYM(f),DOTSYM(f3264)
#else
#define SYSCALL(func)		.long	sys_##func
#define COMPAT_SYS(func)	.long	sys_##func
#define PPC_SYS(func)		.long	ppc_##func
#define OLDSYS(func)		.long	sys_##func
#define SYS32ONLY(func)		.long	sys_##func
#define SYSX(f, f3264, f32)	.long	f32
#endif
#define SYSCALL_SPU(func)	SYSCALL(func)
#define COMPAT_SYS_SPU(func)	COMPAT_SYS(func)
#define PPC_SYS_SPU(func)	PPC_SYS(func)
#define SYSX_SPU(f, f3264, f32)	SYSX(f, f3264, f32)

.section .rodata,"a"

#ifdef CONFIG_PPC64
#define sys_sigpending	sys_ni_syscall
#define sys_old_getrlimit sys_ni_syscall

	.p2align	3
#endif

.globl sys_call_table
sys_call_table:

#include <asm/systbl.h>
