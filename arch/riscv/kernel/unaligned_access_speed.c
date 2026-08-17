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

#define MISALIGNED_ACCESS_NS 8000000
#define MISALIGNED_BUFFER_SIZE 0x4000
#define MISALIGNED_BUFFER_ORDER get_order(MISALIGNED_BUFFER_SIZE)
#define MISALIGNED_COPY_SIZE ((MISALIGNED_BUFFER_SIZE / 2) - 0x80)

DEFINE_PER_CPU(long, misaligned_access_speed) = RISCV_HWPROBE_MISALIGNED_SCALAR_UNKNOWN;
DEFINE_PER_CPU(long, vector_misaligned_access) = RISCV_HWPROBE_MISALIGNED_VECTOR_UNSUPPORTED;

static long unaligned_scalar_speed_param = RISCV_HWPROBE_MISALIGNED_SCALAR_UNKNOWN;
static long unaligned_vector_speed_param = RISCV_HWPROBE_MISALIGNED_VECTOR_UNKNOWN;

static u64 __maybe_unused
measure_cycles(void (*func)(void *dst, const void *src, size_t len),
	       void *dst, void *src, size_t len)
{
	u64 start_cycles, end_cycles, cycles = -1ULL;
	u64 start_ns;

	/* Do a warmup. */
	func(dst, src, len);

	preempt_disable();

	/*
	 * For a fixed amount of time, repeatedly try the function, and take
	 * the best time in cycles as the measurement.
	 */
	start_ns = ktime_get_mono_fast_ns();
	while (ktime_get_mono_fast_ns() < start_ns + MISALIGNED_ACCESS_NS) {
		start_cycles = get_cycles64();
		/* Ensure the CSR read can't reorder WRT to the copy. */
		mb();
		func(dst, src, len);
		/* Ensure the copy ends before the end time is snapped. */
		mb();
		end_cycles = get_cycles64();
		if ((end_cycles - start_cycles) < cycles)
			cycles = end_cycles - start_cycles;
	}

	preempt_enable();

	return cycles;
}

/*
 * Return:
 *     1 if unaligned accesses are fast
 *     0 if unaligned accesses are slow
 *    -1 if check cannot be done
 */
static int __maybe_unused
compare_unaligned_access(void (*word_copy)(void *dst, const void *src, size_t len),
			 void (*byte_copy)(void *dst, const void *src, size_t len),
			 void *buf, const char *type)
{
	int cpu = smp_processor_id();
	u64 word_cycles;
	u64 byte_cycles;
	void *dst, *src;
	bool fast;
	int ratio;

	/* Make an unaligned destination buffer. */
	dst = (void *)((unsigned long)buf | 0x1);
	/* Unalign src as well, but differently (off by 1 + 2 = 3). */
	src = dst + (MISALIGNED_BUFFER_SIZE / 2);
	src += 2;

	word_cycles = measure_cycles(word_copy, dst, src, MISALIGNED_COPY_SIZE);
	byte_cycles = measure_cycles(byte_copy, dst, src, MISALIGNED_COPY_SIZE);

	/* Don't divide by zero. */
	if (!word_cycles || !byte_cycles) {
		pr_warn("cpu%d: rdtime lacks granularity needed to measure %s unaligned access speed\n",
			cpu, type);

		return -1;
	}

	fast = word_cycles < byte_cycles;

	ratio = div_u64((byte_cycles * 100), word_cycles);
	pr_info("cpu%d: %s unaligned word access speed is %d.%02dx byte access speed (%s)\n",
		cpu,
		type,
		ratio / 100,
		ratio % 100,
		fast ? "fast" : "slow");

	return fast;
}

#ifdef CONFIG_RISCV_PROBE_UNALIGNED_ACCESS
static int check_unaligned_access(struct page *page)
{
	void *buf = page_address(page);
	int cpu = smp_processor_id();
	int ret;

	if (per_cpu(misaligned_access_speed, cpu) != RISCV_HWPROBE_MISALIGNED_SCALAR_UNKNOWN)
		return 0;

	ret = compare_unaligned_access(__riscv_copy_words_unaligned,
				       __riscv_copy_bytes_unaligned,
				       buf, "scalar");
	if (ret < 0)
		return 0;

	/*
	 * Set the value of fast_misaligned_access of a CPU. These operations
	 * are atomic to avoid race conditions.
	 */
	if (ret)
		per_cpu(misaligned_access_speed, cpu) = RISCV_HWPROBE_MISALIGNED_SCALAR_FAST;
	else
		per_cpu(misaligned_access_speed, cpu) = RISCV_HWPROBE_MISALIGNED_SCALAR_SLOW;

	return 0;
}

