// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2014 Darius Rad <darius@bluespec.com>
 * Copyright (C) 2017 SiFive
 */

#include <linux/syscalls.h>
#include <asm/cacheflush.h>
#include <asm/cpufeature.h>
#include <asm/hwprobe.h>
#include <asm/sbi.h>
#include <asm/vector.h>
#include <asm/switch_to.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm-generic/mman-common.h>
#include <vdso/vsyscall.h>

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
 * are supported by the hardware.  See Documentation/arch/riscv/hwprobe.rst for more
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

		if (first) {
			id = cpu_id;
			first = false;
		}

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

static void hwprobe_isa_ext0(struct riscv_hwprobe *pair,
			     const struct cpumask *cpus)
{
	int cpu;
	u64 missing = 0;

	pair->value = 0;
	if (has_fpu())
		pair->value |= RISCV_HWPROBE_IMA_FD;

	if (riscv_isa_extension_available(NULL, c))
		pair->value |= RISCV_HWPROBE_IMA_C;

	if (has_vector())
		pair->value |= RISCV_HWPROBE_IMA_V;

	/*
	 * Loop through and record extensions that 1) anyone has, and 2) anyone
	 * doesn't have.
	 */
	for_each_cpu(cpu, cpus) {
		struct riscv_isainfo *isainfo = &hart_isa[cpu];

#define EXT_KEY(ext)									\
	do {										\
		if (__riscv_isa_extension_available(isainfo->isa, RISCV_ISA_EXT_##ext))	\
			pair->value |= RISCV_HWPROBE_EXT_##ext;				\
		else									\
			missing |= RISCV_HWPROBE_EXT_##ext;				\
	} while (false)

		/*
		 * Only use EXT_KEY() for extensions which can be exposed to userspace,
		 * regardless of the kernel's configuration, as no other checks, besides
		 * presence in the hart_isa bitmap, are made.
		 */
		EXT_KEY(ZBA);
		EXT_KEY(ZBB);
		EXT_KEY(ZBS);
		EXT_KEY(ZICBOZ);
#undef EXT_KEY
	}

	/* Now turn off reporting features if any CPU is missing it. */
	pair->value &= ~missing;
}

static bool hwprobe_ext0_has(const struct cpumask *cpus, u64 ext)
{
	struct riscv_hwprobe pair;

	hwprobe_isa_ext0(&pair, cpus);
	return (pair.value & ext);
}

static u64 hwprobe_misaligned(const struct cpumask *cpus)
{
	int cpu;
	u64 perf = -1ULL;

	for_each_cpu(cpu, cpus) {
		int this_perf = per_cpu(misaligned_access_speed, cpu);

		if (perf == -1ULL)
			perf = this_perf;

		if (perf != this_perf) {
			perf = RISCV_HWPROBE_MISALIGNED_UNKNOWN;
			break;
		}
	}

	if (perf == -1ULL)
		return RISCV_HWPROBE_MISALIGNED_UNKNOWN;

	return perf;
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
	 * The kernel already assumes that the base single-letter ISA
	 * extensions are supported on all harts, and only supports the
	 * IMA base, so just cheat a bit here and tell that to
	 * userspace.
	 */
	case RISCV_HWPROBE_KEY_BASE_BEHAVIOR:
		pair->value = RISCV_HWPROBE_BASE_BEHAVIOR_IMA;
		break;

	case RISCV_HWPROBE_KEY_IMA_EXT_0:
		hwprobe_isa_ext0(pair, cpus);
		break;

	case RISCV_HWPROBE_KEY_CPUPERF_0:
		pair->value = hwprobe_misaligned(cpus);
		break;

	case RISCV_HWPROBE_KEY_ZICBOZ_BLOCK_SIZE:
		pair->value = 0;
		if (hwprobe_ext0_has(cpus, RISCV_HWPROBE_EXT_ZICBOZ))
			pair->value = riscv_cboz_block_size;
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

#ifdef CONFIG_MMU

static int __init init_hwprobe_vdso_data(void)
{
	struct vdso_data *vd = __arch_get_k_vdso_data();
	struct arch_vdso_data *avd = &vd->arch_data;
	u64 id_bitsmash = 0;
	struct riscv_hwprobe pair;
	int key;

	/*
	 * Initialize vDSO data with the answers for the "all CPUs" case, to
	 * save a syscall in the common case.
	 */
	for (key = 0; key <= RISCV_HWPROBE_MAX_KEY; key++) {
		pair.key = key;
		hwprobe_one_pair(&pair, cpu_online_mask);

		WARN_ON_ONCE(pair.key < 0);

		avd->all_cpu_hwprobe_values[key] = pair.value;
		/*
		 * Smash together the vendor, arch, and impl IDs to see if
		 * they're all 0 or any negative.
		 */
		if (key <= RISCV_HWPROBE_KEY_MIMPID)
			id_bitsmash |= pair.value;
	}

	/*
	 * If the arch, vendor, and implementation ID are all the same across
	 * all harts, then assume all CPUs are the same, and allow the vDSO to
	 * answer queries for arbitrary masks. However if all values are 0 (not
	 * populated) or any value returns -1 (varies across CPUs), then the
	 * vDSO should defer to the kernel for exotic cpu masks.
	 */
	avd->homogeneous_cpus = id_bitsmash != 0 && id_bitsmash != -1;
	return 0;
}

arch_initcall_sync(init_hwprobe_vdso_data);

#endif /* CONFIG_MMU */

SYSCALL_DEFINE5(riscv_hwprobe, struct riscv_hwprobe __user *, pairs,
		size_t, pair_count, size_t, cpu_count, unsigned long __user *,
		cpus, unsigned int, flags)
{
	return do_riscv_hwprobe(pairs, pair_count, cpu_count,
				cpus, flags);
}

/* Not defined using SYSCALL_DEFINE0 to avoid error injection */
asmlinkage long __riscv_sys_ni_syscall(const struct pt_regs *__unused)
{
	return -ENOSYS;
}
