/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __TT_H__
#define __TT_H__

#include "sysdep/ptrace.h"

extern int gdb_pid;
extern int debug;
extern int debug_stop;
extern int debug_trace;

extern int honeypot;

extern int fork_tramp(void *sig_stack);
extern int do_proc_op(void *t, int proc_id);
extern int tracer(int (*init_proc)(void *), void *sp);
extern void attach_process(int pid);
extern void tracer_panic(char *format, ...);
extern void set_init_pid(int pid);
extern int set_user_mode(void *task);
extern void set_tracing(void *t, int tracing);
extern int is_tracing(void *task);
extern void syscall_handler(int sig, union uml_pt_regs *regs);
extern void exit_kernel(int pid, void *task);
extern void do_syscall(void *task, int pid, int local_using_sysemu);
extern void do_sigtrap(void *task);
extern int is_valid_pid(int pid);
extern void remap_data(void *segment_start, void *segment_end, int w);
extern long execute_syscall_tt(void *r);

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
