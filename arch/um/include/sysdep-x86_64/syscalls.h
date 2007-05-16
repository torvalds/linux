/*
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */

#ifndef __SYSDEP_X86_64_SYSCALLS_H__
#define __SYSDEP_X86_64_SYSCALLS_H__

#include <linux/msg.h>
#include <linux/shm.h>
#include <kern_constants.h>

typedef long syscall_handler_t(void);

extern syscall_handler_t *sys_call_table[];

#define EXECUTE_SYSCALL(syscall, regs) \
	(((long (*)(long, long, long, long, long, long)) \
	  (*sys_call_table[syscall]))(UPT_SYSCALL_ARG1(&regs->regs), \
		 		      UPT_SYSCALL_ARG2(&regs->regs), \
				      UPT_SYSCALL_ARG3(&regs->regs), \
				      UPT_SYSCALL_ARG4(&regs->regs), \
				      UPT_SYSCALL_ARG5(&regs->regs), \
				      UPT_SYSCALL_ARG6(&regs->regs)))

extern long old_mmap(unsigned long addr, unsigned long len,
		     unsigned long prot, unsigned long flags,
		     unsigned long fd, unsigned long pgoff);
extern syscall_handler_t sys_modify_ldt;
extern syscall_handler_t sys_arch_prctl;

#define NR_syscalls (UM_NR_syscall_max + 1)

#endif
