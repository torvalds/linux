/*
 * Copyright (C) 2012 ROCKCHIP, Inc.
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
#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/reboot.h>
#include <linux/suspend.h>
#include <linux/tick.h>
#include <linux/workqueue.h>
#include <asm/smp_plat.h>
#include <asm/cpu.h>
#include <mach/dvfs.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/earlysuspend.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
#include <mach/ddr.h>
#ifdef DEBUG
#define FREQ_PRINTK_DBG(fmt, args...) pr_debug(fmt, ## args)
#define FREQ_PRINTK_LOG(fmt, args...) pr_debug(fmt, ## args)
#else
#define FREQ_PRINTK_DBG(fmt, args...) do {} while(0)
#define FREQ_PRINTK_LOG(fmt, args...) do {} while(0)
#endif
#define FREQ_PRINTK_ERR(fmt, args...) pr_err(fmt, ## args)

/* Frequency table index must be sequential starting at 0 */
static struct cpufreq_frequency_table default_freq_table[] = {
	{.frequency = 816 * 1000, .index = 1100 * 1000},
	{.frequency = CPUFREQ_TABLE_END},
};

static struct cpufreq_frequency_table *freq_table = default_freq_table;
static unsigned int max_freq = -1;

/*********************************************************/

/* additional symantics for "relation" in cpufreq with pm */
#define DISABLE_FURTHER_CPUFREQ         0x10
#define ENABLE_FURTHER_CPUFREQ          0x20
#define MASK_FURTHER_CPUFREQ            0x30
/* With 0x00(NOCHANGE), it depends on the previous "further" status */
static int no_cpufreq_access;
static unsigned int suspend_freq = 600 * 1000;
static unsigned int reboot_freq = 816 * 1000;

static struct workqueue_struct *freq_wq;
static struct clk *cpu_clk;
static struct clk *cpu_pll;
static struct clk *cpu_gpll;


static DEFINE_MUTEX(cpufreq_mutex);

static struct clk *gpu_clk;
#define GPU_MAX_RATE 350*1000*1000

static int cpufreq_scale_rate_for_dvfs(struct clk *clk, unsigned long rate, dvfs_set_rate_callback set_rate);

/*******************************************************/
static unsigned int rk30_getspeed(unsigned int cpu)
{
	unsigned long rate;

	if (cpu >= NR_CPUS)
		return 0;

	rate = clk_get_rate(cpu_clk) / 1000;
	return rate;
}

static bool rk30_cpufreq_is_ondemand_policy(struct cpufreq_policy *policy)
{
	char c = 0;
	if (policy && policy->governor)
		c = policy->governor->name[0];
	return (c == 'o' || c == 'i' || c == 'c' || c == 'h');
}

/**********************thermal limit**************************/
#define CONFIG_RK30_CPU_FREQ_LIMIT_BY_TEMP

#ifdef CONFIG_RK30_CPU_FREQ_LIMIT_BY_TEMP
static void rk30_cpufreq_temp_limit_work_func(struct work_struct *work);

static DECLARE_DELAYED_WORK(rk30_cpufreq_temp_limit_work, rk30_cpufreq_temp_limit_work_func);

static unsigned int temp_limt_freq = -1;
module_param(temp_limt_freq, uint, 0444);

#define TEMP_LIMIT_FREQ 816000

static const struct cpufreq_frequency_table temp_limits[] = {
	{.frequency = 1416 * 1000, .index = 50},
	{.frequency = 1200 * 1000, .index = 55},
	{.frequency = 1008 * 1000, .index = 60},
	{.frequency =  816 * 1000, .index = 75},
};

//extern int rk30_tsadc_get_temp(unsigned int chn);

