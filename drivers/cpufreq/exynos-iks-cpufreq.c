/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS - CPU frequency scaling support for EXYNOS series
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/cpufreq.h>
#include <linux/suspend.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/cpu.h>
#include <linux/pm_qos.h>

#include <asm/cputype.h>
#include <asm/bL_switcher.h>
#include <mach/cpufreq.h>
#include <mach/regs-pmu.h>
#include <mach/tmu.h>
#include <mach/asv-exynos.h>
#include <plat/cpu.h>

struct lpj_info {
	unsigned long   ref;
	unsigned int    freq;
};

static struct lpj_info global_lpj_ref;

/* Use boot_freq when entering sleep mode */
static unsigned int boot_freq;

/* For switcher */
static unsigned int freq_min[CA_END] __read_mostly;	/* Minimum (Big/Little) clock frequency */
static unsigned int freq_max[CA_END] __read_mostly;	/* Maximum (Big/Little) clock frequency */
static struct cpumask cluster_cpus[CA_END];	/* cpu number on (Big/Little) cluster */
static unsigned long lpj[CA_END];

#define ACTUAL_FREQ(x, cur)  ((cur == CA7) ? (x) << 1 : (x))
#define VIRT_FREQ(x, cur)    ((cur == CA7) ? (x) >> 1 : (x))

/*
 * This value is based on the difference between the dmips value of A15/A7
 * It is used to revise cpu frequency when changing cluster
 */
#define UP_IKS_THRESH		6
#define DOWN_IKS_THRESH		18
#define DOWN_STEP_OLD		1100000
#define DOWN_STEP_NEW		600000
#define UP_STEP_OLD		550000
#define UP_STEP_NEW		600000
#define STEP_LEVEL_CA7_MAX	600000
#define STEP_LEVEL_CA15_MIN	800000

#define LIMIT_COLD_VOLTAGE	1250000
#define CPU_MAX_COUNT		4

static struct exynos_dvfs_info *exynos_info[CA_END];
static struct exynos_dvfs_info exynos_info_CA7;
static struct exynos_dvfs_info exynos_info_CA15;

static struct cpufreq_frequency_table *merge_freq_table;

static struct regulator *arm_regulator;
static struct regulator *kfc_regulator;
static unsigned int volt_offset;
static int volt_powerdown[CA_END];
static unsigned int policy_max_freq;

static struct cpufreq_freqs *freqs[CA_END];

static unsigned int exynos5410_bb_con0;
static unsigned int user_set_max_freq;
static unsigned int user_set_eagle_count;
static unsigned int all_cpu_freqs[CPU_MAX_COUNT];

static DEFINE_MUTEX(cpufreq_lock);
static DEFINE_MUTEX(cpufreq_scale_lock);

static bool exynos_cpufreq_init_done;

/* Include CPU mask of each cluster */
static cluster_type boot_cluster;

DEFINE_PER_CPU(cluster_type, cpu_cur_cluster);
static DEFINE_PER_CPU(unsigned int, req_freq);

static struct pm_qos_request boot_cpu_qos;
static struct pm_qos_request min_cpu_qos;

static struct cpufreq_policy fake_policy[CA_END][NR_CPUS];

static unsigned int get_limit_voltage(unsigned int voltage)
{
	if (voltage > LIMIT_COLD_VOLTAGE)
		return voltage;

	if (voltage + volt_offset > LIMIT_COLD_VOLTAGE)
		return LIMIT_COLD_VOLTAGE;

	return voltage + volt_offset;
}

static void init_cpumask_cluster_set(unsigned int cluster)
{
	unsigned int i;

	for_each_cpu(i, cpu_possible_mask) {
		per_cpu(cpu_cur_cluster, i) = cluster;
		if (cluster == CA15)
			cpumask_set_cpu(i, &cluster_cpus[CA15]);
		else
			cpumask_set_cpu(i, &cluster_cpus[CA7]);
	}
}

static cluster_type get_cur_cluster(unsigned int cpu)
{
	return per_cpu(cpu_cur_cluster, cpu);
}

static void set_cur_cluster(unsigned int cpu, cluster_type target_cluster)
{
	per_cpu(cpu_cur_cluster, cpu) = target_cluster;
}

void reset_lpj_for_cluster(cluster_type cluster)
{
	lpj[!cluster] = 0;
}

static unsigned int get_num_CA15(void)
{
	unsigned int j, num = 0;

	for_each_cpu(j, cpu_possible_mask) {
		if (per_cpu(cpu_cur_cluster, j) == CA15 && cpu_online(j))
			num++;
	}

	return num;
}

static void set_boot_freq(void)
{
	int i;

	for (i = 0; i < CA_END; i++) {
		if (exynos_info[i] == NULL)
			continue;

		exynos_info[i]->boot_freq
				= clk_get_rate(exynos_info[i]->cpu_clk) / 1000;
	}
}

static unsigned int get_boot_freq(unsigned int cluster)
{
	if (exynos_info[cluster] == NULL)
		return 0;

	return exynos_info[cluster]->boot_freq;
}

/* Get table size */
static unsigned int cpufreq_get_table_size(
				struct cpufreq_frequency_table *table,
				unsigned int cluster_id)
{
	int size = 0;

	if (cluster_id == CA15) {
		for (; (table[size].frequency != CPUFREQ_TABLE_END); size++)
			;
	} else {
		for (; (table[size].frequency != CPUFREQ_TABLE_END); size++)
			;
	}
	return size;
}

