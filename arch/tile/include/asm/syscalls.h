/*
 * syscalls.h - Linux syscall interfaces (arch-specific)
 *
 * Copyright (c) 2008 Jaswinder Singh Rajput
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_SYSCALLS_H
#define _ASM_TILE_SYSCALLS_H

#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/signal.h>
#include <linux/types.h>
#include <linux/compat.h>

/* The array of function pointers for syscalls. */
extern void *sys_call_table[];
#ifdef CONFIG_COMPAT
extern void *compat_sys_call_table[];
#endif

/*
 * Note that by convention, any syscall which requires the current
 * register set takes an additional "struct pt_regs *" pointer; the
 * sys_xxx() function just adds the pointer and tail-calls to _sys_xxx().
 */

/* kernel/sys.c */
ssize_t sys32_readahead(int fd, u32 offset_lo, u32 offset_hi, u32 count);
long sys32_fadvise64(int fd, u32 offset_lo, u32 offset_hi,
		     u32 len, int advice);
int sys32_fadvise64_64(int fd, u32 offset_lo, u32 offset_hi,
		       u32 len_lo, u32 len_hi, int advice);
long sys_flush_cache(void);
long sys_mmap2(unsigned long addr, unsigned long len,
	       unsigned long prot, unsigned long flags,
	       unsigned long fd, unsigned long pgoff);
#ifdef __tilegx__
long sys_mmap(unsigned long addr, unsigned long len,
	      unsigned long prot, unsigned long flags,
	      unsigned long fd, off_t pgoff);
#endif

/* kernel/process.c */
long sys_clone(unsigned long clone_flags, unsigned long newsp,
	       void __user *parent_tid, void __user *child_tid);
long _sys_clone(unsigned long clone_flags, unsigned long newsp,
		void __user *parent_tid, void __user *child_tid,
		struct pt_regs *regs);
long sys_fork(void);
long _sys_fork(struct pt_regs *regs);
long sys_vfork(void);
long _sys_vfork(struct pt_regs *regs);
long sys_execve(const char __user *filename,
		const char __user *const __user *argv,
		const char __user *const __user *envp);
long _sys_execve(const char __user *filename,
		 const char __user *const __user *argv,
		 const char __user *const __user *envp, struct pt_regs *regs);

/* kernel/signal.c */
long sys_sigaltstack(const stack_t __user *, stack_t __user *);
long _sys_sigaltstack(const stack_t __user *, stack_t __user *,
		      struct pt_regs *);
long sys_rt_sigreturn(void);
long _sys_rt_sigreturn(struct pt_regs *regs);

/* platform-independent functions */
long sys_rt_sigsuspend(sigset_t __user *unewset, size_t sigsetsize);
long sys_rt_sigaction(int sig, const struct sigaction __user *act,
		      struct sigaction __user *oact, size_t sigsetsize);

#ifndef __tilegx__
/* mm/fault.c */
int sys_cmpxchg_badaddr(unsigned long address);
int _sys_cmpxchg_badaddr(unsigned long address, struct pt_regs *);
#endif

#ifdef CONFIG_COMPAT
long compat_sys_execve(const char __user *path,
		       const compat_uptr_t __user *argv,
		       const compat_uptr_t __user *envp);
long _compat_sys_execve(const char __user *path,
			const compat_uptr_t __user *argv,
			const compat_uptr_t __user *envp,
			struct pt_regs *regs);
long compat_sys_sigaltstack(const struct compat_sigaltstack __user *uss_ptr,
			    struct compat_sigaltstack __user *uoss_ptr);
long _compat_sys_sigaltstack(const struct compat_sigaltstack __user *uss_ptr,
			     struct compat_sigaltstack __user *uoss_ptr,
			     struct pt_regs *regs);
long compat_sys_rt_sigreturn(void);
long _compat_sys_rt_sigreturn(struct pt_regs *regs);

/* These four are not defined for 64-bit, but serve as "compat" syscalls. */
long sys_fcntl64(unsigned int fd, unsigned int cmd, unsigned long arg);
long sys_fstat64(unsigned long fd, struct stat64 __user *statbuf);
long sys_truncate64(const char __user *path, loff_t length);
long sys_ftruncate64(unsigned int fd, loff_t length);
#endif

#endif /* _ASM_TILE_SYSCALLS_H */
