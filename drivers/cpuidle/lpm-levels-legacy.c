// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/cpu.h>
#include <linux/of.h>
#include <linux/psci.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/tick.h>
#include <linux/suspend.h>
#include <linux/pm_qos.h>
#include <linux/of_platform.h>
#include <linux/smp.h>
#include <linux/dma-mapping.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/cpu_pm.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <soc/qcom/spm.h>
#include <soc/qcom/pm-legacy.h>
#include <soc/qcom/lpm-stats.h>
#include <soc/qcom/lpm_levels.h>
#include <asm/cputype.h>
#include <asm/arch_timer.h>
#include <linux/cacheflush.h>
#include <asm/suspend.h>
#include "lpm-levels-legacy.h"
#include <trace/events/power.h>
#define CREATE_TRACE_POINTS
#include <trace/events/trace_msm_low_power.h>
#if defined(CONFIG_COMMON_CLK)
#include "../clk/clk.h"
#elif defined(CONFIG_COMMON_CLK_MSM)
#include "../../drivers/clk/msm/clock.h"
#endif /* CONFIG_COMMON_CLK */
#include <soc/qcom/minidump.h>
#include <soc/qcom/smp.h>

#define SCLK_HZ (32768)
#define PSCI_POWER_STATE(reset) (reset << 30)
#define PSCI_AFFINITY_LEVEL(lvl) ((lvl & 0x3) << 24)
#define MUTEX_NUM_PID 128
#define MUTEX_TID_START MUTEX_NUM_PID
#define SCM_HANDOFF_LOCK_ID 7

#ifndef PM_QOS_CPU_DMA_LAT_DEFAULT_VALUE
#define PM_QOS_CPU_DMA_LAT_DEFAULT_VALUE (2000 * USEC_PER_SEC)
#endif

#if defined(CONFIG_SCHED_WALT) && NR_CPUS > 1
#define num_isolated_cpus()     cpumask_weight(cpu_isolated_mask)
#define num_online_uniso_cpus()					\
({									\
	cpumask_t mask;							\
									\
	cpumask_andnot(&mask, cpu_online_mask, cpu_isolated_mask);	\
	cpumask_weight(&mask);						\
})
#define cpu_isolated(cpu)       cpumask_test_cpu((cpu), cpu_isolated_mask)
#else /* !CONFIG_SCHED_WALT || NR_CPUS == 1 */
#define num_isolated_cpus()     0U
#define num_online_uniso_cpus() num_online_cpus()
#define cpu_isolated(cpu)       0U
#endif

/* sfpb implementation for hardware spinlock usage */
static phys_addr_t reg_base;
static uint32_t reg_size;
static uint32_t lock_size;

static void __iomem *hw_mutex_reg_base;

struct mutex_reg {
	uint32_t regaddr;
};

enum {
	MSM_LPM_LVL_DBG_SUSPEND_LIMITS = BIT(0),
	MSM_LPM_LVL_DBG_IDLE_LIMITS = BIT(1),
};

enum debug_event {
	CPU_ENTER,
	CPU_EXIT,
	CLUSTER_ENTER,
	CLUSTER_EXIT,
	PRE_PC_CB,
	CPU_HP_STARTING,
	CPU_HP_DYING,
};

struct lpm_debug {
	u64 time;
	enum debug_event evt;
	int cpu;
	uint32_t arg1;
	uint32_t arg2;
	uint32_t arg3;
	uint32_t arg4;
};

static struct system_pm_ops *sys_pm_ops;
struct lpm_cluster *lpm_root_node;

static DEFINE_PER_CPU(ktime_t, next_hrtimer);
static DEFINE_PER_CPU(struct lpm_cluster*, cpu_cluster);
static bool suspend_in_progress;
static struct hrtimer lpm_hrtimer;
static struct lpm_debug *lpm_debug;
static phys_addr_t lpm_debug_phys;

static const int num_dbg_elements = 0x100;

static void cluster_unprepare(struct lpm_cluster *cluster,
		const struct cpumask *cpu, int child_idx, bool from_idle,
		int64_t time, bool success);
static void cluster_prepare(struct lpm_cluster *cluster,
		const struct cpumask *cpu, int child_idx, bool from_idle,
		int64_t time);

static bool menu_select;
module_param_named(
	menu_select, menu_select, bool, 0664
);

static bool print_parsed_dt;
module_param_named(
	print_parsed_dt, print_parsed_dt, bool, 0664
);

static bool sleep_disabled;
module_param_named(sleep_disabled,
	sleep_disabled, bool, 0664);

s32 msm_cpuidle_get_deep_idle_latency(void)
{
	return 10;
}
EXPORT_SYMBOL_GPL(msm_cpuidle_get_deep_idle_latency);

uint32_t register_system_pm_ops(struct system_pm_ops *pm_ops)
{
	if (sys_pm_ops)
		return -EUSERS;

	sys_pm_ops = pm_ops;

	return 0;
}

#ifdef CONFIG_SCHED_WALT
static bool check_cpu_isolated(int cpu)
{
	return cpu_isolated(cpu);
}
#else
static bool check_cpu_isolated(int cpu)
{
	return false;
}
#endif

static uint32_t least_cluster_latency(struct lpm_cluster *cluster,
					struct latency_level *lat_level)
{
	struct list_head *list;
	struct lpm_cluster_level *level;
	struct lpm_cluster *n;
	struct power_params *pwr_params;
	uint32_t latency = 0;
	int i;

	if (!cluster->list.next) {
		for (i = 0; i < cluster->nlevels; i++) {
			level = &cluster->levels[i];
			pwr_params = &level->pwr;
			if (lat_level->reset_level == level->reset_level) {
				if ((latency > pwr_params->latency_us)
						|| (!latency))
					latency = pwr_params->latency_us;
				break;
			}
		}
	} else {
		list_for_each(list, &cluster->parent->child) {
			n = list_entry(list, typeof(*n), list);
			if (lat_level->level_name) {
				if (strcmp(lat_level->level_name,
						 n->cluster_name))
					continue;
			}
			for (i = 0; i < n->nlevels; i++) {
				level = &n->levels[i];
				pwr_params = &level->pwr;
				if (lat_level->reset_level ==
						level->reset_level) {
					if ((latency > pwr_params->latency_us)
								|| (!latency))
						latency =
						pwr_params->latency_us;
					break;
				}
			}
		}
	}
	return latency;
}

