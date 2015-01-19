/*
 *
 * arch/arm/plat-meson/cpu_freq.c
 *
 *  Copyright (C) 2010 AMLOGIC, INC.
 *
 * License terms: GNU General Public License (GPL) version 2
 * CPU frequence management.
 *
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/input.h>

//#include <mach/hardware.h>
#include <mach/clock.h>
#include <mach/cpufreq_table.h>
#include <plat/cpufreq.h>
#include <asm/system.h>
#include <asm/smp_plat.h>
#include <asm/cpu.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/machine.h>
#include <linux/amlogic/aml_dvfs.h>
#include "voltage.h"


struct meson_cpufreq {
    struct device *dev;
    struct clk *armclk;
};

static struct meson_cpufreq cpufreq;
#ifdef CONFIG_FIX_SYSPLL
static int fix_syspll = 0;
#endif

static DEFINE_MUTEX(meson_cpufreq_mutex);

static void adjust_jiffies(unsigned int freqOld, unsigned int freqNew);

//static struct cpufreq_frequency_table *p_meson_freq_table;


static int meson_cpufreq_verify(struct cpufreq_policy *policy)
{
    struct cpufreq_frequency_table *freq_table = cpufreq_frequency_get_table(policy->cpu);

    if (freq_table) {
        return cpufreq_frequency_table_verify(policy, freq_table);
    }

    if (policy->cpu)
        return -EINVAL;

    cpufreq_verify_within_limits(policy, policy->cpuinfo.min_freq,
                                 policy->cpuinfo.max_freq);

    policy->min = clk_round_rate(cpufreq.armclk, policy->min * 1000) / 1000;
    policy->max = clk_round_rate(cpufreq.armclk, policy->max * 1000) / 1000;
    cpufreq_verify_within_limits(policy, policy->cpuinfo.min_freq,
                                 policy->cpuinfo.max_freq);
    return 0;
}

static int early_suspend_flag = 0;
#if (defined CONFIG_SMP) && (defined CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>
static void meson_system_early_suspend(struct early_suspend *h)
{
    early_suspend_flag=1;
}

static void meson_system_late_resume(struct early_suspend *h)
{
    early_suspend_flag=0;

}
static struct early_suspend early_suspend={
        .level = EARLY_SUSPEND_LEVEL_DISABLE_FB,
            .suspend = meson_system_early_suspend,
            .resume = meson_system_late_resume,

};

#endif
static int meson_cpufreq_target_locked(struct cpufreq_policy *policy,
                                       unsigned int target_freq,
                                       unsigned int relation)
{
    struct cpufreq_freqs freqs;
    uint cpu = policy ? policy->cpu : 0;
    int ret = -EINVAL;
    unsigned int freqInt = 0;
#ifdef CONFIG_FIX_SYSPLL
	struct cpufreq_frequency_table *freq_table = NULL;
    unsigned int freq_new, index;
#endif

	if (cpu > (NR_CPUS - 1)) {
        printk(KERN_ERR"cpu %d set target freq error\n",cpu);
        return ret;
    }

#if (defined CONFIG_SMP) && (defined CONFIG_HAS_EARLYSUSPEND)
//    if(early_suspend_flag)
//    {
//        printk("suspend in progress target_freq=%d\n",target_freq);
//        return -EINVAL;
//    }
#endif
    /* Ensure desired rate is within allowed range.  Some govenors
     * (ondemand) will just pass target_freq=0 to get the minimum. */
    if (policy) {
        if (target_freq < policy->min) {
            target_freq = policy->min;
        }
        if (target_freq > policy->max) {
            target_freq = policy->max;
        }
    }

#ifdef CONFIG_FIX_SYSPLL
    /*
     * CPU frequent should only select from aviliable frequent table 
     * if under fix syspll mode
     */
    if (fix_syspll) {
        freq_table = cpufreq_frequency_get_table(policy->cpu);
        ret        = cpufreq_frequency_table_target(policy, freq_table, target_freq,
        		                                    CPUFREQ_RELATION_H, &index);
        if(ret >= 0) {
        	freq_new = freq_table[index].frequency;
            target_freq = freq_new;
        } else {
            printk(KERN_ERR"input frequent :%d cannot found in frequent table, ret:%d\n", target_freq, ret);
        }
    }
