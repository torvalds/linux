/*
 * arch/arm/mach-sun4i/cpu-freq/cpu-freq.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Kevin Zhang <kevin@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>
#include "cpu-freq.h"


static struct sun4i_cpu_freq_t  cpu_cur;    /* current cpu frequency configuration  */
static unsigned int last_target = ~0;       /* backup last target frequency         */

static struct clk *clk_pll; /* pll clock handler */
static struct clk *clk_cpu; /* cpu clock handler */
static struct clk *clk_axi; /* axi clock handler */
static struct clk *clk_ahb; /* ahb clock handler */
static struct clk *clk_apb; /* apb clock handler */


#ifdef CONFIG_CPU_FREQ_DVFS
static struct regulator *corevdd;
static unsigned int last_vdd    = 1400;     /* backup last target voltage, default is 1.4v  */
#endif

/*
*********************************************************************************************************
*                           sun4i_cpufreq_verify
*
*Description: check if the cpu frequency policy is valid;
*
*Arguments  : policy    cpu frequency policy;
*
*Return     : result, return if verify ok, else return -EINVAL;
*
*Notes      :
*
*********************************************************************************************************
*/
static int sun4i_cpufreq_verify(struct cpufreq_policy *policy)
{
    if (policy->cpu != 0)
        return -EINVAL;

    return 0;
}


/*
*********************************************************************************************************
*                           sun4i_cpufreq_show
*
*Description: show cpu frequency information;
*
*Arguments  : pfx   name;
*
*
*Return     :
*
*Notes      :
*
*********************************************************************************************************
*/
static void sun4i_cpufreq_show(const char *pfx, struct sun4i_cpu_freq_t *cfg)
{
    CPUFREQ_DBG("%s: pll=%u, cpudiv=%u, axidiv=%u, ahbdiv=%u, apb=%u\n",
        pfx, cfg->pll, cfg->div.s.cpu_div, cfg->div.s.axi_div, cfg->div.s.ahb_div, cfg->div.s.apb_div);
}


#ifdef CONFIG_CPU_FREQ_DVFS
/*
*********************************************************************************************************
*                           __get_vdd_value
*
*Description: get vdd with cpu frequency.
*
*Arguments  : freq  cpu frequency;
*
*Return     : vdd value;
*
*Notes      :
*
*********************************************************************************************************
*/
static inline unsigned int __get_vdd_value(unsigned int freq)
{
    static struct cpufreq_dvfs *dvfs = NULL;
    struct cpufreq_dvfs *dvfs_inf;

    if (unlikely(dvfs == NULL))
        dvfs = sunxi_dvfs_table();

    dvfs_inf = dvfs;
    while((dvfs_inf+1)->freq >= freq) dvfs_inf++;

    return dvfs_inf->volt;
}
#endif


/*
*********************************************************************************************************
*                           __set_cpufreq_hw
*
*Description: set cpu frequency configuration to hardware.
*
*Arguments  : freq  frequency configuration;
*
*Return     : result
*
*Notes      :
*
*********************************************************************************************************
*/
static inline int __set_cpufreq_hw(struct sun4i_cpu_freq_t *freq)
{
    int             ret;
    unsigned int    frequency;

    /* try to adjust pll frequency */
    ret = clk_set_rate(clk_pll, freq->pll);
    /* try to adjust cpu frequency */
    frequency = freq->pll / freq->div.s.cpu_div;
    ret |= clk_set_rate(clk_cpu, frequency);
    /* try to adjuxt axi frequency */
    frequency /= freq->div.s.axi_div;
    ret |= clk_set_rate(clk_axi, frequency);
    /* try to adjust ahb frequency */
    frequency /= freq->div.s.ahb_div;
    ret |= clk_set_rate(clk_ahb, frequency);
    /* try to adjust apb frequency */
    frequency /= freq->div.s.apb_div;
    ret |= clk_set_rate(clk_apb, frequency);

    return ret;
}


