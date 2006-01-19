/*
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __SKAS_H
#define __SKAS_H

#include "mm_id.h"
#include "sysdep/ptrace.h"

extern int userspace_pid[];
extern int proc_mm, ptrace_faultinfo, ptrace_ldt;
extern int skas_needs_stub;

extern int user_thread(unsigned long stack, int flags);
extern void new_thread_proc(void *stack, void (*handler)(int sig));
extern void new_thread_handler(int sig);
extern void handle_syscall(union uml_pt_regs *regs);
extern void user_signal(int sig, union uml_pt_regs *regs, int pid);
extern int new_mm(unsigned long stack);
extern void get_skas_faultinfo(int pid, struct faultinfo * fi);
extern long execute_syscall_skas(void *r);
extern unsigned long current_stub_stack(void);

#endif
