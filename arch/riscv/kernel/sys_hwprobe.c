// SPDX-License-Identifier: GPL-2.0-only
/*
 * The hwprobe interface, for allowing userspace to probe to see which features
 * are supported by the hardware.  See Documentation/arch/riscv/hwprobe.rst for
 * more details.
 */
#include <linux/syscalls.h>
#include <asm/cacheflush.h>
#include <asm/cpufeature.h>
#include <asm/hwprobe.h>
#include <asm/processor.h>
#include <asm/delay.h>
#include <asm/sbi.h>
#include <asm/switch_to.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm/vector.h>
#include <asm/vendor_extensions/thead_hwprobe.h>
#include <vdso/vsyscall.h>


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

	if (has_vector() && riscv_isa_extension_available(NULL, v))
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
		EXT_KEY(ZACAS);
		EXT_KEY(ZAWRS);
		EXT_KEY(ZBA);
		EXT_KEY(ZBB);
		EXT_KEY(ZBC);
		EXT_KEY(ZBKB);
		EXT_KEY(ZBKC);
		EXT_KEY(ZBKX);
		EXT_KEY(ZBS);
		EXT_KEY(ZCA);
		EXT_KEY(ZCB);
		EXT_KEY(ZCMOP);
		EXT_KEY(ZICBOZ);
		EXT_KEY(ZICOND);
		EXT_KEY(ZIHINTNTL);
		EXT_KEY(ZIHINTPAUSE);
		EXT_KEY(ZIMOP);
		EXT_KEY(ZKND);
		EXT_KEY(ZKNE);
		EXT_KEY(ZKNH);
		EXT_KEY(ZKSED);
		EXT_KEY(ZKSH);
		EXT_KEY(ZKT);
		EXT_KEY(ZTSO);

		/*
		 * All the following extensions must depend on the kernel
		 * support of V.
		 */
		if (has_vector()) {
			EXT_KEY(ZVBB);
			EXT_KEY(ZVBC);
			EXT_KEY(ZVE32F);
			EXT_KEY(ZVE32X);
			EXT_KEY(ZVE64D);
			EXT_KEY(ZVE64F);
			EXT_KEY(ZVE64X);
			EXT_KEY(ZVFH);
			EXT_KEY(ZVFHMIN);
			EXT_KEY(ZVKB);
			EXT_KEY(ZVKG);
			EXT_KEY(ZVKNED);
			EXT_KEY(ZVKNHA);
			EXT_KEY(ZVKNHB);
			EXT_KEY(ZVKSED);
			EXT_KEY(ZVKSH);
			EXT_KEY(ZVKT);
		}

		if (has_fpu()) {
			EXT_KEY(ZCD);
			EXT_KEY(ZCF);
			EXT_KEY(ZFA);
			EXT_KEY(ZFH);
			EXT_KEY(ZFHMIN);
		}

		if (IS_ENABLED(CONFIG_RISCV_ISA_SUPM))
			EXT_KEY(SUPM);
#undef EXT_KEY
	}

	/* Now turn off reporting features if any CPU is missing it. */
	pair->value &= ~missing;
}

static bool hwprobe_ext0_has(const struct cpumask *cpus, unsigned long ext)
{
	struct riscv_hwprobe pair;

	hwprobe_isa_ext0(&pair, cpus);
	return (pair.value & ext);
}

#if defined(CONFIG_RISCV_PROBE_UNALIGNED_ACCESS)
static u64 hwprobe_misaligned(const struct cpumask *cpus)
{
	int cpu;
	u64 perf = -1ULL;

	for_each_cpu(cpu, cpus) {
		int this_perf = per_cpu(misaligned_access_speed, cpu);

		if (perf == -1ULL)
			perf = this_perf;

		if (perf != this_perf) {
			perf = RISCV_HWPROBE_MISALIGNED_SCALAR_UNKNOWN;
			break;
		}
	}

	if (perf == -1ULL)
		return RISCV_HWPROBE_MISALIGNED_SCALAR_UNKNOWN;

	return perf;
}
#else
static u64 hwprobe_misaligned(const struct cpumask *cpus)
{
	if (IS_ENABLED(CONFIG_RISCV_EFFICIENT_UNALIGNED_ACCESS))
		return RISCV_HWPROBE_MISALIGNED_SCALAR_FAST;

	if (IS_ENABLED(CONFIG_RISCV_EMULATED_UNALIGNED_ACCESS) && unaligned_ctl_available())
		return RISCV_HWPROBE_MISALIGNED_SCALAR_EMULATED;

	return RISCV_HWPROBE_MISALIGNED_SCALAR_SLOW;
}
#endif

