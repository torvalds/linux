/*
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __MODE_SKAS_H__
#define __MODE_SKAS_H__

#include <sysdep/ptrace.h>

extern unsigned long exec_regs[];
extern unsigned long exec_fp_regs[];
extern unsigned long exec_fpx_regs[];
extern int have_fpx_regs;

extern void user_time_init_skas(void);
extern void sig_handler_common_skas(int sig, void *sc_ptr);
extern void halt_skas(void);
extern void reboot_skas(void);
extern void kill_off_processes_skas(void);
extern int is_skas_winch(int pid, int fd, void *data);

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
