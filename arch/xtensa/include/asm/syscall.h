/*
 * include/asm-xtensa/syscall.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2007 Tensilica Inc.
 */

struct pt_regs;
struct sigaction;
asmlinkage long xtensa_execve(char*, char**, char**, struct pt_regs*);
asmlinkage long xtensa_clone(unsigned long, unsigned long, struct pt_regs*);
asmlinkage long xtensa_ptrace(long, long, long, long);
asmlinkage long xtensa_sigreturn(struct pt_regs*);
asmlinkage long xtensa_rt_sigreturn(struct pt_regs*);
asmlinkage long xtensa_sigaction(int, const struct old_sigaction*,
				 struct old_sigaction*);
asmlinkage long xtensa_sigaltstack(struct pt_regs *regs);
asmlinkage long sys_rt_sigaction(int,
				 const struct sigaction __user *,
				 struct sigaction __user *,
				 size_t);
asmlinkage long xtensa_shmat(int, char __user *, int);
asmlinkage long xtensa_fadvise64_64(int, int,
				    unsigned long long, unsigned long long);

/* Should probably move to linux/syscalls.h */
struct pollfd;
asmlinkage long sys_pselect6(int n, fd_set __user *inp, fd_set __user *outp,
	fd_set __user *exp, struct timespec __user *tsp, void __user *sig);
asmlinkage long sys_ppoll(struct pollfd __user *ufds, unsigned int nfds,
	struct timespec __user *tsp, const sigset_t __user *sigmask,
	size_t sigsetsize);


