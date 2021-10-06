/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_POWERPC_SYSCALLS_H
#define __ASM_POWERPC_SYSCALLS_H
#ifdef __KERNEL__

#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/compat.h>

struct rtas_args;

asmlinkage long sys_mmap(unsigned long addr, size_t len,
		unsigned long prot, unsigned long flags,
		unsigned long fd, off_t offset);
asmlinkage long sys_mmap2(unsigned long addr, size_t len,
		unsigned long prot, unsigned long flags,
		unsigned long fd, unsigned long pgoff);
asmlinkage long ppc64_personality(unsigned long personality);
asmlinkage long sys_rtas(struct rtas_args __user *uargs);

#ifdef CONFIG_COMPAT
unsigned long compat_sys_mmap2(unsigned long addr, size_t len,
			       unsigned long prot, unsigned long flags,
			       unsigned long fd, unsigned long pgoff);

compat_ssize_t compat_sys_pread64(unsigned int fd, char __user *ubuf, compat_size_t count,
				  u32 reg6, u32 pos1, u32 pos2);

compat_ssize_t compat_sys_pwrite64(unsigned int fd, const char __user *ubuf, compat_size_t count,
				   u32 reg6, u32 pos1, u32 pos2);

compat_ssize_t compat_sys_readahead(int fd, u32 r4, u32 offset1, u32 offset2, u32 count);

int compat_sys_truncate64(const char __user *path, u32 reg4,
			  unsigned long len1, unsigned long len2);

long compat_sys_fallocate(int fd, int mode, u32 offset1, u32 offset2, u32 len1, u32 len2);

int compat_sys_ftruncate64(unsigned int fd, u32 reg4, unsigned long len1,
			   unsigned long len2);

long ppc32_fadvise64(int fd, u32 unused, u32 offset1, u32 offset2,
		     size_t len, int advice);

long compat_sys_sync_file_range2(int fd, unsigned int flags,
				 unsigned int offset1, unsigned int offset2,
				 unsigned int nbytes1, unsigned int nbytes2);
#endif

#endif /* __KERNEL__ */
#endif /* __ASM_POWERPC_SYSCALLS_H */