/*
 * copy entries of all the per-cluster cpufreq_frequency_table entries
 * into a single frequency table which is published to cpufreq core
 */
static int cpufreq_merge_tables(void)
{
	int cluster_id, i;
	unsigned int total_sz = 0, size[CA_END];
	struct cpufreq_frequency_table *freq_table;

	for (cluster_id = 0; cluster_id < CA_END; cluster_id++) {
		size[cluster_id] =  cpufreq_get_table_size(
		   exynos_info[cluster_id]->freq_table, cluster_id);
		total_sz += size[cluster_id];
	}

	freq_table = kzalloc(sizeof(struct cpufreq_frequency_table) *
						(total_sz + 1), GFP_KERNEL);
	merge_freq_table = freq_table;

	memcpy(freq_table, exynos_info[CA15]->freq_table,
			size[CA15] * sizeof(struct cpufreq_frequency_table));
	freq_table += size[CA15];
	memcpy(freq_table, exynos_info[CA7]->freq_table,
			size[CA7] * sizeof(struct cpufreq_frequency_table));

	for (i = size[CA15]; i <= total_sz ; i++) {
		if (merge_freq_table[i].frequency != CPUFREQ_ENTRY_INVALID)
			merge_freq_table[i].frequency >>= 1;
	}

	merge_freq_table[total_sz].frequency = CPUFREQ_TABLE_END;

	for (i = 0; merge_freq_table[i].frequency != CPUFREQ_TABLE_END; i++) {
		pr_debug("merged_table index: %d freq: %d\n", i,
						merge_freq_table[i].frequency);
	}

	return 0;
}

static bool is_alive(unsigned int cluster)
{
	unsigned int tmp;
	tmp = __raw_readl(cluster == CA15 ? EXYNOS5410_ARM_COMMON_STATUS :
					EXYNOS5410_KFC_COMMON_STATUS) & 0x3;

	return tmp ? true : false;
}

/*
 * Requesting core switch to other cluster. It save the current status
 * and wakes up the core of new cluster. Then waking core restore status and
 * take a task of other cluster.
 */
#ifdef CONFIG_BL_SWITCHER
static void switch_to_entry(unsigned int cpu,
			    cluster_type target_cluster)
{
	bL_switch_request(cpu, !target_cluster);
	per_cpu(cpu_cur_cluster, cpu) = target_cluster;
}
#endif

int exynos_verify_speed(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, merge_freq_table);
}

unsigned int exynos_getspeed_cluster(cluster_type cluster)
{
	return VIRT_FREQ(clk_get_rate(exynos_info[cluster]->cpu_clk) / 1000, cluster);
}

unsigned int exynos_getspeed(unsigned int cpu)
{
	unsigned int cur = get_cur_cluster(cpu);

	return exynos_getspeed_cluster(cur);
}

static unsigned int get_max_req_freq(unsigned int cluster_id)
{
	unsigned int i, max_freq = 0, tmp = 0, cur;

	for_each_online_cpu(i) {
		cur = get_cur_cluster(i);
		if (cur == cluster_id) {
			tmp = per_cpu(req_freq, i);
			if (tmp > max_freq)
				max_freq = tmp;
		}
	}

	return max_freq;
}

static void set_req_freq(unsigned int cpu, unsigned int freq)
{
	per_cpu(req_freq, cpu) = freq;
}

static unsigned int exynos_get_safe_volt(unsigned int old_index,
					unsigned int new_index,
					unsigned int cur)
{
	unsigned int safe_arm_volt = 0;
	struct cpufreq_frequency_table *freq_table
					= exynos_info[cur]->freq_table;
	unsigned int *volt_table = exynos_info[cur]->volt_table;

	/*
	 * ARM clock source will be changed APLL to MPLL temporary
	 * To support this level, need to control regulator for
	 * reguired voltage level
	 */
	if (exynos_info[cur]->need_apll_change != NULL) {
		if (exynos_info[cur]->need_apll_change(old_index, new_index) &&
			(freq_table[new_index].frequency
					< exynos_info[cur]->mpll_freq_khz) &&
			(freq_table[old_index].frequency
					< exynos_info[cur]->mpll_freq_khz)) {
				safe_arm_volt
				  = volt_table[exynos_info[cur]->pll_safe_idx];
		}
	}

	return safe_arm_volt;
}

/* Determine valid target frequency using freq_table */
int exynos5_frequency_table_target(struct cpufreq_policy *policy,
				   struct cpufreq_frequency_table *table,
				   unsigned int target_freq,
				   unsigned int relation,
				   unsigned int *index)
{
	unsigned int i;

	if (!cpu_online(policy->cpu))
		return -EINVAL;

	for (i = 0; (table[i].frequency != CPUFREQ_TABLE_END); i++) {
		unsigned int freq = table[i].frequency;
		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;

		if (target_freq == freq) {
			*index = i;
			break;
		}
	}

	if (table[i].frequency == CPUFREQ_TABLE_END)
		return -EINVAL;

	return 0;
}

static int is_cpufreq_valid(int cpu)
{
	struct cpufreq_policy policy;

	return !cpufreq_get_policy(&policy, cpu);
}