static uint32_t least_cpu_latency(struct list_head *child,
				struct latency_level *lat_level)
{
	struct list_head *list;
	struct lpm_cpu_level *level;
	struct power_params *pwr_params;
	struct lpm_cpu *cpu;
	struct lpm_cluster *n;
	uint32_t latency = 0;
	int i;

	list_for_each(list, child) {
		n = list_entry(list, typeof(*n), list);
		if (lat_level->level_name) {
			if (strcmp(lat_level->level_name, n->cluster_name))
				continue;
		}
		cpu = n->cpu;
		for (i = 0; i < cpu->nlevels; i++) {
			level = &cpu->levels[i];
			pwr_params = &level->pwr;
			if (lat_level->reset_level == level->reset_level) {
				if ((latency > pwr_params->latency_us)
							|| (!latency))
					latency = pwr_params->latency_us;
				break;
			}
		}
	}
	return latency;
}

static struct lpm_cluster *cluster_aff_match(struct lpm_cluster *cluster,
							int affinity_level)
{
	struct lpm_cluster *n;

	if ((cluster->aff_level == affinity_level)
		|| ((cluster->cpu) && (affinity_level == 0)))
		return cluster;
	else if (!cluster->cpu) {
		n =  list_entry(cluster->child.next, typeof(*n), list);
		return cluster_aff_match(n, affinity_level);
	} else
		return NULL;
}

int lpm_get_latency(struct latency_level *level, uint32_t *latency)
{
	struct lpm_cluster *cluster;
	uint32_t val;

	if (!lpm_root_node) {
		pr_err("%s: lpm_probe not completed\n", __func__);
		return -EAGAIN;
	}

	if ((level->affinity_level < 0)
		|| (level->affinity_level > lpm_root_node->aff_level)
		|| (level->reset_level < LPM_RESET_LVL_RET)
		|| (level->reset_level > LPM_RESET_LVL_PC)
		|| !latency)
		return -EINVAL;

	cluster = cluster_aff_match(lpm_root_node, level->affinity_level);
	if (!cluster) {
		pr_err("%s:No matching cluster found for affinity_level:%d\n",
					__func__, level->affinity_level);
		return -EINVAL;
	}

	if (level->affinity_level == 0)
		val = least_cpu_latency(&cluster->parent->child, level);
	else
		val = least_cluster_latency(cluster, level);

	if (!val) {
		pr_err("%s:No mode with affinity_level:%d reset_level:%d\n",
			__func__, level->affinity_level, level->reset_level);
		return -EINVAL;
	}

	*latency = val;

	return 0;
}
EXPORT_SYMBOL_GPL(lpm_get_latency);

static void update_debug_pc_event(enum debug_event event, uint32_t arg1,
		uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
	struct lpm_debug *dbg;
	int idx;
	static DEFINE_SPINLOCK(debug_lock);
	static int pc_event_index;

	if (!lpm_debug)
		return;

	spin_lock(&debug_lock);
	idx = pc_event_index++;
	dbg = &lpm_debug[idx & (num_dbg_elements - 1)];

	dbg->evt = event;
	dbg->time = __arch_counter_get_cntpct();
	dbg->cpu = raw_smp_processor_id();
	dbg->arg1 = arg1;
	dbg->arg2 = arg2;
	dbg->arg3 = arg3;
	dbg->arg4 = arg4;
	spin_unlock(&debug_lock);
}

int set_l2_mode(struct low_power_ops *ops, int mode,
				struct lpm_cluster_level *level)
{
	int lpm = mode;
	int rc = 0;
	bool notify_rpm = level->notify_rpm;
	struct low_power_ops *cpu_ops = per_cpu(cpu_cluster,
			smp_processor_id())->lpm_dev;

	switch (mode) {
	case MSM_SPM_MODE_STANDALONE_POWER_COLLAPSE:
	case MSM_SPM_MODE_POWER_COLLAPSE:
	case MSM_SPM_MODE_FASTPC:
		if (level->no_cache_flush)
			cpu_ops->tz_flag = MSM_SCM_L2_GDHS;
		else
			cpu_ops->tz_flag = MSM_SCM_L2_OFF;
		break;
	case MSM_SPM_MODE_GDHS:
		cpu_ops->tz_flag = MSM_SCM_L2_GDHS;
		break;
	case MSM_SPM_MODE_CLOCK_GATING:
	case MSM_SPM_MODE_RETENTION:
	case MSM_SPM_MODE_DISABLED:
		cpu_ops->tz_flag = MSM_SCM_L2_ON;
		break;
	default:
		cpu_ops->tz_flag = MSM_SCM_L2_ON;
		lpm = MSM_SPM_MODE_DISABLED;
		break;
	}

	rc = msm_spm_config_low_power_mode(ops->spm, lpm, notify_rpm);

	if (rc)
		pr_err("%s: Failed to set L2 low power mode %d, ERR %d\n",
				__func__, lpm, rc);

	return rc;
}

int set_l3_mode(struct low_power_ops *ops, int mode,
				struct lpm_cluster_level *level)
{
	bool notify_rpm = level->notify_rpm;
	struct low_power_ops *cpu_ops = per_cpu(cpu_cluster,
			smp_processor_id())->lpm_dev;

	switch (mode) {
	case MSM_SPM_MODE_STANDALONE_POWER_COLLAPSE:
	case MSM_SPM_MODE_POWER_COLLAPSE:
	case MSM_SPM_MODE_FASTPC:
		cpu_ops->tz_flag |= MSM_SCM_L3_PC_OFF;
		break;
	default:
		break;
	}
	return msm_spm_config_low_power_mode(ops->spm, mode, notify_rpm);
}


int set_system_mode(struct low_power_ops *ops, int mode,
				struct lpm_cluster_level *level)
{
	bool notify_rpm = level->notify_rpm;

