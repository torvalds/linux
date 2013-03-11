#ifndef _ASM_METAG_SYSCALLS_H
#define _ASM_METAG_SYSCALLS_H

#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/signal.h>

/* kernel/signal.c */
#define sys_rt_sigreturn sys_rt_sigreturn
asmlinkage long sys_rt_sigreturn(void);

#include <asm-generic/syscalls.h>

/* kernel/sys_metag.c */
asmlinkage int sys_metag_setglobalbit(char __user *, int);
asmlinkage void sys_metag_set_fpu_flags(unsigned int);
asmlinkage int sys_metag_set_tls(void __user *);
asmlinkage void *sys_metag_get_tls(void);

asmlinkage long sys_truncate64_metag(const char __user *, unsigned long,
				     unsigned long);
asmlinkage long sys_ftruncate64_metag(unsigned int, unsigned long,
				      unsigned long);
asmlinkage long sys_fadvise64_64_metag(int, unsigned long, unsigned long,
				       unsigned long, unsigned long, int);
asmlinkage long sys_readahead_metag(int, unsigned long, unsigned long, size_t);
asmlinkage ssize_t sys_pread64_metag(unsigned long, char __user *, size_t,
				     unsigned long, unsigned long);
asmlinkage ssize_t sys_pwrite64_metag(unsigned long, char __user *, size_t,
				      unsigned long, unsigned long);
asmlinkage long sys_sync_file_range_metag(int, unsigned long, unsigned long,
					  unsigned long, unsigned long,
					  unsigned int);

int do_work_pending(struct pt_regs *regs, unsigned int thread_flags,
		    int syscall);

#endif /* _ASM_METAG_SYSCALLS_H */