static int exynos_cpufreq_scale(unsigned int target_freq,
				unsigned int curr_freq, unsigned int cpu)
{
	unsigned int cur = get_cur_cluster(cpu);
	struct cpufreq_frequency_table *freq_table
					= exynos_info[cur]->freq_table;
	unsigned int *volt_table = exynos_info[cur]->volt_table;
	struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);
	struct regulator *regulator = exynos_info[cur]->regulator;
	unsigned int new_index, old_index, j;
	unsigned int volt, safe_volt = 0;
	int ret = 0;

	if (!policy)
		return ret;

	if (!is_alive(cur))
		goto out;

	freqs[cur]->cpu = cpu;
	freqs[cur]->new = target_freq;

	if (exynos5_frequency_table_target(policy, freq_table,
				ACTUAL_FREQ(curr_freq, cur),
				CPUFREQ_RELATION_L, &old_index)) {
		ret = -EINVAL;
		goto out;
	}

	if (exynos5_frequency_table_target(policy, freq_table,
				ACTUAL_FREQ(freqs[cur]->new, cur),
				CPUFREQ_RELATION_L, &new_index)) {
		ret = -EINVAL;
		goto out;
	}

	/*
	 * ARM clock source will be changed APLL to MPLL temporary
	 * To support this level, need to control regulator for
	 * required voltage level
	 */
	safe_volt = exynos_get_safe_volt(old_index, new_index, cur);
	if (safe_volt)
		safe_volt = get_limit_voltage(safe_volt);

	volt = get_limit_voltage(volt_table[new_index]);

	/* Update policy current frequency */
	for_each_cpu(j, &cluster_cpus[cur]) {
		if (is_cpufreq_valid(j)) {
			freqs[cur]->cpu = j;
			cpufreq_notify_transition(
					freqs[cur], CPUFREQ_PRECHANGE);
		}
	}

	/* When the new frequency is higher than current frequency */
	if ((ACTUAL_FREQ(freqs[cur]->new, cur) >
			ACTUAL_FREQ(freqs[cur]->old, cur)) && !safe_volt)
		/* Firstly, voltage up to increase frequency */
		regulator_set_voltage(regulator, volt, volt);

	if (safe_volt)
		regulator_set_voltage(regulator, safe_volt, safe_volt);

	if (old_index != new_index)
		exynos_info[cur]->set_freq(old_index, new_index);

	if (!global_lpj_ref.freq) {
		global_lpj_ref.ref = loops_per_jiffy;
		global_lpj_ref.freq = freqs[cur]->old;
	}

	lpj[cur] = cpufreq_scale(global_lpj_ref.ref,
			global_lpj_ref.freq, freqs[cur]->new);

	loops_per_jiffy = max(lpj[CA7], lpj[CA15]);

	for_each_cpu(j, &cluster_cpus[cur]) {
		if (is_cpufreq_valid(j)) {
			freqs[cur]->cpu = j;
			cpufreq_notify_transition(
					freqs[cur], CPUFREQ_POSTCHANGE);
		}
	}

	/* When the new frequency is lower than current frequency */
	if ((ACTUAL_FREQ(freqs[cur]->new, cur) <
					ACTUAL_FREQ(freqs[cur]->old, cur)) ||
	   ((ACTUAL_FREQ(freqs[cur]->new, cur) >
			ACTUAL_FREQ(freqs[cur]->old, cur)) && safe_volt))
		/* down the voltage after frequency change */
		regulator_set_voltage(regulator, volt, volt);

out:
	cpufreq_cpu_put(policy);
	return ret;
}

static cluster_type exynos_switch(struct cpufreq_policy *policy, cluster_type cur)
{
	unsigned int cpu;
	cluster_type new_cluster;

	new_cluster = !cur;

	for_each_cpu(cpu, policy->cpus) {
		switch_to_entry(cpu, new_cluster);
		/* set big/litte-cpu mask */
		cpumask_clear_cpu(cpu, &cluster_cpus[cur]);
		cpumask_set_cpu(cpu, &cluster_cpus[new_cluster]);
	}

	return new_cluster;
}

