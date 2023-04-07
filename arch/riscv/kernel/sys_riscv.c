// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2014 Darius Rad <darius@bluespec.com>
 * Copyright (C) 2017 SiFive
 */

#include <linux/syscalls.h>
#include <asm/cacheflush.h>
#include <asm/hwprobe.h>
#include <asm/sbi.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm-generic/mman-common.h>

static long riscv_sys_mmap(unsigned long addr, unsigned long len,
			   unsigned long prot, unsigned long flags,
			   unsigned long fd, off_t offset,
			   unsigned long page_shift_offset)
{
	if (unlikely(offset & (~PAGE_MASK >> page_shift_offset)))
		return -EINVAL;

	return ksys_mmap_pgoff(addr, len, prot, flags, fd,
			       offset >> (PAGE_SHIFT - page_shift_offset));
}

#ifdef CONFIG_64BIT
SYSCALL_DEFINE6(mmap, unsigned long, addr, unsigned long, len,
	unsigned long, prot, unsigned long, flags,
	unsigned long, fd, off_t, offset)
{
	return riscv_sys_mmap(addr, len, prot, flags, fd, offset, 0);
}
#endif

#if defined(CONFIG_32BIT) || defined(CONFIG_COMPAT)
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
#endif

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
	/* Check the reserved flags. */
	if (unlikely(flags & ~SYS_RISCV_FLUSH_ICACHE_ALL))
		return -EINVAL;

	flush_icache_mm(current->mm, flags & SYS_RISCV_FLUSH_ICACHE_LOCAL);

	return 0;
}

/*
 * The hwprobe interface, for allowing userspace to probe to see which features
 * are supported by the hardware.  See Documentation/riscv/hwprobe.rst for more
 * details.
 */
static void hwprobe_arch_id(struct riscv_hwprobe *pair,
			    const struct cpumask *cpus)
{
	u64 id = -1ULL;
	bool first = true;
	int cpu;

	for_each_cpu(cpu, cpus) {
		u64 cpu_id;

		switch (pair->key) {
		case RISCV_HWPROBE_KEY_MVENDORID:
			cpu_id = riscv_cached_mvendorid(cpu);
			break;
		case RISCV_HWPROBE_KEY_MIMPID:
			cpu_id = riscv_cached_mimpid(cpu);
			break;
		case RISCV_HWPROBE_KEY_MARCHID:
			cpu_id = riscv_cached_marchid(cpu);
			break;
		}

		if (first)
			id = cpu_id;

		/*
		 * If there's a mismatch for the given set, return -1 in the
		 * value.
		 */
		if (id != cpu_id) {
			id = -1ULL;
			break;
		}
	}

	pair->value = id;
}

static void hwprobe_one_pair(struct riscv_hwprobe *pair,
			     const struct cpumask *cpus)
{
	switch (pair->key) {
	case RISCV_HWPROBE_KEY_MVENDORID:
	case RISCV_HWPROBE_KEY_MARCHID:
	case RISCV_HWPROBE_KEY_MIMPID:
		hwprobe_arch_id(pair, cpus);
		break;

	/*
	 * For forward compatibility, unknown keys don't fail the whole
	 * call, but get their element key set to -1 and value set to 0
	 * indicating they're unrecognized.
	 */
	default:
		pair->key = -1;
		pair->value = 0;
		break;
	}
}

static int do_riscv_hwprobe(struct riscv_hwprobe __user *pairs,
			    size_t pair_count, size_t cpu_count,
			    unsigned long __user *cpus_user,
			    unsigned int flags)
{
	size_t out;
	int ret;
	cpumask_t cpus;

	/* Check the reserved flags. */
	if (flags != 0)
		return -EINVAL;

	/*
	 * The interface supports taking in a CPU mask, and returns values that
	 * are consistent across that mask. Allow userspace to specify NULL and
	 * 0 as a shortcut to all online CPUs.
	 */
	cpumask_clear(&cpus);
	if (!cpu_count && !cpus_user) {
		cpumask_copy(&cpus, cpu_online_mask);
	} else {
		if (cpu_count > cpumask_size())
			cpu_count = cpumask_size();

		ret = copy_from_user(&cpus, cpus_user, cpu_count);
		if (ret)
			return -EFAULT;

		/*
		 * Userspace must provide at least one online CPU, without that
		 * there's no way to define what is supported.
		 */
		cpumask_and(&cpus, &cpus, cpu_online_mask);
		if (cpumask_empty(&cpus))
			return -EINVAL;
	}

	for (out = 0; out < pair_count; out++, pairs++) {
		struct riscv_hwprobe pair;

		if (get_user(pair.key, &pairs->key))
			return -EFAULT;

		pair.value = 0;
		hwprobe_one_pair(&pair, &cpus);
		ret = put_user(pair.key, &pairs->key);
		if (ret == 0)
			ret = put_user(pair.value, &pairs->value);

		if (ret)
			return -EFAULT;
	}

	return 0;
}

SYSCALL_DEFINE5(riscv_hwprobe, struct riscv_hwprobe __user *, pairs,
		size_t, pair_count, size_t, cpu_count, unsigned long __user *,
		cpus, unsigned int, flags)
{
	return do_riscv_hwprobe(pairs, pair_count, cpu_count,
				cpus, flags);
}
