/*
 * Copyright (C) 2015 Fuzhou Rockchip Electronics Co., Ltd
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/kernel_stat.h>
#include <linux/init.h>
#include <linux/reboot.h>
#include <linux/suspend.h>
#include <linux/tick.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/string.h>
#ifdef CONFIG_ROCKCHIP_CPUQUIET
#include <linux/cpuquiet.h>
#include <linux/pm_qos.h>
#endif
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/dvfs.h>
#include <asm/smp_plat.h>
#include <asm/unistd.h>
#include <linux/uaccess.h>
#include <asm/system_misc.h>
#include <linux/cpu.h>
#include <linux/of.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/rockchip/common.h>
#include <dt-bindings/clock/rk_system_status.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include "../../../drivers/clk/rockchip/clk-pd.h"

#define VERSION "1.0"
#define MAX_CLUSTERS 2
#define B_CLUSTER	0
#define L_CLUSTER	1

#ifdef DEBUG
#define FREQ_DBG(fmt, args...) pr_debug(fmt, ## args)
#define FREQ_LOG(fmt, args...) pr_debug(fmt, ## args)
#else
#define FREQ_DBG(fmt, args...) do {} while (0)
#define FREQ_LOG(fmt, args...) do {} while (0)
#endif
#define FREQ_ERR(fmt, args...) pr_err(fmt, ## args)

static struct cpufreq_frequency_table *freq_table[MAX_CLUSTERS];
/*********************************************************/
/* additional symantics for "relation" in cpufreq with pm */
#define DISABLE_FURTHER_CPUFREQ         0x10
#define ENABLE_FURTHER_CPUFREQ          0x20
#define MASK_FURTHER_CPUFREQ            0x30
#define CPU_LOW_FREQ	600000    /* KHz */
#define CCI_LOW_RATE	288000000 /* Hz */
#define CCI_HIGH_RATE	576000000 /* Hz */
/* With 0x00(NOCHANGE), it depends on the previous "further" status */
#define CPUFREQ_PRIVATE                 0x100
static unsigned int no_cpufreq_access[MAX_CLUSTERS] = { 0 };
static unsigned int suspend_freq[MAX_CLUSTERS] = { 816 * 1000, 816 * 1000 };
static unsigned int suspend_volt = 1100000;
static unsigned int low_battery_freq[MAX_CLUSTERS] = { 600 * 1000,
	600 * 1000 };
static unsigned int low_battery_capacity = 5;
static bool is_booting = true;
static DEFINE_MUTEX(cpufreq_mutex);
static struct dvfs_node *clk_cpu_dvfs_node[MAX_CLUSTERS];
static struct dvfs_node *clk_gpu_dvfs_node;
static struct dvfs_node *clk_ddr_dvfs_node;
static cpumask_var_t cluster_policy_mask[MAX_CLUSTERS];
static struct clk *aclk_cci;
static unsigned long cci_rate;
static unsigned int cpu_bl_freq[MAX_CLUSTERS];

#ifdef CONFIG_ROCKCHIP_CPUQUIET
static void rockchip_bl_balanced_cpufreq_transition(unsigned int cluster,
						    unsigned int cpu_freq);
static struct cpuquiet_governor rockchip_bl_balanced_governor;
#endif

/*******************************************************/
static inline int cpu_to_cluster(int cpu)
{
	int id = topology_physical_package_id(cpu);
	if (id < 0)
		id = 0;
	return id;
}

static unsigned int rockchip_bl_cpufreq_get_rate(unsigned int cpu)
{
	u32 cur_cluster = cpu_to_cluster(cpu);

	if (clk_cpu_dvfs_node[cur_cluster])
		return clk_get_rate(clk_cpu_dvfs_node[cur_cluster]->clk) / 1000;

	return 0;
}

static bool cpufreq_is_ondemand(struct cpufreq_policy *policy)
{
	char c = 0;

	if (policy && policy->governor)
		c = policy->governor->name[0];
	return (c == 'o' || c == 'i' || c == 'c' || c == 'h');
}

static unsigned int get_freq_from_table(unsigned int max_freq,
					unsigned int cluster)
{
	unsigned int i;
	unsigned int target_freq = 0;

	for (i = 0; freq_table[cluster][i].frequency != CPUFREQ_TABLE_END;
	     i++) {
		unsigned int freq = freq_table[cluster][i].frequency;

		if (freq <= max_freq && target_freq < freq)
			target_freq = freq;
	}
	if (!target_freq)
		target_freq = max_freq;
	return target_freq;
}

static int rockchip_bl_cpufreq_notifier_policy(struct notifier_block *nb,
					       unsigned long val,
					       void *data)
{
	static unsigned int min_rate = 0, max_rate = -1;
	struct cpufreq_policy *policy = data;
	u32 cur_cluster = cpu_to_cluster(policy->cpu);

	if (val != CPUFREQ_ADJUST)
		return 0;

	if (cpufreq_is_ondemand(policy)) {
		FREQ_DBG("queue work\n");
		dvfs_clk_enable_limit(clk_cpu_dvfs_node[cur_cluster],
				      min_rate, max_rate);
	} else {
		FREQ_DBG("cancel work\n");
		dvfs_clk_get_limit(clk_cpu_dvfs_node[cur_cluster],
				   &min_rate, &max_rate);
	}

	return 0;
}

static struct notifier_block notifier_policy_block = {
	.notifier_call = rockchip_bl_cpufreq_notifier_policy
};

