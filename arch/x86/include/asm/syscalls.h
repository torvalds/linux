/*
 * syscalls.h - Linux syscall interfaces (arch-specific)
 *
 * Copyright (c) 2008 Jaswinder Singh Rajput
 *
 * This file is released under the GPLv2.
 * See the file COPYING for more details.
 */

#ifndef _ASM_X86_SYSCALLS_H
#define _ASM_X86_SYSCALLS_H

#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/signal.h>
#include <linux/types.h>

/* Common in X86_32 and X86_64 */
/* kernel/ioport.c */
asmlinkage long sys_ioperm(unsigned long, unsigned long, int);
long sys_iopl(unsigned int, struct pt_regs *);

/* kernel/process.c */
int sys_fork(struct pt_regs *);
int sys_vfork(struct pt_regs *);
long sys_execve(char __user *, char __user * __user *,
		char __user * __user *, struct pt_regs *);
long sys_clone(unsigned long, unsigned long, void __user *,
	       void __user *, struct pt_regs *);

/* kernel/ldt.c */
asmlinkage int sys_modify_ldt(int, void __user *, unsigned long);

/* kernel/signal.c */
long sys_rt_sigreturn(struct pt_regs *);
long sys_sigaltstack(const stack_t __user *, stack_t __user *,
		     struct pt_regs *);


/* kernel/tls.c */
asmlinkage int sys_set_thread_area(struct user_desc __user *);
asmlinkage int sys_get_thread_area(struct user_desc __user *);

/* X86_32 only */
#ifdef CONFIG_X86_32

/* kernel/signal.c */
asmlinkage int sys_sigsuspend(int, int, old_sigset_t);
asmlinkage int sys_sigaction(int, const struct old_sigaction __user *,
			     struct old_sigaction __user *);
unsigned long sys_sigreturn(struct pt_regs *);

/* kernel/sys_i386_32.c */
struct mmap_arg_struct;
struct sel_arg_struct;
struct oldold_utsname;
struct old_utsname;

asmlinkage int old_mmap(struct mmap_arg_struct __user *);
asmlinkage int old_select(struct sel_arg_struct __user *);
asmlinkage int sys_ipc(uint, int, int, int, void __user *, long);
asmlinkage int sys_uname(struct old_utsname __user *);
asmlinkage int sys_olduname(struct oldold_utsname __user *);

/* kernel/vm86_32.c */
int sys_vm86old(struct vm86_struct __user *, struct pt_regs *);
int sys_vm86(unsigned long, unsigned long, struct pt_regs *);

#else /* CONFIG_X86_32 */

/* X86_64 only */
/* kernel/process_64.c */
long sys_arch_prctl(int, unsigned long);

/* kernel/sys_x86_64.c */
struct new_utsname;

asmlinkage long sys_mmap(unsigned long, unsigned long, unsigned long,
			 unsigned long, unsigned long, unsigned long);
asmlinkage long sys_uname(struct new_utsname __user *);

#endif /* CONFIG_X86_32 */
#endif /* _ASM_X86_SYSCALLS_H */
