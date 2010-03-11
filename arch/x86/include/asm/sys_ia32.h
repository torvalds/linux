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

#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/signal.h>
#include <asm/compat.h>
#include <asm/ia32.h>

/* ia32/sys_ia32.c */
asmlinkage long sys32_truncate64(char __user *, unsigned long, unsigned long);
asmlinkage long sys32_ftruncate64(unsigned int, unsigned long, unsigned long);

asmlinkage long sys32_stat64(char __user *, struct stat64 __user *);
asmlinkage long sys32_lstat64(char __user *, struct stat64 __user *);
asmlinkage long sys32_fstat64(unsigned int, struct stat64 __user *);
asmlinkage long sys32_fstatat(unsigned int, char __user *,
			      struct stat64 __user *, int);
struct mmap_arg_struct;
asmlinkage long sys32_mmap(struct mmap_arg_struct __user *);
asmlinkage long sys32_mprotect(unsigned long, size_t, unsigned long);

asmlinkage long sys32_pipe(int __user *);
struct sigaction32;
struct old_sigaction32;
asmlinkage long sys32_rt_sigaction(int, struct sigaction32 __user *,
				   struct sigaction32 __user *, unsigned int);
asmlinkage long sys32_sigaction(int, struct old_sigaction32 __user *,
				struct old_sigaction32 __user *);
asmlinkage long sys32_rt_sigprocmask(int, compat_sigset_t __user *,
				     compat_sigset_t __user *, unsigned int);
asmlinkage long sys32_alarm(unsigned int);

struct sel_arg_struct;
asmlinkage long sys32_old_select(struct sel_arg_struct __user *);
asmlinkage long sys32_waitpid(compat_pid_t, unsigned int *, int);
asmlinkage long sys32_sysfs(int, u32, u32);

asmlinkage long sys32_sched_rr_get_interval(compat_pid_t,
					    struct compat_timespec __user *);
asmlinkage long sys32_rt_sigpending(compat_sigset_t __user *, compat_size_t);
asmlinkage long sys32_rt_sigqueueinfo(int, int, compat_siginfo_t __user *);

#ifdef CONFIG_SYSCTL_SYSCALL
struct sysctl_ia32;
asmlinkage long sys32_sysctl(struct sysctl_ia32 __user *);
#endif

asmlinkage long sys32_pread(unsigned int, char __user *, u32, u32, u32);
asmlinkage long sys32_pwrite(unsigned int, char __user *, u32, u32, u32);

asmlinkage long sys32_personality(unsigned long);
asmlinkage long sys32_sendfile(int, int, compat_off_t __user *, s32);

struct oldold_utsname;
struct old_utsname;
asmlinkage long sys32_olduname(struct oldold_utsname __user *);
long sys32_uname(struct old_utsname __user *);

asmlinkage long sys32_execve(char __user *, compat_uptr_t __user *,
			     compat_uptr_t __user *, struct pt_regs *);
asmlinkage long sys32_clone(unsigned int, unsigned int, struct pt_regs *);

long sys32_lseek(unsigned int, int, unsigned int);
long sys32_kill(int, int);
long sys32_fadvise64_64(int, __u32, __u32, __u32, __u32, int);
long sys32_vm86_warning(void);
long sys32_lookup_dcookie(u32, u32, char __user *, size_t);

asmlinkage ssize_t sys32_readahead(int, unsigned, unsigned, size_t);
asmlinkage long sys32_sync_file_range(int, unsigned, unsigned,
				      unsigned, unsigned, int);
asmlinkage long sys32_fadvise64(int, unsigned, unsigned, size_t, int);
asmlinkage long sys32_fallocate(int, int, unsigned,
				unsigned, unsigned, unsigned);

/* ia32/ia32_signal.c */
asmlinkage long sys32_sigsuspend(int, int, old_sigset_t);
asmlinkage long sys32_sigaltstack(const stack_ia32_t __user *,
				  stack_ia32_t __user *, struct pt_regs *);
asmlinkage long sys32_sigreturn(struct pt_regs *);
asmlinkage long sys32_rt_sigreturn(struct pt_regs *);

/* ia32/ipc32.c */
asmlinkage long sys32_ipc(u32, int, int, int, compat_uptr_t, u32);
#endif /* _ASM_X86_SYS_IA32_H */
