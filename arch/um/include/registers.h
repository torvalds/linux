/*
 * Copyright (C) 2004 PathScale, Inc
 * Licensed under the GPL
 */

#ifndef __REGISTERS_H
#define __REGISTERS_H

#include "sysdep/ptrace.h"

extern void init_thread_registers(union uml_pt_regs *to);
extern int save_fp_registers(int pid, unsigned long *fp_regs);
extern int restore_fp_registers(int pid, unsigned long *fp_regs);
extern void save_registers(int pid, union uml_pt_regs *regs);
extern void restore_registers(int pid, union uml_pt_regs *regs);
extern void init_registers(int pid);
extern void get_safe_registers(unsigned long * regs);

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