/* Set clock frequency */
static int exynos_target(struct cpufreq_policy *policy,
			  unsigned int target_freq,
			  unsigned int relation)
{
	/* read current cluster */
	cluster_type cur = get_cur_cluster(policy->cpu);
	unsigned int index, new_freq = 0, do_switch = 0;
	int count, ret = 0;
	bool user = false, later = false, limit_eagle = false;
	int cpu, delta;
	unsigned int min_cpu = 0, min_freq = UINT_MAX;

	mutex_lock(&cpufreq_lock);

	all_cpu_freqs[policy->cpu] = target_freq;

	/* delta means the number of cores which has to switch */
	delta = get_num_CA15() - user_set_eagle_count;

	if (delta > 0) {
		/* find minimum frequency core among CA15 */
		for_each_online_cpu(cpu) {
			if (all_cpu_freqs[cpu] > freq_max[CA7] &&
					all_cpu_freqs[cpu] < min_freq) {
				min_cpu = cpu;
				min_freq = all_cpu_freqs[cpu];
			}
		}

		/* if current core freq is minimum, switch to CA7 */
		if (min_cpu == policy->cpu) {
			target_freq = freq_max[CA7];
			limit_eagle = true;
			all_cpu_freqs[policy->cpu] = target_freq;
		}
	}

	/* set target_freq only if core cannot swtich due to user set value */
	if (get_num_CA15() < CPU_MAX_COUNT) {
		if (cur == CA7 && user_set_eagle_count <= get_num_CA15()
				&& target_freq > freq_max[CA7]) {
			target_freq = freq_max[CA7];
			limit_eagle = true;
			all_cpu_freqs[policy->cpu] = target_freq;
		}
	}

	if (exynos_info[cur]->blocked)
		goto out;

	count = get_num_CA15();

	/* get current frequency */
	freqs[cur]->old = exynos_getspeed(policy->cpu);

	/* save the frequency & cpu number */
	set_req_freq(policy->cpu, target_freq);

#if defined(CONFIG_CPU_FREQ_GOV_USERSPACE) || defined(CONFIG_CPU_FREQ_GOV_PERFORMANCE)
	if ((strcmp(policy->governor->name, "userspace") == 0)
		|| strcmp(policy->governor->name, "performance") == 0) {
		user = true;
		goto done;
	}
#endif

	if (freqs[cur]->old <= UP_STEP_OLD  && target_freq > UP_STEP_NEW)
		target_freq = STEP_LEVEL_CA7_MAX;

	if (freqs[cur]->old >= DOWN_STEP_OLD && target_freq < DOWN_STEP_NEW) {
		if (strcmp(policy->governor->name, "ondemand") == 0)
			target_freq = STEP_LEVEL_CA15_MIN;
		else
			target_freq = STEP_LEVEL_CA7_MAX;
	}

	if (!limit_eagle)
		target_freq = max((unsigned int)pm_qos_request(PM_QOS_CPU_FREQ_MIN), target_freq);

done:
	if (cur == CA15 && target_freq < freq_min[CA15]) {
		do_switch = 1;	/* Switch to Little */
	} else if (cur == CA7 && user_set_eagle_count > get_num_CA15()
			&& target_freq > freq_max[CA7]) {
		do_switch = 1;	/* Switch from LITTLE to big */
		if (count > 0 && count < 4 &&
			target_freq > exynos_info[cur]->max_op_freqs[count + 1])
				later = true;
	}

#ifdef CONFIG_BL_SWITCHER
	if (do_switch) {
		if  (later) {
			cur = !cur;
			count++;
			set_cur_cluster(policy->cpu, cur);
		} else {
			cur = exynos_switch(policy, cur);
		}

		freqs[cur]->old = exynos_getspeed_cluster(cur);
		policy->cur = freqs[cur]->old;
	}
#endif

	if (user)
		new_freq = target_freq;
	else
		new_freq = max(get_max_req_freq(cur), target_freq);

	new_freq = min(new_freq, exynos_info[cur]->max_op_freqs[count]);

	if (cpufreq_frequency_table_target(&fake_policy[cur][policy->cpu],
			exynos_info[cur]->freq_table, ACTUAL_FREQ(new_freq, cur), relation, &index)) {
		ret = -EINVAL;
		goto out;
	}
	new_freq = exynos_info[cur]->freq_table[index].frequency;

	/* frequency and volt scaling */
	ret = exynos_cpufreq_scale(VIRT_FREQ(new_freq, cur),
						freqs[cur]->old, policy->cpu);

#ifdef CONFIG_BL_SWITCHER
	if (do_switch && later)
		exynos_switch(policy, !cur);
#endif
out:
	mutex_unlock(&cpufreq_lock);

	return ret;
}

#ifdef CONFIG_PM
static int exynos_cpufreq_suspend(struct cpufreq_policy *policy)
{
	exynos5410_bb_con0 = __raw_readl(EXYNOS5410_BB_CON0);
	return 0;
}

static int exynos_cpufreq_resume(struct cpufreq_policy *policy)
{
	freqs[CA7]->old = VIRT_FREQ(get_boot_freq(CA7), CA7);
	freqs[CA15]->old = VIRT_FREQ(get_boot_freq(CA15), CA15);

	__raw_writel(exynos5410_bb_con0, EXYNOS5410_BB_CON0);
	return 0;
}
#endif

void exynos_lowpower_for_cluster(cluster_type cluster, bool on)
{
	int volt;

	mutex_lock(&cpufreq_lock);
	if (cluster == CA15) {
		if (on) {
			volt_powerdown[CA15] = regulator_get_voltage(arm_regulator);
			volt = get_match_volt(ID_ARM, ACTUAL_FREQ(freq_min[CA15], CA15));
			volt = get_limit_voltage(volt);
			regulator_set_voltage(arm_regulator, volt, volt);
		} else {
			volt = volt_powerdown[CA15];
			volt = get_limit_voltage(volt);
			regulator_set_voltage(arm_regulator, volt, volt);
		}
	} else {
		if (on) {
			volt_powerdown[CA7] = regulator_get_voltage(kfc_regulator);
			volt = get_match_volt(ID_KFC, ACTUAL_FREQ(freq_min[CA7], CA7));
			volt = get_limit_voltage(volt);
			regulator_set_voltage(kfc_regulator, volt, volt);
		} else {
			volt = volt_powerdown[CA7];
			volt = get_limit_voltage(volt);
			regulator_set_voltage(kfc_regulator, volt, volt);
		}
	}
	mutex_unlock(&cpufreq_lock);
}

