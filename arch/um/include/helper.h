/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __HELPER_H__
#define __HELPER_H__

extern int run_helper(void (*pre_exec)(void *), void *pre_data, char **argv,
		      unsigned long *stack_out);
extern int run_helper_thread(int (*proc)(void *), void *arg, 
			     unsigned int flags, unsigned long *stack_out,
			     int stack_order);
extern int helper_wait(int pid);

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
