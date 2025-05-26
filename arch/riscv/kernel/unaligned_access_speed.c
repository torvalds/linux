// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Rivos Inc.
 */

#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/jump_label.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/types.h>
#include <asm/cpufeature.h>
#include <asm/hwprobe.h>
#include <asm/vector.h>

#include "copy-unaligned.h"

#define MISALIGNED_ACCESS_JIFFIES_LG2 1
#define MISALIGNED_BUFFER_SIZE 0x4000
#define MISALIGNED_BUFFER_ORDER get_order(MISALIGNED_BUFFER_SIZE)
#define MISALIGNED_COPY_SIZE ((MISALIGNED_BUFFER_SIZE / 2) - 0x80)

DEFINE_PER_CPU(long, misaligned_access_speed) = RISCV_HWPROBE_MISALIGNED_SCALAR_UNKNOWN;
DEFINE_PER_CPU(long, vector_misaligned_access) = RISCV_HWPROBE_MISALIGNED_VECTOR_UNSUPPORTED;

static long unaligned_scalar_speed_param = RISCV_HWPROBE_MISALIGNED_SCALAR_UNKNOWN;
static long unaligned_vector_speed_param = RISCV_HWPROBE_MISALIGNED_VECTOR_UNKNOWN;

static cpumask_t fast_misaligned_access;

#ifdef CONFIG_RISCV_PROBE_UNALIGNED_ACCESS
static int check_unaligned_access(void *param)
{
	int cpu = smp_processor_id();
	u64 start_cycles, end_cycles;
	u64 word_cycles;
	u64 byte_cycles;
	int ratio;
	unsigned long start_jiffies, now;
	struct page *page = param;
	void *dst;
	void *src;
	long speed = RISCV_HWPROBE_MISALIGNED_SCALAR_SLOW;

	if (per_cpu(misaligned_access_speed, cpu) != RISCV_HWPROBE_MISALIGNED_SCALAR_UNKNOWN)
		return 0;

	/* Make an unaligned destination buffer. */
	dst = (void *)((unsigned long)page_address(page) | 0x1);
	/* Unalign src as well, but differently (off by 1 + 2 = 3). */
	src = dst + (MISALIGNED_BUFFER_SIZE / 2);
	src += 2;
	word_cycles = -1ULL;
	/* Do a warmup. */
	__riscv_copy_words_unaligned(dst, src, MISALIGNED_COPY_SIZE);
	preempt_disable();
	start_jiffies = jiffies;
	while ((now = jiffies) == start_jiffies)
		cpu_relax();

	/*
	 * For a fixed amount of time, repeatedly try the function, and take
	 * the best time in cycles as the measurement.
	 */
	while (time_before(jiffies, now + (1 << MISALIGNED_ACCESS_JIFFIES_LG2))) {
		start_cycles = get_cycles64();
		/* Ensure the CSR read can't reorder WRT to the copy. */
		mb();
		__riscv_copy_words_unaligned(dst, src, MISALIGNED_COPY_SIZE);
		/* Ensure the copy ends before the end time is snapped. */
		mb();
		end_cycles = get_cycles64();
		if ((end_cycles - start_cycles) < word_cycles)
			word_cycles = end_cycles - start_cycles;
	}

	byte_cycles = -1ULL;
	__riscv_copy_bytes_unaligned(dst, src, MISALIGNED_COPY_SIZE);
	start_jiffies = jiffies;
	while ((now = jiffies) == start_jiffies)
		cpu_relax();

	while (time_before(jiffies, now + (1 << MISALIGNED_ACCESS_JIFFIES_LG2))) {
		start_cycles = get_cycles64();
		mb();
		__riscv_copy_bytes_unaligned(dst, src, MISALIGNED_COPY_SIZE);
		mb();
		end_cycles = get_cycles64();
		if ((end_cycles - start_cycles) < byte_cycles)
			byte_cycles = end_cycles - start_cycles;
	}

	preempt_enable();

	/* Don't divide by zero. */
	if (!word_cycles || !byte_cycles) {
		pr_warn("cpu%d: rdtime lacks granularity needed to measure unaligned access speed\n",
			cpu);

		return 0;
	}

	if (word_cycles < byte_cycles)
		speed = RISCV_HWPROBE_MISALIGNED_SCALAR_FAST;

	ratio = div_u64((byte_cycles * 100), word_cycles);
	pr_info("cpu%d: Ratio of byte access time to unaligned word access is %d.%02d, unaligned accesses are %s\n",
		cpu,
		ratio / 100,
		ratio % 100,
		(speed == RISCV_HWPROBE_MISALIGNED_SCALAR_FAST) ? "fast" : "slow");

	per_cpu(misaligned_access_speed, cpu) = speed;

	/*
	 * Set the value of fast_misaligned_access of a CPU. These operations
	 * are atomic to avoid race conditions.
	 */
	if (speed == RISCV_HWPROBE_MISALIGNED_SCALAR_FAST)
		cpumask_set_cpu(cpu, &fast_misaligned_access);
	else
		cpumask_clear_cpu(cpu, &fast_misaligned_access);

	return 0;
}