static int rockchip_bl_cpufreq_notifier_trans(struct notifier_block *nb,
					      unsigned long val, void *data)
{
	struct cpufreq_freqs *freq = data;
	unsigned int cluster = cpu_to_cluster(freq->cpu);
	int ret;

	cpu_bl_freq[cluster] = freq->new;

	switch (val) {
	case CPUFREQ_PRECHANGE:
		if (cpu_bl_freq[B_CLUSTER] > CPU_LOW_FREQ ||
		    cpu_bl_freq[L_CLUSTER] > CPU_LOW_FREQ) {
			if (cci_rate != CCI_HIGH_RATE) {
				ret = clk_set_rate(aclk_cci, CCI_HIGH_RATE);
				if (ret)
					break;
				pr_debug("ccirate %ld-->%d Hz\n",
					 cci_rate, CCI_HIGH_RATE);
				cci_rate = CCI_HIGH_RATE;
			}
		}
		break;
	case CPUFREQ_POSTCHANGE:
		if (cpu_bl_freq[B_CLUSTER] <= CPU_LOW_FREQ &&
		    cpu_bl_freq[L_CLUSTER] <= CPU_LOW_FREQ) {
			if (cci_rate != CCI_LOW_RATE) {
				ret = clk_set_rate(aclk_cci, CCI_LOW_RATE);
				if (ret)
					break;
				pr_debug("ccirate %ld-->%d Hz\n",
					 cci_rate, CCI_LOW_RATE);
				cci_rate = CCI_LOW_RATE;
			}
		}
		break;
	}

	return 0;
}

static struct notifier_block notifier_trans_block = {
	.notifier_call = rockchip_bl_cpufreq_notifier_trans,
};

static int rockchip_bl_cpufreq_verify(struct cpufreq_policy *policy)
{
	u32 cur_cluster = cpu_to_cluster(policy->cpu);

	if (!freq_table[cur_cluster])
		return -EINVAL;
	return cpufreq_frequency_table_verify(policy, freq_table[cur_cluster]);
}

static int clk_node_get_cluster_id(struct clk *clk)
{
	int i;

	for (i = 0; i < MAX_CLUSTERS; i++) {
		if (clk_cpu_dvfs_node[i]->clk == clk)
			return i;
	}
	return 0;
}

static int rockchip_bl_cpufreq_scale_rate_for_dvfs(struct clk *clk,
						   unsigned long rate)
{
	int ret;
	struct cpufreq_freqs freqs;
	struct cpufreq_policy *policy;
	u32 cur_cluster, cpu;

	cur_cluster = clk_node_get_cluster_id(clk);
	cpu = cpumask_first_and(cluster_policy_mask[cur_cluster],
		cpu_online_mask);
	if (cpu >= nr_cpu_ids)
		return -EINVAL;
	policy = cpufreq_cpu_get(cpu);
	if (!policy)
		return -EINVAL;

	freqs.new = rate / 1000;
	freqs.old = clk_get_rate(clk) / 1000;

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_PRECHANGE);

	FREQ_DBG("cpufreq_scale_rate_for_dvfs(%lu)\n", rate);

	ret = clk_set_rate(clk, rate);

	freqs.new = clk_get_rate(clk) / 1000;

#ifdef CONFIG_ROCKCHIP_CPUQUIET
	rockchip_bl_balanced_cpufreq_transition(cur_cluster, freqs.new);
#endif

	/* notifiers */
	cpufreq_notify_transition(policy, &freqs, CPUFREQ_POSTCHANGE);

	cpufreq_cpu_put(policy);
	return ret;
}

static int cluster_cpus_freq_dvfs_init(u32 cluster_id, char *dvfs_name)
{
	int v = INT_MAX;
	int i;

	clk_cpu_dvfs_node[cluster_id] = clk_get_dvfs_node(dvfs_name);

	if (!clk_cpu_dvfs_node[cluster_id]) {
		FREQ_ERR("%s:cluster_id=%d,get dvfs err\n",
			 __func__, cluster_id);
		return -EINVAL;
	}
	dvfs_clk_register_set_rate_callback(
		clk_cpu_dvfs_node[cluster_id],
		rockchip_bl_cpufreq_scale_rate_for_dvfs);
	freq_table[cluster_id] =
		dvfs_get_freq_volt_table(clk_cpu_dvfs_node[cluster_id]);
	if (!freq_table[cluster_id]) {
		FREQ_ERR("No freq table for cluster %d\n", cluster_id);
		return -EINVAL;
	}

	for (i = 0; freq_table[cluster_id][i].frequency != CPUFREQ_TABLE_END;
	     i++) {
		if (freq_table[cluster_id][i].index >= suspend_volt &&
		    v > freq_table[cluster_id][i].index) {
			suspend_freq[cluster_id] =
				freq_table[cluster_id][i].frequency;
			v = freq_table[cluster_id][i].index;
		}
	}
	low_battery_freq[cluster_id] =
		get_freq_from_table(low_battery_freq[cluster_id], cluster_id);
	clk_enable_dvfs(clk_cpu_dvfs_node[cluster_id]);
	return 0;
}

static int rockchip_bl_cpufreq_init_cpu0(struct cpufreq_policy *policy)
{
	clk_gpu_dvfs_node = clk_get_dvfs_node("clk_gpu");
	if (clk_gpu_dvfs_node)
		clk_enable_dvfs(clk_gpu_dvfs_node);

	clk_ddr_dvfs_node = clk_get_dvfs_node("clk_ddr");
	if (clk_ddr_dvfs_node)
		clk_enable_dvfs(clk_ddr_dvfs_node);

	cluster_cpus_freq_dvfs_init(B_CLUSTER, "clk_core_b");
	cluster_cpus_freq_dvfs_init(L_CLUSTER, "clk_core_l");

	cpufreq_register_notifier(&notifier_policy_block,
				  CPUFREQ_POLICY_NOTIFIER);

	aclk_cci = clk_get(NULL, "aclk_cci");
	if (!IS_ERR(aclk_cci)) {
		cci_rate = clk_get_rate(aclk_cci);
		if (clk_cpu_dvfs_node[L_CLUSTER])
			cpu_bl_freq[L_CLUSTER] =
			clk_get_rate(clk_cpu_dvfs_node[L_CLUSTER]->clk) / 1000;
		if (clk_cpu_dvfs_node[B_CLUSTER])
			cpu_bl_freq[B_CLUSTER] =
			clk_get_rate(clk_cpu_dvfs_node[B_CLUSTER]->clk) / 1000;
		cpufreq_register_notifier(&notifier_trans_block,
					  CPUFREQ_TRANSITION_NOTIFIER);
	}

	pr_info("version " VERSION ", suspend freq %d %d MHz\n",
		suspend_freq[0] / 1000, suspend_freq[1] / 1000);
	return 0;
}