#ifdef CONFIG_RISCV_VECTOR_MISALIGNED
static u64 hwprobe_vec_misaligned(const struct cpumask *cpus)
{
	int cpu;
	u64 perf = -1ULL;

	/* Return if supported or not even if speed wasn't probed */
	for_each_cpu(cpu, cpus) {
		int this_perf = per_cpu(vector_misaligned_access, cpu);

		if (perf == -1ULL)
			perf = this_perf;

		if (perf != this_perf) {
			perf = RISCV_HWPROBE_MISALIGNED_VECTOR_UNKNOWN;
			break;
		}
	}

	if (perf == -1ULL)
		return RISCV_HWPROBE_MISALIGNED_VECTOR_UNKNOWN;

	return perf;
}
#else
static u64 hwprobe_vec_misaligned(const struct cpumask *cpus)
{
	if (IS_ENABLED(CONFIG_RISCV_EFFICIENT_VECTOR_UNALIGNED_ACCESS))
		return RISCV_HWPROBE_MISALIGNED_VECTOR_FAST;

	if (IS_ENABLED(CONFIG_RISCV_SLOW_VECTOR_UNALIGNED_ACCESS))
		return RISCV_HWPROBE_MISALIGNED_VECTOR_SLOW;

	return RISCV_HWPROBE_MISALIGNED_VECTOR_UNKNOWN;
}
#endif

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
	case RISCV_HWPROBE_KEY_MISALIGNED_SCALAR_PERF:
		pair->value = hwprobe_misaligned(cpus);
		break;

	case RISCV_HWPROBE_KEY_MISALIGNED_VECTOR_PERF:
		pair->value = hwprobe_vec_misaligned(cpus);
		break;

	case RISCV_HWPROBE_KEY_ZICBOZ_BLOCK_SIZE:
		pair->value = 0;
		if (hwprobe_ext0_has(cpus, RISCV_HWPROBE_EXT_ZICBOZ))
			pair->value = riscv_cboz_block_size;
		break;
	case RISCV_HWPROBE_KEY_HIGHEST_VIRT_ADDRESS:
		pair->value = user_max_virt_addr();
		break;

	case RISCV_HWPROBE_KEY_TIME_CSR_FREQ:
		pair->value = riscv_timebase;
		break;

	case RISCV_HWPROBE_KEY_VENDOR_EXT_THEAD_0:
		hwprobe_isa_vendor_ext_thead_0(pair, cpus);
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

static int hwprobe_get_values(struct riscv_hwprobe __user *pairs,
			      size_t pair_count, size_t cpusetsize,
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
	if (!cpusetsize && !cpus_user) {
		cpumask_copy(&cpus, cpu_online_mask);
	} else {
		if (cpusetsize > cpumask_size())
			cpusetsize = cpumask_size();

		ret = copy_from_user(&cpus, cpus_user, cpusetsize);
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

static int hwprobe_get_cpus(struct riscv_hwprobe __user *pairs,
			    size_t pair_count, size_t cpusetsize,
			    unsigned long __user *cpus_user,
			    unsigned int flags)
{
	cpumask_t cpus, one_cpu;
	bool clear_all = false;
	size_t i;
	int ret;

	if (flags != RISCV_HWPROBE_WHICH_CPUS)
		return -EINVAL;

	if (!cpusetsize || !cpus_user)
		return -EINVAL;

	if (cpusetsize > cpumask_size())
		cpusetsize = cpumask_size();

	ret = copy_from_user(&cpus, cpus_user, cpusetsize);
	if (ret)
		return -EFAULT;

	if (cpumask_empty(&cpus))
		cpumask_copy(&cpus, cpu_online_mask);

	cpumask_and(&cpus, &cpus, cpu_online_mask);

	cpumask_clear(&one_cpu);

	for (i = 0; i < pair_count; i++) {
		struct riscv_hwprobe pair, tmp;
		int cpu;

		ret = copy_from_user(&pair, &pairs[i], sizeof(pair));
		if (ret)
			return -EFAULT;

		if (!riscv_hwprobe_key_is_valid(pair.key)) {
			clear_all = true;
			pair = (struct riscv_hwprobe){ .key = -1, };
			ret = copy_to_user(&pairs[i], &pair, sizeof(pair));
			if (ret)
				return -EFAULT;
		}

		if (clear_all)
			continue;

		tmp = (struct riscv_hwprobe){ .key = pair.key, };

		for_each_cpu(cpu, &cpus) {
			cpumask_set_cpu(cpu, &one_cpu);

			hwprobe_one_pair(&tmp, &one_cpu);

			if (!riscv_hwprobe_pair_cmp(&tmp, &pair))
				cpumask_clear_cpu(cpu, &cpus);

			cpumask_clear_cpu(cpu, &one_cpu);
		}
	}

	if (clear_all)
		cpumask_clear(&cpus);

	ret = copy_to_user(cpus_user, &cpus, cpusetsize);
	if (ret)
		return -EFAULT;

	return 0;
}

static int do_riscv_hwprobe(struct riscv_hwprobe __user *pairs,
			    size_t pair_count, size_t cpusetsize,
			    unsigned long __user *cpus_user,
			    unsigned int flags)
{
	if (flags & RISCV_HWPROBE_WHICH_CPUS)
		return hwprobe_get_cpus(pairs, pair_count, cpusetsize,
					cpus_user, flags);

	return hwprobe_get_values(pairs, pair_count, cpusetsize,
				  cpus_user, flags);
}

#ifdef CONFIG_MMU

static int __init init_hwprobe_vdso_data(void)
{
	struct vdso_arch_data *avd = vdso_k_arch_data;
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
		size_t, pair_count, size_t, cpusetsize, unsigned long __user *,
		cpus, unsigned int, flags)
{
	return do_riscv_hwprobe(pairs, pair_count, cpusetsize,
				cpus, flags);
}
