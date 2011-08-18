/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __SYSDEP_I386_PTRACE_USER_H__
#define __SYSDEP_I386_PTRACE_USER_H__

#include <sys/ptrace.h>
#include <linux/ptrace.h>
#include <asm/ptrace.h>
#include <generated/user_constants.h>

#define PT_OFFSET(r) ((r) * sizeof(long))

#define PT_SYSCALL_NR(regs) ((regs)[ORIG_EAX])
#define PT_SYSCALL_NR_OFFSET PT_OFFSET(ORIG_EAX)

#define PT_SYSCALL_RET_OFFSET PT_OFFSET(EAX)

#define REGS_IP_INDEX EIP
#define REGS_SP_INDEX UESP

#define FP_SIZE ((HOST_FPX_SIZE > HOST_FP_SIZE) ? HOST_FPX_SIZE : HOST_FP_SIZE)

#endif
