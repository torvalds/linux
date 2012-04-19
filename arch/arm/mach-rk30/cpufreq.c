/* arch/arm/mach-rk29/cpufreq.c
 *
 * Copyright (C) 2010, 2011 ROCKCHIP, Inc.
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

#define FREQ_PRINTK_DBG(fmt, args...) {while(0);}//pr_debug(fmt, ## args)
#define FREQ_PRINTK_ERR(fmt, args...) pr_err(fmt, ## args)
#define FREQ_PRINTK_LOG(fmt, args...) pr_debug(fmt, ## args)
/* Frequency table index must be sequential starting at 0 */
static struct cpufreq_frequency_table default_freq_table[] = {
	{.frequency = 816*1000, .index	= 1080*1000},
	{.frequency = CPUFREQ_TABLE_END},
};
static struct cpufreq_frequency_table *freq_table = default_freq_table;

/*********************************************************/

/* additional symantics for "relation" in cpufreq with pm */
#define DISABLE_FURTHER_CPUFREQ         0x10
#define ENABLE_FURTHER_CPUFREQ          0x20
#define MASK_FURTHER_CPUFREQ            0x30
/* With 0x00(NOCHANGE), it depends on the previous "further" status */
static int no_cpufreq_access;
static unsigned int suspend_freq = 816 * 1000;

#define NUM_CPUS	2
static struct workqueue_struct *freq_wq;
static struct clk *cpu_clk;
//static struct clk *gpu_clk;
//static struct clk *ddr_clk;
//static DEFINE_PER_CPU(unsigned int, target_rate);
static DEFINE_MUTEX(cpufreq_mutex);

static int cpufreq_scale_rate_for_dvfs(struct clk * clk,unsigned long rate,dvfs_set_rate_callback set_rate);

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
	return (c == 'o' || c == 'i' || c == 'c');
}

/**********************thermal limit**************************/
#define CONFIG_RK30_CPU_FREQ_LIMIT_BY_TEMP

#ifdef CONFIG_RK30_CPU_FREQ_LIMIT_BY_TEMP
static void rk30_cpufreq_temp_limit_work_func(struct work_struct *work);

static DECLARE_DELAYED_WORK(rk30_cpufreq_temp_limit_work, rk30_cpufreq_temp_limit_work_func);

static unsigned int temp_limt_freq=0;
#define _TEMP_LIMIT_FREQ 816000
#define TEMP_NOR 55
#define TEMP_HOT 80

#define TEMP_NOR_DELAY 5000 //2s
unsigned int get_detect_temp_dly(int temp)
{
	unsigned int dly=TEMP_NOR_DELAY;
	if(temp>TEMP_NOR)
		dly-=(temp-TEMP_NOR)*25;
	//FREQ_PRINTK_DBG("cpu_thermal delay(%d)\n",dly);
	return dly;
} 
extern int rk30_tsadc_get_temp(unsigned int chn);

#define get_cpu_thermal() rk30_tsadc_get_temp(0)
static void rk30_cpufreq_temp_limit_work_func(struct work_struct *work)
{
	struct cpufreq_policy *policy;
	int temp_rd=0,avg_temp,i;	
	avg_temp=get_cpu_thermal();
	
	for(i=0;i<4;i++)
	{
		temp_rd=get_cpu_thermal();
		avg_temp=(avg_temp+temp_rd)>>1;
	}
	FREQ_PRINTK_LOG("cpu_thermal(%d)\n",temp_rd);
	
	if(avg_temp>TEMP_HOT)
	{
		temp_limt_freq=_TEMP_LIMIT_FREQ;
		policy = cpufreq_cpu_get(0);
		FREQ_PRINTK_DBG("temp_limit set rate %d kHz\n",temp_limt_freq);
		cpufreq_driver_target(policy, policy->cur, CPUFREQ_RELATION_L);
		cpufreq_cpu_put(policy);
	}
	else
		temp_limt_freq=0;
	
	queue_delayed_work(freq_wq, &rk30_cpufreq_temp_limit_work, 
		msecs_to_jiffies(get_detect_temp_dly(avg_temp)));
}

