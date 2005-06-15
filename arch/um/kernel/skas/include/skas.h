/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __SKAS_H
#define __SKAS_H

#include "sysdep/ptrace.h"

extern int userspace_pid[];

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
extern void map(int fd, unsigned long virt, unsigned long len, int r, int w,
		int x, int phys_fd, unsigned long long offset);
extern int unmap(int fd, void *addr, unsigned long len);
extern int protect(int fd, unsigned long addr, unsigned long len, 
		   int r, int w, int x);
extern void user_signal(int sig, union uml_pt_regs *regs, int pid);
extern int new_mm(int from);
extern void start_userspace(int cpu);
extern void get_skas_faultinfo(int pid, struct faultinfo * fi);
extern long execute_syscall_skas(void *r);

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