/*
*********************************************************************************************************
*                           __set_cpufreq_target
*
*Description: set target frequency, the frequency limitation of axi is 450Mhz, the frequency
*             limitation of ahb is 250Mhz, and the limitation of apb is 150Mhz. for usb connecting,
*             the frequency of ahb must not lower than 60Mhz.
*
*Arguments  : old   cpu/axi/ahb/apb frequency old configuration.
*             new   cpu/axi/ahb/apb frequency new configuration.
*
*Return     : result, 0 - set frequency successed, !0 - set frequency failed;
*
*Notes      : we check two frequency point: 204Mhz, 408Mhz, 816Mhz and 1200Mhz.
*             if increase cpu frequency, the flow should be:
*               low(1:1:1:2) -> 204Mhz(1:1:1:2) -> 204Mhz(1:1:2:2) -> 408Mhz(1:1:2:2)
*               -> 408Mhz(1:2:2:2) -> 816Mhz(1:2:2:2) -> 816Mhz(1:3:2:2) -> 1200Mhz(1:3:2:2)
*               -> 1200Mhz(1:4:2:2) -> target(1:4:2:2) -> target(x:x:x:x)
*             if decrease cpu frequency, the flow should be:
*               high(x:x:x:x) -> target(1:4:2:2) -> 1200Mhz(1:4:2:2) -> 1200Mhz(1:3:2:2)
*               -> 816Mhz(1:3:2:2) -> 816Mhz(1:2:2:2) -> 408Mhz(1:2:2:2) -> 408Mhz(1:1:2:2)
*               -> 204Mhz(1:1:2:2) -> 204Mhz(1:1:1:2) -> target(1:1:1:2)
*********************************************************************************************************
*/
static int __set_cpufreq_target(struct sun4i_cpu_freq_t *old, struct sun4i_cpu_freq_t *new)
{
    int ret = 0;
    int div_table_len;
    unsigned int i = 0;
    unsigned int j = 0;
    struct sun4i_cpu_freq_t old_freq, new_freq;
    static struct cpufreq_div_order *div_order_tbl = NULL;

    if (unlikely(div_order_tbl == NULL))
        div_order_tbl = sunxi_div_order_table(&div_table_len);

    if (!old || !new)
        return -EINVAL;

    old_freq = *old;
    new_freq = *new;

    CPUFREQ_INF("cpu: %dMhz->%dMhz\n", old_freq.pll/1000000, new_freq.pll/1000000);

    /* We're raising our clock */
    if (new_freq.pll > old_freq.pll) {
        /* We have a div table, the old and the new divs,
         * let's change them in order */

        /* Figure out old one */
        while (i < div_table_len-1 &&
              div_order_tbl[i].pll < old_freq.pll) i++;

        /* Figure out new one */
        j = i; /* it's either the same or bigger */
        while (j < div_table_len-1 &&
              div_order_tbl[j].pll < new_freq.pll) j++;

        for (; i < div_table_len-1 && i < j; i++) {
            old_freq.pll = div_order_tbl[i].pll;
            old_freq.div.i = div_order_tbl[i].div;
            ret |= __set_cpufreq_hw(&old_freq);

            old_freq.div.i = div_order_tbl[i+1].div;
            ret |= __set_cpufreq_hw(&old_freq);
        }
    /* We're lowering our clock */
    } else if (new_freq.pll < old_freq.pll) {
        /* We have a div table, the old and the new divs, let's change them in order */

        /* Figure out new one*/
        while (i < div_table_len-1 &&
              div_order_tbl[i].pll < new_freq.pll) i++;

        /* Figure out old one */
        j = i; /* it's either the same or bigger */
        while (j < div_table_len-1 &&
              div_order_tbl[j].pll < old_freq.pll) j++;

        for (; j > 0 && i < j; j--) {
            old_freq.pll = div_order_tbl[j-1].pll;
            old_freq.div.i = div_order_tbl[j].div;
            ret |= __set_cpufreq_hw(&old_freq);

            old_freq.div.i = div_order_tbl[j-1].div;
            ret |= __set_cpufreq_hw(&old_freq);
        }
    }

    /* adjust to target frequency */
    ret |= __set_cpufreq_hw(&new_freq);

    if (ret) {
        unsigned int frequency;

        CPUFREQ_ERR("try to set target frequency failed!\n");

        /* try to restore frequency configuration */
        frequency = clk_get_rate(clk_cpu);
        frequency /= 4;
        clk_set_rate(clk_axi, frequency);
        frequency /= 2;
        clk_set_rate(clk_ahb, frequency);
        frequency /= 2;
        clk_set_rate(clk_apb, frequency);

        clk_set_rate(clk_pll, old->pll);
        frequency = old->pll / old->div.s.cpu_div;
        clk_set_rate(clk_cpu, frequency);
        frequency /= old->div.s.axi_div;
        clk_set_rate(clk_axi, frequency);
        frequency /= old->div.s.ahb_div;
        clk_set_rate(clk_ahb, frequency);
        frequency /= old->div.s.apb_div;
        clk_set_rate(clk_apb, frequency);

        CPUFREQ_ERR(KERN_ERR "no compatible settings cpu freq for %d\n", new_freq.pll);
        return -1;
    }

    return 0;
}