#endif

    freqs.old = clk_get_rate(cpufreq.armclk) / 1000;
    freqs.new = clk_round_rate(cpufreq.armclk, target_freq * 1000) / 1000;
    freqs.cpu = cpu;

    if (freqs.old == freqs.new) {
        return ret;
    }


    cpufreq_notify_transition(policy, &freqs, CPUFREQ_PRECHANGE);

#ifndef CONFIG_CPU_FREQ_DEBUG
    pr_debug("cpufreq-meson: CPU%d transition: %u --> %u\n",
           freqs.cpu, freqs.old, freqs.new);
#endif


    /* if moving to higher frequency, move to an intermediate frequency
     * that does not require a voltage change first.
     */
    ret = aml_dvfs_freq_change(AML_DVFS_ID_VCCK, freqs.new, freqs.old, AML_DVFS_FREQ_PRECHANGE);
    if (ret) {
        goto out;
    }

    if (freqs.new > freqs.old)
        adjust_jiffies(freqInt != 0 ? freqInt : freqs.old, freqs.new);

    ret = clk_set_rate(cpufreq.armclk, freqs.new * 1000);
    if (ret)
        goto out;


    freqs.new = clk_get_rate(cpufreq.armclk) / 1000;
    if (freqs.new < freqs.old)
        adjust_jiffies(freqs.old, freqs.new);


    /* if moving to lower freq, lower the voltage after lowering freq
     * This should be done after CPUFREQ_PRECHANGE, which will adjust lpj and
     * affect our udelays.
     */
    ret = aml_dvfs_freq_change(AML_DVFS_ID_VCCK, freqs.new, freqs.old, AML_DVFS_FREQ_POSTCHANGE);
    if (ret) {
        goto out;
    }

out:
    freqs.new = clk_get_rate(cpufreq.armclk) / 1000;
    if (ret) {
        adjust_jiffies(freqInt != 0 ? freqInt : freqs.old, freqs.new);
    }
    cpufreq_notify_transition(policy, &freqs, CPUFREQ_POSTCHANGE);

    return ret;
}

static int meson_cpufreq_target(struct cpufreq_policy *policy,
                                unsigned int target_freq,
                                unsigned int relation)
{
    int ret;

    mutex_lock(&meson_cpufreq_mutex);
    ret = meson_cpufreq_target_locked(policy, target_freq, relation);
    mutex_unlock(&meson_cpufreq_mutex);

    return ret;
}

static unsigned int meson_cpufreq_get(unsigned int cpu)
{
    unsigned long rate;
    if(cpu > (NR_CPUS-1))
    {
        printk(KERN_ERR "cpu %d on current thread error\n",cpu);
        return 0;
    }
    rate = clk_get_rate(cpufreq.armclk) / 1000;
    return rate;
}

static int meson_cpufreq_init(struct cpufreq_policy *policy)
{
    struct cpufreq_frequency_table *freq_table = NULL;
    int index = 0;

    if (policy->cpu != 0)
        return -EINVAL;

    if (policy->cpu > (NR_CPUS - 1)) {
        printk(KERN_ERR "cpu %d on current thread error\n", policy->cpu);
        return -1;
    }
#if 0
    /* Finish platform specific initialization */
    freq_table = aml_dvfs_get_freq_table(AML_DVFS_ID_VCCK);
    if (freq_table) {
	    cpufreq_frequency_table_get_attr(freq_table,
                                         policy->cpu);
    } else {
#endif
#ifdef CONFIG_FIX_SYSPLL
    if (fix_syspll) {           // select fix pll table if syspll_fixed is enabled in dts
	    cpufreq_frequency_table_get_attr(meson_freq_table_fix_syspll,
                                         policy->cpu);
    } else {
#endif
	    cpufreq_frequency_table_get_attr(meson_freq_table,
                                         policy->cpu);
#ifdef CONFIG_FIX_SYSPLL
    }
#endif
#if 0
    }
#endif
    freq_table = cpufreq_frequency_get_table(policy->cpu);
	while(freq_table[index].frequency != CPUFREQ_TABLE_END)
		index++;
	index -= 1;

	policy->min = freq_table[0].frequency;
	policy->max = freq_table[index].frequency;
	policy->cpuinfo.min_freq = clk_round_rate(cpufreq.armclk, 0) / 1000;
    policy->cpuinfo.max_freq = clk_round_rate(cpufreq.armclk, 0xffffffff) / 1000;

	if(policy->min < policy->cpuinfo.min_freq)
		policy->min = policy->cpuinfo.min_freq;
	if(policy->max > policy->cpuinfo.max_freq)
		policy->max = policy->cpuinfo.max_freq;

	policy->cur =  clk_round_rate(cpufreq.armclk, clk_get_rate(cpufreq.armclk)) / 1000;;

	/* FIXME: what's the actual transition time? */
    policy->cpuinfo.transition_latency = 200 * 1000;

	if (is_smp()) {
	 /* Both cores must be set to same frequency.  Set affected_cpus to all. */
//		policy->shared_type = CPUFREQ_SHARED_TYPE_ANY;
		cpumask_setall(policy->cpus);
	}

    return 0;
}

