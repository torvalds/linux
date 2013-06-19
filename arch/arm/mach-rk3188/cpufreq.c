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

//#define DEBUG 1
#define pr_fmt(fmt) "cpufreq: " fmt
#include <linux/clk.h>
#include <linux/cpu.h>
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
#include <linux/earlysuspend.h>
#include <asm/smp_plat.h>
#include <asm/cpu.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
#include <mach/cpu.h>
#include <mach/ddr.h>
#include <mach/dvfs.h>

#define VERSION "2.0"

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
	{.frequency = 816 * 1000, .index = 1000 * 1000},
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
static int no_cpufreq_access;
static unsigned int suspend_freq = 816 * 1000;
static unsigned int suspend_volt = 1000000; // 1V
static unsigned int low_battery_freq = 600 * 1000;
static unsigned int low_battery_capacity = 5; // 5%
static unsigned int nr_cpus = NR_CPUS;
static bool is_booting = true;

static struct workqueue_struct *freq_wq;
static struct clk *cpu_clk;

static DEFINE_MUTEX(cpufreq_mutex);

static struct clk *gpu_clk;
static bool gpu_is_mali400;
static struct clk *ddr_clk;

static int cpufreq_scale_rate_for_dvfs(struct clk *clk, unsigned long rate, dvfs_set_rate_callback set_rate);

/*******************************************************/
static unsigned int rk3188_cpufreq_get(unsigned int cpu)
{
	unsigned long freq;

	if (cpu >= nr_cpus)
		return 0;

	freq = clk_get_rate(cpu_clk) / 1000;
	return freq;
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

/**********************thermal limit**************************/
#define CONFIG_RK30_CPU_FREQ_LIMIT_BY_TEMP

#ifdef CONFIG_RK30_CPU_FREQ_LIMIT_BY_TEMP
static unsigned int temp_limit_freq = -1;
module_param(temp_limit_freq, uint, 0444);

static struct cpufreq_frequency_table temp_limits[4][4] = {
	{
		{.frequency =          -1, .index = 50},
		{.frequency =          -1, .index = 55},
		{.frequency =          -1, .index = 60},
		{.frequency = 1608 * 1000, .index = 75},
	}, {
		{.frequency = 1800 * 1000, .index = 50},
		{.frequency = 1608 * 1000, .index = 55},
		{.frequency = 1416 * 1000, .index = 60},
		{.frequency = 1200 * 1000, .index = 75},
	}, {
		{.frequency = 1704 * 1000, .index = 50},
		{.frequency = 1512 * 1000, .index = 55},
		{.frequency = 1296 * 1000, .index = 60},
		{.frequency = 1104 * 1000, .index = 75},
	}, {
		{.frequency = 1608 * 1000, .index = 50},
		{.frequency = 1416 * 1000, .index = 55},
		{.frequency = 1200 * 1000, .index = 60},
		{.frequency = 1008 * 1000, .index = 75},
	}
};

static struct cpufreq_frequency_table temp_limits_cpu_perf[] = {
	{.frequency = 1008 * 1000, .index = 100},
};

static struct cpufreq_frequency_table temp_limits_gpu_perf[] = {
	{.frequency = 1008 * 1000, .index = 0},
};

static int rk3188_get_temp(void)
{
	return 60;
}

static char sys_state;
static ssize_t sys_state_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	char state;

	if (count < 1)
		return count;
	if (copy_from_user(&state, buffer, 1)) {
		return -EFAULT;
	}

	sys_state = state;
	return count;
}

static const struct file_operations sys_state_fops = {
	.owner	= THIS_MODULE,
	.write	= sys_state_write,
};

static struct miscdevice sys_state_dev = {
	.fops	= &sys_state_fops,
	.name	= "sys_state",
	.minor	= MISC_DYNAMIC_MINOR,
};

