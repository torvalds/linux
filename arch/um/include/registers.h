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
extern void get_safe_registers(unsigned long * regs, unsigned long * fp_regs);
extern void get_thread_regs(union uml_pt_regs *uml_regs, void *buffer);

#endif
