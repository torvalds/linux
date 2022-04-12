// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 */
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <soc/rockchip/rockchip_performance.h>
#include <../../kernel/sched/sched.h>

static int perf_level = CONFIG_ROCKCHIP_PERFORMANCE_LEVEL;
static cpumask_var_t cpul_mask, cpub_mask;
static bool perf_init_done;
static DEFINE_MUTEX(update_mutex);

#ifdef CONFIG_UCLAMP_TASK
static inline void set_uclamp_util_min_rt(unsigned int util)
{
	sysctl_sched_uclamp_util_min_rt_default = util;
	static_branch_enable(&sched_uclamp_used);
	rockchip_perf_uclamp_sync_util_min_rt_default();
}
#else
static inline void set_uclamp_util_min_rt(unsigned int util) { };
#endif

static void update_perf_level_locked(int level)
{
	struct em_perf_domain *em;
	unsigned long target_cost, target_freq, max_freq;
	unsigned long scale_cpu0 = arch_scale_cpu_capacity(0);
	unsigned int uclamp_util_min_rt = scale_cpu0 * 2 / 3;
	int i;

	if (perf_init_done && perf_level == level)
		return;

	perf_level = level;

	if (level == 0) {
		set_uclamp_util_min_rt(0);
		return;
	}

	if ((level == 1) || (level == 2)) {
		set_uclamp_util_min_rt(SCHED_CAPACITY_SCALE);
		return;
	}

	/* find a better efficient frequency and consider performance */
	em = em_cpu_get(0);
	if (em) {
		target_cost = em->table[0].cost + (em->table[0].cost >> 2);

		for (i = 1; i < em->nr_perf_states; i++) {
			if (em->table[i].cost >= target_cost)
				break;
		}
		target_freq = em->table[i-1].frequency;
		max_freq = em->table[em->nr_perf_states-1].frequency;
		uclamp_util_min_rt = scale_cpu0 * target_freq / max_freq;
	}

	/* schedutil will reserve 20% util, and we need more 5% for debounce */
	uclamp_util_min_rt = uclamp_util_min_rt * 3 / 4;
	set_uclamp_util_min_rt(uclamp_util_min_rt);
}

static void update_perf_level(int level)
{
	mutex_lock(&update_mutex);
	update_perf_level_locked(level);
	mutex_unlock(&update_mutex);
}

static int param_set_level(const char *buf, const struct kernel_param *kp)
{
	int ret, level;

	ret = kstrtoint(buf, 10, &level);
	if (ret || (level < 0) || (level > 2))
		return -EINVAL;

	if (!perf_init_done)
		return 0;

	update_perf_level(level);

	return 0;
}

static const struct kernel_param_ops level_param_ops = {
	.set = param_set_level,
	.get = param_get_int,
};
module_param_cb(level, &level_param_ops, &perf_level, 0644);

static __init int rockchip_perf_init(void)
{
	int cpu;

	if (!zalloc_cpumask_var(&cpul_mask, GFP_KERNEL))
		return -ENOMEM;
	if (!zalloc_cpumask_var(&cpub_mask, GFP_KERNEL))
		return -ENOMEM;

	for_each_possible_cpu(cpu) {
		if (arch_scale_cpu_capacity(cpu) == SCHED_CAPACITY_SCALE)
			cpumask_set_cpu(cpu, cpub_mask);
		else
			cpumask_set_cpu(cpu, cpul_mask);
	}

	update_perf_level(perf_level);

	perf_init_done = true;

	return 0;
}
late_initcall_sync(rockchip_perf_init);

int rockchip_perf_get_level(void)
{
	return perf_level;
}

struct cpumask *rockchip_perf_get_cpul_mask(void)
{
	if (static_branch_unlikely(&sched_asym_cpucapacity))
		return cpul_mask;

	return NULL;
}

struct cpumask *rockchip_perf_get_cpub_mask(void)
{
	if (static_branch_unlikely(&sched_asym_cpucapacity))
		return cpub_mask;

	return NULL;
}

#ifdef CONFIG_SMP
int rockchip_perf_select_rt_cpu(int prev_cpu, struct cpumask *lowest_mask)
{
	struct cpumask target_mask;
	int cpu = nr_cpu_ids;

	if (!perf_init_done)
		return prev_cpu;

	if (static_branch_unlikely(&sched_asym_cpucapacity)) {
		if (perf_level == 0)
			cpumask_and(&target_mask, lowest_mask, cpul_mask);
		if (perf_level == 2)
			cpumask_and(&target_mask, lowest_mask, cpub_mask);

		if (cpumask_test_cpu(prev_cpu, &target_mask))
			return prev_cpu;

		cpu = cpumask_first(&target_mask);

		if (cpu < nr_cpu_ids)
			return cpu;
	}

	return prev_cpu;
}

bool rockchip_perf_misfit_rt(int cpu)
{
	if (!perf_init_done)
		return false;

	if (static_branch_unlikely(&sched_asym_cpucapacity)) {
		if ((perf_level == 0) && cpumask_test_cpu(cpu, cpub_mask))
			return true;
		if ((perf_level == 2) && cpumask_test_cpu(cpu, cpul_mask))
			return true;
	}

	return false;
}
#endif /* CONFIG_SMP */