static void __init _check_unaligned_access(void *param)
{
	unsigned int cpu = smp_processor_id();
	struct page **pages = param;

	check_unaligned_access(pages[cpu]);
}

/* Measure unaligned access speed on all CPUs present at boot in parallel. */
static void __init check_unaligned_access_speed_all_cpus(void)
{
	unsigned int cpu;
	unsigned int cpu_count = num_possible_cpus();
	struct page **bufs = kzalloc_objs(*bufs, cpu_count);

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

	on_each_cpu(_check_unaligned_access, bufs, 1);

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

static void modify_unaligned_access_branches(const cpumask_t *mask)
{
	bool fast = true;
	int cpu;

	for_each_cpu(cpu, mask) {
		if (per_cpu(misaligned_access_speed, cpu) != RISCV_HWPROBE_MISALIGNED_SCALAR_FAST) {
			fast = false;
			break;
		}
	}

	if (fast)
		static_branch_enable_cpuslocked(&fast_unaligned_access_speed_key);
	else
		static_branch_disable_cpuslocked(&fast_unaligned_access_speed_key);
}

static int riscv_online_cpu(unsigned int cpu)
{
	int ret = cpu_online_unaligned_access_init(cpu);

	if (ret)
		return ret;

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
	modify_unaligned_access_branches(cpu_online_mask);

	return 0;
}

static int riscv_offline_cpu(unsigned int cpu)
{
	cpumask_t mask;

	cpumask_copy(&mask, cpu_online_mask);
	cpumask_clear_cpu(cpu, &mask);

	modify_unaligned_access_branches(&mask);

	return 0;
}

#ifdef CONFIG_RISCV_PROBE_VECTOR_UNALIGNED_ACCESS
static void check_vector_unaligned_access(struct work_struct *work __always_unused)
{
	int cpu = smp_processor_id();
	struct page *page;
	int ret;

	if (per_cpu(vector_misaligned_access, cpu) != RISCV_HWPROBE_MISALIGNED_VECTOR_UNKNOWN)
		return;

	page = alloc_pages(GFP_KERNEL, MISALIGNED_BUFFER_ORDER);
	if (!page) {
		pr_warn("Allocation failure, not measuring vector misaligned performance\n");
		return;
	}

	kernel_vector_begin();

	ret = compare_unaligned_access(__riscv_copy_vec_words_unaligned,
				       __riscv_copy_vec_bytes_unaligned,
				       page_address(page), "vector");
	kernel_vector_end();

	if (ret < 0)
		goto free;

	if (ret)
		per_cpu(vector_misaligned_access, cpu) = RISCV_HWPROBE_MISALIGNED_VECTOR_FAST;
	else
		per_cpu(vector_misaligned_access, cpu) = RISCV_HWPROBE_MISALIGNED_VECTOR_SLOW;

free:
	__free_pages(page, MISALIGNED_BUFFER_ORDER);
}

/* Measure unaligned access speed on all CPUs present at boot in parallel. */
static int __init vec_check_unaligned_access_speed_all_cpus(void *unused __always_unused)
{
	schedule_on_each_cpu(check_vector_unaligned_access);
	riscv_hwprobe_complete_async_probe();

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

	unaligned_access_init();

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
		riscv_hwprobe_register_async_probe();
		if (IS_ERR(kthread_run(vec_check_unaligned_access_speed_all_cpus,
				       NULL, "vec_check_unaligned_access_speed_all_cpus"))) {
			pr_warn("Failed to create vec_unalign_check kthread\n");
			riscv_hwprobe_complete_async_probe();
		}
	}

	/*
	 * Setup hotplug callbacks for any new CPUs that come online or go
	 * offline.
	 */
	cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN, "riscv:online",
				  riscv_online_cpu, riscv_offline_cpu);
	cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN, "riscv:online",
				  riscv_online_cpu_vec, NULL);

	cpus_read_lock();
	modify_unaligned_access_branches(cpu_online_mask);
	cpus_read_unlock();

	return 0;
}

late_initcall(check_unaligned_access_all_cpus);