/*
*********************************************************************************************************
*                           sun4i_cpufreq_settarget
*
*Description: adjust cpu frequency;
*
*Arguments  : policy    cpu frequency policy, to mark if need notify;
*             cpu_freq  new cpu frequency configuration;
*
*Return     : return 0 if set successed, otherwise, return -EINVAL
*
*Notes      :
*
*********************************************************************************************************
*/
static int sun4i_cpufreq_settarget(struct cpufreq_policy *policy, struct sun4i_cpu_freq_t *cpu_freq)
{
    struct cpufreq_freqs    freqs;
    struct sun4i_cpu_freq_t cpu_new;

    #ifdef CONFIG_CPU_FREQ_DVFS
    unsigned int    new_vdd;
    #endif

    /* show current cpu frequency configuration, just for debug */
    sun4i_cpufreq_show("cur", &cpu_cur);

    /* get new cpu frequency configuration */
    cpu_new = *cpu_freq;
    sun4i_cpufreq_show("new", &cpu_new);

    /* notify that cpu clock will be adjust if needed */
    if (policy) {
        freqs.cpu = 0;
        freqs.old = cpu_cur.pll / 1000;
        freqs.new = cpu_new.pll / 1000;
        cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
    }

    #ifdef CONFIG_CPU_FREQ_DVFS
    /* get vdd value for new frequency */
    new_vdd = __get_vdd_value(cpu_new.pll);

    if(corevdd && (new_vdd > last_vdd)) {
        CPUFREQ_INF("set core vdd to %d\n", new_vdd);
        if(regulator_set_voltage(corevdd, new_vdd*1000, new_vdd*1000)) {
            CPUFREQ_INF("try to set voltage failed!\n");

            /* notify everyone that clock transition finish */
    	    if (policy) {
                freqs.cpu = 0;
                freqs.old = freqs.new;
                freqs.new = cpu_cur.pll / 1000;
                cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
            }
            return -EINVAL;
        }
    }
    #endif

    if(__set_cpufreq_target(&cpu_cur, &cpu_new)){

        /* try to set cpu frequency failed */

        #ifdef CONFIG_CPU_FREQ_DVFS
        if(corevdd && (new_vdd > last_vdd)) {
            CPUFREQ_INF("set core vdd to %d\n", last_vdd);
            if(regulator_set_voltage(corevdd, last_vdd*1000, last_vdd*1000)){
                CPUFREQ_INF("try to set voltage failed!\n");
                last_vdd = new_vdd;
            }
        }
        #endif

        /* notify everyone that clock transition finish */
    	if (policy) {
            freqs.cpu = 0;
            freqs.old = freqs.new;
            freqs.new = cpu_cur.pll / 1000;
            cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
        }

        return -EINVAL;
    }

    #ifdef CONFIG_CPU_FREQ_DVFS
    if(corevdd && (new_vdd < last_vdd)) {
        CPUFREQ_INF("set core vdd to %d\n", new_vdd);
        if(regulator_set_voltage(corevdd, new_vdd*1000, new_vdd*1000)) {
            CPUFREQ_INF("try to set voltage failed!\n");
            new_vdd = last_vdd;
        }
    }
    last_vdd = new_vdd;
    #endif

    /* update our current settings */
    cpu_cur = cpu_new;

    /* notify everyone we've done this */
    if (policy) {
        cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
    }

    CPUFREQ_DBG("%s: finished\n", __func__);
    return 0;
}


/*
*********************************************************************************************************
*                           sun4i_cpufreq_target
*
*Description: adjust the frequency that cpu is currently running;
*
*Arguments  : policy    cpu frequency policy;
*             freq      target frequency to be set, based on khz;
*             relation  method for selecting the target requency;
*
*Return     : result, return 0 if set target frequency successed,
*                     else, return -EINVAL;
*
*Notes      : this function is called by the cpufreq core;
*
*********************************************************************************************************
*/
static int sun4i_cpufreq_target(struct cpufreq_policy *policy, __u32 freq, __u32 relation)
{
    int                     ret;
    unsigned int            index;
    struct sun4i_cpu_freq_t freq_cfg;
    static struct cpufreq_frequency_table *sun4i_freq_tbl = NULL;

    if (unlikely(sun4i_freq_tbl == NULL))
        sun4i_freq_tbl = sunxi_cpufreq_table();

    /* avoid repeated calls which cause a needless amout of duplicated
     * logging output (and CPU time as the calculation process is
     * done) */
    if (freq == last_target) {
        return 0;
    }

    /* try to look for a valid frequency value from cpu frequency table */
    if (cpufreq_frequency_table_target(policy, sun4i_freq_tbl, freq, relation, &index)) {
        CPUFREQ_ERR("%s: try to look for a valid frequency for %u failed!\n", __func__, freq);
        return -EINVAL;
    }

    if (sun4i_freq_tbl[index].frequency == last_target) {
        /* frequency is same as the value last set, need not adjust */
        return 0;
    }
    freq = sun4i_freq_tbl[index].frequency;

    /* update the target frequency */
    freq_cfg.pll = sun4i_freq_tbl[index].frequency * 1000;
    freq_cfg.div.i = sun4i_freq_tbl[index].index;
    CPUFREQ_DBG("%s: target frequency find is %u, entry %u\n", __func__, freq_cfg.pll, index);

    /* try to set target frequency */
    ret = sun4i_cpufreq_settarget(policy, &freq_cfg);
    if(!ret) {
        last_target = freq;
    }

    return ret;
}