/*
 * exynos_cpufreq_pm_notifier - block CPUFREQ's activities in suspend-resume
 *			context
 * @notifier
 * @pm_event
 * @v
 *
 * While cpufreq_disable == true, target() ignores every frequency but
 * boot_freq. The boot_freq value is the initial frequency,
 * which is set by the bootloader. In order to eliminate possible
 * inconsistency in clock values, we save and restore frequencies during
 * suspend and resume and block CPUFREQ activities. Note that the standard
 * suspend/resume cannot be used as they are too deep (syscore_ops) for
 * regulator actions.
 */
static int exynos_cpufreq_pm_notifier(struct notifier_block *notifier,
				       unsigned long pm_event, void *v)
{
	unsigned int freqCA7, freqCA15;
	unsigned int bootfreqCA7, bootfreqCA15;
	int volt;

	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		mutex_lock(&cpufreq_lock);
		exynos_info[CA7]->blocked = true;
		exynos_info[CA15]->blocked = true;
		mutex_unlock(&cpufreq_lock);

		bootfreqCA7 = VIRT_FREQ(get_boot_freq(CA7), CA7);
		bootfreqCA15 = VIRT_FREQ(get_boot_freq(CA15), CA15);

		freqCA7 = exynos_getspeed_cluster(CA7);
		freqCA15 = exynos_getspeed_cluster(CA15);

		volt = max(get_match_volt(ID_KFC, ACTUAL_FREQ(bootfreqCA7, CA7)),
				get_match_volt(ID_KFC, ACTUAL_FREQ(freqCA7, CA7)));
		volt = get_limit_voltage(volt);

		if (regulator_set_voltage(kfc_regulator, volt, volt))
			goto err;

		volt = max(get_match_volt(ID_ARM, ACTUAL_FREQ(bootfreqCA15, CA15)),
				get_match_volt(ID_ARM, ACTUAL_FREQ(freqCA15, CA15)));
		volt = get_limit_voltage(volt);

		if (regulator_set_voltage(arm_regulator, volt, volt))
			goto err;

		pr_debug("PM_SUSPEND_PREPARE for CPUFREQ\n");
		break;
	case PM_POST_SUSPEND:
		pr_debug("PM_POST_SUSPEND for CPUFREQ\n");

		mutex_lock(&cpufreq_lock);
		exynos_info[CA7]->blocked = false;
		exynos_info[CA15]->blocked = false;
		mutex_unlock(&cpufreq_lock);

		break;
	}
	return NOTIFY_OK;
err:
	pr_err("%s: failed to set voltage\n", __func__);

	return NOTIFY_BAD;
}

static struct notifier_block exynos_cpufreq_nb = {
	.notifier_call = exynos_cpufreq_pm_notifier,
};

static int exynos_cpufreq_tmu_notifier(struct notifier_block *notifier,
				       unsigned long event, void *v)
{
	int volt;
	int *on = v;

	if (event != TMU_COLD)
		return NOTIFY_OK;

	mutex_lock(&cpufreq_lock);
	if (*on)
		volt_offset = 75000;
	else
		volt_offset = 0;

	volt = get_limit_voltage(regulator_get_voltage(arm_regulator));
	regulator_set_voltage(arm_regulator, volt, volt);

	volt = get_limit_voltage(regulator_get_voltage(kfc_regulator));
	regulator_set_voltage(kfc_regulator, volt, volt);
	mutex_unlock(&cpufreq_lock);

	return NOTIFY_OK;
}

static struct notifier_block exynos_tmu_nb = {
	.notifier_call = exynos_cpufreq_tmu_notifier,
};
static int exynos_policy_notifier(struct notifier_block *nb,
		unsigned long val, void *data)
{
	struct cpufreq_policy *policy = data;
	unsigned int cpu = policy->cpu;

	if (val != CPUFREQ_ADJUST)
		return 0;

	if (!cpu_online(cpu))
		return -EINVAL;

	if (policy->max <= freq_max[CA7]) {
		fake_policy[CA7][cpu].max = ACTUAL_FREQ(policy->max, CA7);
		fake_policy[CA15][cpu].max = freq_max[CA15];
	} else {
		fake_policy[CA7][cpu].max = ACTUAL_FREQ(freq_max[CA7], CA7);
		fake_policy[CA15][cpu].max = policy->max;
	}

	if (policy->min <= freq_max[CA7]) {
		fake_policy[CA7][cpu].min = ACTUAL_FREQ(policy->min, CA7);
		fake_policy[CA15][cpu].min = freq_min[CA15];
	} else {
		fake_policy[CA7][cpu].min = ACTUAL_FREQ(freq_min[CA7], CA7);
		fake_policy[CA15][cpu].min = policy->min;
	}

	return 0;
}

static struct notifier_block notifier_policy_block = {
	.notifier_call = exynos_policy_notifier,
};

static int exynos_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	pr_debug("%s: cpu[%d]\n", __func__, policy->cpu);
	policy->cur = policy->min = policy->max = exynos_getspeed(policy->cpu);
	freqs[CA7]->old = exynos_getspeed_cluster(CA7);
	freqs[CA15]->old = exynos_getspeed_cluster(CA15);

	boot_freq = exynos_getspeed(policy->cpu);

	cpufreq_frequency_table_get_attr(
		merge_freq_table, policy->cpu);

	/* set the transition latency value */
	policy->cpuinfo.transition_latency = 100000;

	cpumask_clear(policy->cpus);
	cpumask_set_cpu(policy->cpu, policy->cpus);

	return cpufreq_frequency_table_cpuinfo(
		policy, merge_freq_table);
}

