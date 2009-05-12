#ifndef __ASM_MICROBLAZE_SYSCALLS_H
#define __ASM_MICROBLAZE_SYSCALLS_H
#ifdef __KERNEL__

#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/signal.h>

/* FIXME will be removed */
asmlinkage int sys_ipc(uint call, int first, int second,
				int third, void *ptr, long fifth);

struct pt_regs;
asmlinkage int sys_vfork(struct pt_regs *regs);
asmlinkage int sys_clone(int flags, unsigned long stack, struct pt_regs *regs);
asmlinkage int sys_execve(char __user *filenamei, char __user *__user *argv,
			char __user *__user *envp, struct pt_regs *regs);

asmlinkage unsigned long sys_mmap2(unsigned long addr, size_t len,
			unsigned long prot, unsigned long flags,
			unsigned long fd, unsigned long pgoff);

asmlinkage unsigned long sys_mmap(unsigned long addr, size_t len,
			unsigned long prot, unsigned long flags,
			unsigned long fd, off_t offset);

/* from signal.c */
asmlinkage int sys_sigsuspend(old_sigset_t mask, struct pt_regs *regs);

asmlinkage int sys_rt_sigsuspend(sigset_t __user *unewset, size_t sigsetsize,
		struct pt_regs *regs);

asmlinkage int sys_sigaction(int sig, const struct old_sigaction *act,
		struct old_sigaction *oact);

asmlinkage long sys_rt_sigaction(int sig, const struct sigaction __user *act,
		struct sigaction __user *oact, size_t sigsetsize);

asmlinkage int sys_sigaltstack(const stack_t __user *uss, stack_t __user *uoss,
		struct pt_regs *regs);

asmlinkage int sys_sigreturn(struct pt_regs *regs);

asmlinkage int sys_rt_sigreturn(struct pt_regs *regs);

#endif /* __KERNEL__ */
#endif /* __ASM_MICROBLAZE_SYSCALLS_H */