static int rockchip_bl_cpufreq_init(struct cpufreq_policy *policy)
{
	static int cpu0_err;
	u32 cur_cluster = cpu_to_cluster(policy->cpu);

	if (policy->cpu == 0)
		cpu0_err = rockchip_bl_cpufreq_init_cpu0(policy);
	if (cpu0_err)
		return cpu0_err;

	/* set freq min max */
	cpufreq_frequency_table_cpuinfo(policy, freq_table[cur_cluster]);
	/* sys nod */
	cpufreq_frequency_table_get_attr(freq_table[cur_cluster], policy->cpu);

	if (cur_cluster < MAX_CLUSTERS) {
		cpumask_copy(policy->cpus, topology_core_cpumask(policy->cpu));
		cpumask_copy(cluster_policy_mask[cur_cluster],
			     topology_core_cpumask(policy->cpu));
	}

	policy->cur = clk_get_rate(clk_cpu_dvfs_node[cur_cluster]->clk) / 1000;

	/* make ondemand default sampling_rate to 40000 */
	policy->cpuinfo.transition_latency = 40 * NSEC_PER_USEC;

	return 0;
}

static int rockchip_bl_cpufreq_exit(struct cpufreq_policy *policy)
{
	u32 cur_cluster = cpu_to_cluster(policy->cpu);

	if (policy->cpu == 0) {
		cpufreq_unregister_notifier(&notifier_policy_block,
					    CPUFREQ_POLICY_NOTIFIER);
	}
	cpufreq_frequency_table_cpuinfo(policy, freq_table[cur_cluster]);
	clk_put_dvfs_node(clk_cpu_dvfs_node[cur_cluster]);

	return 0;
}

static struct freq_attr *rockchip_bl_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

#ifdef CONFIG_CHARGER_DISPLAY
extern int rk_get_system_battery_capacity(void);
#else
static int rk_get_system_battery_capacity(void)
{
	return 100;
}
#endif

static unsigned int
rockchip_bl_cpufreq_scale_limit(unsigned int target_freq,
				struct cpufreq_policy *policy, bool is_private)
{
	bool is_ondemand = cpufreq_is_ondemand(policy);
	u32 cur_cluster = cpu_to_cluster(policy->cpu);

	if (!is_ondemand)
		return target_freq;

	if (is_booting) {
		s64 boottime_ms = ktime_to_ms(ktime_get_boottime());

		if (boottime_ms > 60 * MSEC_PER_SEC) {
			is_booting = false;
		} else if (target_freq > low_battery_freq[cur_cluster] &&
			   rk_get_system_battery_capacity() <=
			   low_battery_capacity) {
			target_freq = low_battery_freq[cur_cluster];
		}
	}

	return target_freq;
}

static int rockchip_bl_cpufreq_target(struct cpufreq_policy *policy,
				      unsigned int target_freq,
				      unsigned int relation)
{
	unsigned int i, new_freq = target_freq, new_rate, cur_rate;
	int ret = 0;
	bool is_private;
	u32 cur_cluster = cpu_to_cluster(policy->cpu);

	if (!freq_table[cur_cluster]) {
		FREQ_ERR("no freq table!\n");
		return -EINVAL;
	}

	mutex_lock(&cpufreq_mutex);

	is_private = relation & CPUFREQ_PRIVATE;
	relation &= ~CPUFREQ_PRIVATE;

	if ((relation & ENABLE_FURTHER_CPUFREQ) &&
	    no_cpufreq_access[cur_cluster])
		no_cpufreq_access[cur_cluster]--;
	if (no_cpufreq_access[cur_cluster]) {
		FREQ_LOG("denied access to %s as it is disabled temporarily\n",
			 __func__);
		ret = -EINVAL;
		goto out;
	}
	if (relation & DISABLE_FURTHER_CPUFREQ)
		no_cpufreq_access[cur_cluster]++;
	relation &= ~MASK_FURTHER_CPUFREQ;

	ret = cpufreq_frequency_table_target(policy, freq_table[cur_cluster],
					     target_freq, relation, &i);
	if (ret) {
		FREQ_ERR("no freq match for %d(ret=%d)\n", target_freq, ret);
		goto out;
	}
	new_freq = freq_table[cur_cluster][i].frequency;
	if (!no_cpufreq_access[cur_cluster])
		new_freq =
		    rockchip_bl_cpufreq_scale_limit(new_freq, policy,
						    is_private);

	new_rate = new_freq * 1000;
	cur_rate = dvfs_clk_get_rate(clk_cpu_dvfs_node[cur_cluster]);
	FREQ_LOG("req = %7u new = %7u (was = %7u)\n", target_freq,
		 new_freq, cur_rate / 1000);
	if (new_rate == cur_rate)
		goto out;
	ret = dvfs_clk_set_rate(clk_cpu_dvfs_node[cur_cluster], new_rate);

out:
	FREQ_DBG("set freq (%7u) end, ret %d\n", new_freq, ret);
	mutex_unlock(&cpufreq_mutex);
	return ret;
}

