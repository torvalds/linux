/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#ifndef __SYSDEP_I386_PTRACE_H
#define __SYSDEP_I386_PTRACE_H

#define MAX_FP_NR HOST_FPX_SIZE

#define UPT_SYSCALL_ARG1(r) UPT_BX(r)
#define UPT_SYSCALL_ARG2(r) UPT_CX(r)
#define UPT_SYSCALL_ARG3(r) UPT_DX(r)
#define UPT_SYSCALL_ARG4(r) UPT_SI(r)
#define UPT_SYSCALL_ARG5(r) UPT_DI(r)
#define UPT_SYSCALL_ARG6(r) UPT_BP(r)

extern void arch_init_registers(int pid);

#endif