/*
*********************************************************************************************************
*                           sun4i_cpufreq_get
*
*Description: get the frequency that cpu currently is running;
*
*Arguments  : cpu   cpu number;
*
*Return     : cpu frequency, based on khz;
*
*Notes      :
*
*********************************************************************************************************
*/
static unsigned int sun4i_cpufreq_get(unsigned int cpu)
{
    return clk_get_rate(clk_cpu) / 1000;
}


/*
*********************************************************************************************************
*                           sun4i_cpufreq_init
*
*Description: cpu frequency initialise a policy;
*
*Arguments  : policy    cpu frequency policy;
*
*Return     : result, return 0 if init ok, else, return -EINVAL;
*
*Notes      :
*
*********************************************************************************************************
*/
static int sun4i_cpufreq_init(struct cpufreq_policy *policy)
{
    CPUFREQ_DBG(KERN_INFO "%s: initialising policy %p\n", __func__, policy);

    if (policy->cpu != 0)
        return -EINVAL;

    policy->cur = sun4i_cpufreq_get(0);
    policy->min = policy->cpuinfo.min_freq = SUN4I_CPUFREQ_MIN / 1000;
    policy->max = policy->cpuinfo.max_freq = SUN4I_CPUFREQ_MAX / 1000;
    policy->governor = CPUFREQ_DEFAULT_GOVERNOR;

    /* feed the latency information from the cpu driver */
    policy->cpuinfo.transition_latency = SUN4I_FREQTRANS_LATENCY;

    return 0;
}


/*
*********************************************************************************************************
*                           sun4i_cpufreq_getcur
*
*Description: get current cpu frequency configuration;
*
*Arguments  : cfg   cpu frequency cofniguration;
*
*Return     : result;
*
*Notes      :
*
*********************************************************************************************************
*/
static int sun4i_cpufreq_getcur(struct sun4i_cpu_freq_t *cfg)
{
    unsigned int    freq, freq0;

    if(!cfg) {
        return -EINVAL;
    }

    cfg->pll = clk_get_rate(clk_pll);
    freq = clk_get_rate(clk_cpu);
    cfg->div.s.cpu_div = cfg->pll / freq;
    freq0 = clk_get_rate(clk_axi);
    cfg->div.s.axi_div = freq / freq0;
    freq = clk_get_rate(clk_ahb);
    cfg->div.s.ahb_div = freq0 / freq;
    freq0 = clk_get_rate(clk_apb);
    cfg->div.s.apb_div = freq /freq0;

    return 0;
}



#ifdef CONFIG_PM

/* variable for backup cpu frequency configuration */
static struct sun4i_cpu_freq_t suspend_freq;

/*
*********************************************************************************************************
*                           sun4i_cpufreq_suspend
*
*Description: back up cpu frequency configuration for suspend;
*
*Arguments  : policy    cpu frequency policy;
*             pmsg      power management message;
*
*Return     : return 0,
*
*Notes      :
*
*********************************************************************************************************
*/
static int sun4i_cpufreq_suspend(struct cpufreq_policy *policy)
{
    struct sun4i_cpu_freq_t suspend;

    CPUFREQ_DBG("%s, set cpu frequency to 60Mhz to prepare enter standby\n", __func__);

    sun4i_cpufreq_getcur(&suspend_freq);

    /* set cpu frequency to 60M hz for standby */
    suspend.pll = 60000000;
    suspend.div.s.cpu_div = 1;
    suspend.div.s.axi_div = 1;
    suspend.div.s.ahb_div = 1;
    suspend.div.s.apb_div = 2;
    __set_cpufreq_target(&suspend_freq, &suspend);

    return 0;
}


