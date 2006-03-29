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

extern void kill_off_processes_skas(void);

#endif
