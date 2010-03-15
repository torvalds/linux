#ifndef _ENTRY_H
#define _ENTRY_H

#include <linux/types.h>
#include <linux/signal.h>
#include <asm/ptrace.h>

typedef void pgm_check_handler_t(struct pt_regs *, long);
extern pgm_check_handler_t *pgm_check_table[128];
pgm_check_handler_t do_protection_exception;
pgm_check_handler_t do_dat_exception;

extern int sysctl_userprocess_debug;

void do_single_step(struct pt_regs *regs);
void syscall_trace(struct pt_regs *regs, int entryexit);
void kernel_stack_overflow(struct pt_regs * regs);
void do_signal(struct pt_regs *regs);
int handle_signal32(unsigned long sig, struct k_sigaction *ka,
		    siginfo_t *info, sigset_t *oldset, struct pt_regs *regs);

void do_extint(struct pt_regs *regs, unsigned short code);
int __cpuinit start_secondary(void *cpuvoid);
void __init startup_init(void);
void die(const char * str, struct pt_regs * regs, long err);

struct s390_mmap_arg_struct;
struct fadvise64_64_args;
struct old_sigaction;

long sys_mmap2(struct s390_mmap_arg_struct __user  *arg);
long sys_s390_ipc(uint call, int first, unsigned long second,
	     unsigned long third, void __user *ptr);
long sys_s390_personality(unsigned long personality);
long sys_s390_fadvise64(int fd, u32 offset_high, u32 offset_low,
		    size_t len, int advice);
long sys_s390_fadvise64_64(struct fadvise64_64_args __user *args);
long sys_s390_fallocate(int fd, int mode, loff_t offset, u32 len_high,
			u32 len_low);
long sys_fork(void);
long sys_clone(unsigned long newsp, unsigned long clone_flags,
	       int __user *parent_tidptr, int __user *child_tidptr);
long sys_vfork(void);
void execve_tail(void);
long sys_execve(char __user *name, char __user * __user *argv,
		char __user * __user *envp);
long sys_sigsuspend(int history0, int history1, old_sigset_t mask);
long sys_sigaction(int sig, const struct old_sigaction __user *act,
		   struct old_sigaction __user *oact);
long sys_sigaltstack(const stack_t __user *uss, stack_t __user *uoss);
long sys_sigreturn(void);
long sys_rt_sigreturn(void);
long sys32_sigreturn(void);
long sys32_rt_sigreturn(void);

#endif /* _ENTRY_H */
