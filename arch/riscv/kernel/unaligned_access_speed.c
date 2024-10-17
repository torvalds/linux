// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Rivos Inc.
 */

#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/jump_label.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/types.h>
#include <asm/cpufeature.h>
#include <asm/hwprobe.h>

#include "copy-unaligned.h"

#define MISALIGNED_ACCESS_JIFFIES_LG2 1
#define MISALIGNED_BUFFER_SIZE 0x4000
#define MISALIGNED_BUFFER_ORDER get_order(MISALIGNED_BUFFER_SIZE)
#define MISALIGNED_COPY_SIZE ((MISALIGNED_BUFFER_SIZE / 2) - 0x80)

DEFINE_PER_CPU(long, misaligned_access_speed);

#ifdef CONFIG_RISCV_PROBE_UNALIGNED_ACCESS
static cpumask_t fast_misaligned_access;
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

static void check_unaligned_access_nonboot_cpu(void *param)
{
	unsigned int cpu = smp_processor_id();
	struct page **pages = param;

	if (smp_processor_id() != 0)
		check_unaligned_access(pages[cpu]);
}

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

static int lock_and_set_unaligned_access_static_branch(void)
{
	cpus_read_lock();
	set_unaligned_access_static_branches();
	cpus_read_unlock();

	return 0;
}

arch_initcall_sync(lock_and_set_unaligned_access_static_branch);

static int riscv_online_cpu(unsigned int cpu)
{
	static struct page *buf;

	/* We are already set since the last check */
	if (per_cpu(misaligned_access_speed, cpu) != RISCV_HWPROBE_MISALIGNED_SCALAR_UNKNOWN)
		goto exit;

	check_unaligned_access_emulated(NULL);
	buf = alloc_pages(GFP_KERNEL, MISALIGNED_BUFFER_ORDER);
	if (!buf) {
		pr_warn("Allocation failure, not measuring misaligned performance\n");
		return -ENOMEM;
	}

	check_unaligned_access(buf);
	__free_pages(buf, MISALIGNED_BUFFER_ORDER);

exit:
	set_unaligned_access_static_branches();

	return 0;
}

static int riscv_offline_cpu(unsigned int cpu)
{
	set_unaligned_access_static_branches_except_cpu(cpu);

	return 0;
}

/* Measure unaligned access speed on all CPUs present at boot in parallel. */
static int check_unaligned_access_speed_all_cpus(void)
{
	unsigned int cpu;
	unsigned int cpu_count = num_possible_cpus();
	struct page **bufs = kcalloc(cpu_count, sizeof(*bufs), GFP_KERNEL);

	if (!bufs) {
		pr_warn("Allocation failure, not measuring misaligned performance\n");
		return 0;
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

	/*
	 * Setup hotplug callbacks for any new CPUs that come online or go
	 * offline.
	 */
	cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN, "riscv:online",
				  riscv_online_cpu, riscv_offline_cpu);

out:
	for_each_cpu(cpu, cpu_online_mask) {
		if (bufs[cpu])
			__free_pages(bufs[cpu], MISALIGNED_BUFFER_ORDER);
	}

	kfree(bufs);
	return 0;
}

static int check_unaligned_access_all_cpus(void)
{
	bool all_cpus_emulated = check_unaligned_access_emulated_all_cpus();

	if (!all_cpus_emulated)
		return check_unaligned_access_speed_all_cpus();

	return 0;
}
#else /* CONFIG_RISCV_PROBE_UNALIGNED_ACCESS */
static int check_unaligned_access_all_cpus(void)
{
	check_unaligned_access_emulated_all_cpus();

	return 0;
}
#endif

arch_initcall(check_unaligned_access_all_cpus);
