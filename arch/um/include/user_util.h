/*
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __USER_UTIL_H__
#define __USER_UTIL_H__

#include "sysdep/ptrace.h"

/* Copied from kernel.h and compiler-gcc.h */

/* Force a compilation error if condition is true, but also produce a
   result (of value 0 and type size_t), so the expression can be used
   e.g. in a structure initializer (or where-ever else comma expressions
   aren't permitted). */
#define BUILD_BUG_ON_ZERO(e) (sizeof(char[1 - 2 * !!(e)]) - 1)

/* &a[0] degrades to a pointer: a different type from an array */
#define __must_be_array(a) \
  BUILD_BUG_ON_ZERO(__builtin_types_compatible_p(typeof(a), typeof(&a[0])))

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))

#define CATCH_EINTR(expr) while ((errno = 0, ((expr) < 0)) && (errno == EINTR))

extern int mode_tt;

extern int grantpt(int __fd);
extern int unlockpt(int __fd);
extern char *ptsname(int __fd);

extern void *add_signal_handler(int sig, void (*handler)(int));
extern void input_cb(void (*proc)(void *), void *arg, int arg_len);
extern int get_pty(void);
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
