#ifndef __ASM_SH_SYSCALLS_H
#define __ASM_SH_SYSCALLS_H

#ifdef __KERNEL__

struct old_utsname;

asmlinkage int old_mmap(unsigned long addr, unsigned long len,
			unsigned long prot, unsigned long flags,
			int fd, unsigned long off);
asmlinkage long sys_mmap2(unsigned long addr, unsigned long len,
			  unsigned long prot, unsigned long flags,
			  unsigned long fd, unsigned long pgoff);
asmlinkage int sys_ipc(uint call, int first, int second,
		       int third, void __user *ptr, long fifth);
asmlinkage int sys_uname(struct old_utsname __user *name);

#ifdef CONFIG_SUPERH32
# include "syscalls_32.h"
#else
# include "syscalls_64.h"
#endif

#endif /* __KERNEL__ */
#endif /* __ASM_SH_SYSCALLS_H */
