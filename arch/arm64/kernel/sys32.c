// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm64/kernel/sys32.c
 *
 * Copyright (C) 2015 ARM Ltd.
 */

#include <linux/compat.h>
#include <linux/compiler.h>
#include <linux/syscalls.h>

#include <asm/syscall.h>
#include <asm/unistd_compat_32.h>

asmlinkage long compat_sys_sigreturn(void);
asmlinkage long compat_sys_rt_sigreturn(void);

COMPAT_SYSCALL_DEFINE3(aarch32_statfs64, const char __user *, pathname,
		       compat_size_t, sz, struct compat_statfs64 __user *, buf)
{
	/*
	 * 32-bit ARM applies an OABI compatibility fixup to statfs64 and
	 * fstatfs64 regardless of whether OABI is in use, and therefore
	 * arbitrary binaries may rely upon it, so we must do the same.
	 * For more details, see commit:
	 *
	 * 713c481519f19df9 ("[ARM] 3108/2: old ABI compat: statfs64 and
	 * fstatfs64")
	 */
	if (sz == 88)
		sz = 84;

	return kcompat_sys_statfs64(pathname, sz, buf);
}

COMPAT_SYSCALL_DEFINE3(aarch32_fstatfs64, unsigned int, fd, compat_size_t, sz,
		       struct compat_statfs64 __user *, buf)
{
	/* see aarch32_statfs64 */
	if (sz == 88)
		sz = 84;

	return kcompat_sys_fstatfs64(fd, sz, buf);
}

/*
 * Note: off_4k is always in units of 4K. If we can't do the
 * requested offset because it is not page-aligned, we return -EINVAL.
 */
COMPAT_SYSCALL_DEFINE6(aarch32_mmap2, unsigned long, addr, unsigned long, len,
		       unsigned long, prot, unsigned long, flags,
		       unsigned long, fd, unsigned long, off_4k)
{
	if (off_4k & (~PAGE_MASK >> 12))
		return -EINVAL;

	off_4k >>= (PAGE_SHIFT - 12);

	return ksys_mmap_pgoff(addr, len, prot, flags, fd, off_4k);
}

#ifdef CONFIG_CPU_BIG_ENDIAN
#define arg_u32p(name)	u32, name##_hi, u32, name##_lo
#else
#define arg_u32p(name)	u32, name##_lo, u32, name##_hi
#endif

#define arg_u64(name)	(((u64)name##_hi << 32) | name##_lo)

COMPAT_SYSCALL_DEFINE6(aarch32_pread64, unsigned int, fd, char __user *, buf,
		       size_t, count, u32, __pad, arg_u32p(pos))
{
	return ksys_pread64(fd, buf, count, arg_u64(pos));
}

COMPAT_SYSCALL_DEFINE6(aarch32_pwrite64, unsigned int, fd,
		       const char __user *, buf, size_t, count, u32, __pad,
		       arg_u32p(pos))
{
	return ksys_pwrite64(fd, buf, count, arg_u64(pos));
}

COMPAT_SYSCALL_DEFINE4(aarch32_truncate64, const char __user *, pathname,
		       u32, __pad, arg_u32p(length))
{
	return ksys_truncate(pathname, arg_u64(length));
}

COMPAT_SYSCALL_DEFINE4(aarch32_ftruncate64, unsigned int, fd, u32, __pad,
		       arg_u32p(length))
{
	return ksys_ftruncate(fd, arg_u64(length));
}

COMPAT_SYSCALL_DEFINE5(aarch32_readahead, int, fd, u32, __pad,
		       arg_u32p(offset), size_t, count)
{
	return ksys_readahead(fd, arg_u64(offset), count);
}

COMPAT_SYSCALL_DEFINE6(aarch32_fadvise64_64, int, fd, int, advice,
		       arg_u32p(offset), arg_u32p(len))
{
	return ksys_fadvise64_64(fd, arg_u64(offset), arg_u64(len), advice);
}

COMPAT_SYSCALL_DEFINE6(aarch32_sync_file_range2, int, fd, unsigned int, flags,
		       arg_u32p(offset), arg_u32p(nbytes))
{
	return ksys_sync_file_range(fd, arg_u64(offset), arg_u64(nbytes),
				    flags);
}

COMPAT_SYSCALL_DEFINE6(aarch32_fallocate, int, fd, int, mode,
		       arg_u32p(offset), arg_u32p(len))
{
	return ksys_fallocate(fd, mode, arg_u64(offset), arg_u64(len));
}

#define __SYSCALL_WITH_COMPAT(nr, sym, compat) __SYSCALL(nr, compat)

#undef __SYSCALL
#define __SYSCALL(nr, sym)	asmlinkage long __arm64_##sym(const struct pt_regs *);
#include <asm/syscall_table_32.h>

#undef __SYSCALL
#define __SYSCALL(nr, sym)	[nr] = __arm64_##sym,

const syscall_fn_t compat_sys_call_table[__NR_compat32_syscalls] = {
	[0 ... __NR_compat32_syscalls - 1] = __arm64_sys_ni_syscall,
#include <asm/syscall_table_32.h>
};