static int rk30_cpufreq_notifier_policy(struct notifier_block *nb,
		unsigned long val, void *data)
{
	struct cpufreq_policy *policy = data;

	if (val != CPUFREQ_NOTIFY)
		return 0;

	if (rk30_cpufreq_is_ondemand_policy(policy)) {
		FREQ_PRINTK_DBG("queue work\n");
		queue_delayed_work(freq_wq, &rk30_cpufreq_temp_limit_work, msecs_to_jiffies(TEMP_NOR_DELAY));
	} else {
		FREQ_PRINTK_DBG("cancel work\n");
		cancel_delayed_work(&rk30_cpufreq_temp_limit_work);
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
static unsigned int freq_dvfs_tst_rate=0;
static void rk30_cpufreq_dvsf_tst_work_func(struct work_struct *work);
static DECLARE_DELAYED_WORK(rk30_cpufreq_dvsf_tst_work, rk30_cpufreq_dvsf_tst_work_func);
static int test_count=0;
#define TEST_FRE_NUM 11
static int test_tlb_rate[TEST_FRE_NUM]={504,1008,504,1200,252,816,1416,252,1512,252,816};
//static int test_tlb_rate[TEST_FRE_NUM]={504,1008,504,1200,252,816,1416,126,1512,126,816};

#define TEST_GPU_NUM 3

static int test_tlb_gpu[TEST_GPU_NUM]={360,400,180};
static int test_tlb_ddr[TEST_GPU_NUM]={401,200,500};


static int gpu_ddr=0;

static void rk30_cpufreq_dvsf_tst_work_func(struct work_struct *work)
{
	struct cpufreq_policy *policy=cpufreq_cpu_get(0);
	
	gpu_ddr++;

#if 0
	FREQ_PRINTK_LOG("cpufreq_dvsf_tst,ddr%u,gpu%u\n",
	test_tlb_ddr[gpu_ddr%TEST_GPU_NUM],test_tlb_gpu[gpu_ddr%TEST_GPU_NUM]);
	clk_set_rate(ddr_clk,test_tlb_ddr[gpu_ddr%TEST_GPU_NUM]*1000*1000);
	clk_set_rate(gpu_clk,test_tlb_gpu[gpu_ddr%TEST_GPU_NUM]*1000*1000);
#endif

	test_count++;
	freq_dvfs_tst_rate=test_tlb_rate[test_count%TEST_FRE_NUM]*1000;
	FREQ_PRINTK_LOG("cpufreq_dvsf_tst,cpu set rate %d\n",freq_dvfs_tst_rate);
	cpufreq_driver_target(policy, policy->cur, CPUFREQ_RELATION_L);
	cpufreq_cpu_put(policy);

	queue_delayed_work(freq_wq, &rk30_cpufreq_dvsf_tst_work, 
		msecs_to_jiffies(1000));
}
#endif
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
		cpu_clk =clk_get(NULL, "cpu");
		if (IS_ERR(cpu_clk))
			return PTR_ERR(cpu_clk);

		dvfs_clk_register_set_rate_callback(cpu_clk,cpufreq_scale_rate_for_dvfs);
		freq_table=dvfs_get_freq_volt_table(cpu_clk);
		if(freq_table==NULL)
		{
			freq_table=default_freq_table;
		}
		clk_enable_dvfs(cpu_clk);

		freq_wq = create_singlethread_workqueue("rk30_cpufreqd");
#ifdef CONFIG_RK30_CPU_FREQ_LIMIT_BY_TEMP
		if (rk30_cpufreq_is_ondemand_policy(policy)) 
		{
			queue_delayed_work(freq_wq, &rk30_cpufreq_temp_limit_work, msecs_to_jiffies(TEMP_NOR_DELAY));
		}
		cpufreq_register_notifier(&notifier_policy_block, CPUFREQ_POLICY_NOTIFIER);
#endif
#ifdef CPU_FREQ_DVFS_TST
		queue_delayed_work(freq_wq, &rk30_cpufreq_dvsf_tst_work, msecs_to_jiffies(20*1000));
#endif
	}

	//set freq min max
	cpufreq_frequency_table_cpuinfo(policy, freq_table);
	//sys nod
	cpufreq_frequency_table_get_attr(freq_table, policy->cpu);
	
	policy->cur = rk30_getspeed(0);

	policy->cpuinfo.transition_latency = 40 * NSEC_PER_USEC; // make ondemand default sampling_rate to 40000


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

/**************************target freq******************************/
static unsigned int cpufreq_scale_limt(unsigned int target_freq,struct cpufreq_policy *policy)
{
	/*
	* If the new frequency is more than the thermal max allowed
	* frequency, go ahead and scale the mpu device to proper frequency.
	*/ 
#ifdef CONFIG_RK30_CPU_FREQ_LIMIT_BY_TEMP
	if(rk30_cpufreq_is_ondemand_policy(policy)&&temp_limt_freq)
	{
		target_freq=min(target_freq,temp_limt_freq);
	}
#endif
#ifdef CPU_FREQ_DVFS_TST
	if(freq_dvfs_tst_rate)
	{
		target_freq=freq_dvfs_tst_rate;
		freq_dvfs_tst_rate=0;
	}
		
#endif		
	return target_freq;
}

int cpufreq_scale_rate_for_dvfs(struct clk * clk,unsigned long rate,dvfs_set_rate_callback set_rate)
{
	unsigned int i;
	int ret=-EINVAL;
	struct cpufreq_freqs freqs;
	
	freqs.new=rate/1000;
	freqs.old=rk30_getspeed(0);
	
	get_online_cpus();
	for_each_online_cpu(freqs.cpu)
		cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
	FREQ_PRINTK_DBG("cpufreq_scale_rate_for_dvfs(%lu)\n",rate);
	ret = set_rate(clk,rate);

#if CONFIG_SMP
	/*
	 * Note that loops_per_jiffy is not updated on SMP systems in
	 * cpufreq driver. So, update the per-CPU loops_per_jiffy value
	 * on frequency transition. We need to update all dependent CPUs.
	 */
	for_each_possible_cpu(i) {
		per_cpu(cpu_data, i).loops_per_jiffy = loops_per_jiffy;
	}
#endif

	freqs.old=freqs.new;
	freqs.new=rk30_getspeed(0);
	/* notifiers */
	for_each_online_cpu(freqs.cpu)
		cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
	put_online_cpus();
	return ret;

}
static int rk30_target(struct cpufreq_policy *policy,
		       unsigned int target_freq,
		       unsigned int relation)
{
	unsigned int i,new_rate=0;
	int ret = 0;

	if (!freq_table) {
		FREQ_PRINTK_ERR("no freq table!\n");
		return -EINVAL;
	}
	
	if (relation & ENABLE_FURTHER_CPUFREQ)
		no_cpufreq_access--;
	if (no_cpufreq_access) {
#ifdef CONFIG_PM_VERBOSE
		pr_err("denied access to %s as it is disabled temporarily\n", __func__);
#endif
		return -EINVAL;
	}
	if (relation & DISABLE_FURTHER_CPUFREQ)
		no_cpufreq_access++;
	relation &= ~MASK_FURTHER_CPUFREQ;

	mutex_lock(&cpufreq_mutex);
	
	ret = cpufreq_frequency_table_target(policy, freq_table, target_freq,
			relation, &i);
	
	if (ret) {
		FREQ_PRINTK_ERR("no freq match for %d(ret=%d)\n",target_freq, ret);
		goto out;
	}
	//FREQ_PRINTK_DBG("%s,tlb rate%u(req %u)*****\n",__func__,freq_table[i].frequency,target_freq);
	//new_rate=freq_table[i].frequency;
	new_rate=cpufreq_scale_limt(freq_table[i].frequency,policy);
	
	FREQ_PRINTK_LOG("cpufreq req=%u,new=%u(was=%u)\n",target_freq,new_rate,rk30_getspeed(0));
	if (new_rate == rk30_getspeed(0))
		goto out;
	ret = clk_set_rate(cpu_clk,new_rate*1000);
out:
	mutex_unlock(&cpufreq_mutex);	
	FREQ_PRINTK_DBG("cpureq set rate (%u) end\n",new_rate);
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
		cpufreq_driver_target(policy, suspend_freq, DISABLE_FURTHER_CPUFREQ | CPUFREQ_RELATION_H);
		cpufreq_cpu_put(policy);
	}

	return NOTIFY_OK;
}

static struct notifier_block rk30_cpufreq_reboot_notifier = {
	.notifier_call = rk30_cpufreq_reboot_notifier_event,
};

static struct cpufreq_driver rk30_cpufreq_driver = {
	.verify		= rk30_verify_speed,
	.target		= rk30_target,
	.get		= rk30_getspeed,
	.init		= rk30_cpu_init,
	.exit		= rk30_cpu_exit,
	.name		= "rk30",
	.attr		= rk30_cpufreq_attr,
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
