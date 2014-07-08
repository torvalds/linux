/*
 * Copyright (C) 2013 ROCKCHIP, Inc.
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
#define pr_fmt(fmt) "cpufreq: " fmt
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
#include <asm/cpu.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
#include <asm/system_misc.h>
#include "../../../drivers/clk/rockchip/clk-pd.h"

extern void dvfs_disable_temp_limit(void);

#define VERSION "1.0"

#ifdef DEBUG
#define FREQ_DBG(fmt, args...) pr_debug(fmt, ## args)
#define FREQ_LOG(fmt, args...) pr_debug(fmt, ## args)
#else
#define FREQ_DBG(fmt, args...) do {} while(0)
#define FREQ_LOG(fmt, args...) do {} while(0)
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
static struct cpufreq_frequency_table *freq_table = default_freq_table;
/*********************************************************/
/* additional symantics for "relation" in cpufreq with pm */
#define DISABLE_FURTHER_CPUFREQ         0x10
#define ENABLE_FURTHER_CPUFREQ          0x20
#define MASK_FURTHER_CPUFREQ            0x30
/* With 0x00(NOCHANGE), it depends on the previous "further" status */
#define CPUFREQ_PRIVATE                 0x100
static unsigned int no_cpufreq_access = 0;
static unsigned int suspend_freq = 816 * 1000;
static unsigned int suspend_volt = 1000000; // 1V
static unsigned int low_battery_freq = 600 * 1000;
static unsigned int low_battery_capacity = 5; // 5%
static bool is_booting = true;
static DEFINE_MUTEX(cpufreq_mutex);
static bool gpu_is_mali400;
struct dvfs_node *clk_cpu_dvfs_node = NULL;
struct dvfs_node *clk_gpu_dvfs_node = NULL;
struct dvfs_node *aclk_vio1_dvfs_node = NULL;
struct dvfs_node *clk_ddr_dvfs_node = NULL;
/*******************************************************/
static unsigned int cpufreq_get_rate(unsigned int cpu)
{
	if (clk_cpu_dvfs_node)
		return clk_get_rate(clk_cpu_dvfs_node->clk) / 1000;

	return 0;
}

static bool cpufreq_is_ondemand(struct cpufreq_policy *policy)
{
	char c = 0;
	if (policy && policy->governor)
		c = policy->governor->name[0];
	return (c == 'o' || c == 'i' || c == 'c' || c == 'h');
}

static unsigned int get_freq_from_table(unsigned int max_freq)
{
	unsigned int i;
	unsigned int target_freq = 0;
	for (i = 0; freq_table[i].frequency != CPUFREQ_TABLE_END; i++) {
		unsigned int freq = freq_table[i].frequency;
		if (freq <= max_freq && target_freq < freq) {
			target_freq = freq;
		}
	}
	if (!target_freq)
		target_freq = max_freq;
	return target_freq;
}

static int cpufreq_notifier_policy(struct notifier_block *nb, unsigned long val, void *data)
{
	static unsigned int min_rate=0, max_rate=-1;
	struct cpufreq_policy *policy = data;

	if (val != CPUFREQ_ADJUST)
		return 0;

	if (cpufreq_is_ondemand(policy)) {
		FREQ_DBG("queue work\n");
		dvfs_clk_enable_limit(clk_cpu_dvfs_node, min_rate, max_rate);
	} else {
		FREQ_DBG("cancel work\n");
		dvfs_clk_get_limit(clk_cpu_dvfs_node, &min_rate, &max_rate);
	}

	return 0;
}

static struct notifier_block notifier_policy_block = {
	.notifier_call = cpufreq_notifier_policy
};

static int cpufreq_verify(struct cpufreq_policy *policy)
{
	if (!freq_table)
		return -EINVAL;
	return cpufreq_frequency_table_verify(policy, freq_table);
}