static struct freq_attr *meson_cpufreq_attr[] = {
    &cpufreq_freq_attr_scaling_available_freqs,
    NULL,
};

static unsigned sleep_freq;
static int meson_cpufreq_suspend(struct cpufreq_policy *policy)
{
    /* Ok, this could be made a bit smarter, but let's be robust for now. We
     * always force a speed change to high speed before sleep, to make sure
     * we have appropriate voltage and/or bus speed for the wakeup process,
     */

    //mutex_lock(&meson_cpufreq_mutex);
    preempt_disable();

    sleep_freq = clk_get_rate(cpufreq.armclk) / 1000;
    printk("cpufreq suspend sleep_freq=%dMhz max=%dMHz\n", sleep_freq/1000, policy->max/1000);

    if (policy->max > sleep_freq) {
        int ret = aml_dvfs_freq_change(AML_DVFS_ID_VCCK,
                                       policy->max,
                                       sleep_freq,
                                       AML_DVFS_FREQ_PRECHANGE);
        if (ret) {
            pr_err("failed to set voltage %d\n", ret);
            //mutex_unlock(&meson_cpufreq_mutex);
            preempt_enable();
            return 0;
        }
        adjust_jiffies(sleep_freq, policy->max);
    }
    clk_set_rate(cpufreq.armclk, policy->max * 1000);

    //mutex_unlock(&meson_cpufreq_mutex);
    preempt_enable();
    return 0;
}

static int meson_cpufreq_resume(struct cpufreq_policy *policy)
{
    unsigned cur;
	int ret;

    printk("cpufreq resume sleep_freq=%dMhz\n", sleep_freq/1000);

    //mutex_lock(&meson_cpufreq_mutex);
	preempt_disable();

    clk_set_rate(cpufreq.armclk, sleep_freq * 1000);
    cur = clk_get_rate(cpufreq.armclk) / 1000;
    if (policy->max > cur) {
        adjust_jiffies(policy->max, cur);
		ret = aml_dvfs_freq_change(AML_DVFS_ID_VCCK,
                                       sleep_freq,
                                       policy->max,
                                       AML_DVFS_FREQ_POSTCHANGE);
        if (ret) {
            pr_err("failed to set voltage %d\n", ret);
            //mutex_unlock(&meson_cpufreq_mutex);
            preempt_enable();
            return 0;
        }
    }
    //mutex_unlock(&meson_cpufreq_mutex);
    preempt_enable();
    return 0;
}

static struct cpufreq_driver meson_cpufreq_driver = {
    .flags      = CPUFREQ_STICKY,
    .verify     = meson_cpufreq_verify,
    .target     = meson_cpufreq_target,
    .get        = meson_cpufreq_get,
    .init       = meson_cpufreq_init,
    .name       = "meson_cpufreq",
    .attr       = meson_cpufreq_attr,
    .suspend    = meson_cpufreq_suspend,
    .resume     = meson_cpufreq_resume
};