static void __init check_unaligned_access_nonboot_cpu(void *param)
{
	unsigned int cpu = smp_processor_id();
	struct page **pages = param;

	if (smp_processor_id() != 0)
		check_unaligned_access(pages[cpu]);
}

/* Measure unaligned access speed on all CPUs present at boot in parallel. */
static void __init check_unaligned_access_speed_all_cpus(void)
{
	unsigned int cpu;
	unsigned int cpu_count = num_possible_cpus();
	struct page **bufs = kcalloc(cpu_count, sizeof(*bufs), GFP_KERNEL);

	if (!bufs) {
		pr_warn("Allocation failure, not measuring misaligned performance\n");
		return;
	}

	/*
	 * Allocate separate buffers for each CPU so there's no fighting over
	 * cache lines.
	 */
	for_each_cpu(cpu, cpu_online_mask) {
		bufs[cpu] = alloc_pages(GFP_KERNEL, MISALIGNED_BUFFER_ORDER);
		if (!bufs[cpu]) {
			pr_warn("Allocation failure, not measuring misaligned performance\n");
			goto out;
		}
	}

	/* Check everybody except 0, who stays behind to tend jiffies. */
	on_each_cpu(check_unaligned_access_nonboot_cpu, bufs, 1);

	/* Check core 0. */
	smp_call_on_cpu(0, check_unaligned_access, bufs[0], true);

out:
	for_each_cpu(cpu, cpu_online_mask) {
		if (bufs[cpu])
			__free_pages(bufs[cpu], MISALIGNED_BUFFER_ORDER);
	}

	kfree(bufs);
}
#else /* CONFIG_RISCV_PROBE_UNALIGNED_ACCESS */
static void __init check_unaligned_access_speed_all_cpus(void)
{
}
#endif

DEFINE_STATIC_KEY_FALSE(fast_unaligned_access_speed_key);

static void modify_unaligned_access_branches(cpumask_t *mask, int weight)
{
	if (cpumask_weight(mask) == weight)
		static_branch_enable_cpuslocked(&fast_unaligned_access_speed_key);
	else
		static_branch_disable_cpuslocked(&fast_unaligned_access_speed_key);
}

static void set_unaligned_access_static_branches_except_cpu(int cpu)
{
	/*
	 * Same as set_unaligned_access_static_branches, except excludes the
	 * given CPU from the result. When a CPU is hotplugged into an offline
	 * state, this function is called before the CPU is set to offline in
	 * the cpumask, and thus the CPU needs to be explicitly excluded.
	 */

	cpumask_t fast_except_me;

	cpumask_and(&fast_except_me, &fast_misaligned_access, cpu_online_mask);
	cpumask_clear_cpu(cpu, &fast_except_me);

	modify_unaligned_access_branches(&fast_except_me, num_online_cpus() - 1);
}

