/**********************************************************************
sysdep.h

Copyright (C) 1999 Lars Brinkhoff.
Copyright (C) 2001 Jeff Dike (jdike@karaya.com)
See the file COPYING for licensing terms and conditions.
**********************************************************************/

extern int get_syscall(pid_t pid, long *arg1, long *arg2, long *arg3, 
		       long *arg4, long *arg5);
extern void syscall_cancel (pid_t pid, long result);
extern void syscall_set_result (pid_t pid, long result);
extern void syscall_continue (pid_t pid);
extern int syscall_pause(pid_t pid);

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