static void rk3188_cpufreq_temp_limit_work_func(struct work_struct *work)
{
	static bool in_perf = false;
	struct cpufreq_policy *policy;
	int temp, i;
	unsigned int new_freq = -1;
	unsigned long delay = HZ / 10; // 100ms
	const struct cpufreq_frequency_table *limits_table = temp_limits[nr_cpus - 1];
	size_t limits_size = ARRAY_SIZE(temp_limits[nr_cpus - 1]);

	temp = rk3188_get_temp();

	if (sys_state == '1') {
		in_perf = true;
		if (gpu_is_mali400) {
			unsigned int gpu_irqs[2];
			gpu_irqs[0] = kstat_irqs(IRQ_GPU_GP);
			msleep(40);
			gpu_irqs[1] = kstat_irqs(IRQ_GPU_GP);
			delay = 0;
			if ((gpu_irqs[1] - gpu_irqs[0]) < 8) {
				limits_table = temp_limits_cpu_perf;
				limits_size = ARRAY_SIZE(temp_limits_cpu_perf);
			} else {
				limits_table = temp_limits_gpu_perf;
				limits_size = ARRAY_SIZE(temp_limits_gpu_perf);
			}
		} else {
			delay = HZ; // 1s
			limits_table = temp_limits_cpu_perf;
			limits_size = ARRAY_SIZE(temp_limits_cpu_perf);
		}
	} else if (in_perf) {
		in_perf = false;
	} else {
		static u64 last_time_in_idle = 0;
		static u64 last_time_in_idle_timestamp = 0;
		u64 time_in_idle = 0, now;
		u32 delta_idle;
		u32 delta_time;
		unsigned cpu;

		for (cpu = 0; cpu < nr_cpus; cpu++)
			time_in_idle += get_cpu_idle_time_us(cpu, &now);
		delta_time = now - last_time_in_idle_timestamp;
		delta_idle = time_in_idle - last_time_in_idle;
		last_time_in_idle = time_in_idle;
		last_time_in_idle_timestamp = now;
		delta_idle += delta_time >> 4; // +6.25%
		if (delta_idle > (nr_cpus - 1) * delta_time && delta_idle < (nr_cpus + 1) * delta_time)
			limits_table = temp_limits[0];
		else if (delta_idle > (nr_cpus - 2) * delta_time)
			limits_table = temp_limits[1];
		else if (delta_idle > (nr_cpus - 3) * delta_time)
			limits_table = temp_limits[2];
		FREQ_DBG("delta time %6u us idle %6u us\n", delta_time, delta_idle);
	}

	for (i = 0; i < limits_size; i++) {
		if (temp >= limits_table[i].index) {
			new_freq = limits_table[i].frequency;
		}
	}

	if (temp_limit_freq != new_freq) {
		unsigned int cur_freq;
		temp_limit_freq = new_freq;
		cur_freq = rk3188_cpufreq_get(0);
		FREQ_DBG("temp limit %7d KHz cur %7d KHz\n", temp_limit_freq, cur_freq);
		if (cur_freq > temp_limit_freq) {
			policy = cpufreq_cpu_get(0);
			cpufreq_driver_target(policy, policy->cur, CPUFREQ_RELATION_L | CPUFREQ_PRIVATE);
			cpufreq_cpu_put(policy);
		}
	}

	queue_delayed_work_on(0, freq_wq, to_delayed_work(work), delay);
}

static DECLARE_DELAYED_WORK(rk3188_cpufreq_temp_limit_work, rk3188_cpufreq_temp_limit_work_func);

static int rk3188_cpufreq_notifier_policy(struct notifier_block *nb, unsigned long val, void *data)
{
	struct cpufreq_policy *policy = data;

	if (val != CPUFREQ_NOTIFY)
		return 0;

	if (cpufreq_is_ondemand(policy)) {
		FREQ_DBG("queue work\n");
		queue_delayed_work_on(0, freq_wq, &rk3188_cpufreq_temp_limit_work, 0);
	} else {
		FREQ_DBG("cancel work\n");
		cancel_delayed_work_sync(&rk3188_cpufreq_temp_limit_work);
	}

	return 0;
}

static struct notifier_block notifier_policy_block = {
	.notifier_call = rk3188_cpufreq_notifier_policy
};
#endif

