/* 
 * Copyright (C) 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __SIGNAL_USER_H__
#define __SIGNAL_USER_H__

extern int signal_stack_size;

extern int change_sig(int signal, int on);
extern void set_sigstack(void *stack, int size);
extern void set_handler(int sig, void (*handler)(int), int flags, ...);
extern int set_signals(int enable);
extern int get_signals(void);

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