/*
*********************************************************************************************************
*                           sun4i_cpufreq_resume
*
*Description: cpu frequency configuration resume;
*
*Arguments  : policy    cpu frequency policy;
*
*Return     : result;
*
*Notes      :
*
*********************************************************************************************************
*/
static int sun4i_cpufreq_resume(struct cpufreq_policy *policy)
{
    struct sun4i_cpu_freq_t suspend;

    /* invalidate last_target setting */
    last_target = ~0;

    CPUFREQ_DBG("%s: resuming with policy %p\n", __func__, policy);
    sun4i_cpufreq_getcur(&suspend);

    /* restore cpu frequency configuration */
    __set_cpufreq_target(&suspend, &suspend_freq);

    CPUFREQ_DBG("%s: resuming done\n", __func__);
    return 0;
}


#else   /* #ifdef CONFIG_PM */

#define sun4i_cpufreq_suspend   NULL
#define sun4i_cpufreq_resume    NULL

#endif  /* #ifdef CONFIG_PM */

static struct freq_attr *sun4i_cpufreq_attr[] = {
    &cpufreq_freq_attr_scaling_available_freqs,
    NULL,
};

static struct cpufreq_driver sun4i_cpufreq_driver = {
    .flags		= CPUFREQ_STICKY,
    .verify		= sun4i_cpufreq_verify,
    .target		= sun4i_cpufreq_target,
    .get		= sun4i_cpufreq_get,
    .init		= sun4i_cpufreq_init,
    .suspend	= sun4i_cpufreq_suspend,
    .resume		= sun4i_cpufreq_resume,
    .name		= "sun4i",
    .attr		= sun4i_cpufreq_attr,
};


/*
*********************************************************************************************************
*                           sun4i_cpufreq_initclks
*
*Description: init cpu frequency clock resource;
*
*Arguments  : none
*
*Return     : result;
*
*Notes      :
*
*********************************************************************************************************
*/
static __init int sun4i_cpufreq_initclks(void)
{
    clk_pll = clk_get(NULL, "core_pll");
    clk_cpu = clk_get(NULL, "cpu");
    clk_axi = clk_get(NULL, "axi");
    clk_ahb = clk_get(NULL, "ahb");
    clk_apb = clk_get(NULL, "apb");

    if (IS_ERR(clk_pll) || IS_ERR(clk_cpu) || IS_ERR(clk_axi) ||
        IS_ERR(clk_ahb) || IS_ERR(clk_apb)) {
        CPUFREQ_INF(KERN_ERR "%s: could not get clock(s)\n", __func__);
        return -ENOENT;
    }

    CPUFREQ_INF("%s: clocks pll=%lu,cpu=%lu,axi=%lu,ahp=%lu,apb=%lu\n", __func__,
           clk_get_rate(clk_pll), clk_get_rate(clk_cpu), clk_get_rate(clk_axi),
           clk_get_rate(clk_ahb), clk_get_rate(clk_apb));

    #ifdef CONFIG_CPU_FREQ_DVFS
    corevdd = regulator_get(NULL, "axp20_core");
    if (IS_ERR(corevdd)) {
        CPUFREQ_INF("try to get regulator failed, core vdd will not changed!\n");
        corevdd = NULL;
    } else {
        CPUFREQ_INF("try to get regulator(0x%x) successed.\n", (__u32)corevdd);
        last_vdd = regulator_get_voltage(corevdd) / 1000;
    }
    #endif

    return 0;
}


/*
*********************************************************************************************************
*                           sun4i_cpufreq_initcall
*
*Description: cpu frequency driver initcall
*
*Arguments  : none
*
*Return     : result
*
*Notes      :
*
*********************************************************************************************************
*/
static int __init sun4i_cpufreq_initcall(void)
{
    int ret = 0;
    struct cpufreq_frequency_table *sun4i_freq_tbl = sunxi_cpufreq_table();

    /* initialise some clock resource */
    ret = sun4i_cpufreq_initclks();
    if(ret) {
        return ret;
    }

    /* initialise current frequency configuration */
    sun4i_cpufreq_getcur(&cpu_cur);
    sun4i_cpufreq_show("cur", &cpu_cur);

    /* register cpu frequency driver */
    ret = cpufreq_register_driver(&sun4i_cpufreq_driver);
    /* register cpu frequency table to cpufreq core */
    cpufreq_frequency_table_get_attr(sun4i_freq_tbl, 0);
    /* update policy for boot cpu */
    cpufreq_update_policy(0);

    return ret;
}
late_initcall(sun4i_cpufreq_initcall);
