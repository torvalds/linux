/* 
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#ifndef __UM_PTRACE_I386_H
#define __UM_PTRACE_I386_H

#define HOST_AUDIT_ARCH AUDIT_ARCH_I386

#include "linux/compiler.h"
#include "asm/ptrace-generic.h"

#define user_mode(r) UPT_IS_USER(&(r)->regs)

/*
 * Forward declaration to avoid including sysdep/tls.h, which causes a
 * circular include, and compilation failures.
 */
struct user_desc;

extern int ptrace_get_thread_area(struct task_struct *child, int idx,
                                  struct user_desc __user *user_desc);

extern int ptrace_set_thread_area(struct task_struct *child, int idx,
                                  struct user_desc __user *user_desc);

#endif
