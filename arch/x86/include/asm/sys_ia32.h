/*
 * sys_ia32.h - Linux ia32 syscall interfaces
 *
 * Copyright (c) 2008 Jaswinder Singh Rajput
 *
 * This file is released under the GPLv2.
 * See the file COPYING for more details.
 */

#ifndef _ASM_X86_SYS_IA32_H
#define _ASM_X86_SYS_IA32_H

#ifdef CONFIG_COMPAT

#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/signal.h>
#include <asm/compat.h>
#include <asm/ia32.h>

/* ia32/sys_ia32.c */
asmlinkage long compat_sys_x86_truncate64(const char __user *, unsigned long,
					  unsigned long);
asmlinkage long compat_sys_x86_ftruncate64(unsigned int, unsigned long,
					   unsigned long);

asmlinkage long compat_sys_x86_stat64(const char __user *,
				      struct stat64 __user *);
asmlinkage long compat_sys_x86_lstat64(const char __user *,
				       struct stat64 __user *);
asmlinkage long compat_sys_x86_fstat64(unsigned int, struct stat64 __user *);
asmlinkage long compat_sys_x86_fstatat(unsigned int, const char __user *,
			      struct stat64 __user *, int);
struct mmap_arg_struct32;
asmlinkage long compat_sys_x86_mmap(struct mmap_arg_struct32 __user *);

asmlinkage long compat_sys_x86_waitpid(compat_pid_t, unsigned int __user *,
				       int);

asmlinkage long compat_sys_x86_pread(unsigned int, char __user *, u32, u32,
				     u32);
asmlinkage long compat_sys_x86_pwrite(unsigned int, const char __user *, u32,
				      u32, u32);

asmlinkage long compat_sys_x86_fadvise64_64(int, __u32, __u32, __u32, __u32,
					    int);

asmlinkage ssize_t compat_sys_x86_readahead(int, unsigned int, unsigned int,
					    size_t);
asmlinkage long compat_sys_x86_sync_file_range(int, unsigned int, unsigned int,
					       unsigned int, unsigned int,
					       int);
asmlinkage long compat_sys_x86_fadvise64(int, unsigned int, unsigned int,
					 size_t, int);
asmlinkage long compat_sys_x86_fallocate(int, int, unsigned int, unsigned int,
					 unsigned int, unsigned int);
asmlinkage long compat_sys_x86_clone(unsigned long, unsigned long, int __user *,
				     unsigned long, int __user *);

/* ia32/ia32_signal.c */
asmlinkage long sys32_sigreturn(void);
asmlinkage long sys32_rt_sigreturn(void);

#endif /* CONFIG_COMPAT */

#endif /* _ASM_X86_SYS_IA32_H */
