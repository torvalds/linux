/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_POWERPC_SYSCALLS_H
#define __ASM_POWERPC_SYSCALLS_H
#ifdef __KERNEL__

#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/compat.h>

#include <asm/syscall.h>
#ifdef CONFIG_PPC64
#include <asm/syscalls_32.h>
#endif
#include <asm/unistd.h>
#include <asm/ucontext.h>

#ifndef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
long sys_ni_syscall(void);
#else
long sys_ni_syscall(const struct pt_regs *regs);
#endif

struct rtas_args;

/*
 * long long munging:
 * The 32 bit ABI passes long longs in an odd even register pair.
 * High and low parts are swapped depending on endian mode,
 * so define a macro (similar to mips linux32) to handle that.
 */
#ifdef __LITTLE_ENDIAN__
#define merge_64(low, high) (((u64)high << 32) | low)
#else
#define merge_64(high, low) (((u64)high << 32) | low)
#endif

/*
 * PowerPC architecture-specific syscalls
 */

#ifndef CONFIG_ARCH_HAS_SYSCALL_WRAPPER

long sys_rtas(struct rtas_args __user *uargs);

#ifdef CONFIG_PPC64
long sys_ppc64_personality(unsigned long personality);
#ifdef CONFIG_COMPAT
long compat_sys_ppc64_personality(unsigned long personality);
#endif /* CONFIG_COMPAT */
#endif /* CONFIG_PPC64 */

long sys_swapcontext(struct ucontext __user *old_ctx,
		     struct ucontext __user *new_ctx, long ctx_size);
long sys_mmap(unsigned long addr, size_t len,
	      unsigned long prot, unsigned long flags,
	      unsigned long fd, off_t offset);
long sys_mmap2(unsigned long addr, size_t len,
	       unsigned long prot, unsigned long flags,
	       unsigned long fd, unsigned long pgoff);
long sys_switch_endian(void);

#ifdef CONFIG_PPC32
long sys_sigreturn(void);
long sys_debug_setcontext(struct ucontext __user *ctx, int ndbg,
			  struct sig_dbg_op __user *dbg);
#endif

long sys_rt_sigreturn(void);

long sys_subpage_prot(unsigned long addr,
		      unsigned long len, u32 __user *map);

#ifdef CONFIG_COMPAT
long compat_sys_swapcontext(struct ucontext32 __user *old_ctx,
			    struct ucontext32 __user *new_ctx,
			    int ctx_size);
long compat_sys_old_getrlimit(unsigned int resource,
			      struct compat_rlimit __user *rlim);
long compat_sys_sigreturn(void);
long compat_sys_rt_sigreturn(void);
#endif /* CONFIG_COMPAT */

/*
 * Architecture specific signatures required by long long munging:
 * The 32 bit ABI passes long longs in an odd even register pair.
 * The following signatures provide a machine long parameter for
 * each register that will be supplied. The implementation is
 * responsible for combining parameter pairs.
 */

#ifdef CONFIG_PPC32
long sys_ppc_pread64(unsigned int fd,
		     char __user *ubuf, compat_size_t count,
		     u32 reg6, u32 pos1, u32 pos2);
long sys_ppc_pwrite64(unsigned int fd,
		      const char __user *ubuf, compat_size_t count,
		      u32 reg6, u32 pos1, u32 pos2);
long sys_ppc_readahead(int fd, u32 r4,
		       u32 offset1, u32 offset2, u32 count);
long sys_ppc_truncate64(const char __user *path, u32 reg4,
		        unsigned long len1, unsigned long len2);
long sys_ppc_ftruncate64(unsigned int fd, u32 reg4,
			 unsigned long len1, unsigned long len2);
long sys_ppc32_fadvise64(int fd, u32 unused, u32 offset1, u32 offset2,
			 size_t len, int advice);
long sys_ppc_sync_file_range2(int fd, unsigned int flags,
			      unsigned int offset1,
			      unsigned int offset2,
			      unsigned int nbytes1,
			      unsigned int nbytes2);
long sys_ppc_fallocate(int fd, int mode, u32 offset1, u32 offset2,
		       u32 len1, u32 len2);
#endif
#ifdef CONFIG_COMPAT
long compat_sys_mmap2(unsigned long addr, size_t len,
		      unsigned long prot, unsigned long flags,
		      unsigned long fd, unsigned long pgoff);
long compat_sys_ppc_pread64(unsigned int fd,
			    char __user *ubuf, compat_size_t count,
			    u32 reg6, u32 pos1, u32 pos2);
long compat_sys_ppc_pwrite64(unsigned int fd,
			     const char __user *ubuf, compat_size_t count,
			     u32 reg6, u32 pos1, u32 pos2);
long compat_sys_ppc_readahead(int fd, u32 r4,
			      u32 offset1, u32 offset2, u32 count);
long compat_sys_ppc_truncate64(const char __user *path, u32 reg4,
			       unsigned long len1, unsigned long len2);
long compat_sys_ppc_ftruncate64(unsigned int fd, u32 reg4,
				unsigned long len1, unsigned long len2);
long compat_sys_ppc32_fadvise64(int fd, u32 unused, u32 offset1, u32 offset2,
				size_t len, int advice);
long compat_sys_ppc_sync_file_range2(int fd, unsigned int flags,
				     unsigned int offset1,
				     unsigned int offset2,
				     unsigned int nbytes1,
				     unsigned int nbytes2);
#endif /* CONFIG_COMPAT */

#if defined(CONFIG_PPC32) || defined(CONFIG_COMPAT)
long sys_ppc_fadvise64_64(int fd, int advice,
			  u32 offset_high, u32 offset_low,
			  u32 len_high, u32 len_low);
#endif

#else

#define __SYSCALL_WITH_COMPAT(nr, native, compat)	__SYSCALL(nr, native)
#define __SYSCALL(nr, entry) \
	long entry(const struct pt_regs *regs);

#ifdef CONFIG_PPC64
#include <asm/syscall_table_64.h>
#else
#include <asm/syscall_table_32.h>
#endif /* CONFIG_PPC64 */

#endif /* CONFIG_ARCH_HAS_SYSCALL_WRAPPER */

#endif /* __KERNEL__ */
#endif /* __ASM_POWERPC_SYSCALLS_H */
