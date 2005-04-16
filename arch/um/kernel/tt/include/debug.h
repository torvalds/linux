/* 
 * Copyright (C) 2000, 2001, 2002  Jeff Dike (jdike@karaya.com) and
 * Lars Brinkhoff.
 * Licensed under the GPL
 */

#ifndef __DEBUG_H
#define __DEBUG_H

extern int debugger_proxy(int status, pid_t pid);
extern void child_proxy(pid_t pid, int status);
extern void init_proxy (pid_t pid, int waiting, int status);
extern int start_debugger(char *prog, int startup, int stop, int *debugger_fd);
extern void fake_child_exit(void);
extern int gdb_config(char *str);
extern int gdb_remove(char *unused);

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