static struct cpufreq_driver exynos_driver = {
	.flags		= CPUFREQ_STICKY,
	.verify		= exynos_verify_speed,
	.target		= exynos_target,
	.get		= exynos_getspeed,
	.init		= exynos_cpufreq_cpu_init,
	.name		= "exynos_cpufreq",
#ifdef CONFIG_PM
	.suspend	= exynos_cpufreq_suspend,
	.resume		= exynos_cpufreq_resume,
#endif

};

/************************** sysfs interface ************************/

static ssize_t show_min_freq(struct kobject *kobj,
			     struct attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", (unsigned int)pm_qos_request(PM_QOS_CPU_FREQ_MIN));
}

static ssize_t show_max_freq(struct kobject *kobj,
			     struct attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", user_set_max_freq);
}

static ssize_t show_max_eagle_count(struct kobject *kobj,
				struct attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", user_set_eagle_count);
}

static ssize_t store_min_freq(struct kobject *kobj, struct attribute *attr,
			      const char *buf, size_t count)
{
	int cpu;
	unsigned int input;
	struct cpufreq_policy *policy;

	if (!sscanf(buf, "%u", &input))
		return -EINVAL;

	if (input > freq_max[CA15])
		input = freq_max[CA15];

	if (input <= freq_min[CA7]) {
		if (pm_qos_request_active(&min_cpu_qos))
			pm_qos_remove_request(&min_cpu_qos);
	} else {
		if (pm_qos_request_active(&min_cpu_qos))
			pm_qos_update_request(&min_cpu_qos, input);
		else
			pm_qos_add_request(&min_cpu_qos, PM_QOS_CPU_FREQ_MIN, input);
	}

	for_each_online_cpu(cpu) {
		policy = cpufreq_cpu_get(cpu);
		if (policy)
			exynos_target(policy, input, CPUFREQ_RELATION_L);
		cpufreq_cpu_put(policy);
	}

	return count;
}

static ssize_t store_max_freq(struct kobject *kobj, struct attribute *attr,
			      const char *buf, size_t count)
{
	int cpu;
	unsigned int input;
	struct cpufreq_policy *policy;

	if (!sscanf(buf, "%u", &input))
		return -EINVAL;

	if (input > freq_max[CA15] || input <= 0)
		input = freq_max[CA15];

	user_set_max_freq = input;

	for_each_online_cpu(cpu) {
		policy = cpufreq_cpu_get(cpu);
		if (policy) {
			policy->user_policy.max = input;
			cpufreq_update_policy(cpu);
		}
		cpufreq_cpu_put(policy);
	}

	return count;
}

static ssize_t store_max_eagle_count(struct kobject *kobj, struct attribute *attr,
				const char *buf, size_t count)
{
	unsigned int input;

	if (!sscanf(buf, "%u", &input))
		return -EINVAL;

	input = (input > CPU_MAX_COUNT) ? CPU_MAX_COUNT : input;
	input = (input < 0) ? 0 : input;

	user_set_eagle_count = input;

	return count;
}

define_one_global_rw(min_freq);
define_one_global_rw(max_freq);
define_one_global_rw(max_eagle_count);

static struct attribute *iks_attributes[] = {
	&min_freq.attr,
	&max_freq.attr,
	&max_eagle_count.attr,
	NULL
};

static struct attribute_group iks_attr_group = {
	.attrs = iks_attributes,
	.name = "iks-cpufreq",
};

/************************** sysfs end ************************/

static int exynos_cpufreq_reboot_notifier_call(struct notifier_block *this,
				   unsigned long code, void *_cmd)
{
	unsigned int freqCA7, freqCA15;
	unsigned int bootfreqCA7, bootfreqCA15;
	int volt;

	mutex_lock(&cpufreq_lock);
	exynos_info[CA7]->blocked = true;
	exynos_info[CA15]->blocked = true;
	mutex_unlock(&cpufreq_lock);

	bootfreqCA7 = VIRT_FREQ(get_boot_freq(CA7), CA7);
	bootfreqCA15 = VIRT_FREQ(get_boot_freq(CA15), CA15);

	freqCA7 = exynos_getspeed_cluster(CA7);
	freqCA15 = exynos_getspeed_cluster(CA15);

	volt = max(get_match_volt(ID_KFC, ACTUAL_FREQ(bootfreqCA7, CA7)),
			get_match_volt(ID_KFC, ACTUAL_FREQ(freqCA7, CA7)));
	volt = get_limit_voltage(volt);

	if (regulator_set_voltage(kfc_regulator, volt, volt))
		goto err;

	volt = max(get_match_volt(ID_ARM, ACTUAL_FREQ(bootfreqCA15, CA15)),
			get_match_volt(ID_ARM, ACTUAL_FREQ(freqCA15, CA15)));
	volt = get_limit_voltage(volt);

	if (regulator_set_voltage(arm_regulator, volt, volt))
		goto err;

	return NOTIFY_DONE;
err:
	pr_err("%s: failed to set voltage\n", __func__);

	return NOTIFY_BAD;
}

