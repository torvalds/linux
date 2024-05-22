/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_SYSCALLS_H
#define __ASM_SH_SYSCALLS_H

asmlinkage int old_mmap(unsigned long addr, unsigned long len,
			unsigned long prot, unsigned long flags,
			int fd, unsigned long off);
asmlinkage long sys_mmap2(unsigned long addr, unsigned long len,
			  unsigned long prot, unsigned long flags,
			  unsigned long fd, unsigned long pgoff);
asmlinkage int sys_cacheflush(unsigned long addr, unsigned long len, int op);

#include <asm/syscalls_32.h>

#endif /* __ASM_SH_SYSCALLS_H */
