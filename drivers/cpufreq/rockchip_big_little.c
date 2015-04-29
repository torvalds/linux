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

#define RK3368_GRF_CPU_CON(n) (0x500 + 4*n)

#define VERSION "1.0"
#define RK_MAX_CLUSTERS 2

#ifdef DEBUG
#define FREQ_DBG(fmt, args...) pr_debug(fmt, ## args)
#define FREQ_LOG(fmt, args...) pr_debug(fmt, ## args)
#else
#define FREQ_DBG(fmt, args...) do {} while (0)
#define FREQ_LOG(fmt, args...) do {} while (0)
#endif
#define FREQ_ERR(fmt, args...) pr_err(fmt, ## args)

/* Frequency table index must be sequential starting at 0 */
static struct cpufreq_frequency_table default_freq_table[] = {
	{.frequency = 312 * 1000,       .index = 875 * 1000},
	{.frequency = 504 * 1000,       .index = 925 * 1000},
	{.frequency = 816 * 1000,       .index = 975 * 1000},
	{.frequency = 1008 * 1000,      .index = 1075 * 1000},
	{.frequency = 1200 * 1000,      .index = 1150 * 1000},
	{.frequency = 1416 * 1000,      .index = 1250 * 1000},
	{.frequency = 1608 * 1000,      .index = 1350 * 1000},
	{.frequency = CPUFREQ_TABLE_END},
};

static struct cpufreq_frequency_table *freq_table[RK_MAX_CLUSTERS + 1];
/*********************************************************/
/* additional symantics for "relation" in cpufreq with pm */
#define DISABLE_FURTHER_CPUFREQ         0x10
#define ENABLE_FURTHER_CPUFREQ          0x20
#define MASK_FURTHER_CPUFREQ            0x30
/* With 0x00(NOCHANGE), it depends on the previous "further" status */
#define CPUFREQ_PRIVATE                 0x100
static unsigned int no_cpufreq_access[RK_MAX_CLUSTERS] = { 0 };
static unsigned int suspend_freq[RK_MAX_CLUSTERS] = { 816 * 1000, 816 * 1000 };
static unsigned int suspend_volt = 1100000;
static unsigned int low_battery_freq[RK_MAX_CLUSTERS] = { 600 * 1000,
	600 * 1000 };
static unsigned int low_battery_capacity = 5;
static bool is_booting = true;
static DEFINE_MUTEX(cpufreq_mutex);
static struct dvfs_node *clk_cpu_dvfs_node[RK_MAX_CLUSTERS];
static struct dvfs_node *clk_gpu_dvfs_node;
static struct dvfs_node *clk_ddr_dvfs_node;
static u32 cluster_policy_cpu[RK_MAX_CLUSTERS];
static unsigned int big_little = 1;

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

	for (i = 0; i < RK_MAX_CLUSTERS; i++) {
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
	u32 cur_cluster;

	cur_cluster = clk_node_get_cluster_id(clk);
	policy = cpufreq_cpu_get(cluster_policy_cpu[cur_cluster]);
	if (!policy)
		return 0;

	freqs.new = rate / 1000;
	freqs.old = clk_get_rate(clk) / 1000;

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_PRECHANGE);

	FREQ_DBG("cpufreq_scale_rate_for_dvfs(%lu)\n", rate);

	ret = clk_set_rate(clk, rate);

	freqs.new = clk_get_rate(clk) / 1000;
	/* notifiers */
	cpufreq_notify_transition(policy, &freqs, CPUFREQ_POSTCHANGE);

	cpufreq_cpu_put(policy);
	return ret;
}

static int cluster_cpus_freq_dvfs_init(u32 cluster_id, char *dvfs_name)
{
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
	if (freq_table[cluster_id] == NULL) {
		freq_table[cluster_id] = default_freq_table;
	} else {
		int v = INT_MAX;
		int i;

		for (i = 0; freq_table[cluster_id][i].frequency !=
		     CPUFREQ_TABLE_END; i++) {
			if (freq_table[cluster_id][i].index >= suspend_volt &&
			    v > freq_table[cluster_id][i].index) {
				suspend_freq[cluster_id] =
					freq_table[cluster_id][i].frequency;
				v = freq_table[cluster_id][i].index;
			}
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

	cluster_cpus_freq_dvfs_init(0, "clk_core_b");
	cluster_cpus_freq_dvfs_init(1, "clk_core_l");

	cpufreq_register_notifier(&notifier_policy_block,
				  CPUFREQ_POLICY_NOTIFIER);

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

	cluster_policy_cpu[cur_cluster] = policy->cpu;

	/* set freq min max */
	cpufreq_frequency_table_cpuinfo(policy, freq_table[cur_cluster]);
	/* sys nod */
	cpufreq_frequency_table_get_attr(freq_table[cur_cluster], policy->cpu);

	if (cur_cluster < RK_MAX_CLUSTERS)
		cpumask_copy(policy->cpus, topology_core_cpumask(policy->cpu));

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

	for (i = 0; i < RK_MAX_CLUSTERS; i++) {
		struct cpufreq_policy *policy =
			cpufreq_cpu_get(cluster_policy_cpu[i]);

		if (!policy)
			return ret;

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

	for (i = 0; i < RK_MAX_CLUSTERS; i++) {
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

static int rockchip_bl_cpufreq_probe(struct platform_device *pdev)
{
	struct device_node *np;
	struct regmap *grf_regmap;
	unsigned int big_bits, litt_bits;
	int ret;

	np = pdev->dev.of_node;
	if (!np)
		return -ENODEV;

	grf_regmap = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(grf_regmap)) {
		FREQ_ERR("Cpufreq couldn't find grf regmap\n");
		return PTR_ERR(grf_regmap);
	}
	ret = regmap_read(grf_regmap, RK3368_GRF_CPU_CON(3), &big_bits);
	if (ret != 0) {
		FREQ_ERR("Cpufreq couldn't read to GRF\n");
		return -1;
	}
	ret = regmap_read(grf_regmap, RK3368_GRF_CPU_CON(1), &litt_bits);
	if (ret != 0) {
		FREQ_ERR("Cpufreq couldn't read to GRF\n");
		return -1;
	}

	big_bits = (big_bits >> 8) & 0x03;
	litt_bits = (litt_bits >> 8) & 0x03;

	if (big_bits == 0x01 && litt_bits == 0x00)
		big_little = 1;
	else if (big_bits == 0x0 && litt_bits == 0x01)
		big_little = 0;
	pr_info("boot from %d\n", big_little);

	register_reboot_notifier(&rockchip_bl_cpufreq_reboot_notifier);
	register_pm_notifier(&rockchip_bl_cpufreq_pm_notifier);

	return cpufreq_register_driver(&rockchip_bl_cpufreq_driver);
}

static int rockchip_bl_cpufreq_remove(struct platform_device *pdev)
{
	cpufreq_unregister_driver(&rockchip_bl_cpufreq_driver);
	return 0;
}

static struct platform_driver rockchip_bl_cpufreq_platdrv = {
	.driver = {
		.name	= "rockchip-bl-cpufreq",
		.owner	= THIS_MODULE,
		.of_match_table = rockchip_bl_cpufreq_match,
	},
	.probe		= rockchip_bl_cpufreq_probe,
	.remove		= rockchip_bl_cpufreq_remove,
};

module_platform_driver(rockchip_bl_cpufreq_platdrv);

MODULE_AUTHOR("Xiao Feng <xf@rock-chips.com>");
MODULE_LICENSE("GPL");
