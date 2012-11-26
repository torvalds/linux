#ifndef __ASM_POWERPC_SYSCALLS_H
#define __ASM_POWERPC_SYSCALLS_H
#ifdef __KERNEL__

#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <asm/signal.h>

struct pt_regs;
struct rtas_args;
struct sigaction;

asmlinkage unsigned long sys_mmap(unsigned long addr, size_t len,
		unsigned long prot, unsigned long flags,
		unsigned long fd, off_t offset);
asmlinkage unsigned long sys_mmap2(unsigned long addr, size_t len,
		unsigned long prot, unsigned long flags,
		unsigned long fd, unsigned long pgoff);
asmlinkage long sys_pipe(int __user *fildes);
asmlinkage long sys_pipe2(int __user *fildes, int flags);
asmlinkage long ppc64_personality(unsigned long personality);
asmlinkage int ppc_rtas(struct rtas_args __user *uargs);
asmlinkage time_t sys64_time(time_t __user * tloc);

asmlinkage long sys_rt_sigsuspend(sigset_t __user *unewset,
		size_t sigsetsize);
asmlinkage long sys_sigaltstack(const stack_t __user *uss,
		stack_t __user *uoss, unsigned long r5, unsigned long r6,
		unsigned long r7, unsigned long r8, struct pt_regs *regs);

#endif /* __KERNEL__ */
#endif /* __ASM_POWERPC_SYSCALLS_H */
