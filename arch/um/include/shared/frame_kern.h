/* SPDX-License-Identifier: GPL-2.0 */
/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 */

#ifndef __FRAME_KERN_H_
#define __FRAME_KERN_H_

extern int setup_signal_stack_sc(unsigned long stack_top, struct ksignal *ksig,
				 struct pt_regs *regs, sigset_t *mask);
extern int setup_signal_stack_si(unsigned long stack_top, struct ksignal *ksig,
				 struct pt_regs *regs, sigset_t *mask);

#endif