/************************************dvfs tst************************************/
//#define CPU_FREQ_DVFS_TST
#ifdef CPU_FREQ_DVFS_TST
static unsigned int freq_dvfs_tst_rate;
static int test_count;
#define TEST_FRE_NUM 11
static int test_tlb_rate[TEST_FRE_NUM] = { 504, 1008, 504, 1200, 252, 816, 1416, 252, 1512, 252, 816 };
//static int test_tlb_rate[TEST_FRE_NUM]={504,1008,504,1200,252,816,1416,126,1512,126,816};

#define TEST_GPU_NUM 3

static int test_tlb_gpu[TEST_GPU_NUM] = { 360, 400, 180 };
static int test_tlb_ddr[TEST_GPU_NUM] = { 401, 200, 500 };

static int gpu_ddr = 0;

static void rk3188_cpufreq_dvsf_tst_work_func(struct work_struct *work)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(0);

	gpu_ddr++;

#if 0
	FREQ_LOG("cpufreq_dvsf_tst,ddr%u,gpu%u\n",
		test_tlb_ddr[gpu_ddr % TEST_GPU_NUM],
		test_tlb_gpu[gpu_ddr % TEST_GPU_NUM]);
	clk_set_rate(ddr_clk, test_tlb_ddr[gpu_ddr % TEST_GPU_NUM] * 1000 * 1000);
	clk_set_rate(gpu_clk, test_tlb_gpu[gpu_ddr % TEST_GPU_NUM] * 1000 * 1000);
#endif

	test_count++;
	freq_dvfs_tst_rate = test_tlb_rate[test_count % TEST_FRE_NUM] * 1000;
	FREQ_LOG("cpufreq_dvsf_tst,cpu set rate %d\n", freq_dvfs_tst_rate);
	cpufreq_driver_target(policy, policy->cur, CPUFREQ_RELATION_L);
	cpufreq_cpu_put(policy);

	queue_delayed_work_on(0, freq_wq, to_delayed_work(work), msecs_to_jiffies(1000));
}

static DECLARE_DELAYED_WORK(rk3188_cpufreq_dvsf_tst_work, rk3188_cpufreq_dvsf_tst_work_func);
#endif /* CPU_FREQ_DVFS_TST */

/***********************************************************************/
static int rk3188_cpufreq_verify(struct cpufreq_policy *policy)
{
	if (!freq_table)
		return -EINVAL;
	return cpufreq_frequency_table_verify(policy, freq_table);
}

static int rk3188_cpufreq_init_cpu0(struct cpufreq_policy *policy)
{
	unsigned int i;

	gpu_is_mali400 = cpu_is_rk3188();
	gpu_clk = clk_get(NULL, "gpu");
	if (IS_ERR(gpu_clk))
		return PTR_ERR(gpu_clk);

	ddr_clk = clk_get(NULL, "ddr");
	if (IS_ERR(ddr_clk))
		return PTR_ERR(ddr_clk);

	cpu_clk = clk_get(NULL, "cpu");
	if (IS_ERR(cpu_clk))
		return PTR_ERR(cpu_clk);

#if defined(CONFIG_ARCH_RK3188)
	if (soc_is_rk3188()) {
		struct cpufreq_frequency_table *table_adjust;
		/* Adjust dvfs table avoid overheat */
		table_adjust = dvfs_get_freq_volt_table(cpu_clk);
		dvfs_adjust_table_lmtvolt(cpu_clk, table_adjust);
		table_adjust = dvfs_get_freq_volt_table(gpu_clk);
		dvfs_adjust_table_lmtvolt(gpu_clk, table_adjust);
	}
#endif
	clk_enable_dvfs(gpu_clk);
	if (gpu_is_mali400)
		dvfs_clk_enable_limit(gpu_clk, 133000000, 600000000);

	clk_enable_dvfs(ddr_clk);

	dvfs_clk_register_set_rate_callback(cpu_clk, cpufreq_scale_rate_for_dvfs);
	freq_table = dvfs_get_freq_volt_table(cpu_clk);
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
	clk_enable_dvfs(cpu_clk);

	nr_cpus = num_possible_cpus();
	freq_wq = alloc_workqueue("rk3188_cpufreqd", WQ_NON_REENTRANT | WQ_MEM_RECLAIM | WQ_HIGHPRI | WQ_FREEZABLE, 1);
#ifdef CONFIG_RK30_CPU_FREQ_LIMIT_BY_TEMP
	{
		struct cpufreq_frequency_table *table = temp_limits[0];
		for (i = 0; i < sizeof(temp_limits) / sizeof(struct cpufreq_frequency_table); i++) {
			table[i].frequency = get_freq_from_table(table[i].frequency);
		}
		table = temp_limits_cpu_perf;
		for (i = 0; i < sizeof(temp_limits_cpu_perf) / sizeof(struct cpufreq_frequency_table); i++) {
			table[i].frequency = get_freq_from_table(table[i].frequency);
		}
		table = temp_limits_gpu_perf;
		for (i = 0; i < sizeof(temp_limits_gpu_perf) / sizeof(struct cpufreq_frequency_table); i++) {
			table[i].frequency = get_freq_from_table(table[i].frequency);
		}
	}
	misc_register(&sys_state_dev);
	if (cpufreq_is_ondemand(policy)) {
		queue_delayed_work_on(0, freq_wq, &rk3188_cpufreq_temp_limit_work, 0*HZ);
	}
	cpufreq_register_notifier(&notifier_policy_block, CPUFREQ_POLICY_NOTIFIER);
#endif
#ifdef CPU_FREQ_DVFS_TST
	queue_delayed_work(freq_wq, &rk3188_cpufreq_dvsf_tst_work, msecs_to_jiffies(20 * 1000));
#endif

	printk("rk3188 cpufreq version " VERSION ", suspend freq %d MHz\n", suspend_freq / 1000);
	return 0;
}

