/*
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */

#ifndef __UM_PTRACE_X86_64_H
#define __UM_PTRACE_X86_64_H

#include "linux/compiler.h"
#include "asm/errno.h"

#define __FRAME_OFFSETS /* Needed to get the R* macros */
#include "asm/ptrace-generic.h"

#define HOST_AUDIT_ARCH AUDIT_ARCH_X86_64

#define PT_REGS_R8(r) UPT_R8(&(r)->regs)
#define PT_REGS_R9(r) UPT_R9(&(r)->regs)
#define PT_REGS_R10(r) UPT_R10(&(r)->regs)
#define PT_REGS_R11(r) UPT_R11(&(r)->regs)
#define PT_REGS_R12(r) UPT_R12(&(r)->regs)
#define PT_REGS_R13(r) UPT_R13(&(r)->regs)
#define PT_REGS_R14(r) UPT_R14(&(r)->regs)
#define PT_REGS_R15(r) UPT_R15(&(r)->regs)

/* XXX */
#define user_mode(r) UPT_IS_USER(&(r)->regs)

struct user_desc;

static inline int ptrace_get_thread_area(struct task_struct *child, int idx,
                                         struct user_desc __user *user_desc)
{
        return -ENOSYS;
}

static inline int ptrace_set_thread_area(struct task_struct *child, int idx,
                                         struct user_desc __user *user_desc)
{
        return -ENOSYS;
}

extern long arch_prctl(struct task_struct *task, int code,
		       unsigned long __user *addr);
#endif
