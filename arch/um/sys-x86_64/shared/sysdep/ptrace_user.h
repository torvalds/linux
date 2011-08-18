/*
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */

#ifndef __SYSDEP_X86_64_PTRACE_USER_H__
#define __SYSDEP_X86_64_PTRACE_USER_H__

#define __FRAME_OFFSETS
#include <sys/ptrace.h>
#include <linux/ptrace.h>
#include <asm/ptrace.h>
#undef __FRAME_OFFSETS
#include <generated/user_constants.h>

#define PT_INDEX(off) ((off) / sizeof(unsigned long))

#define PT_SYSCALL_NR(regs) ((regs)[PT_INDEX(ORIG_RAX)])
#define PT_SYSCALL_NR_OFFSET (ORIG_RAX)

#define PT_SYSCALL_RET_OFFSET (RAX)

/*
 * x86_64 FC3 doesn't define this in /usr/include/linux/ptrace.h even though
 * it's defined in the kernel's include/linux/ptrace.h. Additionally, use the
 * 2.4 name and value for 2.4 host compatibility.
 */
#ifndef PTRACE_OLDSETOPTIONS
#define PTRACE_OLDSETOPTIONS 21
#endif

#define REGS_IP_INDEX PT_INDEX(RIP)
#define REGS_SP_INDEX PT_INDEX(RSP)

#define FP_SIZE (HOST_FP_SIZE)

#endif