	return msm_spm_config_low_power_mode(ops->spm, mode, notify_rpm);
}

static int set_device_mode(struct lpm_cluster *cluster, int ndevice,
		struct lpm_cluster_level *level)
{
	struct low_power_ops *ops;

	if (use_psci)
		return 0;

	ops = &cluster->lpm_dev[ndevice];
	if (ops && ops->set_mode)
		return ops->set_mode(ops, level->mode[ndevice],
				level);
	else
		return -EINVAL;
}

static inline uint32_t get_cpus_qos(const struct cpumask *mask)
{
	int cpu;
	uint32_t n;
	uint32_t latency = PM_QOS_CPU_DMA_LAT_DEFAULT_VALUE;

	for_each_cpu(cpu, mask) {
		if (check_cpu_isolated(cpu))
			continue;
		n = cpuidle_governor_latency_req(cpu);
		if (n < latency)
			latency = n;
	}

	return latency;
}

static int cpu_power_select(struct cpuidle_device *dev,
		struct lpm_cpu *cpu, bool *stop_tick)
{
	int best_level = 0;
	uint32_t latency_us = get_cpus_qos(cpumask_of(dev->cpu));
	ktime_t delta_next;
	s64 sleep_us = ktime_to_us(tick_nohz_get_sleep_length(&delta_next));
	uint32_t next_event_us = 0;
	int i;
	uint32_t lvl_latency_us = 0;
	uint32_t *residency = get_per_cpu_max_residency(dev->cpu);

	if (!cpu)
		return best_level;

	if ((sleep_disabled && !cpu_isolated(dev->cpu)) || sleep_us  < 0)
		return 0;

	if (((sleep_us * NSEC_PER_USEC) < TICK_NSEC) && !tick_nohz_tick_stopped())
		*stop_tick = false;

	for (i = 0; i < cpu->nlevels; i++) {
		struct lpm_cpu_level *level = &cpu->levels[i];
		struct power_params *pwr_params = &level->pwr;
		uint32_t next_wakeup_us = (uint32_t)sleep_us;
		bool allow;

		allow = lpm_cpu_mode_allow(dev->cpu, i, true);

		if (!allow)
			continue;

		lvl_latency_us = pwr_params->latency_us;

		if (latency_us < lvl_latency_us)
			break;

		best_level = i;

		if (next_wakeup_us <= residency[i])
			break;
	}

	trace_cpu_power_select(best_level, sleep_us, latency_us, next_event_us);

	return best_level;
}

static uint64_t get_cluster_sleep_time(struct lpm_cluster *cluster,
		struct cpumask *mask, bool from_idle)
{
	int cpu;
	int next_cpu = raw_smp_processor_id();
	ktime_t next_event;
	struct cpumask online_cpus_in_cluster;

	next_event = KTIME_MAX;
	if (!from_idle) {
		if (mask)
			cpumask_copy(mask, cpumask_of(raw_smp_processor_id()));
		return ~0ULL;
	}

	cpumask_and(&online_cpus_in_cluster,
			&cluster->num_children_in_sync, cpu_online_mask);

	for_each_cpu(cpu, &online_cpus_in_cluster) {
		ktime_t next_event_c = per_cpu(next_hrtimer, next_cpu);

		if (next_event_c < next_event) {
			next_event = next_event_c;
			next_cpu = cpu;
		}
	}

	if (mask)
		cpumask_copy(mask, cpumask_of(next_cpu));


	if (ktime_to_us(next_event) > ktime_to_us(ktime_get()))
		return ktime_to_us(ktime_sub(next_event, ktime_get()));
	else
		return 0;
}

static int cluster_select(struct lpm_cluster *cluster, bool from_idle)
{
	int best_level = -1;
	int i;
	struct cpumask mask;
	uint32_t latency_us = ~0U;
	uint32_t sleep_us;

	if (!cluster)
		return -EINVAL;

	sleep_us = (uint32_t)get_cluster_sleep_time(cluster, NULL, from_idle);

	if (cpumask_and(&mask, cpu_online_mask, &cluster->child_cpus))
		latency_us = get_cpus_qos(&mask);

	/*
	 * If at least one of the core in the cluster is online, the cluster
	 * low power modes should be determined by the idle characteristics
	 * even if the last core enters the low power mode as a part of
	 * hotplug.
	 */

	if (!from_idle && num_online_cpus() > 1 &&
		cpumask_intersects(&cluster->child_cpus, cpu_online_mask))
		from_idle = true;

	for (i = 0; i < cluster->nlevels; i++) {
		struct lpm_cluster_level *level = &cluster->levels[i];
		struct power_params *pwr_params = &level->pwr;

		if (!lpm_cluster_mode_allow(cluster, i, from_idle))
			continue;

		if (level->last_core_only &&
			cpumask_weight(cpu_online_mask) > 1)
			continue;

		if (!cpumask_equal(&cluster->num_children_in_sync,
					&level->num_cpu_votes))
			continue;

		if (from_idle && latency_us < pwr_params->latency_us)
			break;

		if (sleep_us < pwr_params->time_overhead_us)
			break;

		if (suspend_in_progress && from_idle && level->notify_rpm)
			continue;

		if (level->notify_rpm) {
			if (!(sys_pm_ops && sys_pm_ops->sleep_allowed))
				continue;
			if (!sys_pm_ops->sleep_allowed())
				continue;
		}

		best_level = i;

		if (from_idle && sleep_us <= pwr_params->max_residency)
			break;
	}

	return best_level;
}

static unsigned int get_next_online_cpu(bool from_idle)
{
	unsigned int cpu;
	ktime_t next_event;
	unsigned int next_cpu = raw_smp_processor_id();

	if (!from_idle)
		return next_cpu;
	next_event = KTIME_MAX;
	for_each_online_cpu(cpu) {
		ktime_t next_event_c = per_cpu(next_hrtimer, cpu);

		if (next_event_c < next_event) {
			next_event = next_event_c;
			next_cpu = cpu;
		}
	}
	return next_cpu;
}