static int rockchip_bl_cpufreq_pm_notifier_event(struct notifier_block *this,
						 unsigned long event, void *ptr)
{
	int ret = NOTIFY_DONE;
	int i;
	struct cpufreq_policy *policy;
	u32 cpu;

	for (i = 0; i < MAX_CLUSTERS; i++) {
		cpu = cpumask_first_and(cluster_policy_mask[i],
			cpu_online_mask);
		if (cpu >= nr_cpu_ids)
			continue;
		policy = cpufreq_cpu_get(cpu);
		if (!policy)
			continue;

		if (!cpufreq_is_ondemand(policy))
			goto out;

		switch (event) {
		case PM_SUSPEND_PREPARE:
			policy->cur++;
			ret = cpufreq_driver_target(policy, suspend_freq[i],
						    DISABLE_FURTHER_CPUFREQ |
						    CPUFREQ_RELATION_H);
			if (ret < 0) {
				ret = NOTIFY_BAD;
				goto out;
			}
			ret = NOTIFY_OK;
			break;
		case PM_POST_RESTORE:
		case PM_POST_SUSPEND:
			/* if (target_freq == policy->cur) then
			   cpufreq_driver_target will return, and
			   our target will not be called, it casue
			   ENABLE_FURTHER_CPUFREQ flag invalid,
			   avoid that. */
			policy->cur++;
			cpufreq_driver_target(policy, suspend_freq[i],
					      ENABLE_FURTHER_CPUFREQ |
					      CPUFREQ_RELATION_H);
			ret = NOTIFY_OK;
			break;
		}
out:
		cpufreq_cpu_put(policy);
	}

	return ret;
}

static struct notifier_block rockchip_bl_cpufreq_pm_notifier = {
	.notifier_call = rockchip_bl_cpufreq_pm_notifier_event,
};

static int rockchip_bl_cpufreq_reboot_limit_freq(void)
{
	struct regulator *regulator;
	int volt = 0;
	u32 rate;
	int i;

	dvfs_disable_temp_limit();

	for (i = 0; i < MAX_CLUSTERS; i++) {
		dvfs_clk_enable_limit(clk_cpu_dvfs_node[i],
				      1000 * suspend_freq[i],
				      1000 * suspend_freq[i]);
		rate = dvfs_clk_get_rate(clk_cpu_dvfs_node[i]);
	}

	regulator = dvfs_get_regulator("vdd_arm");
	if (regulator)
		volt = regulator_get_voltage(regulator);
	else
		pr_info("get arm regulator failed\n");
	pr_info("reboot set cluster0 rate=%lu, cluster1 rate=%lu, volt=%d\n",
		dvfs_clk_get_rate(clk_cpu_dvfs_node[0]),
		dvfs_clk_get_rate(clk_cpu_dvfs_node[1]), volt);

	return 0;
}

static int rockchip_bl_cpufreq_reboot_notifier_event(struct notifier_block
						     *this, unsigned long event,
						     void *ptr)
{
	rockchip_set_system_status(SYS_STATUS_REBOOT);
	rockchip_bl_cpufreq_reboot_limit_freq();

	return NOTIFY_OK;
};

static struct notifier_block rockchip_bl_cpufreq_reboot_notifier = {
	.notifier_call = rockchip_bl_cpufreq_reboot_notifier_event,
};

static struct cpufreq_driver rockchip_bl_cpufreq_driver = {
	.flags = CPUFREQ_CONST_LOOPS,
	.verify = rockchip_bl_cpufreq_verify,
	.target = rockchip_bl_cpufreq_target,
	.get = rockchip_bl_cpufreq_get_rate,
	.init = rockchip_bl_cpufreq_init,
	.exit = rockchip_bl_cpufreq_exit,
	.name = "rockchip-bl",
	.have_governor_per_policy = true,
	.attr = rockchip_bl_cpufreq_attr,
};

static const struct of_device_id rockchip_bl_cpufreq_match[] = {
	{
		.compatible = "rockchip,rk3368-cpufreq",
	},
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_bl_cpufreq_match);

static int __init rockchip_bl_cpufreq_probe(struct platform_device *pdev)
{
	int ret, i;

	for (i = 0; i < MAX_CLUSTERS; i++) {
		if (!alloc_cpumask_var(&cluster_policy_mask[i], GFP_KERNEL))
			return -ENOMEM;
	}

	register_reboot_notifier(&rockchip_bl_cpufreq_reboot_notifier);
	register_pm_notifier(&rockchip_bl_cpufreq_pm_notifier);

	ret = cpufreq_register_driver(&rockchip_bl_cpufreq_driver);

#ifdef CONFIG_ROCKCHIP_CPUQUIET
	ret = cpuquiet_register_governor(&rockchip_bl_balanced_governor);
#endif

	return ret;
}

static int rockchip_bl_cpufreq_remove(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < MAX_CLUSTERS; i++)
		free_cpumask_var(cluster_policy_mask[i]);
	cpufreq_unregister_driver(&rockchip_bl_cpufreq_driver);
	return 0;
}

static struct platform_driver rockchip_bl_cpufreq_platdrv = {
	.driver = {
		.name	= "rockchip-bl-cpufreq",
		.owner	= THIS_MODULE,
		.of_match_table = rockchip_bl_cpufreq_match,
	},
	.remove		= rockchip_bl_cpufreq_remove,
};

module_platform_driver_probe(rockchip_bl_cpufreq_platdrv,
			     rockchip_bl_cpufreq_probe);

MODULE_AUTHOR("Xiao Feng <xf@rock-chips.com>");
MODULE_LICENSE("GPL");

#ifdef CONFIG_ROCKCHIP_CPUQUIET
extern struct cpumask hmp_slow_cpu_mask;

enum cpu_speed_balance {
	CPU_SPEED_BALANCED,
	CPU_SPEED_BIASED,
	CPU_SPEED_SKEWED,
	CPU_SPEED_BOOST,
};

enum balanced_state {
	IDLE,
	DOWN,
	UP,
};

struct idle_info {
	u64 idle_last_us;
	u64 idle_current_us;
};