static int __init meson_cpufreq_probe(struct platform_device *pdev)
{
		int voltage_control = 0;

#ifdef CONFIG_USE_OF
		const void *prop;
#ifdef CONFIG_FIX_SYSPLL
    int err = 0;
    if (pdev->dev.of_node) {
        err = of_property_read_bool(pdev->dev.of_node, "syspll_fixed");
        if (err) {
            printk("%s:SYSPLL request to be fixed\n", __func__);
            fix_syspll = 1;
        }
    }
#endif
		if (pdev->dev.of_node) {
			prop = of_get_property(pdev->dev.of_node, "voltage_control", NULL);
			if(prop)
				voltage_control = of_read_ulong(prop,1);
			else{
				printk("meson_cpufreq: no voltage_control prop\n");
			}

			printk("voltage_control = %d\n",voltage_control);
		}
#endif

    cpufreq.dev = &pdev->dev;
    cpufreq.armclk = clk_get_sys("a9_clk", NULL);
    if (IS_ERR(cpufreq.armclk)) {
        dev_err(cpufreq.dev, "Unable to get ARM clock\n");
        return PTR_ERR(cpufreq.armclk);
    }

   return cpufreq_register_driver(&meson_cpufreq_driver);
}


static int __exit meson_cpufreq_remove(struct platform_device *pdev)
{
    return cpufreq_unregister_driver(&meson_cpufreq_driver);
}

#ifdef CONFIG_OF
static const struct of_device_id amlogic_cpufreq_meson_dt_match[]={
	{	.compatible = "amlogic,cpufreq-meson",
	},
	{},
};
#else
#define amlogic_cpufreq_meson_dt_match NULL
#endif


static struct platform_driver meson_cpufreq_parent_driver = {
    .driver = {
        .name   = "cpufreq-meson",
        .owner  = THIS_MODULE,
        .of_match_table = amlogic_cpufreq_meson_dt_match,
    },
    .remove = meson_cpufreq_remove,
};

static int __init meson_cpufreq_parent_init(void)
{
#if (defined CONFIG_SMP) && (defined CONFIG_HAS_EARLYSUSPEND)

//    early_suspend.param = pdev;
    register_early_suspend(&early_suspend);
#endif


    return platform_driver_probe(&meson_cpufreq_parent_driver,
                                 meson_cpufreq_probe);

//	return meson_cpufreq_probe((struct platform_device *)&cpufreq);
}
late_initcall(meson_cpufreq_parent_init);


/* assumes all CPUs run at same frequency */
static unsigned long global_l_p_j_ref;
static unsigned long global_l_p_j_ref_freq;

static void adjust_jiffies(unsigned int freqOld, unsigned int freqNew)
{
    int i;

    if (!global_l_p_j_ref) {
        global_l_p_j_ref = loops_per_jiffy;
        global_l_p_j_ref_freq = freqOld;
    }

    loops_per_jiffy = cpufreq_scale(global_l_p_j_ref,
                                    global_l_p_j_ref_freq,
                                    freqNew);
#ifdef	CONFIG_SMP
    for_each_present_cpu(i) {
        per_cpu(cpu_data, i).loops_per_jiffy = loops_per_jiffy;
    }
#endif
}

#ifdef CONFIG_ARCH_MESON6
int meson_cpufreq_boost(unsigned int freq)
{
    int ret = 0;
	struct cpufreq_policy * policy = NULL;

    if (!early_suspend_flag) {
        // only allow freq boost when not in early suspend
        //check last_cpu_rate. inaccurate but no lock
        //printk("%u %u\n", last_cpu_rate, freq);
        //if (last_cpu_rate < freq) {
        if ((clk_get_rate(cpufreq.armclk) / 1000) < freq) {
            mutex_lock(&meson_cpufreq_mutex);
            if ((clk_get_rate(cpufreq.armclk) / 1000) < freq) {
				policy = cpufreq_cpu_get(0);
                ret = meson_cpufreq_target_locked(policy,
                        freq,
                        CPUFREQ_RELATION_H);
            }
            mutex_unlock(&meson_cpufreq_mutex);
        }
        //}
    }
    return ret;
}
#endif