//#define get_cpu_thermal() rk30_tsadc_get_temp(0)
static void rk30_cpufreq_temp_limit_work_func(struct work_struct *work)
{
	struct cpufreq_policy *policy;
	int temp = 25, i;
	unsigned int new = -1;

	if (clk_get_rate(gpu_clk) > GPU_MAX_RATE)
		goto out;

	//temp = max(rk30_tsadc_get_temp(0), rk30_tsadc_get_temp(1));
	FREQ_PRINTK_LOG("cpu_thermal(%d)\n", temp);

	for (i = 0; i < ARRAY_SIZE(temp_limits); i++) {
		if (temp > temp_limits[i].index) {
			new = temp_limits[i].frequency;
		}
	}
	if (temp_limt_freq != new) {
		temp_limt_freq = new;
		if (new != -1) {
			FREQ_PRINTK_DBG("temp_limit set rate %d kHz\n", temp_limt_freq);
			policy = cpufreq_cpu_get(0);
			cpufreq_driver_target(policy, policy->cur, CPUFREQ_RELATION_L);
			cpufreq_cpu_put(policy);
		}
	}

out:
	queue_delayed_work(freq_wq, &rk30_cpufreq_temp_limit_work, HZ);
}

static int rk30_cpufreq_notifier_policy(struct notifier_block *nb,
					unsigned long val, void *data)
{
	struct cpufreq_policy *policy = data;

	if (val != CPUFREQ_NOTIFY)
		return 0;

	if (rk30_cpufreq_is_ondemand_policy(policy)) {
		FREQ_PRINTK_DBG("queue work\n");
		queue_delayed_work(freq_wq, &rk30_cpufreq_temp_limit_work, 0);
	} else {
		FREQ_PRINTK_DBG("cancel work\n");
		cancel_delayed_work_sync(&rk30_cpufreq_temp_limit_work);
	}

	return 0;
}

static struct notifier_block notifier_policy_block = {
	.notifier_call = rk30_cpufreq_notifier_policy
};
#endif

/************************************dvfs tst************************************/
//#define CPU_FREQ_DVFS_TST
#ifdef CPU_FREQ_DVFS_TST
static unsigned int freq_dvfs_tst_rate;
static void rk30_cpufreq_dvsf_tst_work_func(struct work_struct *work);
static DECLARE_DELAYED_WORK(rk30_cpufreq_dvsf_tst_work, rk30_cpufreq_dvsf_tst_work_func);
static int test_count;
#define TEST_FRE_NUM 11
static int test_tlb_rate[TEST_FRE_NUM] = { 504, 1008, 504, 1200, 252, 816, 1416, 252, 1512, 252, 816 };
//static int test_tlb_rate[TEST_FRE_NUM]={504,1008,504,1200,252,816,1416,126,1512,126,816};

#define TEST_GPU_NUM 3

static int test_tlb_gpu[TEST_GPU_NUM] = { 360, 400, 180 };
static int test_tlb_ddr[TEST_GPU_NUM] = { 401, 200, 500 };

static int gpu_ddr = 0;

static void rk30_cpufreq_dvsf_tst_work_func(struct work_struct *work)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(0);

	gpu_ddr++;

#if 0
	FREQ_PRINTK_LOG("cpufreq_dvsf_tst,ddr%u,gpu%u\n",
			test_tlb_ddr[gpu_ddr % TEST_GPU_NUM],
			test_tlb_gpu[gpu_ddr % TEST_GPU_NUM]);
	clk_set_rate(ddr_clk, test_tlb_ddr[gpu_ddr % TEST_GPU_NUM] * 1000 * 1000);
	clk_set_rate(gpu_clk, test_tlb_gpu[gpu_ddr % TEST_GPU_NUM] * 1000 * 1000);
#endif

	test_count++;
	freq_dvfs_tst_rate = test_tlb_rate[test_count % TEST_FRE_NUM] * 1000;
	FREQ_PRINTK_LOG("cpufreq_dvsf_tst,cpu set rate %d\n", freq_dvfs_tst_rate);
	cpufreq_driver_target(policy, policy->cur, CPUFREQ_RELATION_L);
	cpufreq_cpu_put(policy);

	queue_delayed_work(freq_wq, &rk30_cpufreq_dvsf_tst_work, msecs_to_jiffies(1000));
}
#endif /* CPU_FREQ_DVFS_TST */

