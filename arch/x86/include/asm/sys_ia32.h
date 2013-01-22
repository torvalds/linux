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
asmlinkage long sys32_truncate64(const char __user *, unsigned long, unsigned long);
asmlinkage long sys32_ftruncate64(unsigned int, unsigned long, unsigned long);

asmlinkage long sys32_stat64(const char __user *, struct stat64 __user *);
asmlinkage long sys32_lstat64(const char __user *, struct stat64 __user *);
asmlinkage long sys32_fstat64(unsigned int, struct stat64 __user *);
asmlinkage long sys32_fstatat(unsigned int, const char __user *,
			      struct stat64 __user *, int);
struct mmap_arg_struct32;
asmlinkage long sys32_mmap(struct mmap_arg_struct32 __user *);
asmlinkage long sys32_mprotect(unsigned long, size_t, unsigned long);

asmlinkage long sys32_alarm(unsigned int);

asmlinkage long sys32_waitpid(compat_pid_t, unsigned int __user *, int);
asmlinkage long sys32_sysfs(int, u32, u32);

asmlinkage long sys32_pread(unsigned int, char __user *, u32, u32, u32);
asmlinkage long sys32_pwrite(unsigned int, const char __user *, u32, u32, u32);

asmlinkage long sys32_personality(unsigned long);

long sys32_kill(int, int);
long sys32_fadvise64_64(int, __u32, __u32, __u32, __u32, int);
long sys32_vm86_warning(void);

asmlinkage ssize_t sys32_readahead(int, unsigned, unsigned, size_t);
asmlinkage long sys32_sync_file_range(int, unsigned, unsigned,
				      unsigned, unsigned, int);
asmlinkage long sys32_fadvise64(int, unsigned, unsigned, size_t, int);
asmlinkage long sys32_fallocate(int, int, unsigned,
				unsigned, unsigned, unsigned);

/* ia32/ia32_signal.c */
asmlinkage long sys32_sigreturn(void);
asmlinkage long sys32_rt_sigreturn(void);

asmlinkage long sys32_fanotify_mark(int, unsigned int, u32, u32, int,
				    const char __user *);

#endif /* CONFIG_COMPAT */

#endif /* _ASM_X86_SYS_IA32_H */
