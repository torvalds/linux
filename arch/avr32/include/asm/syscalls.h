/*
 * syscalls.h - Linux syscall interfaces (arch-specific)
 *
 * Copyright (c) 2008 Jaswinder Singh
 *
 * This file is released under the GPLv2.
 * See the file COPYING for more details.
 */

#ifndef _ASM_AVR32_SYSCALLS_H
#define _ASM_AVR32_SYSCALLS_H

#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/signal.h>

/* kernel/process.c */
asmlinkage int sys_fork(struct pt_regs *);
asmlinkage int sys_clone(unsigned long, unsigned long,
			 unsigned long, unsigned long,
			 struct pt_regs *);
asmlinkage int sys_vfork(struct pt_regs *);
asmlinkage int sys_execve(char __user *, char __user *__user *,
			  char __user *__user *, struct pt_regs *);

/* kernel/signal.c */
asmlinkage int sys_sigaltstack(const stack_t __user *, stack_t __user *,
			       struct pt_regs *);
asmlinkage int sys_rt_sigreturn(struct pt_regs *);

/* kernel/sys_avr32.c */
asmlinkage long sys_mmap2(unsigned long, unsigned long, unsigned long,
			  unsigned long, unsigned long, off_t);

/* mm/cache.c */
asmlinkage int sys_cacheflush(int, void __user *, size_t);

#endif /* _ASM_AVR32_SYSCALLS_H */