static int cluster_configure(struct lpm_cluster *cluster, int idx,
		bool from_idle)
{
	struct lpm_cluster_level *level = &cluster->levels[idx];
	struct cpumask cpumask;
	unsigned int cpu;
	int ret, i;

	if (!cpumask_equal(&cluster->num_children_in_sync, &cluster->child_cpus)
			|| is_IPI_pending(&cluster->num_children_in_sync)) {
		return -EPERM;
	}

	if (idx != cluster->default_level) {
		update_debug_pc_event(CLUSTER_ENTER, idx,
			cluster->num_children_in_sync.bits[0],
			cluster->child_cpus.bits[0], from_idle);
		trace_cluster_enter(cluster->cluster_name, idx,
			cluster->num_children_in_sync.bits[0],
			cluster->child_cpus.bits[0], from_idle);
		lpm_stats_cluster_enter(cluster->stats, idx);
	}

	for (i = 0; i < cluster->ndevices; i++) {
		ret = set_device_mode(cluster, i, level);
		if (ret)
			goto failed_set_mode;
	}

	if (level->notify_rpm) {
		struct cpumask *nextcpu;

		cpu = get_next_online_cpu(from_idle);
		cpumask_copy(&cpumask, cpumask_of(cpu));
		nextcpu = level->disable_dynamic_routing ? NULL : &cpumask;

		if (sys_pm_ops && sys_pm_ops->enter) {
			ret = sys_pm_ops->enter(nextcpu);
			if (ret)
				goto failed_set_mode;
		}

		if (cluster->no_saw_devices && !use_psci)
			msm_spm_set_rpm_hs(true);
	}

	/* Notify cluster enter event after successfully config completion */
	cluster->last_level = idx;
	return 0;

failed_set_mode:

	for (i = 0; i < cluster->ndevices; i++) {
		int rc = 0;

		level = &cluster->levels[cluster->default_level];
		rc = set_device_mode(cluster, i, level);
		WARN_ON(rc);
	}
	return ret;
}

static void cluster_prepare(struct lpm_cluster *cluster,
		const struct cpumask *cpu, int child_idx, bool from_idle,
		int64_t start_time)
{
	int i;

	if (!cluster)
		return;

	if (cluster->min_child_level > child_idx)
		return;

	spin_lock(&cluster->sync_lock);
	cpumask_or(&cluster->num_children_in_sync, cpu,
			&cluster->num_children_in_sync);

	for (i = 0; i < cluster->nlevels; i++) {
		struct lpm_cluster_level *lvl = &cluster->levels[i];

		if (child_idx >= lvl->min_child_level)
			cpumask_or(&lvl->num_cpu_votes, cpu,
					&lvl->num_cpu_votes);
	}

	/*
	 * cluster_select() does not make any configuration changes. So its ok
	 * to release the lock here. If a core wakes up for a rude request,
	 * it need not wait for another to finish its cluster selection and
	 * configuration process
	 */

	if (!cpumask_equal(&cluster->num_children_in_sync,
				&cluster->child_cpus))
		goto failed;

	i = cluster_select(cluster, from_idle);

	if (i < 0)
		goto failed;

	if (cluster_configure(cluster, i, from_idle))
		goto failed;

	cluster->stats->sleep_time = start_time;
	cluster_prepare(cluster->parent, &cluster->num_children_in_sync, i,
			from_idle, start_time);

	spin_unlock(&cluster->sync_lock);

	if (!use_psci) {
		struct lpm_cluster_level *level = &cluster->levels[i];

		if (level->notify_rpm)
			if (sys_pm_ops && sys_pm_ops->update_wakeup)
				sys_pm_ops->update_wakeup(from_idle);
	}

	return;
failed:
	spin_unlock(&cluster->sync_lock);
	cluster->stats->sleep_time = 0;
}

static void cluster_unprepare(struct lpm_cluster *cluster,
		const struct cpumask *cpu, int child_idx, bool from_idle,
		int64_t end_time, bool success)
{
	struct lpm_cluster_level *level;
	bool first_cpu;
	int last_level, i, ret;

	if (!cluster)
		return;

	if (cluster->min_child_level > child_idx)
		return;

	spin_lock(&cluster->sync_lock);
	last_level = cluster->default_level;
	first_cpu = cpumask_equal(&cluster->num_children_in_sync,
				&cluster->child_cpus);
	cpumask_andnot(&cluster->num_children_in_sync,
			&cluster->num_children_in_sync, cpu);

	for (i = 0; i < cluster->nlevels; i++) {
		struct lpm_cluster_level *lvl = &cluster->levels[i];

		if (child_idx >= lvl->min_child_level)
			cpumask_andnot(&lvl->num_cpu_votes,
					&lvl->num_cpu_votes, cpu);
	}

	if (!first_cpu || cluster->last_level == cluster->default_level)
		goto unlock_return;

	if (cluster->stats->sleep_time)
		cluster->stats->sleep_time = end_time -
			cluster->stats->sleep_time;
	lpm_stats_cluster_exit(cluster->stats, cluster->last_level, success);

	level = &cluster->levels[cluster->last_level];
	if (level->notify_rpm) {
		if (sys_pm_ops && sys_pm_ops->exit)
			sys_pm_ops->exit(success);

		if (cluster->no_saw_devices && !use_psci)
			msm_spm_set_rpm_hs(false);

	}

	update_debug_pc_event(CLUSTER_EXIT, cluster->last_level,
			cluster->num_children_in_sync.bits[0],
			cluster->child_cpus.bits[0], from_idle);
	trace_cluster_exit(cluster->cluster_name, cluster->last_level,
			cluster->num_children_in_sync.bits[0],
			cluster->child_cpus.bits[0], from_idle);

	last_level = cluster->last_level;
	cluster->last_level = cluster->default_level;

	for (i = 0; i < cluster->ndevices; i++) {
		level = &cluster->levels[cluster->default_level];
		ret = set_device_mode(cluster, i, level);

		WARN_ON(ret);

	}

	cluster_unprepare(cluster->parent, &cluster->child_cpus,
			last_level, from_idle, end_time, success);
unlock_return:
	spin_unlock(&cluster->sync_lock);
}