static void set_unaligned_access_static_branches(void)
{
	/*
	 * This will be called after check_unaligned_access_all_cpus so the
	 * result of unaligned access speed for all CPUs will be available.
	 *
	 * To avoid the number of online cpus changing between reading
	 * cpu_online_mask and calling num_online_cpus, cpus_read_lock must be
	 * held before calling this function.
	 */

	cpumask_t fast_and_online;

	cpumask_and(&fast_and_online, &fast_misaligned_access, cpu_online_mask);

	modify_unaligned_access_branches(&fast_and_online, num_online_cpus());
}

static int __init lock_and_set_unaligned_access_static_branch(void)
{
	cpus_read_lock();
	set_unaligned_access_static_branches();
	cpus_read_unlock();

	return 0;
}

arch_initcall_sync(lock_and_set_unaligned_access_static_branch);

static int riscv_online_cpu(unsigned int cpu)
{
	/* We are already set since the last check */
	if (per_cpu(misaligned_access_speed, cpu) != RISCV_HWPROBE_MISALIGNED_SCALAR_UNKNOWN) {
		goto exit;
	} else if (unaligned_scalar_speed_param != RISCV_HWPROBE_MISALIGNED_SCALAR_UNKNOWN) {
		per_cpu(misaligned_access_speed, cpu) = unaligned_scalar_speed_param;
		goto exit;
	}

#ifdef CONFIG_RISCV_PROBE_UNALIGNED_ACCESS
	{
		static struct page *buf;

		check_unaligned_access_emulated(NULL);
		buf = alloc_pages(GFP_KERNEL, MISALIGNED_BUFFER_ORDER);
		if (!buf) {
			pr_warn("Allocation failure, not measuring misaligned performance\n");
			return -ENOMEM;
		}

		check_unaligned_access(buf);
		__free_pages(buf, MISALIGNED_BUFFER_ORDER);
	}
#endif

exit:
	set_unaligned_access_static_branches();

	return 0;
}

static int riscv_offline_cpu(unsigned int cpu)
{
	set_unaligned_access_static_branches_except_cpu(cpu);

	return 0;
}

