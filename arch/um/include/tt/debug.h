/*
 * Copyright (C) 2000, 2001, 2002  Jeff Dike (jdike@karaya.com) and
 * Lars Brinkhoff.
 * Licensed under the GPL
 */

#ifndef __UML_TT_DEBUG_H
#define __UML_TT_DEBUG_H

extern int debugger_proxy(int status, pid_t pid);
extern void child_proxy(pid_t pid, int status);
extern void init_proxy (pid_t pid, int waiting, int status);
extern int start_debugger(char *prog, int startup, int stop, int *debugger_fd);
extern void fake_child_exit(void);
extern int gdb_config(char *str);
extern int gdb_remove(int unused);

#endif