static inline void cpu_prepare(struct lpm_cluster *cluster, int cpu_index,
				bool from_idle)
{
	struct lpm_cpu_level *cpu_level = &cluster->cpu->levels[cpu_index];

	/* Use broadcast timer for aggregating sleep mode within a cluster.
	 * A broadcast timer could be used in the following scenarios
	 * 1) The architected timer HW gets reset during certain low power
	 * modes and the core relies on a external(broadcast) timer to wake up
	 * from sleep. This information is passed through device tree.
	 * 2) The CPU low power mode could trigger a system low power mode.
	 * The low power module relies on Broadcast timer to aggregate the
	 * next wakeup within a cluster, in which case, CPU switches over to
	 * use broadcast timer.
	 */
	if (from_idle && (cpu_level->use_bc_timer ||
			(cpu_index >= cluster->min_child_level)))
		tick_broadcast_enter();

	if (from_idle && ((cpu_level->mode == MSM_PM_SLEEP_MODE_POWER_COLLAPSE)
		|| (cpu_level->mode ==
			MSM_PM_SLEEP_MODE_POWER_COLLAPSE_STANDALONE)
			|| (cpu_level->is_reset)))
		cpu_pm_enter();

}

static inline void cpu_unprepare(struct lpm_cluster *cluster, int cpu_index,
				bool from_idle)
{
	struct lpm_cpu_level *cpu_level = &cluster->cpu->levels[cpu_index];

	if (from_idle && (cpu_level->use_bc_timer ||
			(cpu_index >= cluster->min_child_level)))
		tick_broadcast_exit();

	if (from_idle && ((cpu_level->mode == MSM_PM_SLEEP_MODE_POWER_COLLAPSE)
		|| (cpu_level->mode ==
			MSM_PM_SLEEP_MODE_POWER_COLLAPSE_STANDALONE)
		|| cpu_level->is_reset))
		cpu_pm_exit();
}

#if defined(CONFIG_ARM_PSCI) || !defined(CONFIG_CPU_V7)
static int get_cluster_id(struct lpm_cluster *cluster, int *aff_lvl)
{
	int state_id = 0;

	if (!cluster)
		return 0;

	spin_lock(&cluster->sync_lock);

	if (!cpumask_equal(&cluster->num_children_in_sync,
			   &cluster->child_cpus))
		goto unlock_and_return;

	state_id |= get_cluster_id(cluster->parent, aff_lvl);

	if (cluster->last_level != cluster->default_level) {
		struct lpm_cluster_level *level
			= &cluster->levels[cluster->last_level];

		state_id |= (level->psci_id & cluster->psci_mode_mask)
					<< cluster->psci_mode_shift;
		(*aff_lvl)++;
	}
unlock_and_return:
	spin_unlock(&cluster->sync_lock);
	return state_id;
}
#endif

#if !defined(CONFIG_CPU_V7)
asmlinkage int __invoke_psci_fn_smc(u64, u64, u64, u64);
static bool psci_enter_sleep(struct lpm_cluster *cluster,
					int idx, bool from_idle)

{
	int ret;
	/*
	 * idx = 0 is the default LPM state
	 */
	if (!idx) {
		stop_critical_timings();
		cpu_do_idle();
		start_critical_timings();
		ret = true;
	} else {
		int affinity_level = 0;
		int state_id = get_cluster_id(cluster, &affinity_level);
		int power_state =
			PSCI_POWER_STATE(cluster->cpu->levels[idx].is_reset);
		bool success = false;

		if (cluster->cpu->levels[idx].hyp_psci) {
			stop_critical_timings();
			__invoke_psci_fn_smc(0xC4000021, 0, 0, 0);
			start_critical_timings();
			return true;
		}

		affinity_level = PSCI_AFFINITY_LEVEL(affinity_level);
		state_id |= (power_state | affinity_level
			| cluster->cpu->levels[idx].psci_id);

		update_debug_pc_event(CPU_ENTER, state_id,
						0xdeaffeed, 0xdeaffeed, true);
		stop_critical_timings();
		ret = psci_cpu_suspend_enter(state_id);
		success = (ret == 0);
		start_critical_timings();
		update_debug_pc_event(CPU_EXIT, state_id,
						success, 0xdeaffeed, true);
		ret = success;
	}
	return ret;
}
#elif defined(CONFIG_ARM_PSCI)
static bool psci_enter_sleep(struct lpm_cluster *cluster,
					int idx, bool from_idle)
{
	bool ret;

	if (!idx) {
		stop_critical_timings();
		cpu_do_idle();
		start_critical_timings();
		ret = true;
	} else {
		int affinity_level = 0;
		int state_id = get_cluster_id(cluster, &affinity_level);
		int power_state =
			PSCI_POWER_STATE(cluster->cpu->levels[idx].is_reset);
		bool success = false;

		affinity_level = PSCI_AFFINITY_LEVEL(affinity_level);
		state_id |= (power_state | affinity_level
			| cluster->cpu->levels[idx].psci_id);

		update_debug_pc_event(CPU_ENTER, state_id,
						0xdeaffeed, 0xdeaffeed, true);
		stop_critical_timings();
		stop_critical_timings();
		ret = psci_cpu_suspend_enter(state_id);
		start_critical_timings();
		update_debug_pc_event(CPU_EXIT, state_id,
						success, 0xdeaffeed, true);
		ret = success;
	}
	return ret;
}
#else
static bool psci_enter_sleep(struct lpm_cluster *cluster,
					int idx, bool from_idle)
{
	WARN_ONCE(true, "PSCI cpu_suspend ops not supported\n");
	return false;
}
#endif

static int lpm_cpuidle_select(struct cpuidle_driver *drv,
		struct cpuidle_device *dev, bool *stop_tick)
{
	struct lpm_cluster *cluster = per_cpu(cpu_cluster, dev->cpu);
	int idx;

	if (!cluster)
		return 0;

	idx = cpu_power_select(dev, cluster->cpu, stop_tick);

	return idx;
}

