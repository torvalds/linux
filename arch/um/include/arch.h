/*
 * Copyright (C) 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#ifndef __ARCH_H__
#define __ARCH_H__

#include "sysdep/ptrace.h"

extern void arch_check_bugs(void);
extern int arch_fixup(unsigned long address, void *sc_ptr);
extern int arch_handle_signal(int sig, union uml_pt_regs *regs);

#endif
