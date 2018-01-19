/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2014 Darius Rad <darius@bluespec.com>
 * Copyright (C) 2017 SiFive
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#include <linux/syscalls.h>
#include <asm/unistd.h>
#include <asm/cacheflush.h>

static long riscv_sys_mmap(unsigned long addr, unsigned long len,
			   unsigned long prot, unsigned long flags,
			   unsigned long fd, off_t offset,
			   unsigned long page_shift_offset)
{
	if (unlikely(offset & (~PAGE_MASK >> page_shift_offset)))
		return -EINVAL;
	return sys_mmap_pgoff(addr, len, prot, flags, fd,
			      offset >> (PAGE_SHIFT - page_shift_offset));
}

#ifdef CONFIG_64BIT
SYSCALL_DEFINE6(mmap, unsigned long, addr, unsigned long, len,
	unsigned long, prot, unsigned long, flags,
	unsigned long, fd, off_t, offset)
{
	return riscv_sys_mmap(addr, len, prot, flags, fd, offset, 0);
}
#else
SYSCALL_DEFINE6(mmap2, unsigned long, addr, unsigned long, len,
	unsigned long, prot, unsigned long, flags,
	unsigned long, fd, off_t, offset)
{
	/*
	 * Note that the shift for mmap2 is constant (12),
	 * regardless of PAGE_SIZE
	 */
	return riscv_sys_mmap(addr, len, prot, flags, fd, offset, 12);
}
#endif /* !CONFIG_64BIT */

#ifdef CONFIG_SMP
/*
 * Allows the instruction cache to be flushed from userspace.  Despite RISC-V
 * having a direct 'fence.i' instruction available to userspace (which we
 * can't trap!), that's not actually viable when running on Linux because the
 * kernel might schedule a process on another hart.  There is no way for
 * userspace to handle this without invoking the kernel (as it doesn't know the
 * thread->hart mappings), so we've defined a RISC-V specific system call to
 * flush the instruction cache.
 *
 * sys_riscv_flush_icache() is defined to flush the instruction cache over an
 * address range, with the flush applying to either all threads or just the
 * caller.  We don't currently do anything with the address range, that's just
 * in there for forwards compatibility.
 */
SYSCALL_DEFINE3(riscv_flush_icache, uintptr_t, start, uintptr_t, end,
	uintptr_t, flags)
{
	struct mm_struct *mm = current->mm;
	bool local = (flags & SYS_RISCV_FLUSH_ICACHE_LOCAL) != 0;

	/* Check the reserved flags. */
	if (unlikely(flags & !SYS_RISCV_FLUSH_ICACHE_ALL))
		return -EINVAL;

	flush_icache_mm(mm, local);

	return 0;
}
#endif