/***********************************************************************/
static int rk30_verify_speed(struct cpufreq_policy *policy)
{
	if (!freq_table)
		return -EINVAL;
	return cpufreq_frequency_table_verify(policy, freq_table);
}

static int rk30_cpu_init(struct cpufreq_policy *policy)
{
	if (policy->cpu == 0) {
		int i;
		struct clk *ddr_clk;
	  	struct clk *aclk_vepu_clk;
	
		gpu_clk = clk_get(NULL, "gpu");
		if (!IS_ERR(gpu_clk))
			clk_enable_dvfs(gpu_clk);
#if 0	

		ddr_clk = clk_get(NULL, "ddr");
		if (!IS_ERR(ddr_clk))
		{
			clk_enable_dvfs(ddr_clk);
			clk_set_rate(ddr_clk,clk_get_rate(ddr_clk)-1);
		}
		
#endif
		cpu_clk = clk_get(NULL, "cpu");
		cpu_pll = clk_get(NULL, "arm_pll");
		
		cpu_gpll = clk_get(NULL, "arm_gpll");
		if (IS_ERR(cpu_clk))
			return PTR_ERR(cpu_clk);
		
		dvfs_clk_register_set_rate_callback(cpu_clk, cpufreq_scale_rate_for_dvfs);
		freq_table = dvfs_get_freq_volt_table(cpu_clk);
		if (freq_table == NULL) {
			freq_table = default_freq_table;
		}
		max_freq = freq_table[0].frequency;
		for (i = 0; freq_table[i].frequency != CPUFREQ_TABLE_END; i++) {
			max_freq = max(max_freq, freq_table[i].frequency);
		}
		clk_enable_dvfs(cpu_clk);

		/* Limit gpu frequency between 133M to 400M */
		dvfs_clk_enable_limit(gpu_clk, 133000000, 400000000);

		ddr_clk = clk_get(NULL, "ddr");
		if (!IS_ERR(ddr_clk))
			clk_enable_dvfs(ddr_clk);

		aclk_vepu_clk = clk_get(NULL, "aclk_vepu");
		if (!IS_ERR(aclk_vepu_clk))
			clk_enable_dvfs(aclk_vepu_clk);

		freq_wq = create_singlethread_workqueue("rk30_cpufreqd");
#ifdef CONFIG_RK30_CPU_FREQ_LIMIT_BY_TEMP
		if (rk30_cpufreq_is_ondemand_policy(policy)) {
			queue_delayed_work(freq_wq, &rk30_cpufreq_temp_limit_work, 0*HZ);
		}
		cpufreq_register_notifier(&notifier_policy_block, CPUFREQ_POLICY_NOTIFIER);
#endif
#ifdef CPU_FREQ_DVFS_TST
		queue_delayed_work(freq_wq, &rk30_cpufreq_dvsf_tst_work, msecs_to_jiffies(20 * 1000));
#endif
	}
	//set freq min max
	cpufreq_frequency_table_cpuinfo(policy, freq_table);
	//sys nod
	cpufreq_frequency_table_get_attr(freq_table, policy->cpu);

	policy->cur = rk30_getspeed(0);

	policy->cpuinfo.transition_latency = 40 * NSEC_PER_USEC;	// make ondemand default sampling_rate to 40000

	/*
	 * On rk30 SMP configuartion, both processors share the voltage
	 * and clock. So both CPUs needs to be scaled together and hence
	 * needs software co-ordination. Use cpufreq affected_cpus
	 * interface to handle this scenario. Additional is_smp() check
	 * is to keep SMP_ON_UP build working.
	 */
	if (is_smp())
		cpumask_setall(policy->cpus);

	return 0;
}

static int rk30_cpu_exit(struct cpufreq_policy *policy)
{
	if (policy->cpu != 0)
		return 0;

	cpufreq_frequency_table_cpuinfo(policy, freq_table);
	clk_put(cpu_clk);
#ifdef CONFIG_RK30_CPU_FREQ_LIMIT_BY_TEMP
	cpufreq_unregister_notifier(&notifier_policy_block, CPUFREQ_POLICY_NOTIFIER);
	if (freq_wq)
		cancel_delayed_work(&rk30_cpufreq_temp_limit_work);
#endif
	if (freq_wq) {
		flush_workqueue(freq_wq);
		destroy_workqueue(freq_wq);
		freq_wq = NULL;
	}

	return 0;
}

