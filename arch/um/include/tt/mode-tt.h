/*
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __MODE_TT_H__
#define __MODE_TT_H__

#include "sysdep/ptrace.h"

enum { OP_NONE, OP_EXEC, OP_FORK, OP_TRACE_ON, OP_REBOOT, OP_HALT, OP_CB };

extern int tracing_pid;

extern int tracer(int (*init_proc)(void *), void *sp);
extern void sig_handler_common_tt(int sig, void *sc);
extern void syscall_handler_tt(int sig, union uml_pt_regs *regs);
extern void reboot_tt(void);
extern void halt_tt(void);
extern int is_tracer_winch(int pid, int fd, void *data);
extern void kill_off_processes_tt(void);

#endif