static int lpm_cpuidle_enter(struct cpuidle_device *dev,
		struct cpuidle_driver *drv, int idx)
{
	struct lpm_cluster *cluster = per_cpu(cpu_cluster, dev->cpu);
	bool success = true;
	const struct cpumask *cpumask = get_cpu_mask(dev->cpu);
	ktime_t start = ktime_get();
	int64_t start_time = ktime_to_ns(ktime_get()), end_time;

	per_cpu(next_hrtimer, dev->cpu) = tick_nohz_get_next_hrtimer();
	if (idx < 0)
		return -EINVAL;

	cpu_prepare(cluster, idx, true);
	cluster_prepare(cluster, cpumask, idx, true, ktime_to_ns(ktime_get()));

	trace_cpu_idle_enter(idx);
	lpm_stats_cpu_enter(idx, start_time);

	if (need_resched())
		goto exit;

	if (!use_psci) {
		if (idx > 0)
			update_debug_pc_event(CPU_ENTER, idx, 0xdeaffeed,
					0xdeaffeed, true);
		success = msm_cpu_pm_enter_sleep(cluster->cpu->levels[idx].mode,
				true);

		if (idx > 0)
			update_debug_pc_event(CPU_EXIT, idx, success,
							0xdeaffeed, true);
	} else {
		success = psci_enter_sleep(cluster, idx, true);
	}

exit:
	end_time = ktime_to_ns(ktime_get());
	lpm_stats_cpu_exit(idx, end_time, success);

	cluster_unprepare(cluster, cpumask, idx, true, end_time, success);
	cpu_unprepare(cluster, idx, true);

	trace_cpu_idle_exit(idx, success);
	dev->last_residency_ns = ktime_to_ns(ktime_sub(ktime_get(), start));

	return idx;
}

#ifdef CONFIG_CPU_IDLE_MULTIPLE_DRIVERS
static int cpuidle_register_cpu(struct cpuidle_driver *drv,
		struct cpumask *mask)
{
	struct cpuidle_device *device;
	int cpu, ret;


	if (!mask || !drv)
		return -EINVAL;

	drv->cpumask = mask;
	ret = cpuidle_register_driver(drv);
	if (ret) {
		pr_err("Failed to register cpuidle driver %d\n", ret);
		goto failed_driver_register;
	}

	for_each_cpu(cpu, mask) {
		device = &per_cpu(cpuidle_dev, cpu);
		device->cpu = cpu;

		ret = cpuidle_register_device(device);
		if (ret) {
			pr_err("Failed to register cpuidle driver for cpu:%u\n",
					cpu);
			goto failed_driver_register;
		}
	}
	return ret;
failed_driver_register:
	for_each_cpu(cpu, mask)
		cpuidle_unregister_driver(drv);
	return ret;
}
#else
static int cpuidle_register_cpu(struct cpuidle_driver *drv,
		struct  cpumask *mask)
{
	return cpuidle_register(drv, NULL);
}
#endif

static struct cpuidle_governor lpm_governor = {
	.name =		"qcom",
	.rating =	30,
	.select =	lpm_cpuidle_select,
};

static int cluster_cpuidle_register(struct lpm_cluster *cl)
{
	int i = 0, ret = 0;
	unsigned int cpu;
	struct lpm_cluster *p = NULL;

	if (!cl->cpu) {
		struct lpm_cluster *n;

		list_for_each_entry(n, &cl->child, list) {
			ret = cluster_cpuidle_register(n);
			if (ret)
				break;
		}
		return ret;
	}

	cl->drv = kzalloc(sizeof(*cl->drv), GFP_KERNEL);
	if (!cl->drv)
		return -ENOMEM;

	cl->drv->name = "msm_idle";

	for (i = 0; i < cl->cpu->nlevels; i++) {
		struct cpuidle_state *st = &cl->drv->states[i];
		struct lpm_cpu_level *cpu_level = &cl->cpu->levels[i];

		snprintf(st->name, CPUIDLE_NAME_LEN, "C%u\n", i);
		snprintf(st->desc, CPUIDLE_DESC_LEN, "%s", cpu_level->name);
		st->flags = 0;
		st->exit_latency = cpu_level->pwr.latency_us;
		st->power_usage = cpu_level->pwr.ss_power;
		st->target_residency = 0;
		st->enter = lpm_cpuidle_enter;
	}

	cl->drv->state_count = cl->cpu->nlevels;
	cl->drv->safe_state_index = 0;
	for_each_cpu(cpu, &cl->child_cpus)
		per_cpu(cpu_cluster, cpu) = cl;

	for_each_possible_cpu(cpu) {
		if (cpu_online(cpu))
			continue;
		p = per_cpu(cpu_cluster, cpu);
		while (p) {
			int j;

			spin_lock(&p->sync_lock);
			cpumask_set_cpu(cpu, &p->num_children_in_sync);
			for (j = 0; j < p->nlevels; j++)
				cpumask_copy(&p->levels[j].num_cpu_votes,
						&p->num_children_in_sync);
			spin_unlock(&p->sync_lock);
			p = p->parent;
		}
	}
	ret = cpuidle_register_cpu(cl->drv, &cl->child_cpus);

	if (ret) {
		kfree(cl->drv);
		return -ENOMEM;
	}
	return 0;
}

/**
 * init_lpm - initializes the governor
 */
static int __init init_lpm(void)
{
	return cpuidle_register_governor(&lpm_governor);
}

postcore_initcall(init_lpm);

static void register_cpu_lpm_stats(struct lpm_cpu *cpu,
		struct lpm_cluster *parent)
{
	const char **level_name;
	int i;

	level_name = kcalloc(cpu->nlevels, sizeof(*level_name), GFP_KERNEL);

	if (!level_name)
		return;

	for (i = 0; i < cpu->nlevels; i++)
		level_name[i] = cpu->levels[i].name;

	lpm_stats_config_level("cpu", level_name, cpu->nlevels,
			parent->stats, &parent->child_cpus);

	kfree(level_name);
}

static void register_cluster_lpm_stats(struct lpm_cluster *cl,
		struct lpm_cluster *parent)
{
	const char **level_name;
	int i;
	struct lpm_cluster *child;

	if (!cl)
		return;

	level_name = kcalloc(cl->nlevels, sizeof(*level_name), GFP_KERNEL);