static u64 idleinfo_timestamp_us;
static u64 idleinfo_last_timestamp_us;
static DEFINE_PER_CPU(struct idle_info, idleinfo);
static DEFINE_PER_CPU(unsigned int, cpu_load);

static struct timer_list load_timer;
static bool load_timer_active;

/* configurable parameters */
static unsigned int  balance_level = 60;
static unsigned int  idle_bottom_freq[MAX_CLUSTERS];
static unsigned int  idle_top_freq[MAX_CLUSTERS];
static unsigned int  cpu_freq[MAX_CLUSTERS];
static unsigned long up_delay_jiffies;
static unsigned long down_delay_jiffies;
static unsigned long last_change_time_jiffies;
static unsigned int  load_sample_rate_jiffies = 20 / (MSEC_PER_SEC / HZ);
static unsigned int  little_high_load = 80;
static unsigned int  little_low_load = 20;
static unsigned int  big_low_load = 20;
static struct workqueue_struct *rockchip_bl_balanced_wq;
static struct delayed_work rockchip_bl_balanced_work;
static enum balanced_state rockchip_bl_balanced_state;
static struct kobject *rockchip_bl_balanced_kobj;
static DEFINE_MUTEX(rockchip_bl_balanced_lock);
static bool rockchip_bl_balanced_enable;

#define GOVERNOR_NAME "bl_balanced"

static u64 get_idle_us(int cpu)
{
	return get_cpu_idle_time(cpu, NULL, 1 /* io_busy */);
}

static void calculate_load_timer(unsigned long data)
{
	int i;
	u64 elapsed_time;

	if (!load_timer_active)
		return;

	idleinfo_last_timestamp_us = idleinfo_timestamp_us;
	idleinfo_timestamp_us = ktime_to_us(ktime_get());
	elapsed_time = idleinfo_timestamp_us - idleinfo_last_timestamp_us;

	for_each_present_cpu(i) {
		struct idle_info *iinfo = &per_cpu(idleinfo, i);
		unsigned int *load = &per_cpu(cpu_load, i);
		u64 idle_time;

		iinfo->idle_last_us = iinfo->idle_current_us;
		iinfo->idle_current_us = get_idle_us(i);

		idle_time = iinfo->idle_current_us - iinfo->idle_last_us;
		idle_time *= 100;
		do_div(idle_time, elapsed_time);
		if (idle_time > 100)
			idle_time = 100;
		*load = 100 - idle_time;
	}
	mod_timer(&load_timer, jiffies + load_sample_rate_jiffies);
}

static void start_load_timer(void)
{
	int i;

	if (load_timer_active)
		return;

	idleinfo_timestamp_us = ktime_to_us(ktime_get());
	for_each_present_cpu(i) {
		struct idle_info *iinfo = &per_cpu(idleinfo, i);

		iinfo->idle_current_us = get_idle_us(i);
	}
	mod_timer(&load_timer, jiffies + load_sample_rate_jiffies);

	load_timer_active = true;
}

static void stop_load_timer(void)
{
	if (!load_timer_active)
		return;

	load_timer_active = false;
	del_timer(&load_timer);
}

static unsigned int get_slowest_cpu(void)
{
	unsigned int cpu = nr_cpu_ids;
	unsigned long minload = ULONG_MAX;
	int i;

	for_each_online_cpu(i) {
		unsigned int load = per_cpu(cpu_load, i);

		if ((i > 0) && (minload >= load)) {
			cpu = i;
			minload = load;
		}
	}

	return cpu;
}

static unsigned int get_offline_big_cpu(void)
{
	struct cpumask big, offline_big;

	cpumask_andnot(&big, cpu_present_mask, &hmp_slow_cpu_mask);
	cpumask_andnot(&offline_big, &big, cpu_online_mask);
	return cpumask_first(&offline_big);
}

static unsigned int cpu_highest_speed(void)
{
	unsigned int maxload = 0;
	int i;

	for_each_online_cpu(i) {
		unsigned int load = per_cpu(cpu_load, i);

		maxload = max(maxload, load);
	}

	return maxload;
}

static unsigned int count_slow_cpus(unsigned int limit)
{
	unsigned int cnt = 0;
	int i;

	for_each_online_cpu(i) {
		unsigned int load = per_cpu(cpu_load, i);

		if (load <= limit)
			cnt++;
	}

	return cnt;
}

#define NR_FSHIFT	2

static unsigned int rt_profile[NR_CPUS] = {
/*      1,  2,  3,  4,  5,  6,  7,  8 - on-line cpus target */
	5,  9, 10, 11, 12, 13, 14,  UINT_MAX
};

static unsigned int nr_run_hysteresis = 2;	/* 0.5 thread */
static unsigned int nr_run_last;

struct runnables_avg_sample {
	u64 previous_integral;
	unsigned int avg;
	bool integral_sampled;
	u64 prev_timestamp;	/* ns */
};

static DEFINE_PER_CPU(struct runnables_avg_sample, avg_nr_sample);

static unsigned int get_avg_nr_runnables(void)
{
	unsigned int i, sum = 0;
	struct runnables_avg_sample *sample;
	u64 integral, old_integral, delta_integral, delta_time, cur_time;

	cur_time = ktime_to_ns(ktime_get());

	for_each_online_cpu(i) {
		sample = &per_cpu(avg_nr_sample, i);
		integral = nr_running_integral(i);
		old_integral = sample->previous_integral;
		sample->previous_integral = integral;
		delta_time = cur_time - sample->prev_timestamp;
		sample->prev_timestamp = cur_time;

		if (!sample->integral_sampled) {
			sample->integral_sampled = true;
			/* First sample to initialize prev_integral, skip
			 * avg calculation
			 */
			continue;
		}

		if (integral < old_integral) {
			/* Overflow */
			delta_integral = (ULLONG_MAX - old_integral) + integral;
		} else {
			delta_integral = integral - old_integral;
		}

		/* Calculate average for the previous sample window */
		do_div(delta_integral, delta_time);
		sample->avg = delta_integral;
		sum += sample->avg;
	}

	return sum;
}