static int rk3188_cpufreq_init(struct cpufreq_policy *policy)
{
	if (policy->cpu == 0) {
		int err = rk3188_cpufreq_init_cpu0(policy);
		if (err)
			return err;
	}

	//set freq min max
	cpufreq_frequency_table_cpuinfo(policy, freq_table);
	//sys nod
	cpufreq_frequency_table_get_attr(freq_table, policy->cpu);

	policy->cur = rk3188_cpufreq_get(0);

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

static int rk3188_cpufreq_exit(struct cpufreq_policy *policy)
{
	if (policy->cpu != 0)
		return 0;

	cpufreq_frequency_table_cpuinfo(policy, freq_table);
	clk_put(cpu_clk);
#ifdef CONFIG_RK30_CPU_FREQ_LIMIT_BY_TEMP
	cpufreq_unregister_notifier(&notifier_policy_block, CPUFREQ_POLICY_NOTIFIER);
	if (freq_wq)
		cancel_delayed_work(&rk3188_cpufreq_temp_limit_work);
#endif
	if (freq_wq) {
		flush_workqueue(freq_wq);
		destroy_workqueue(freq_wq);
		freq_wq = NULL;
	}

	return 0;
}

static struct freq_attr *rk3188_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

#ifdef CONFIG_POWER_SUPPLY
extern int rk_get_system_battery_capacity(void);
#else
static int rk_get_system_battery_capacity(void) { return 100; }
#endif

static unsigned int cpufreq_scale_limit(unsigned int target_freq, struct cpufreq_policy *policy, bool is_private)
{
	bool is_ondemand = cpufreq_is_ondemand(policy);

#ifdef CPU_FREQ_DVFS_TST
	if (freq_dvfs_tst_rate) {
		target_freq = freq_dvfs_tst_rate;
		freq_dvfs_tst_rate = 0;
		return target_freq;
	}
#endif

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

#ifdef CONFIG_RK30_CPU_FREQ_LIMIT_BY_TEMP
	{
		static unsigned int ondemand_target = 816 * 1000;
		if (is_private)
			target_freq = ondemand_target;
		else
			ondemand_target = target_freq;
	}

	/*
	 * If the new frequency is more than the thermal max allowed
	 * frequency, go ahead and scale the mpu device to proper frequency.
	 */
	target_freq = min(target_freq, temp_limit_freq);
#endif

	return target_freq;
}

static int cpufreq_scale_rate_for_dvfs(struct clk *clk, unsigned long rate, dvfs_set_rate_callback set_rate)
{
	unsigned int i;
	int ret;
	struct cpufreq_freqs freqs;

	freqs.new = rate / 1000;
	freqs.old = clk_get_rate(clk) / 1000;

	for_each_online_cpu(freqs.cpu) {
		cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
	}
	FREQ_DBG("cpufreq_scale_rate_for_dvfs(%lu)\n", rate);
	ret = set_rate(clk, rate);

#ifdef CONFIG_SMP
	/*
	 * Note that loops_per_jiffy is not updated on SMP systems in
	 * cpufreq driver. So, update the per-CPU loops_per_jiffy value
	 * on frequency transition. We need to update all dependent CPUs.
	 */
	for_each_possible_cpu(i) {
		per_cpu(cpu_data, i).loops_per_jiffy = loops_per_jiffy;
	}
#endif

	freqs.new = clk_get_rate(clk) / 1000;
	/* notifiers */
	for_each_online_cpu(freqs.cpu) {
		cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
	}

	return ret;
}

static int rk3188_cpufreq_target(struct cpufreq_policy *policy, unsigned int target_freq, unsigned int relation)
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

	if (relation & ENABLE_FURTHER_CPUFREQ)
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
	cur_rate = clk_get_rate(cpu_clk);
	FREQ_LOG("req = %7u new = %7u (was = %7u)\n", target_freq, new_freq, cur_rate / 1000);
	if (new_rate == cur_rate)
		goto out;
	ret = clk_set_rate(cpu_clk, new_rate);

out:
	FREQ_DBG("set freq (%7u) end, ret %d\n", new_freq, ret);
	mutex_unlock(&cpufreq_mutex);
	return ret;
}

