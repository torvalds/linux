#ifndef _SYSTBLS_H
#define _SYSTBLS_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/utrap.h>
#include <asm/signal.h>

extern asmlinkage unsigned long sys_getpagesize(void);
extern asmlinkage long sparc_pipe(struct pt_regs *regs);
extern asmlinkage long sys_sparc_ipc(unsigned int call, int first,
			       unsigned long second,
			       unsigned long third,
			       void __user *ptr, long fifth);
extern asmlinkage long sparc64_personality(unsigned long personality);
extern asmlinkage long sys64_munmap(unsigned long addr, size_t len);
extern asmlinkage unsigned long sys64_mremap(unsigned long addr,
					     unsigned long old_len,
					     unsigned long new_len,
					     unsigned long flags,
					     unsigned long new_addr);
extern asmlinkage unsigned long c_sys_nis_syscall(struct pt_regs *regs);
extern asmlinkage long sys_getdomainname(char __user *name, int len);
extern asmlinkage long sys_utrap_install(utrap_entry_t type,
					 utrap_handler_t new_p,
					 utrap_handler_t new_d,
					 utrap_handler_t __user *old_p,
					 utrap_handler_t __user *old_d);
extern asmlinkage long sparc_memory_ordering(unsigned long model,
					     struct pt_regs *regs);
extern asmlinkage long sys_rt_sigaction(int sig,
					const struct sigaction __user *act,
					struct sigaction __user *oact,
					void __user *restorer,
					size_t sigsetsize);

extern asmlinkage void sparc64_set_context(struct pt_regs *regs);
extern asmlinkage void sparc64_get_context(struct pt_regs *regs);
extern asmlinkage long sys_sigpause(unsigned int set);
extern asmlinkage long sys_sigsuspend(old_sigset_t set);
extern void do_rt_sigreturn(struct pt_regs *regs);

#endif /* _SYSTBLS_H */