#ifdef CONFIG_RISCV_PROBE_VECTOR_UNALIGNED_ACCESS
static void check_vector_unaligned_access(struct work_struct *work __always_unused)
{
	int cpu = smp_processor_id();
	u64 start_cycles, end_cycles;
	u64 word_cycles;
	u64 byte_cycles;
	int ratio;
	unsigned long start_jiffies, now;
	struct page *page;
	void *dst;
	void *src;
	long speed = RISCV_HWPROBE_MISALIGNED_VECTOR_SLOW;

	if (per_cpu(vector_misaligned_access, cpu) != RISCV_HWPROBE_MISALIGNED_VECTOR_UNKNOWN)
		return;

	page = alloc_pages(GFP_KERNEL, MISALIGNED_BUFFER_ORDER);
	if (!page) {
		pr_warn("Allocation failure, not measuring vector misaligned performance\n");
		return;
	}

	/* Make an unaligned destination buffer. */
	dst = (void *)((unsigned long)page_address(page) | 0x1);
	/* Unalign src as well, but differently (off by 1 + 2 = 3). */
	src = dst + (MISALIGNED_BUFFER_SIZE / 2);
	src += 2;
	word_cycles = -1ULL;

	/* Do a warmup. */
	kernel_vector_begin();
	__riscv_copy_vec_words_unaligned(dst, src, MISALIGNED_COPY_SIZE);

	start_jiffies = jiffies;
	while ((now = jiffies) == start_jiffies)
		cpu_relax();

	/*
	 * For a fixed amount of time, repeatedly try the function, and take
	 * the best time in cycles as the measurement.
	 */
	while (time_before(jiffies, now + (1 << MISALIGNED_ACCESS_JIFFIES_LG2))) {
		start_cycles = get_cycles64();
		/* Ensure the CSR read can't reorder WRT to the copy. */
		mb();
		__riscv_copy_vec_words_unaligned(dst, src, MISALIGNED_COPY_SIZE);
		/* Ensure the copy ends before the end time is snapped. */
		mb();
		end_cycles = get_cycles64();
		if ((end_cycles - start_cycles) < word_cycles)
			word_cycles = end_cycles - start_cycles;
	}

	byte_cycles = -1ULL;
	__riscv_copy_vec_bytes_unaligned(dst, src, MISALIGNED_COPY_SIZE);
	start_jiffies = jiffies;
	while ((now = jiffies) == start_jiffies)
		cpu_relax();

	while (time_before(jiffies, now + (1 << MISALIGNED_ACCESS_JIFFIES_LG2))) {
		start_cycles = get_cycles64();
		/* Ensure the CSR read can't reorder WRT to the copy. */
		mb();
		__riscv_copy_vec_bytes_unaligned(dst, src, MISALIGNED_COPY_SIZE);
		/* Ensure the copy ends before the end time is snapped. */
		mb();
		end_cycles = get_cycles64();
		if ((end_cycles - start_cycles) < byte_cycles)
			byte_cycles = end_cycles - start_cycles;
	}

	kernel_vector_end();

	/* Don't divide by zero. */
	if (!word_cycles || !byte_cycles) {
		pr_warn("cpu%d: rdtime lacks granularity needed to measure unaligned vector access speed\n",
			cpu);

		goto free;
	}

	if (word_cycles < byte_cycles)
		speed = RISCV_HWPROBE_MISALIGNED_VECTOR_FAST;

	ratio = div_u64((byte_cycles * 100), word_cycles);
	pr_info("cpu%d: Ratio of vector byte access time to vector unaligned word access is %d.%02d, unaligned accesses are %s\n",
		cpu,
		ratio / 100,
		ratio % 100,
		(speed ==  RISCV_HWPROBE_MISALIGNED_VECTOR_FAST) ? "fast" : "slow");

	per_cpu(vector_misaligned_access, cpu) = speed;

free:
	__free_pages(page, MISALIGNED_BUFFER_ORDER);
}

/* Measure unaligned access speed on all CPUs present at boot in parallel. */
static int __init vec_check_unaligned_access_speed_all_cpus(void *unused __always_unused)
{
	schedule_on_each_cpu(check_vector_unaligned_access);

	return 0;
}
#else /* CONFIG_RISCV_PROBE_VECTOR_UNALIGNED_ACCESS */
static int __init vec_check_unaligned_access_speed_all_cpus(void *unused __always_unused)
{
	return 0;
}
#endif

static int riscv_online_cpu_vec(unsigned int cpu)
{
	if (unaligned_vector_speed_param != RISCV_HWPROBE_MISALIGNED_VECTOR_UNKNOWN) {
		per_cpu(vector_misaligned_access, cpu) = unaligned_vector_speed_param;
		return 0;
	}

#ifdef CONFIG_RISCV_PROBE_VECTOR_UNALIGNED_ACCESS
	if (per_cpu(vector_misaligned_access, cpu) != RISCV_HWPROBE_MISALIGNED_VECTOR_UNKNOWN)
		return 0;

	check_vector_unaligned_access_emulated(NULL);
	check_vector_unaligned_access(NULL);
#endif

	return 0;
}

static const char * const speed_str[] __initconst = { NULL, NULL, "slow", "fast", "unsupported" };

