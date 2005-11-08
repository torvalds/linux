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

extern void switch_threads(void *me, void *next);
extern void thread_wait(void *sw, void *fb);
extern void new_thread(void *stack, void **switch_buf_ptr, void **fork_buf_ptr,
                       void (*handler)(int));
extern int start_idle_thread(void *stack, void *switch_buf_ptr, 
			     void **fork_buf_ptr);
extern int user_thread(unsigned long stack, int flags);
extern void userspace(union uml_pt_regs *regs);
extern void new_thread_proc(void *stack, void (*handler)(int sig));
extern void remove_sigstack(void);
extern void new_thread_handler(int sig);
extern void handle_syscall(union uml_pt_regs *regs);
extern int map(struct mm_id * mm_idp, unsigned long virt,
	       unsigned long len, int r, int w, int x, int phys_fd,
	       unsigned long long offset, int done, void **data);
extern int unmap(struct mm_id * mm_idp, void *addr, unsigned long len,
		 int done, void **data);
extern int protect(struct mm_id * mm_idp, unsigned long addr,
		   unsigned long len, int r, int w, int x, int done,
		   void **data);
extern void user_signal(int sig, union uml_pt_regs *regs, int pid);
extern int new_mm(int from, unsigned long stack);
extern int start_userspace(unsigned long stub_stack);
extern int copy_context_skas0(unsigned long stack, int pid);
extern void get_skas_faultinfo(int pid, struct faultinfo * fi);
extern long execute_syscall_skas(void *r);
extern unsigned long current_stub_stack(void);
extern long run_syscall_stub(struct mm_id * mm_idp,
                             int syscall, unsigned long *args, long expected,
                             void **addr, int done);
extern long syscall_stub_data(struct mm_id * mm_idp,
                              unsigned long *data, int data_count,
                              void **addr, void **stub_addr);

#endif