static int cpufreq_scale_rate_for_dvfs(struct clk *clk, unsigned long rate)
{
	int ret;
	struct cpufreq_freqs freqs;
	struct cpufreq_policy *policy;
	
	freqs.new = rate / 1000;
	freqs.old = clk_get_rate(clk) / 1000;
	
	for_each_online_cpu(freqs.cpu) {
		policy = cpufreq_cpu_get(freqs.cpu);
		cpufreq_notify_transition(policy, &freqs, CPUFREQ_PRECHANGE);
		cpufreq_cpu_put(policy);
	}
	
	FREQ_DBG("cpufreq_scale_rate_for_dvfs(%lu)\n", rate);
	
	ret = clk_set_rate(clk, rate);

	freqs.new = clk_get_rate(clk) / 1000;
	/* notifiers */
	for_each_online_cpu(freqs.cpu) {
		policy = cpufreq_cpu_get(freqs.cpu);
		cpufreq_notify_transition(policy, &freqs, CPUFREQ_POSTCHANGE);
		cpufreq_cpu_put(policy);
	}

	return ret;
	
}

static int cpufreq_init_cpu0(struct cpufreq_policy *policy)
{
	unsigned int i;
	int ret;
	struct regulator *vdd_gpu_regulator;

	gpu_is_mali400 = cpu_is_rk3188();

	clk_gpu_dvfs_node = clk_get_dvfs_node("clk_gpu");
	if (clk_gpu_dvfs_node){
		clk_enable_dvfs(clk_gpu_dvfs_node);
		vdd_gpu_regulator = dvfs_get_regulator("vdd_gpu");
		if (!IS_ERR_OR_NULL(vdd_gpu_regulator)) {
			if (!regulator_is_enabled(vdd_gpu_regulator)) {
				ret = regulator_enable(vdd_gpu_regulator);
				arm_pm_restart('h', NULL);
			}
			/* make sure vdd_gpu_regulator is in use,
			so it will not be disable by regulator_init_complete*/
			ret = regulator_enable(vdd_gpu_regulator);
			if (ret != 0)
				arm_pm_restart('h', NULL);
		}
		if (gpu_is_mali400)
			dvfs_clk_enable_limit(clk_gpu_dvfs_node, 133000000, 600000000);	
	}

	clk_ddr_dvfs_node = clk_get_dvfs_node("clk_ddr");
	if (clk_ddr_dvfs_node){
		clk_enable_dvfs(clk_ddr_dvfs_node);
	}

	clk_cpu_dvfs_node = clk_get_dvfs_node("clk_core");
	if (!clk_cpu_dvfs_node){
		return -EINVAL;
	}
	dvfs_clk_register_set_rate_callback(clk_cpu_dvfs_node, cpufreq_scale_rate_for_dvfs);
	freq_table = dvfs_get_freq_volt_table(clk_cpu_dvfs_node);
	if (freq_table == NULL) {
		freq_table = default_freq_table;
	} else {
		int v = INT_MAX;
		for (i = 0; freq_table[i].frequency != CPUFREQ_TABLE_END; i++) {
			if (freq_table[i].index >= suspend_volt && v > freq_table[i].index) {
				suspend_freq = freq_table[i].frequency;
				v = freq_table[i].index;
			}
		}
	}
	low_battery_freq = get_freq_from_table(low_battery_freq);
	clk_enable_dvfs(clk_cpu_dvfs_node);

	cpufreq_register_notifier(&notifier_policy_block, CPUFREQ_POLICY_NOTIFIER);

	printk("cpufreq version " VERSION ", suspend freq %d MHz\n", suspend_freq / 1000);
	return 0;
}

