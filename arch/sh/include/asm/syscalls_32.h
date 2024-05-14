/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_SYSCALLS_32_H
#define __ASM_SH_SYSCALLS_32_H

#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/types.h>

struct pt_regs;

asmlinkage int sys_sigreturn(void);
asmlinkage int sys_rt_sigreturn(void);
asmlinkage int sys_sh_pipe(void);
asmlinkage ssize_t sys_pread_wrapper(unsigned int fd, char __user *buf,
				     size_t count, long dummy, loff_t pos);
asmlinkage ssize_t sys_pwrite_wrapper(unsigned int fd, const char __user *buf,
				      size_t count, long dummy, loff_t pos);
asmlinkage int sys_fadvise64_64_wrapper(int fd, u32 offset0, u32 offset1,
					u32 len0, u32 len1, int advice);

/* Misc syscall related bits */
asmlinkage long do_syscall_trace_enter(struct pt_regs *regs);
asmlinkage void do_syscall_trace_leave(struct pt_regs *regs);
asmlinkage void do_notify_resume(struct pt_regs *regs, unsigned int save_r0,
				 unsigned long thread_info_flags);

#endif /* __ASM_SH_SYSCALLS_32_H */
