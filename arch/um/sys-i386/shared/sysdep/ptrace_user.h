/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __SYSDEP_I386_PTRACE_USER_H__
#define __SYSDEP_I386_PTRACE_USER_H__

#include <sys/ptrace.h>
#include <linux/ptrace.h>
#include <asm/ptrace.h>
#include "user_constants.h"

#define PT_OFFSET(r) ((r) * sizeof(long))

#define PT_SYSCALL_NR(regs) ((regs)[ORIG_EAX])
#define PT_SYSCALL_NR_OFFSET PT_OFFSET(ORIG_EAX)

#define PT_SYSCALL_ARG1_OFFSET PT_OFFSET(EBX)
#define PT_SYSCALL_ARG2_OFFSET PT_OFFSET(ECX)
#define PT_SYSCALL_ARG3_OFFSET PT_OFFSET(EDX)
#define PT_SYSCALL_ARG4_OFFSET PT_OFFSET(ESI)
#define PT_SYSCALL_ARG5_OFFSET PT_OFFSET(EDI)
#define PT_SYSCALL_ARG6_OFFSET PT_OFFSET(EBP)

#define PT_SYSCALL_RET_OFFSET PT_OFFSET(EAX)

#define REGS_SYSCALL_NR EAX /* This is used before a system call */
#define REGS_SYSCALL_ARG1 EBX
#define REGS_SYSCALL_ARG2 ECX
#define REGS_SYSCALL_ARG3 EDX
#define REGS_SYSCALL_ARG4 ESI
#define REGS_SYSCALL_ARG5 EDI
#define REGS_SYSCALL_ARG6 EBP

#define REGS_IP_INDEX EIP
#define REGS_SP_INDEX UESP

#define PT_IP_OFFSET PT_OFFSET(EIP)
#define PT_IP(regs) ((regs)[EIP])
#define PT_SP_OFFSET PT_OFFSET(UESP)
#define PT_SP(regs) ((regs)[UESP])

#define FP_SIZE ((HOST_FPX_SIZE > HOST_FP_SIZE) ? HOST_FPX_SIZE : HOST_FP_SIZE)

#ifndef FRAME_SIZE
#define FRAME_SIZE (17)
#endif

#endif
