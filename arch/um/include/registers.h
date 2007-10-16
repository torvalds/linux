/*
 * Copyright (C) 2004 PathScale, Inc
 * Licensed under the GPL
 */

#ifndef __REGISTERS_H
#define __REGISTERS_H

#include "sysdep/ptrace.h"
#include "sysdep/archsetjmp.h"

extern void init_thread_registers(struct uml_pt_regs *to);
extern int save_fp_registers(int pid, unsigned long *fp_regs);
extern int restore_fp_registers(int pid, unsigned long *fp_regs);
extern int save_fpx_registers(int pid, unsigned long *fp_regs);
extern int restore_fpx_registers(int pid, unsigned long *fp_regs);
extern void save_registers(int pid, struct uml_pt_regs *regs);
extern void restore_registers(int pid, struct uml_pt_regs *regs);
extern void init_registers(int pid);
extern void get_safe_registers(unsigned long *regs);
extern unsigned long get_thread_reg(int reg, jmp_buf *buf);

#endif
