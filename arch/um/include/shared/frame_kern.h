/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __FRAME_KERN_H_
#define __FRAME_KERN_H_

extern int setup_signal_stack_sc(unsigned long stack_top, int sig, 
				 struct k_sigaction *ka,
				 struct pt_regs *regs, 
				 sigset_t *mask);
extern int setup_signal_stack_si(unsigned long stack_top, int sig, 
				 struct k_sigaction *ka,
				 struct pt_regs *regs, siginfo_t *info, 
				 sigset_t *mask);

#endif