static int __init set_unaligned_scalar_speed_param(char *str)
{
	if (!strcmp(str, speed_str[RISCV_HWPROBE_MISALIGNED_SCALAR_SLOW]))
		unaligned_scalar_speed_param = RISCV_HWPROBE_MISALIGNED_SCALAR_SLOW;
	else if (!strcmp(str, speed_str[RISCV_HWPROBE_MISALIGNED_SCALAR_FAST]))
		unaligned_scalar_speed_param = RISCV_HWPROBE_MISALIGNED_SCALAR_FAST;
	else if (!strcmp(str, speed_str[RISCV_HWPROBE_MISALIGNED_SCALAR_UNSUPPORTED]))
		unaligned_scalar_speed_param = RISCV_HWPROBE_MISALIGNED_SCALAR_UNSUPPORTED;
	else
		return -EINVAL;

	return 1;
}
__setup("unaligned_scalar_speed=", set_unaligned_scalar_speed_param);

static int __init set_unaligned_vector_speed_param(char *str)
{
	if (!strcmp(str, speed_str[RISCV_HWPROBE_MISALIGNED_VECTOR_SLOW]))
		unaligned_vector_speed_param = RISCV_HWPROBE_MISALIGNED_VECTOR_SLOW;
	else if (!strcmp(str, speed_str[RISCV_HWPROBE_MISALIGNED_VECTOR_FAST]))
		unaligned_vector_speed_param = RISCV_HWPROBE_MISALIGNED_VECTOR_FAST;
	else if (!strcmp(str, speed_str[RISCV_HWPROBE_MISALIGNED_VECTOR_UNSUPPORTED]))
		unaligned_vector_speed_param = RISCV_HWPROBE_MISALIGNED_VECTOR_UNSUPPORTED;
	else
		return -EINVAL;

	return 1;
}
__setup("unaligned_vector_speed=", set_unaligned_vector_speed_param);

static int __init check_unaligned_access_all_cpus(void)
{
	int cpu;

	if (unaligned_scalar_speed_param != RISCV_HWPROBE_MISALIGNED_SCALAR_UNKNOWN) {
		pr_info("scalar unaligned access speed set to '%s' (%lu) by command line\n",
			speed_str[unaligned_scalar_speed_param], unaligned_scalar_speed_param);
		for_each_online_cpu(cpu)
			per_cpu(misaligned_access_speed, cpu) = unaligned_scalar_speed_param;
	} else if (!check_unaligned_access_emulated_all_cpus()) {
		check_unaligned_access_speed_all_cpus();
	}

	if (unaligned_vector_speed_param != RISCV_HWPROBE_MISALIGNED_VECTOR_UNKNOWN) {
		if (!has_vector() &&
		    unaligned_vector_speed_param != RISCV_HWPROBE_MISALIGNED_VECTOR_UNSUPPORTED) {
			pr_warn("vector support is not available, ignoring unaligned_vector_speed=%s\n",
				speed_str[unaligned_vector_speed_param]);
		} else {
			pr_info("vector unaligned access speed set to '%s' (%lu) by command line\n",
				speed_str[unaligned_vector_speed_param], unaligned_vector_speed_param);
		}
	}

	if (!has_vector())
		unaligned_vector_speed_param = RISCV_HWPROBE_MISALIGNED_VECTOR_UNSUPPORTED;

	if (unaligned_vector_speed_param != RISCV_HWPROBE_MISALIGNED_VECTOR_UNKNOWN) {
		for_each_online_cpu(cpu)
			per_cpu(vector_misaligned_access, cpu) = unaligned_vector_speed_param;
	} else if (!check_vector_unaligned_access_emulated_all_cpus() &&
		   IS_ENABLED(CONFIG_RISCV_PROBE_VECTOR_UNALIGNED_ACCESS)) {
		kthread_run(vec_check_unaligned_access_speed_all_cpus,
			    NULL, "vec_check_unaligned_access_speed_all_cpus");
	}

	/*
	 * Setup hotplug callbacks for any new CPUs that come online or go
	 * offline.
	 */
	cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN, "riscv:online",
				  riscv_online_cpu, riscv_offline_cpu);
	cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN, "riscv:online",
				  riscv_online_cpu_vec, NULL);

	return 0;
}

arch_initcall(check_unaligned_access_all_cpus);