static bool rockchip_bl_balanced_speed_boost(void)
{
	unsigned int cpu;
	struct cpumask online_little;
	unsigned int big_cpu;
	bool has_low_load_little_cpu = false;

	if (cpu_freq[L_CLUSTER] < idle_top_freq[L_CLUSTER])
		return false;

	cpumask_and(&online_little, cpu_online_mask, &hmp_slow_cpu_mask);

	for_each_cpu(cpu, &online_little) {
		if (per_cpu(cpu_load, cpu) < little_low_load) {
			has_low_load_little_cpu = true;
			break;
		}
	}

	for_each_cpu(cpu, &online_little) {
		unsigned int load;
		unsigned int avg;
		struct cpumask online_big;
		bool has_low_load_big_cpu;

		load = per_cpu(cpu_load, cpu);
		/* skip low load cpu */
		if (load < little_high_load)
			continue;

		avg = per_cpu(avg_nr_sample, cpu).avg;
		/*
		 * skip when we have low load cpu,
		 * when cpu load is high because run many task.
		 * we can migrate the task to low load cpu
		 */
		if (has_low_load_little_cpu &&
		    (avg >> (FSHIFT - NR_FSHIFT)) >= 4)
			continue;

		/*
		 * found one cpu which is busy by run one thread,
		 * break if no big cpu offline
		 */
		if (get_offline_big_cpu() >= nr_cpu_ids)
			break;

		cpumask_andnot(&online_big,
			       cpu_online_mask, &hmp_slow_cpu_mask);

		has_low_load_big_cpu = false;
		for_each_cpu(big_cpu, &online_big) {
			unsigned int big_load;

			big_load = per_cpu(cpu_load, big_cpu);
			if (big_load < big_low_load) {
				has_low_load_big_cpu = true;
				break;
			}
		}
		/* if we have idle big cpu, never up new one */
		if (has_low_load_big_cpu)
			break;

		return true;
	}

	return false;
}

static enum cpu_speed_balance rockchip_bl_balanced_speed_balance(void)
{
	unsigned long highest_speed = cpu_highest_speed();
	unsigned long balanced_speed = highest_speed * balance_level / 100;
	unsigned long skewed_speed = balanced_speed / 2;
	unsigned int nr_cpus = num_online_cpus();
	unsigned int max_cpus = pm_qos_request(PM_QOS_MAX_ONLINE_CPUS);
	unsigned int min_cpus = pm_qos_request(PM_QOS_MIN_ONLINE_CPUS);
	unsigned int avg_nr_run = get_avg_nr_runnables();
	unsigned int nr_run;

	if (max_cpus > nr_cpu_ids || max_cpus == 0)
		max_cpus = nr_cpu_ids;

	if (rockchip_bl_balanced_speed_boost())
		return CPU_SPEED_BOOST;

	/* balanced: freq targets for all CPUs are above 60% of highest speed
	   biased: freq target for at least one CPU is below 60% threshold
	   skewed: freq targets for at least 2 CPUs are below 30% threshold */
	for (nr_run = 1; nr_run < ARRAY_SIZE(rt_profile); nr_run++) {
		unsigned int nr_threshold = rt_profile[nr_run - 1];

		if (nr_run_last <= nr_run)
			nr_threshold += nr_run_hysteresis;
		if (avg_nr_run <= (nr_threshold << (FSHIFT - NR_FSHIFT)))
			break;
	}
	nr_run_last = nr_run;

	if ((count_slow_cpus(skewed_speed) >= 2 ||
	     nr_run < nr_cpus ||
	     (cpu_freq[B_CLUSTER] <= idle_bottom_freq[B_CLUSTER] &&
	      cpu_freq[L_CLUSTER] <= idle_bottom_freq[L_CLUSTER]) ||
	     nr_cpus > max_cpus) &&
	    nr_cpus > min_cpus)
		return CPU_SPEED_SKEWED;

	if ((count_slow_cpus(balanced_speed) >= 1 ||
	     nr_run <= nr_cpus ||
	     (cpu_freq[B_CLUSTER] <= idle_bottom_freq[B_CLUSTER] &&
	      cpu_freq[L_CLUSTER] <= idle_bottom_freq[L_CLUSTER]) ||
	     nr_cpus == max_cpus) &&
	    nr_cpus >= min_cpus)
		return CPU_SPEED_BIASED;

	return CPU_SPEED_BALANCED;
}

static void rockchip_bl_balanced_work_func(struct work_struct *work)
{
	bool up = false;
	unsigned int cpu = nr_cpu_ids;
	unsigned long now = jiffies;
	struct workqueue_struct *wq = rockchip_bl_balanced_wq;
	struct delayed_work *dwork = to_delayed_work(work);
	enum cpu_speed_balance balance;

	mutex_lock(&rockchip_bl_balanced_lock);

	if (!rockchip_bl_balanced_enable)
		goto out;

	switch (rockchip_bl_balanced_state) {
	case IDLE:
		break;
	case DOWN:
		cpu = get_slowest_cpu();
		if (cpu < nr_cpu_ids) {
			up = false;
			queue_delayed_work(wq, dwork, up_delay_jiffies);
		} else {
			stop_load_timer();
		}
		break;
	case UP:
		balance = rockchip_bl_balanced_speed_balance();
		switch (balance) {
		case CPU_SPEED_BOOST:
			cpu = get_offline_big_cpu();
			if (cpu < nr_cpu_ids)
				up = true;
			break;
		/* cpu speed is up and balanced - one more on-line */
		case CPU_SPEED_BALANCED:
			cpu = cpumask_next_zero(0, cpu_online_mask);
			if (cpu < nr_cpu_ids)
				up = true;
			break;
		/* cpu speed is up, but skewed - remove one core */
		case CPU_SPEED_SKEWED:
			cpu = get_slowest_cpu();
			if (cpu < nr_cpu_ids)
				up = false;
			break;
		/* cpu speed is up, but under-utilized - do nothing */
		case CPU_SPEED_BIASED:
		default:
			break;
		}
		queue_delayed_work(wq, dwork, up_delay_jiffies);
		break;
	default:
		pr_err("%s: invalid cpuquiet governor state %d\n",
		       __func__, rockchip_bl_balanced_state);
	}

	if (!up && ((now - last_change_time_jiffies) < down_delay_jiffies))
		cpu = nr_cpu_ids;

	if (cpu < nr_cpu_ids) {
		last_change_time_jiffies = now;
		if (up)
			cpuquiet_wake_cpu(cpu, false);
		else
			cpuquiet_quiesence_cpu(cpu, false);
	}

out:
	mutex_unlock(&rockchip_bl_balanced_lock);
}