static int cpufreq_init(struct cpufreq_policy *policy)
{
	static int cpu0_err;
	
	if (policy->cpu == 0) {
		cpu0_err = cpufreq_init_cpu0(policy);
	}
	
	if (cpu0_err)
		return cpu0_err;
	
	//set freq min max
	cpufreq_frequency_table_cpuinfo(policy, freq_table);
	//sys nod
	cpufreq_frequency_table_get_attr(freq_table, policy->cpu);


	policy->cur = clk_get_rate(clk_cpu_dvfs_node->clk) / 1000;

	policy->cpuinfo.transition_latency = 40 * NSEC_PER_USEC;	// make ondemand default sampling_rate to 40000

	/*
	 * On SMP configuartion, both processors share the voltage
	 * and clock. So both CPUs needs to be scaled together and hence
	 * needs software co-ordination. Use cpufreq affected_cpus
	 * interface to handle this scenario. Additional is_smp() check
	 * is to keep SMP_ON_UP build working.
	 */
	if (is_smp())
		cpumask_setall(policy->cpus);

	return 0;

}

static int cpufreq_exit(struct cpufreq_policy *policy)
{
	if (policy->cpu != 0)
		return 0;

	cpufreq_frequency_table_cpuinfo(policy, freq_table);
	clk_put_dvfs_node(clk_cpu_dvfs_node);
	cpufreq_unregister_notifier(&notifier_policy_block, CPUFREQ_POLICY_NOTIFIER);

	return 0;
}

static struct freq_attr *cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

#ifdef CONFIG_CHARGER_DISPLAY
extern int rk_get_system_battery_capacity(void);
#else
static int rk_get_system_battery_capacity(void) { return 100; }
#endif

static unsigned int cpufreq_scale_limit(unsigned int target_freq, struct cpufreq_policy *policy, bool is_private)
{
	bool is_ondemand = cpufreq_is_ondemand(policy);

	if (!is_ondemand)
		return target_freq;

	if (is_booting) {
		s64 boottime_ms = ktime_to_ms(ktime_get_boottime());
		if (boottime_ms > 60 * MSEC_PER_SEC) {
			is_booting = false;
		} else if (target_freq > low_battery_freq &&
			   rk_get_system_battery_capacity() <= low_battery_capacity) {
			target_freq = low_battery_freq;
		}
	}

	return target_freq;
}

static int cpufreq_target(struct cpufreq_policy *policy, unsigned int target_freq, unsigned int relation)
{
	unsigned int i, new_freq = target_freq, new_rate, cur_rate;
	int ret = 0;
	bool is_private;

	if (!freq_table) {
		FREQ_ERR("no freq table!\n");
		return -EINVAL;
	}

	mutex_lock(&cpufreq_mutex);

	is_private = relation & CPUFREQ_PRIVATE;
	relation &= ~CPUFREQ_PRIVATE;

	if ((relation & ENABLE_FURTHER_CPUFREQ) && no_cpufreq_access)
		no_cpufreq_access--;
	if (no_cpufreq_access) {
		FREQ_LOG("denied access to %s as it is disabled temporarily\n", __func__);
		ret = -EINVAL;
		goto out;
	}
	if (relation & DISABLE_FURTHER_CPUFREQ)
		no_cpufreq_access++;
	relation &= ~MASK_FURTHER_CPUFREQ;

	ret = cpufreq_frequency_table_target(policy, freq_table, target_freq, relation, &i);
	if (ret) {
		FREQ_ERR("no freq match for %d(ret=%d)\n", target_freq, ret);
		goto out;
	}
	new_freq = freq_table[i].frequency;
	if (!no_cpufreq_access)
		new_freq = cpufreq_scale_limit(new_freq, policy, is_private);

	new_rate = new_freq * 1000;
	cur_rate = dvfs_clk_get_rate(clk_cpu_dvfs_node);
	FREQ_LOG("req = %7u new = %7u (was = %7u)\n", target_freq, new_freq, cur_rate / 1000);
	if (new_rate == cur_rate)
		goto out;
	ret = dvfs_clk_set_rate(clk_cpu_dvfs_node, new_rate);

out:
	FREQ_DBG("set freq (%7u) end, ret %d\n", new_freq, ret);
	mutex_unlock(&cpufreq_mutex);
	return ret;

}