	if (!level_name)
		return;

	for (i = 0; i < cl->nlevels; i++)
		level_name[i] = cl->levels[i].level_name;

	cl->stats = lpm_stats_config_level(cl->cluster_name, level_name,
			cl->nlevels, parent ? parent->stats : NULL, NULL);

	kfree(level_name);

	if (cl->cpu) {
		register_cpu_lpm_stats(cl->cpu, cl);
		return;
	}

	list_for_each_entry(child, &cl->child, list)
		register_cluster_lpm_stats(child, cl);
}

static int lpm_suspend_prepare(void)
{
	suspend_in_progress = true;
	lpm_stats_suspend_enter();

	return 0;
}

static void lpm_suspend_wake(void)
{
	suspend_in_progress = false;
	lpm_stats_suspend_exit();
}

static int lpm_suspend_enter(suspend_state_t state)
{
	int cpu = raw_smp_processor_id();
	struct lpm_cluster *cluster = per_cpu(cpu_cluster, cpu);
	struct lpm_cpu *lpm_cpu = cluster->cpu;
	const struct cpumask *cpumask = get_cpu_mask(cpu);
	int idx;
	bool success = true;

	for (idx = lpm_cpu->nlevels - 1; idx >= 0; idx--) {

		if (lpm_cpu_mode_allow(cpu, idx, false))
			break;
	}
	if (idx < 0) {
		pr_err("Failed suspend\n");
		return 0;
	}
	cpu_prepare(cluster, idx, false);
	cluster_prepare(cluster, cpumask, idx, false, 0);
	if (idx > 0)
		update_debug_pc_event(CPU_ENTER, idx, 0xdeaffeed,
					0xdeaffeed, false);
	if (!use_psci)
		msm_cpu_pm_enter_sleep(cluster->cpu->levels[idx].mode, false);
	else
		success = psci_enter_sleep(cluster, idx, true);

	if (idx > 0)
		update_debug_pc_event(CPU_EXIT, idx, true, 0xdeaffeed,
					false);

	cluster_unprepare(cluster, cpumask, idx, false, 0, success);
	cpu_unprepare(cluster, idx, false);
	return 0;
}

static int lpm_dying_cpu(unsigned int cpu)
{
	struct lpm_cluster *cluster = per_cpu(cpu_cluster, cpu);

	update_debug_pc_event(CPU_HP_DYING, cpu,
				cluster->num_children_in_sync.bits[0],
				cluster->child_cpus.bits[0], false);
	cluster_prepare(cluster, get_cpu_mask(cpu), NR_LPM_LEVELS, false, 0);
	return 0;
}

static int lpm_starting_cpu(unsigned int cpu)
{
	struct lpm_cluster *cluster = per_cpu(cpu_cluster, cpu);

	update_debug_pc_event(CPU_HP_STARTING, cpu,
				cluster->num_children_in_sync.bits[0],
				cluster->child_cpus.bits[0], false);
	cluster_unprepare(cluster, get_cpu_mask(cpu), NR_LPM_LEVELS,
							false, 0, true);
	return 0;
}

static const struct platform_suspend_ops lpm_suspend_ops = {
	.enter = lpm_suspend_enter,
	.valid = suspend_valid_only_mem,
	.prepare_late = lpm_suspend_prepare,
	.wake = lpm_suspend_wake,
};

static int init_hw_mutex(struct device_node *node)
{
	struct resource r;
	int rc;
	static uint32_t lock_count;

	rc = of_address_to_resource(node, 0, &r);
	if (rc) {
		pr_err("Failed to get resource\n");
		return 1;
	}

	rc = of_property_read_u32(node, "qcom,num-locks", &lock_count);
	if (rc) {
		pr_err("Failed to get num-locks property\n");
		return 1;
	}

	reg_base = r.start;
	reg_size = (uint32_t)(resource_size(&r));
	lock_size = reg_size / lock_count;

	return 0;
}