static void rockchip_bl_balanced_cpufreq_transition(unsigned int cluster,
						    unsigned int new_cpu_freq)
{
	struct workqueue_struct *wq;
	struct delayed_work *dwork;

	mutex_lock(&rockchip_bl_balanced_lock);

	if (!rockchip_bl_balanced_enable)
		goto out;

	wq = rockchip_bl_balanced_wq;
	dwork = &rockchip_bl_balanced_work;
	cpu_freq[cluster] = new_cpu_freq;

	switch (rockchip_bl_balanced_state) {
	case IDLE:
		if (cpu_freq[B_CLUSTER] >= idle_top_freq[B_CLUSTER] ||
		    cpu_freq[L_CLUSTER] >= idle_top_freq[L_CLUSTER]) {
			rockchip_bl_balanced_state = UP;
			queue_delayed_work(wq, dwork, up_delay_jiffies);
			start_load_timer();
		} else if (cpu_freq[B_CLUSTER] <= idle_bottom_freq[B_CLUSTER] &&
			   cpu_freq[L_CLUSTER] <= idle_bottom_freq[L_CLUSTER]) {
			rockchip_bl_balanced_state = DOWN;
			queue_delayed_work(wq, dwork, down_delay_jiffies);
			start_load_timer();
		}
		break;
	case DOWN:
		if (cpu_freq[B_CLUSTER] >= idle_top_freq[B_CLUSTER] ||
		    cpu_freq[L_CLUSTER] >= idle_top_freq[L_CLUSTER]) {
			rockchip_bl_balanced_state = UP;
			queue_delayed_work(wq, dwork, up_delay_jiffies);
			start_load_timer();
		}
		break;
	case UP:
		if (cpu_freq[B_CLUSTER] <= idle_bottom_freq[B_CLUSTER] &&
		    cpu_freq[L_CLUSTER] <= idle_bottom_freq[L_CLUSTER]) {
			rockchip_bl_balanced_state = DOWN;
			queue_delayed_work(wq, dwork, up_delay_jiffies);
			start_load_timer();
		}
		break;
	default:
		pr_err("%s: invalid cpuquiet governor state %d\n",
		       __func__, rockchip_bl_balanced_state);
	}

out:
	mutex_unlock(&rockchip_bl_balanced_lock);
}

static void delay_callback(struct cpuquiet_attribute *attr)
{
	unsigned long val;

	if (attr) {
		val = (*((unsigned long *)(attr->param)));
		(*((unsigned long *)(attr->param))) = msecs_to_jiffies(val);
	}
}