static struct freq_attr *rk30_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

/**************************earlysuspend freeze cpu frequency******************************/
static struct early_suspend ff_early_suspend;

#define FILE_GOV_MODE "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"
#define FILE_SETSPEED "/sys/devices/system/cpu/cpu0/cpufreq/scaling_setspeed"
#define FILE_CUR_FREQ "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq"

#define FF_DEBUG(fmt, args...) printk(KERN_DEBUG "FREEZE FREQ DEBUG:\t"fmt, ##args)
#define FF_ERROR(fmt, args...) printk(KERN_ERR "FREEZE FREQ ERROR:\t"fmt, ##args)

static int ff_read(char *file_path, char *buf)
{
	struct file *file = NULL;
	mm_segment_t old_fs;
	loff_t offset = 0;

	FF_DEBUG("read %s\n", file_path);
	file = filp_open(file_path, O_RDONLY, 0);

	if (IS_ERR(file)) {
		FF_ERROR("%s error open file  %s\n", __func__, file_path);
		return -1;
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	file->f_op->read(file, (char *)buf, 32, &offset);
	sscanf(buf, "%s", buf);

	set_fs(old_fs);
	filp_close(file, NULL);  

	file = NULL;

	return 0;

}

static int ff_write(char *file_path, char *buf)
{
	struct file *file = NULL;
	mm_segment_t old_fs;
	loff_t offset = 0;

	FF_DEBUG("write %s %s size = %d\n", file_path, buf, strlen(buf));
	file = filp_open(file_path, O_RDWR, 0);

	if (IS_ERR(file)) {
		FF_ERROR("%s error open file  %s\n", __func__, file_path);
		return -1;
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	file->f_op->write(file, (char *)buf, strlen(buf), &offset);

	set_fs(old_fs);
	filp_close(file, NULL);  

	file = NULL;

	return 0;

}

static void ff_scale_votlage(char *name, int volt)
{
	struct regulator* regulator;
	int ret = 0;

	FF_DEBUG("enter %s\n", __func__);
	regulator = dvfs_get_regulator(name);
	if (!regulator) {
		FF_ERROR("get regulator %s ERROR\n", name);
		return ;
	}

	ret = regulator_set_voltage(regulator, volt, volt);
	if (ret != 0) {
		FF_ERROR("set voltage error %s %d, ret = %d\n", name, volt, ret);
	}

}
int clk_set_parent_force(struct clk *clk, struct clk *parent);
static void ff_early_suspend_func(struct early_suspend *h)
{
	char buf[32];
	FF_DEBUG("enter %s\n", __func__);
	if (ff_read(FILE_GOV_MODE, buf) != 0) {
		FF_ERROR("read current governor error\n");
		return ;
	} else {
		FF_DEBUG("current governor = %s\n", buf);
	}

	strcpy(buf, "userspace");
	if (ff_write(FILE_GOV_MODE, buf) != 0) {
		FF_ERROR("set current governor error\n");
		return ;
	}

	strcpy(buf, "252000");
	if (ff_write(FILE_SETSPEED, buf) != 0) {
		FF_ERROR("set speed to 252MHz error\n");
		return ;
	}
	
	if (!IS_ERR(cpu_pll)&&!IS_ERR(cpu_gpll)&&!IS_ERR(cpu_clk))
	{
		clk_set_parent_force(cpu_clk,cpu_gpll);
		clk_set_rate(cpu_clk,300*1000*1000);
		
		clk_disable_dvfs(cpu_clk);
	}	
	if (!IS_ERR(gpu_clk))
		dvfs_clk_enable_limit(gpu_clk,75*1000*1000,133*1000*1000);
	
	//ff_scale_votlage("vdd_cpu", 1000000);
	//ff_scale_votlage("vdd_core", 1000000);
#ifdef CONFIG_HOTPLUG_CPU
	cpu_down(1);
#endif
}

static void ff_early_resume_func(struct early_suspend *h)
{
	char buf[32];
	FF_DEBUG("enter %s\n", __func__);

	if (!IS_ERR(cpu_pll)&&!IS_ERR(cpu_gpll)&&!IS_ERR(cpu_clk))
	{
		clk_set_parent_force(cpu_clk,cpu_pll);
		clk_set_rate(cpu_clk,300*1000*1000);
		clk_enable_dvfs(cpu_clk);
	}	
	
	if (!IS_ERR(gpu_clk))
		dvfs_clk_disable_limit(gpu_clk);
#ifdef CONFIG_HOTPLUG_CPU
	cpu_up(1);
#endif
	if (ff_read(FILE_GOV_MODE, buf) != 0) {
		FF_ERROR("read current governor error\n");
		return ;
	} else {
		FF_DEBUG("current governor = %s\n", buf);
	}

	if (ff_read(FILE_CUR_FREQ, buf) != 0) {
		FF_ERROR("read current frequency error\n");
		return ;
	} else {
		FF_DEBUG("current frequency = %s\n", buf);
	}

	strcpy(buf, "interactive");
	if (ff_write(FILE_GOV_MODE, buf) != 0) {
		FF_ERROR("set current governor error\n");
		return ;
	}
	
	strcpy(buf, "interactive");
	if (ff_write(FILE_GOV_MODE, buf) != 0) {
		FF_ERROR("set current governor error\n");
		return ;
	}
}

static int __init ff_init(void)
{
	FF_DEBUG("enter %s\n", __func__);
	ff_early_suspend.suspend = ff_early_suspend_func;
	ff_early_suspend.resume = ff_early_resume_func;
	ff_early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 100;
	register_early_suspend(&ff_early_suspend);
	return 0;
}

static void __exit ff_exit(void)
{
	FF_DEBUG("enter %s\n", __func__);
	unregister_early_suspend(&ff_early_suspend);
}


/**************************target freq******************************/
static unsigned int cpufreq_scale_limt(unsigned int target_freq, struct cpufreq_policy *policy)
{
	bool is_ondemand = rk30_cpufreq_is_ondemand_policy(policy);
	static bool is_booting = true;

	if (is_ondemand && clk_get_rate(gpu_clk) > GPU_MAX_RATE) // high performance?
		return max_freq;
	if (is_ondemand && is_booting && target_freq >= 1600 * 1000) {
		s64 boottime_ms = ktime_to_ms(ktime_get_boottime());
		if (boottime_ms > 30 * MSEC_PER_SEC) {
			is_booting = false;
		} else {
			target_freq = 1416 * 1000;
		}
	}
#ifdef CONFIG_RK30_CPU_FREQ_LIMIT_BY_TEMP
	if (is_ondemand && target_freq > policy->cur && policy->cur >= TEMP_LIMIT_FREQ) {
		unsigned int i;
		if (cpufreq_frequency_table_target(policy, freq_table, policy->cur + 1, CPUFREQ_RELATION_L, &i) == 0) {
			unsigned int f = freq_table[i].frequency;
			if (f < target_freq) {
				target_freq = f;
			}
		}
	}
	/*
	 * If the new frequency is more than the thermal max allowed
	 * frequency, go ahead and scale the mpu device to proper frequency.
	 */
	if (is_ondemand) {
		target_freq = min(target_freq, temp_limt_freq);
	}
#endif
#ifdef CPU_FREQ_DVFS_TST
	if (freq_dvfs_tst_rate) {
		target_freq = freq_dvfs_tst_rate;
		freq_dvfs_tst_rate = 0;
	}
#endif
	return target_freq;
}

int cpufreq_scale_rate_for_dvfs(struct clk *clk, unsigned long rate, dvfs_set_rate_callback set_rate)
{
	unsigned int i;
	int ret = -EINVAL;
	struct cpufreq_freqs freqs;

	freqs.new = rate / 1000;
	freqs.old = rk30_getspeed(0);

	for_each_online_cpu(freqs.cpu) {
		cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
	}
	FREQ_PRINTK_DBG("cpufreq_scale_rate_for_dvfs(%lu)\n", rate);
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

	freqs.new = rk30_getspeed(0);
	/* notifiers */
	for_each_online_cpu(freqs.cpu) {
		cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
	}
	return ret;

}

static int rk30_target(struct cpufreq_policy *policy, unsigned int target_freq, unsigned int relation)
{
	unsigned int i, new_rate = 0;
	int ret = 0;

	if (!freq_table) {
		FREQ_PRINTK_ERR("no freq table!\n");
		return -EINVAL;
	}

	mutex_lock(&cpufreq_mutex);

	if (relation & ENABLE_FURTHER_CPUFREQ)
		no_cpufreq_access--;
	if (no_cpufreq_access) {
#ifdef CONFIG_PM_VERBOSE
		pr_err("denied access to %s as it is disabled temporarily\n", __func__);
#endif
		ret = -EINVAL;
		goto out;
	}
	if (relation & DISABLE_FURTHER_CPUFREQ)
		no_cpufreq_access++;
	relation &= ~MASK_FURTHER_CPUFREQ;

	ret = cpufreq_frequency_table_target(policy, freq_table, target_freq, relation, &i);
	if (ret) {
		FREQ_PRINTK_ERR("no freq match for %d(ret=%d)\n", target_freq, ret);
		goto out;
	}
	new_rate = freq_table[i].frequency;
	if (!no_cpufreq_access)
		new_rate = cpufreq_scale_limt(new_rate, policy);

	FREQ_PRINTK_LOG("cpufreq req=%u,new=%u(was=%u)\n", target_freq, new_rate, rk30_getspeed(0));
	if (new_rate == rk30_getspeed(0))
		goto out;
	ret = clk_set_rate(cpu_clk, new_rate * 1000);
out:
	mutex_unlock(&cpufreq_mutex);
	FREQ_PRINTK_DBG("cpureq set rate (%u) end\n", new_rate);
	return ret;
}

static int rk30_cpufreq_pm_notifier_event(struct notifier_block *this,
					  unsigned long event, void *ptr)
{
	int ret = NOTIFY_DONE;
	struct cpufreq_policy *policy = cpufreq_cpu_get(0);

	if (!policy)
		return ret;

	if (!rk30_cpufreq_is_ondemand_policy(policy))
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

static struct notifier_block rk30_cpufreq_pm_notifier = {
	.notifier_call = rk30_cpufreq_pm_notifier_event,
};

static int rk30_cpufreq_reboot_notifier_event(struct notifier_block *this,
					      unsigned long event, void *ptr)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(0);

	if (policy) {
		cpufreq_driver_target(policy, reboot_freq, DISABLE_FURTHER_CPUFREQ | CPUFREQ_RELATION_L);
		cpufreq_cpu_put(policy);
	}

	return NOTIFY_OK;
}

static struct notifier_block rk30_cpufreq_reboot_notifier = {
	.notifier_call = rk30_cpufreq_reboot_notifier_event,
};

static struct cpufreq_driver rk30_cpufreq_driver = {
	.flags = CPUFREQ_CONST_LOOPS,
	.verify = rk30_verify_speed,
	.target = rk30_target,
	.get = rk30_getspeed,
	.init = rk30_cpu_init,
	.exit = rk30_cpu_exit,
	.name = "rk30",
	.attr = rk30_cpufreq_attr,
};

static int __init rk30_cpufreq_init(void)
{
	register_pm_notifier(&rk30_cpufreq_pm_notifier);
	register_reboot_notifier(&rk30_cpufreq_reboot_notifier);
	return cpufreq_register_driver(&rk30_cpufreq_driver);
}

static void __exit rk30_cpufreq_exit(void)
{
	cpufreq_unregister_driver(&rk30_cpufreq_driver);
}

MODULE_DESCRIPTION("cpufreq driver for rock chip rk30");
MODULE_LICENSE("GPL");
device_initcall(rk30_cpufreq_init);
module_exit(rk30_cpufreq_exit);