static int cpufreq_pm_notifier_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	int ret = NOTIFY_DONE;
	struct cpufreq_policy *policy = cpufreq_cpu_get(0);

	if (!policy)
		return ret;

	if (!cpufreq_is_ondemand(policy))
		goto out;

	switch (event) {
	case PM_SUSPEND_PREPARE:
		policy->cur++;
		ret = cpufreq_driver_target(policy, suspend_freq, DISABLE_FURTHER_CPUFREQ | CPUFREQ_RELATION_H);
		if (ret < 0) {
			ret = NOTIFY_BAD;
			goto out;
		}
		ret = NOTIFY_OK;
		break;
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		//if (target_freq == policy->cur) then cpufreq_driver_target
		//will return, and our target will not be called, it casue
		//ENABLE_FURTHER_CPUFREQ flag invalid, avoid that.
		policy->cur++;
		cpufreq_driver_target(policy, suspend_freq, ENABLE_FURTHER_CPUFREQ | CPUFREQ_RELATION_H);
		ret = NOTIFY_OK;
		break;
	}
out:
	cpufreq_cpu_put(policy);
	return ret;
}

static struct notifier_block cpufreq_pm_notifier = {
	.notifier_call = cpufreq_pm_notifier_event,
};
#if 0
static int cpufreq_reboot_notifier_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(0);

	if (policy) {
		is_booting = false;
		policy->cur++;
		cpufreq_driver_target(policy, suspend_freq, DISABLE_FURTHER_CPUFREQ | CPUFREQ_RELATION_H);
		cpufreq_cpu_put(policy);
	}

	return NOTIFY_OK;
}
#endif
int rockchip_cpufreq_reboot_limit_freq(void)
{
	dvfs_disable_temp_limit();
	dvfs_clk_enable_limit(clk_cpu_dvfs_node, 1000*suspend_freq, 1000*suspend_freq);
	printk("cpufreq: reboot set core rate=%lu, volt=%d\n", dvfs_clk_get_rate(clk_cpu_dvfs_node), 
		regulator_get_voltage(clk_cpu_dvfs_node->vd->regulator));

	return 0;
}
#if 0
static struct notifier_block cpufreq_reboot_notifier = {
	.notifier_call = cpufreq_reboot_notifier_event,
};
#endif
static int clk_pd_vio_notifier_call(struct notifier_block *nb, unsigned long event, void *ptr)
{
	switch (event) {
	case RK_CLK_PD_PREPARE:
		if (aclk_vio1_dvfs_node)
			clk_enable_dvfs(aclk_vio1_dvfs_node);
		break;
	case RK_CLK_PD_UNPREPARE:
		if (aclk_vio1_dvfs_node)
			clk_disable_dvfs(aclk_vio1_dvfs_node);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block clk_pd_vio_notifier = {
	.notifier_call = clk_pd_vio_notifier_call,
};


static struct cpufreq_driver cpufreq_driver = {
	.flags = CPUFREQ_CONST_LOOPS,
	.verify = cpufreq_verify,
	.target = cpufreq_target,
	.get = cpufreq_get_rate,
	.init = cpufreq_init,
	.exit = cpufreq_exit,
	.name = "rockchip",
	.attr = cpufreq_attr,
};

static int __init cpufreq_driver_init(void)
{
	struct clk *clk;

	clk = clk_get(NULL, "pd_vio");
	if (clk) {
		rk_clk_pd_notifier_register(clk, &clk_pd_vio_notifier);
		aclk_vio1_dvfs_node = clk_get_dvfs_node("aclk_vio1");
		if (aclk_vio1_dvfs_node && __clk_is_enabled(clk)){
			clk_enable_dvfs(aclk_vio1_dvfs_node);
		}
	}
	register_pm_notifier(&cpufreq_pm_notifier);
	return cpufreq_register_driver(&cpufreq_driver);
}

device_initcall(cpufreq_driver_init);