static int lpm_probe(struct platform_device *pdev)
{
	int ret;
	int size;
	struct kobject *module_kobj = NULL;
	struct md_region md_entry;
	struct device_node *node;

	cpus_read_lock();
	lpm_root_node = lpm_of_parse_cluster(pdev);

	if (IS_ERR_OR_NULL(lpm_root_node)) {
		pr_err("%s(): Failed to probe low power modes\n", __func__);
		cpus_read_unlock();
		return PTR_ERR(lpm_root_node);
	}

	if (print_parsed_dt)
		cluster_dt_walkthrough(lpm_root_node);

	/*
	 * Register hotplug notifier before broadcast time to ensure there
	 * to prevent race where a broadcast timer might not be setup on for a
	 * core.  BUG in existing code but no known issues possibly because of
	 * how late lpm_levels gets initialized.
	 */
	suspend_set_ops(&lpm_suspend_ops);
	hrtimer_init(&lpm_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	size = num_dbg_elements * sizeof(struct lpm_debug);
	lpm_debug = dma_alloc_coherent(&pdev->dev, size,
			&lpm_debug_phys, GFP_KERNEL);
	register_cluster_lpm_stats(lpm_root_node, NULL);

	ret = cluster_cpuidle_register(lpm_root_node);
	cpus_read_unlock();
	if (ret) {
		pr_err("%s()Failed to register with cpuidle framework\n",
				__func__);
		goto failed;
	}
	ret = cpuhp_setup_state(CPUHP_AP_QCOM_TIMER_STARTING,
			"AP_QCOM_SLEEP_STARTING",
			lpm_starting_cpu, lpm_dying_cpu);
	if (ret)
		goto failed;

	module_kobj = kset_find_obj(module_kset, KBUILD_MODNAME);
	if (!module_kobj) {
		pr_err("%s: cannot find kobject for module %s\n",
			__func__, KBUILD_MODNAME);
		ret = -ENOENT;
		goto failed;
	}

	ret = create_cluster_lvl_nodes(lpm_root_node, module_kobj);
	if (ret) {
		pr_err("%s(): Failed to create cluster level nodes\n",
				__func__);
		goto failed;
	}

	/* Add lpm_debug to Minidump*/
	strscpy(md_entry.name, "KLPMDEBUG", sizeof(md_entry.name));
	md_entry.virt_addr = (uintptr_t)lpm_debug;
	md_entry.phys_addr = lpm_debug_phys;
	md_entry.size = size;
	if (msm_minidump_add_region(&md_entry))
		pr_info("Failed to add lpm_debug in Minidump\n");

	node = of_find_node_by_name(NULL, "qcom,ipc-spinlock");
	if (!node) {
		pr_err("Failed to find ipc-spinlock node\n");
		ret = -ENODEV;
		goto failed;
	}

	if (init_hw_mutex(node)) {
		ret = -EINVAL;
		of_node_put(node);
		goto failed;
	}

	hw_mutex_reg_base = ioremap(reg_base, reg_size);
	if (!hw_mutex_reg_base) {
		pr_err("ioremap failed\n");
		ret = -ENOMEM;
		of_node_put(node);
		goto failed;
	}
	of_node_put(node);

	return 0;
failed:
	free_cluster_node(lpm_root_node);
	lpm_root_node = NULL;
	return ret;
}

static const struct of_device_id lpm_mtch_tbl[] = {
	{.compatible = "qcom,lpm-levels"},
	{},
};

static struct platform_driver lpm_driver = {
	.probe = lpm_probe,
	.driver = {
		.name = "lpm-levels",
		.of_match_table = lpm_mtch_tbl,
	},
};

static int __init lpm_levels_module_init(void)
{
	int rc;

	rc = platform_driver_register(&lpm_driver);
	if (rc) {
		pr_info("Error registering %s\n", lpm_driver.driver.name);
		goto fail;
	}

fail:
	return rc;
}
late_initcall(lpm_levels_module_init);

static void mutex_reg_write(uint32_t tid)
{
	struct mutex_reg *lock;

	lock = hw_mutex_reg_base + (SCM_HANDOFF_LOCK_ID * lock_size);
	do {
		writel_relaxed(tid, lock);
		/* barrier for proper semantics */
		smp_mb();
	} while (readl_relaxed(lock) != tid);
}


enum msm_pm_l2_scm_flag lpm_cpu_pre_pc_cb(unsigned int cpu)
{
	struct lpm_cluster *cluster = per_cpu(cpu_cluster, cpu);
	enum msm_pm_l2_scm_flag retflag = MSM_SCM_L2_ON;
	uint32_t tid;
	/*
	 * No need to acquire the lock if probe isn't completed yet
	 * In the event of the hotplug happening before lpm probe, we want to
	 * flush the cache to make sure that L2 is flushed. In particular, this
	 * could cause incoherencies for a cluster architecture. This wouldn't
	 * affect the idle case as the idle driver wouldn't be registered
	 * before the probe function
	 */
	if (!cluster)
		return MSM_SCM_L2_OFF;

	/*
	 * Assumes L2 only. What/How parameters gets passed into TZ will
	 * determine how this function reports this info back in msm-pm.c
	 */
	spin_lock(&cluster->sync_lock);

	if (!cluster->lpm_dev) {
		retflag = MSM_SCM_L2_OFF;
		goto unlock_and_return;
	}

	if (!cpumask_equal(&cluster->num_children_in_sync,
						&cluster->child_cpus))
		goto unlock_and_return;

	if (cluster->lpm_dev)
		retflag = cluster->lpm_dev->tz_flag;
	/*
	 * The scm_handoff_lock will be release by the secure monitor.
	 * It is used to serialize power-collapses from this point on,
	 * so that both Linux and the secure context have a consistent
	 * view regarding the number of running cpus (cpu_count).
	 *
	 * It must be acquired before releasing the cluster lock.
	 */
unlock_and_return:
	update_debug_pc_event(PRE_PC_CB, retflag, 0xdeadbeef, 0xdeadbeef,
			0xdeadbeef);
	trace_pre_pc_cb(retflag);
	tid = MUTEX_TID_START + cpu;
	mutex_reg_write(tid);
	spin_unlock(&cluster->sync_lock);
	return retflag;
}

/**
 * lpm_cpu_hotplug_enter(): Called by dying CPU to terminate in low power mode
 *
 * @cpu: cpuid of the dying CPU
 *
 * Called from platform_cpu_kill() to terminate hotplug in a low power mode
 */
void lpm_cpu_hotplug_enter(unsigned int cpu)
{
	enum msm_pm_sleep_mode mode = MSM_PM_SLEEP_MODE_NR;
	struct lpm_cluster *cluster = per_cpu(cpu_cluster, cpu);
	int i;
	int idx = -1;

	/*
	 * If lpm isn't probed yet, try to put cpu into the one of the modes
	 * available
	 */
	if (!cluster) {
		if (msm_spm_is_mode_avail(
					MSM_SPM_MODE_POWER_COLLAPSE)){
			mode = MSM_PM_SLEEP_MODE_POWER_COLLAPSE;
		} else if (msm_spm_is_mode_avail(
				MSM_SPM_MODE_FASTPC)) {
			mode = MSM_PM_SLEEP_MODE_FASTPC;
		} else if (msm_spm_is_mode_avail(
				MSM_SPM_MODE_RETENTION)) {
			mode = MSM_PM_SLEEP_MODE_RETENTION;
		} else {
			pr_err("No mode avail for cpu%d hotplug\n", cpu);
			WARN_ON(1);
			return;
		}
	} else {
		struct lpm_cpu *lpm_cpu;
		uint32_t ss_pwr = ~0U;

		lpm_cpu = cluster->cpu;
		for (i = 0; i < lpm_cpu->nlevels; i++) {
			if (ss_pwr < lpm_cpu->levels[i].pwr.ss_power)
				continue;
			ss_pwr = lpm_cpu->levels[i].pwr.ss_power;
			idx = i;
			mode = lpm_cpu->levels[i].mode;
		}

		if (mode == MSM_PM_SLEEP_MODE_NR)
			return;

		WARN_ON(idx < 0);
		cluster_prepare(cluster, get_cpu_mask(cpu), idx, false, 0);
	}

	msm_cpu_pm_enter_sleep(mode, false);
}
