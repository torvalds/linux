/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "asm/unistd.h"
#include "sysdep/ptrace.h"

typedef long syscall_handler_t(struct pt_regs);

/* Not declared on x86, incompatible declarations on x86_64, so these have
 * to go here rather than in sys_call_table.c
 */
extern syscall_handler_t sys_rt_sigaction;

extern syscall_handler_t old_mmap_i386;

extern syscall_handler_t *sys_call_table[];

#define EXECUTE_SYSCALL(syscall, regs) \
	((long (*)(struct syscall_args)) (*sys_call_table[syscall]))(SYSCALL_ARGS(&regs->regs))

extern long sys_mmap2(unsigned long addr, unsigned long len,
		      unsigned long prot, unsigned long flags,
		      unsigned long fd, unsigned long pgoff);