static struct notifier_block exynos_cpufreq_reboot_notifier = {
	.notifier_call = exynos_cpufreq_reboot_notifier_call,
};

static int exynos_qos_handler(struct notifier_block *b, unsigned long val, void *v)
{
	int ret;
	unsigned int freq;
	cluster_type cluster;
	struct cpufreq_policy *policy;
	int cpu;

	if (val > freq_max[CA7]) {
		freq = exynos_getspeed_cluster(CA15);
		cluster = CA15;
	} else {
		freq = exynos_getspeed_cluster(CA7);
		cluster = CA7;
	}

	if (freq >= val || (cluster == CA7 && cpumask_empty(&cluster_cpus[CA7])))
		return NOTIFY_OK;

	if (cluster == CA15 && cpumask_empty(&cluster_cpus[CA15]))
		cpu = 0;
	else
		cpu = cpumask_first(&cluster_cpus[cluster]);

	policy = cpufreq_cpu_get(cpu);

	if (!policy)
		return NOTIFY_BAD;

#if defined(CONFIG_CPU_FREQ_GOV_USERSPACE) || defined(CONFIG_CPU_FREQ_GOV_PERFORMANCE)
	if ((strcmp(policy->governor->name, "userspace") == 0)
			|| strcmp(policy->governor->name, "performance") == 0)
		return NOTIFY_OK;
#endif

	ret = cpufreq_driver_target(policy, val,
			CPUFREQ_RELATION_H);

	cpufreq_cpu_put(policy);

	if (ret < 0)
		return NOTIFY_BAD;

	return NOTIFY_OK;
}

static struct notifier_block exynos_qos_notifier = {
	.notifier_call = exynos_qos_handler,
};

