/*
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __USER_UTIL_H__
#define __USER_UTIL_H__

#include "sysdep/ptrace.h"

extern int mode_tt;

extern int grantpt(int __fd);
extern int unlockpt(int __fd);

extern void *add_signal_handler(int sig, void (*handler)(int));
extern void input_cb(void (*proc)(void *), void *arg, int arg_len);
extern int switcheroo(int fd, int prot, void *from, void *to, int size);
extern void do_exec(int old_pid, int new_pid);
extern void tracer_panic(char *msg, ...)
	__attribute__ ((format (printf, 1, 2)));
extern int detach(int pid, int sig);
extern int attach(int pid);
extern void kill_child_dead(int pid);
extern int cont(int pid);
extern void check_sigio(void);
extern int raw(int fd);

#endif