static int rk3188_cpufreq_pm_notifier_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	int ret = NOTIFY_DONE;
	struct cpufreq_policy *policy = cpufreq_cpu_get(0);

	if (!policy)
		return ret;

	if (!cpufreq_is_ondemand(policy))
		goto out;

	switch (event) {
	case PM_SUSPEND_PREPARE:
		ret = cpufreq_driver_target(policy, suspend_freq, DISABLE_FURTHER_CPUFREQ | CPUFREQ_RELATION_H);
		if (ret < 0) {
			ret = NOTIFY_BAD;
			goto out;
		}
		ret = NOTIFY_OK;
		break;
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		cpufreq_driver_target(policy, suspend_freq, ENABLE_FURTHER_CPUFREQ | CPUFREQ_RELATION_H);
		ret = NOTIFY_OK;
		break;
	}
out:
	cpufreq_cpu_put(policy);
	return ret;
}

static struct notifier_block rk3188_cpufreq_pm_notifier = {
	.notifier_call = rk3188_cpufreq_pm_notifier_event,
};

static int rk3188_cpufreq_reboot_notifier_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(0);

	if (policy) {
		is_booting = false;
		cpufreq_driver_target(policy, suspend_freq, DISABLE_FURTHER_CPUFREQ | CPUFREQ_RELATION_H);
		cpufreq_cpu_put(policy);
	}

	return NOTIFY_OK;
}

static struct notifier_block rk3188_cpufreq_reboot_notifier = {
	.notifier_call = rk3188_cpufreq_reboot_notifier_event,
};

static struct cpufreq_driver rk3188_cpufreq_driver = {
	.flags = CPUFREQ_CONST_LOOPS,
	.verify = rk3188_cpufreq_verify,
	.target = rk3188_cpufreq_target,
	.get = rk3188_cpufreq_get,
	.init = rk3188_cpufreq_init,
	.exit = rk3188_cpufreq_exit,
	.name = "rk3188",
	.attr = rk3188_cpufreq_attr,
};

static int __init rk3188_cpufreq_driver_init(void)
{
	register_pm_notifier(&rk3188_cpufreq_pm_notifier);
	register_reboot_notifier(&rk3188_cpufreq_reboot_notifier);
	return cpufreq_register_driver(&rk3188_cpufreq_driver);
}

static void __exit rk3188_cpufreq_driver_exit(void)
{
	cpufreq_unregister_driver(&rk3188_cpufreq_driver);
}

device_initcall(rk3188_cpufreq_driver_init);
module_exit(rk3188_cpufreq_driver_exit);