#define CPQ_BASIC_ATTRIBUTE_B(_name, _mode, _type) \
	static struct cpuquiet_attribute _name ## _b_attr = {		\
		.attr = {.name = __stringify(_name ## _b), .mode = _mode },\
		.show = show_ ## _type ## _attribute,			\
		.store = store_ ## _type ## _attribute,			\
		.param = &_name[B_CLUSTER],				\
}
#define CPQ_BASIC_ATTRIBUTE_L(_name, _mode, _type) \
	static struct cpuquiet_attribute _name ## _l_attr = {		\
		.attr = {.name = __stringify(_name ## _l), .mode = _mode },\
		.show = show_ ## _type ## _attribute,			\
		.store = store_ ## _type ## _attribute,			\
		.param = &_name[L_CLUSTER],				\
}
CPQ_BASIC_ATTRIBUTE(balance_level, 0644, uint);
CPQ_BASIC_ATTRIBUTE_B(idle_bottom_freq, 0644, uint);
CPQ_BASIC_ATTRIBUTE_L(idle_bottom_freq, 0644, uint);
CPQ_BASIC_ATTRIBUTE_B(idle_top_freq, 0644, uint);
CPQ_BASIC_ATTRIBUTE_L(idle_top_freq, 0644, uint);
CPQ_BASIC_ATTRIBUTE(load_sample_rate_jiffies, 0644, uint);
CPQ_BASIC_ATTRIBUTE(nr_run_hysteresis, 0644, uint);
CPQ_BASIC_ATTRIBUTE(little_high_load, 0644, uint);
CPQ_BASIC_ATTRIBUTE(little_low_load, 0644, uint);
CPQ_BASIC_ATTRIBUTE(big_low_load, 0644, uint);
CPQ_ATTRIBUTE(up_delay_jiffies, 0644, ulong, delay_callback);
CPQ_ATTRIBUTE(down_delay_jiffies, 0644, ulong, delay_callback);

#define MAX_BYTES 100

static ssize_t show_rt_profile(struct cpuquiet_attribute *attr, char *buf)
{
	char buffer[MAX_BYTES];
	unsigned int i;
	int size = 0;

	buffer[0] = 0;
	for (i = 0; i < ARRAY_SIZE(rt_profile); i++) {
		size += snprintf(buffer + size, sizeof(buffer) - size,
				"%u ", rt_profile[i]);
	}
	return snprintf(buf, sizeof(buffer), "%s\n", buffer);
}

static ssize_t store_rt_profile(struct cpuquiet_attribute *attr,
				const char *buf, size_t count)
{
	int ret, i = 0;
	char *val, *str, input[MAX_BYTES];
	unsigned int profile[ARRAY_SIZE(rt_profile)];

	if (!count || count >= MAX_BYTES)
		return -EINVAL;
	strncpy(input, buf, count);
	input[count] = '\0';
	str = input;
	memcpy(profile, rt_profile, sizeof(rt_profile));
	while ((val = strsep(&str, " ")) != NULL) {
		if (*val == '\0')
			continue;
		if (i == ARRAY_SIZE(rt_profile) - 1)
			break;
		ret = kstrtouint(val, 10, &profile[i]);
		if (ret)
			return -EINVAL;
		i++;
	}

	memcpy(rt_profile, profile, sizeof(profile));

	return count;
}
CPQ_ATTRIBUTE_CUSTOM(rt_profile, 0644,
		     show_rt_profile, store_rt_profile);

static struct attribute *rockchip_bl_balanced_attributes[] = {
	&balance_level_attr.attr,
	&idle_bottom_freq_b_attr.attr,
	&idle_bottom_freq_l_attr.attr,
	&idle_top_freq_b_attr.attr,
	&idle_top_freq_l_attr.attr,
	&up_delay_jiffies_attr.attr,
	&down_delay_jiffies_attr.attr,
	&load_sample_rate_jiffies_attr.attr,
	&nr_run_hysteresis_attr.attr,
	&rt_profile_attr.attr,
	&little_high_load_attr.attr,
	&little_low_load_attr.attr,
	&big_low_load_attr.attr,
	NULL,
};

static const struct sysfs_ops rockchip_bl_balanced_sysfs_ops = {
	.show = cpuquiet_auto_sysfs_show,
	.store = cpuquiet_auto_sysfs_store,
};

static struct kobj_type rockchip_bl_balanced_ktype = {
	.sysfs_ops = &rockchip_bl_balanced_sysfs_ops,
	.default_attrs = rockchip_bl_balanced_attributes,
};

static int rockchip_bl_balanced_sysfs(void)
{
	int err;
	struct kobject *kobj;

	kobj = kzalloc(sizeof(*kobj), GFP_KERNEL);

	if (!kobj)
		return -ENOMEM;

	err = cpuquiet_kobject_init(kobj, &rockchip_bl_balanced_ktype,
				    GOVERNOR_NAME);

	if (err)
		kfree(kobj);

	rockchip_bl_balanced_kobj = kobj;

	return err;
}

static void rockchip_bl_balanced_stop(void)
{
	mutex_lock(&rockchip_bl_balanced_lock);

	rockchip_bl_balanced_enable = false;
	/* now we can force the governor to be idle */
	rockchip_bl_balanced_state = IDLE;

	mutex_unlock(&rockchip_bl_balanced_lock);

	cancel_delayed_work_sync(&rockchip_bl_balanced_work);

	destroy_workqueue(rockchip_bl_balanced_wq);
	rockchip_bl_balanced_wq = NULL;
	del_timer_sync(&load_timer);

	kobject_put(rockchip_bl_balanced_kobj);
	kfree(rockchip_bl_balanced_kobj);
	rockchip_bl_balanced_kobj = NULL;
}

static int rockchip_bl_balanced_start(void)
{
	int err, count, cluster;
	struct cpufreq_frequency_table *table;
	unsigned int initial_freq;

	err = rockchip_bl_balanced_sysfs();
	if (err)
		return err;

	up_delay_jiffies = msecs_to_jiffies(100);
	down_delay_jiffies = msecs_to_jiffies(2000);

	for (cluster = 0; cluster < MAX_CLUSTERS; cluster++) {
		table = freq_table[cluster];
		if (!table)
			return -EINVAL;

		for (count = 0; table[count].frequency != CPUFREQ_TABLE_END;
		     count++)
			;

		if (count < 4)
			return -EINVAL;

		idle_top_freq[cluster] = table[(count / 2) - 1].frequency;
		idle_bottom_freq[cluster] = table[(count / 2) - 2].frequency;
	}

	rockchip_bl_balanced_wq
		= alloc_workqueue(GOVERNOR_NAME, WQ_UNBOUND | WQ_FREEZABLE, 1);
	if (!rockchip_bl_balanced_wq)
		return -ENOMEM;

	INIT_DELAYED_WORK(&rockchip_bl_balanced_work,
			  rockchip_bl_balanced_work_func);

	init_timer(&load_timer);
	load_timer.function = calculate_load_timer;

	mutex_lock(&rockchip_bl_balanced_lock);
	rockchip_bl_balanced_enable = true;
	if (clk_cpu_dvfs_node[L_CLUSTER])
		cpu_freq[L_CLUSTER] =
			clk_get_rate(clk_cpu_dvfs_node[L_CLUSTER]->clk) / 1000;
	if (clk_cpu_dvfs_node[B_CLUSTER])
		cpu_freq[B_CLUSTER] =
			clk_get_rate(clk_cpu_dvfs_node[B_CLUSTER]->clk) / 1000;
	mutex_unlock(&rockchip_bl_balanced_lock);

	/* Kick start the state machine */
	initial_freq = cpufreq_get(0);
	if (initial_freq)
		rockchip_bl_balanced_cpufreq_transition(L_CLUSTER,
							initial_freq);

	return 0;
}

static struct cpuquiet_governor rockchip_bl_balanced_governor = {
	.name		= GOVERNOR_NAME,
	.start		= rockchip_bl_balanced_start,
	.stop		= rockchip_bl_balanced_stop,
	.owner		= THIS_MODULE,
};
#endif
