/*
 * Copyright 2003 PathScale, Inc.
 * Copyright (C) 2003 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 *
 * Licensed under the GPL
 */

#ifndef __SYSDEP_X86_64_PTRACE_H
#define __SYSDEP_X86_64_PTRACE_H

#define REGS_R8(r) ((r)[HOST_R8])
#define REGS_R9(r) ((r)[HOST_R9])
#define REGS_R10(r) ((r)[HOST_R10])
#define REGS_R11(r) ((r)[HOST_R11])
#define REGS_R12(r) ((r)[HOST_R12])
#define REGS_R13(r) ((r)[HOST_R13])
#define REGS_R14(r) ((r)[HOST_R14])
#define REGS_R15(r) ((r)[HOST_R15])

#define HOST_FS_BASE 21
#define HOST_GS_BASE 22
#define HOST_DS 23
#define HOST_ES 24
#define HOST_FS 25
#define HOST_GS 26

/* Also defined in asm/ptrace-x86_64.h, but not in libc headers.  So, these
 * are already defined for kernel code, but not for userspace code.
 */
#ifndef FS_BASE
/* These aren't defined in ptrace.h, but exist in struct user_regs_struct,
 * which is what x86_64 ptrace actually uses.
 */
#define FS_BASE (HOST_FS_BASE * sizeof(long))
#define GS_BASE (HOST_GS_BASE * sizeof(long))
#define DS (HOST_DS * sizeof(long))
#define ES (HOST_ES * sizeof(long))
#define FS (HOST_FS * sizeof(long))
#define GS (HOST_GS * sizeof(long))
#endif

#define UPT_R8(r) REGS_R8((r)->gp)
#define UPT_R9(r) REGS_R9((r)->gp)
#define UPT_R10(r) REGS_R10((r)->gp)
#define UPT_R11(r) REGS_R11((r)->gp)
#define UPT_R12(r) REGS_R12((r)->gp)
#define UPT_R13(r) REGS_R13((r)->gp)
#define UPT_R14(r) REGS_R14((r)->gp)
#define UPT_R15(r) REGS_R15((r)->gp)

#define UPT_SYSCALL_ARG1(r) UPT_DI(r)
#define UPT_SYSCALL_ARG2(r) UPT_SI(r)
#define UPT_SYSCALL_ARG3(r) UPT_DX(r)
#define UPT_SYSCALL_ARG4(r) UPT_R10(r)
#define UPT_SYSCALL_ARG5(r) UPT_R8(r)
#define UPT_SYSCALL_ARG6(r) UPT_R9(r)

#endif
