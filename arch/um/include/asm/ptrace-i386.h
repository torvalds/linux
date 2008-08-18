/* 
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#ifndef __UM_PTRACE_I386_H
#define __UM_PTRACE_I386_H

#define HOST_AUDIT_ARCH AUDIT_ARCH_I386

#include "linux/compiler.h"
#include "asm/ptrace-generic.h"

#define PT_REGS_EAX(r) UPT_EAX(&(r)->regs)
#define PT_REGS_EBX(r) UPT_EBX(&(r)->regs)
#define PT_REGS_ECX(r) UPT_ECX(&(r)->regs)
#define PT_REGS_EDX(r) UPT_EDX(&(r)->regs)
#define PT_REGS_ESI(r) UPT_ESI(&(r)->regs)
#define PT_REGS_EDI(r) UPT_EDI(&(r)->regs)
#define PT_REGS_EBP(r) UPT_EBP(&(r)->regs)

#define PT_REGS_CS(r) UPT_CS(&(r)->regs)
#define PT_REGS_SS(r) UPT_SS(&(r)->regs)
#define PT_REGS_DS(r) UPT_DS(&(r)->regs)
#define PT_REGS_ES(r) UPT_ES(&(r)->regs)
#define PT_REGS_FS(r) UPT_FS(&(r)->regs)
#define PT_REGS_GS(r) UPT_GS(&(r)->regs)

#define PT_REGS_EFLAGS(r) UPT_EFLAGS(&(r)->regs)

#define PT_REGS_ORIG_SYSCALL(r) PT_REGS_EAX(r)
#define PT_REGS_SYSCALL_RET(r) PT_REGS_EAX(r)
#define PT_FIX_EXEC_STACK(sp) do ; while(0)

#define profile_pc(regs) PT_REGS_IP(regs)

#define user_mode(r) UPT_IS_USER(&(r)->regs)

/*
 * Forward declaration to avoid including sysdep/tls.h, which causes a
 * circular include, and compilation failures.
 */
struct user_desc;

extern int get_fpxregs(struct user_fxsr_struct __user *buf,
		       struct task_struct *child);
extern int set_fpxregs(struct user_fxsr_struct __user *buf,
		       struct task_struct *tsk);

extern int ptrace_get_thread_area(struct task_struct *child, int idx,
                                  struct user_desc __user *user_desc);

extern int ptrace_set_thread_area(struct task_struct *child, int idx,
                                  struct user_desc __user *user_desc);

#endif
