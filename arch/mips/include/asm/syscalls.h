/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MIPS_SYSCALLS_H
#define _ASM_MIPS_SYSCALLS_H

#include <linux/linkage.h>
#include <linux/compat.h>

asmlinkage void sys_sigreturn(void);
asmlinkage void sys_rt_sigreturn(void);
asmlinkage int sysm_pipe(void);
asmlinkage long mipsmt_sys_sched_setaffinity(pid_t pid, unsigned int len,
                                     unsigned long __user *user_mask_ptr);
asmlinkage long mipsmt_sys_sched_getaffinity(pid_t pid, unsigned int len,
                                     unsigned long __user *user_mask_ptr);
asmlinkage long sys32_fallocate(int fd, int mode, unsigned offset_a2,
				unsigned offset_a3, unsigned len_a4,
				unsigned len_a5);
asmlinkage long sys32_fadvise64_64(int fd, int __pad,
				   unsigned long a2, unsigned long a3,
				   unsigned long a4, unsigned long a5,
				   int flags);
asmlinkage ssize_t sys32_readahead(int fd, u32 pad0, u64 a2, u64 a3,
				   size_t count);
asmlinkage long sys32_sync_file_range(int fd, int __pad,
				      unsigned long a2, unsigned long a3,
				      unsigned long a4, unsigned long a5,
				      int flags);
asmlinkage void sys32_rt_sigreturn(void);
asmlinkage void sys32_sigreturn(void);
asmlinkage int sys32_sigsuspend(compat_sigset_t __user *uset);
asmlinkage void sysn32_rt_sigreturn(void);

#endif
