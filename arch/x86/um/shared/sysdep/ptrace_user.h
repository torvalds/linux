/* SPDX-License-Identifier: GPL-2.0 */
#include <generated/user_constants.h>

#define PT_OFFSET(r) ((r) * sizeof(long))

#define PT_SYSCALL_NR(regs) ((regs)[HOST_ORIG_AX])
#define PT_SYSCALL_NR_OFFSET PT_OFFSET(HOST_ORIG_AX)

#define PT_SYSCALL_RET_OFFSET PT_OFFSET(HOST_AX)

#define REGS_IP_INDEX HOST_IP
#define REGS_SP_INDEX HOST_SP

/*
 * glibc before 2.27 does not include PTRACE_SYSEMU_SINGLESTEP in its enum,
 * ensure we have a definition by (re-)defining it here.
 */
#ifndef PTRACE_SYSEMU_SINGLESTEP
#define PTRACE_SYSEMU_SINGLESTEP 32
#endif