static int __cpuinit exynos_hotplug_cpu_handler(struct notifier_block *nfb,
					unsigned long action, void *hcpu)
{
	unsigned int cpu;
	unsigned int wakeup_cpu = (unsigned long)hcpu, max_freq;
	struct cpufreq_policy *policy;
	bool change_max = false;

	switch (action) {
	case CPU_ONLINE:
		if (!policy_max_freq)
			return NOTIFY_OK;

		if (num_online_cpus() < NR_CPUS) {
			/*
			 * If policy.max of other cores are changed,
			 * set the wakeup core's policy.max to same value
			 */
			policy = cpufreq_cpu_get(wakeup_cpu);
			if (!policy)
				return NOTIFY_BAD;

			if (policy->max != policy_max_freq) {
				policy->user_policy.max = policy_max_freq;
				cpufreq_update_policy(wakeup_cpu);
			}
			cpufreq_cpu_put(policy);
		} else {
			/* All cores wake up, restore the policy.max to original */
			for_each_online_cpu(cpu) {
				policy = cpufreq_cpu_get(cpu);
				if (!policy)
					return NOTIFY_BAD;

				if (policy->max != policy_max_freq) {
					policy->user_policy.max = policy_max_freq;
					pr_info("IKS-CPUFREQ: Restore cpu%d max_freq [%d] -> [%d]\n",
								cpu, policy->max, policy_max_freq);

					cpufreq_update_policy(cpu);
				}
				cpufreq_cpu_put(policy);
			}
			policy_max_freq = 0;
		}
		break;
	case CPU_UP_PREPARE:
		if (per_cpu(cpu_cur_cluster, wakeup_cpu) == CA15) {
			policy = cpufreq_cpu_get(smp_processor_id());
			if (!policy)
				return NOTIFY_BAD;

			max_freq = exynos_info[CA15]->max_op_freqs[get_num_CA15() + 1];
			if (policy->max > max_freq) {
				change_max = true;
				if (!policy_max_freq)
					policy_max_freq = policy->max;
			}
			cpufreq_cpu_put(policy);

			if (change_max) {
				for_each_online_cpu(cpu) {
					policy = cpufreq_cpu_get(cpu);
					if (!policy)
						return NOTIFY_BAD;

					policy->user_policy.max = max_freq;
					pr_info("IKS-CPUFREQ: Change cpu%d max_freq [%d] -> [%d]\n",
							cpu, policy->max, max_freq);
					cpufreq_update_policy(cpu);
					cpufreq_cpu_put(policy);
				}
			}
		}
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata exynos_hotplug_cpu_notifier = {
	.notifier_call = exynos_hotplug_cpu_handler,
};

static int __init exynos_cpufreq_init(void)
{
	int ret = -EINVAL;
	int cpu;

	boot_cluster = 0;

	exynos_info[CA7] = kzalloc(sizeof(struct exynos_dvfs_info), GFP_KERNEL);
	if (!exynos_info[CA7]) {
		ret = -ENOMEM;
		goto err_alloc_info_CA7;
	}

	exynos_info[CA15] = kzalloc(sizeof(struct exynos_dvfs_info), GFP_KERNEL);
	if (!exynos_info[CA15]) {
		ret = -ENOMEM;
		goto err_alloc_info_CA15;
	}

	freqs[CA7] = kzalloc(sizeof(struct cpufreq_freqs), GFP_KERNEL);
	if (!freqs[CA7]) {
		ret = -ENOMEM;
		goto err_alloc_freqs_CA7;
	}

	freqs[CA15] = kzalloc(sizeof(struct cpufreq_freqs), GFP_KERNEL);
	if (!freqs[CA15]) {
		ret = -ENOMEM;
		goto err_alloc_freqs_CA15;
	}

	/* Get to boot_cluster_num - 0 for CA7; 1 for CA15 */
	boot_cluster = !(read_cpuid(CPUID_MPIDR) >> 8 & 0xf);
	pr_debug("%s: boot_cluster is %s\n", __func__,
					boot_cluster == CA7 ? "CA7" : "CA15");

	init_cpumask_cluster_set(boot_cluster);

	ret = exynos5410_cpufreq_CA7_init(&exynos_info_CA7);
	if (ret)
		goto err_init_cpufreq;

	ret = exynos5410_cpufreq_CA15_init(&exynos_info_CA15);
	if (ret)
		goto err_init_cpufreq;

	arm_regulator = regulator_get(NULL, "vdd_arm");
	if (IS_ERR(arm_regulator)) {
		pr_err("%s: failed to get resource vdd_arm\n", __func__);
		goto err_vdd_arm;
	}

	kfc_regulator = regulator_get(NULL, "vdd_kfc");
	if (IS_ERR(kfc_regulator)) {
		pr_err("%s:failed to get resource vdd_kfc\n", __func__);
		goto err_vdd_kfc;
	}

	memcpy(exynos_info[CA7], &exynos_info_CA7,
				sizeof(struct exynos_dvfs_info));
	exynos_info[CA7]->regulator = kfc_regulator;

	memcpy(exynos_info[CA15], &exynos_info_CA15,
				sizeof(struct exynos_dvfs_info));
	exynos_info[CA15]->regulator = arm_regulator;

	if (exynos_info[CA7]->set_freq == NULL) {
		pr_err("%s: No set_freq function (ERR)\n", __func__);
		goto err_set_freq;
	}

	freq_max[CA15] = exynos_info[CA15]->
		freq_table[exynos_info[CA15]->max_support_idx].frequency;
	freq_min[CA15] = exynos_info[CA15]->
		freq_table[exynos_info[CA15]->min_support_idx].frequency;
	freq_max[CA7] = VIRT_FREQ(exynos_info[CA7]->
		freq_table[exynos_info[CA7]->max_support_idx].frequency, CA7);
	freq_min[CA7] = VIRT_FREQ(exynos_info[CA7]->
		freq_table[exynos_info[CA7]->min_support_idx].frequency, CA7);

	cpufreq_merge_tables();

	set_boot_freq();

	register_pm_notifier(&exynos_cpufreq_nb);
	register_reboot_notifier(&exynos_cpufreq_reboot_notifier);
	exynos_tmu_add_notifier(&exynos_tmu_nb);
	pm_qos_add_notifier(PM_QOS_CPU_FREQ_MIN, &exynos_qos_notifier);

	for_each_cpu(cpu, cpu_possible_mask) {
		fake_policy[CA15][cpu].cpu = cpu;
		fake_policy[CA15][cpu].max = freq_max[CA15];
		fake_policy[CA15][cpu].min = freq_min[CA15];
		fake_policy[CA7][cpu].max = ACTUAL_FREQ(freq_max[CA7], CA7);
		fake_policy[CA7][cpu].min = ACTUAL_FREQ(freq_min[CA7], CA7);
	}

	cpufreq_register_notifier(&notifier_policy_block, CPUFREQ_POLICY_NOTIFIER);

	if (cpufreq_register_driver(&exynos_driver)) {
		pr_err("%s: failed to register cpufreq driver\n", __func__);
		goto err_cpufreq;
	}

	register_cpu_notifier(&exynos_hotplug_cpu_notifier);

	user_set_eagle_count = CPU_MAX_COUNT;
	user_set_max_freq = freq_max[CA15];
	pm_qos_add_request(&min_cpu_qos, PM_QOS_CPU_FREQ_MIN, freq_min[CA7]);

	ret = sysfs_create_group(cpufreq_global_kobject, &iks_attr_group);
	if (ret) {
		pr_err("%s: failed to create iks-cpufreq sysfs interface\n", __func__);
		goto err_cpufreq;
	}

	pm_qos_add_request(&boot_cpu_qos, PM_QOS_CPU_FREQ_MIN, 0);
	pm_qos_update_request_timeout(&boot_cpu_qos, 1200000, 20000 * 1000);

	exynos_cpufreq_init_done = true;

	return 0;

err_cpufreq:
	unregister_pm_notifier(&exynos_cpufreq_nb);
err_set_freq:
	regulator_put(kfc_regulator);
err_vdd_kfc:
	if (!IS_ERR(kfc_regulator))
		regulator_put(kfc_regulator);
err_vdd_arm:
	if (!IS_ERR(arm_regulator))
		regulator_put(arm_regulator);
err_init_cpufreq:
	kfree(freqs[CA15]);
err_alloc_freqs_CA15:
	kfree(freqs[CA7]);
err_alloc_freqs_CA7:
	kfree(exynos_info[CA15]);
err_alloc_info_CA15:
	kfree(exynos_info[CA7]);
err_alloc_info_CA7:
	pr_err("%s: failed initialization\n", __func__);

	return ret;
}

late_initcall(exynos_cpufreq_init);
