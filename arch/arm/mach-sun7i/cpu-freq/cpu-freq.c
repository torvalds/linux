/*
 *  arch/arm/mach-sun7i/cpu-freq/cpu-freq.c
 *
 * Copyright (c) 2012 Allwinner.
 * kevin.z.m (kevin@allwinnertech.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <plat/sys_config.h>
#include <linux/cpu.h>
#include <asm/cpu.h>

#include "cpu-freq.h"
#include <linux/pm.h>
#include <linux/regulator/consumer.h>
#include <mach/includes.h>

#define AHB_APB_CLK_ASYNC

static struct sunxi_cpu_freq_t  cpu_cur;    /* current cpu frequency configuration  */
static unsigned int last_target = ~0;       /* backup last target frequency         */

static struct clk *clk_pll; /* pll clock handler */
static struct clk *clk_cpu; /* cpu clock handler */
static struct clk *clk_axi; /* axi clock handler */
static struct clk *clk_ahb; /* ahb clock handler */
static struct clk *clk_apb; /* apb clock handler */
#ifdef AHB_APB_CLK_ASYNC
static struct clk *clk_sata_pll; /* apb clock handler */
#endif

static DEFINE_MUTEX(sunxi_cpu_lock);

static unsigned int cpu_freq_max = SUNXI_CPUFREQ_MAX / 1000;
static unsigned int cpu_freq_min = SUNXI_CPUFREQ_MIN / 1000;

#ifdef CONFIG_SMP
static struct cpumask sunxi_cpumask;
static int cpus_initialized;
#endif

int setgetfreq_debug = 0;
#ifdef CONFIG_CPU_FREQ_SETFREQ_DEBUG
unsigned long long setfreq_time_usecs = 0;
unsigned long long getfreq_time_usecs = 0;
#endif
#ifdef CONFIG_CPU_FREQ_DVFS
#define TABLE_LENGTH (16)
struct cpufreq_dvfs {
    unsigned int    freq;   /* cpu frequency    */
    unsigned int    volt;   /* voltage for the frequency    */
};
static struct cpufreq_dvfs dvfs_table[] = {
    {.freq = 1008000000, .volt = 1450}, /* core vdd is 1.40v if cpu frequency is (912Mhz,  1008Mhz] */
    {.freq = 912000000,  .volt = 1400}, /* core vdd is 1.40v if cpu frequency is (864Mhz,   912Mhz] */
    {.freq = 864000000,  .volt = 1300}, /* core vdd is 1.30v if cpu frequency is (720Mhz,   864Mhz] */
    {.freq = 720000000,  .volt = 1200}, /* core vdd is 1.20v if cpu frequency is (528Mhz,   720Mhz] */
    {.freq = 528000000,  .volt = 1100}, /* core vdd is 1.10v if cpu frequency is (336Mhz,   528Mhz] */
    {.freq = 312000000,  .volt = 1000}, /* core vdd is 1.00v if cpu frequency is (144Mhz,   312Mhz] */
    {.freq = 144000000,  .volt = 900},  /* core vdd is 0.90v if cpu frequency is (  0Mhz,   144Mhz] */
    {.freq = 0,          .volt = 900},  /* end of cpu dvfs table                                    */
};
static struct cpufreq_dvfs dvfs_table_syscfg[TABLE_LENGTH];
static unsigned int table_length_syscfg = 0;
static int use_default_table = 0;
static struct regulator *corevdd;
static unsigned int last_vdd    = 1400;     /* backup last target voltage, default is 1.4v  */
#endif

/*
 *check if the cpu frequency policy is valid;
 */
static int sunxi_cpufreq_verify(struct cpufreq_policy *policy)
{
    return 0;
}


/*
 *show cpu frequency information;
 */
static void sunxi_cpufreq_show(const char *pfx, struct sunxi_cpu_freq_t *cfg)
{
#ifndef AHB_APB_CLK_ASYNC
	CPUFREQ_DBG("%s: pll=%u, cpudiv=%u, axidiv=%u, ahbdiv=%u, apb=%u\n",
        pfx, cfg->pll, cfg->div.cpu_div, cfg->div.axi_div, cfg->div.ahb_div, cfg->div.apb_div);
#else
    CPUFREQ_DBG("%s: pll=%u, cpudiv=%u, axidiv=%u\n", pfx, cfg->pll, cfg->div.cpu_div, cfg->div.axi_div);
#endif
}


#ifdef CONFIG_CPU_FREQ_DVFS
/*
*********************************************************************************************************
*                           __init_vftable_syscfg
*
*Description: init vftable from sysconfig.
*
*Arguments  : none;
*
*Return     : result, 0 - init vftable successed, !0 - init vftable failed;
*
*Notes      : LV1: core vdd is 1.50v if cpu frequency is (1008Mhz, 1056Mhz]
*             LV2: core vdd is 1.40v if cpu frequency is (912Mhz,  1008Mhz]
*             LV3: core vdd is 1.35v if cpu frequency is (864Mhz,   912Mhz]
*             LV4: core vdd is 1.30v if cpu frequency is (624Mhz,   864Mhz]
*             LV5: core vdd is 1.25v if cpu frequency is (60Mhz,    624Mhz]
*
*********************************************************************************************************
*/
static int __init_vftable_syscfg(void)
{
	int i, ret, level_freq, level_volt;
	char name[16] = {0};

	if (script_parser_fetch("dvfs_table", "LV_count", &table_length_syscfg,
				sizeof(int)) != 0) {
		CPUFREQ_ERR("get LV_count from sysconfig failed\n");
		use_default_table = 1;
		ret = -1;
		goto fail;
	}

	/* table_length_syscfg must be < TABLE_LENGTH */
	if(table_length_syscfg >= TABLE_LENGTH){
		CPUFREQ_ERR("LV_count from sysconfig is out of bounder\n");
		use_default_table = 1;
		ret = -1;
		goto fail;
	}

	for (i = 1; i <= table_length_syscfg; i++){
		sprintf(name, "LV%d_freq", i);
		if (script_parser_fetch("dvfs_table", name, &level_freq,
					sizeof(int)) != 0) {
			CPUFREQ_ERR("get LV%d_freq from sysconfig failed\n", i);
			use_default_table = 1;
			ret = -1;
			goto fail;
		}

		sprintf(name, "LV%d_volt", i);
		if (script_parser_fetch("dvfs_table", name, &level_volt,
					sizeof(int)) != 0) {
			CPUFREQ_ERR("get LV%d_freq from sysconfig failed\n", i);
			use_default_table = 1;
			ret = -1;
			goto fail;
		}

		dvfs_table_syscfg[i-1].freq = level_freq;
		dvfs_table_syscfg[i-1].volt = level_volt;
	}

	/* end of cpu dvfs table */
	dvfs_table_syscfg[table_length_syscfg].freq = 0;
	dvfs_table_syscfg[table_length_syscfg].volt = 1000;

fail:
	return ret;
}

/*
*********************************************************************************************************
*                           __vftable_show
*
*Description: show vftable information
*
*Arguments  : none;
*
*Return     : none;
*
*Notes      :
*
*********************************************************************************************************
*/
static void __vftable_show(void)
{
	int i;

	CPUFREQ_INF("-------------------V-F Table-------------------\n");
	if(use_default_table){
		for(i = 0; i < sizeof(dvfs_table)/sizeof(dvfs_table[0]); i++){
			CPUFREQ_INF("\tvoltage = %4dmv \tfrequency = %4dMHz\n", dvfs_table[i].volt,
					dvfs_table[i].freq/1000000);
		}
	}
	else{
		for(i = 0; i <= table_length_syscfg; i++){
			CPUFREQ_INF("\tvoltage = %4dmv \tfrequency = %4dMHz\n", dvfs_table_syscfg[i].volt,
					dvfs_table_syscfg[i].freq/1000000);
		}
	}
	CPUFREQ_INF("-----------------------------------------------\n");
}

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
    struct cpufreq_dvfs *dvfs_inf = NULL;
	if(use_default_table)
		dvfs_inf = &dvfs_table[0];
	else
		dvfs_inf = &dvfs_table_syscfg[0];

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
static inline int __set_cpufreq_hw(struct sunxi_cpu_freq_t *freq)
{
    int             ret;
    unsigned int    frequency;

    /* try to adjust pll frequency */
    ret = clk_set_rate(clk_pll, freq->pll);
    /* try to adjust cpu frequency */
    frequency = freq->pll / freq->div.cpu_div;
    ret |= clk_set_rate(clk_cpu, frequency);
    /* try to adjuxt axi frequency */
    frequency /= freq->div.axi_div;
    ret |= clk_set_rate(clk_axi, frequency);
#ifndef AHB_APB_CLK_ASYNC
    /* try to adjust ahb frequency */
    frequency /= freq->div.ahb_div;
    ret |= clk_set_rate(clk_ahb, frequency);
    /* try to adjust apb frequency */
    frequency /= freq->div.apb_div;
    ret |= clk_set_rate(clk_apb, frequency);
#endif

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
static int __set_cpufreq_target(struct sunxi_cpu_freq_t *old, struct sunxi_cpu_freq_t *new)
{
    int     ret = 0;
    struct sunxi_cpu_freq_t old_freq, new_freq;

    if(!old || !new) {
        return -EINVAL;
    }

    old_freq = *old;
    new_freq = *new;

    CPUFREQ_INF("cpu: %dMhz->%dMhz\n", old_freq.pll/1000000, new_freq.pll/1000000);

    if(new_freq.pll > old_freq.pll) {
        if((old_freq.pll <= 204000000) && (new_freq.pll >= 204000000)) {
            /* set to 204Mhz (1:1:1:2) */
            old_freq.pll = 204000000;
            old_freq.div.cpu_div = 1;
            old_freq.div.axi_div = 1;
#ifndef AHB_APB_CLK_ASYNC
            old_freq.div.ahb_div = 1;
            old_freq.div.apb_div = 2;
#endif
            ret |= __set_cpufreq_hw(&old_freq);
#ifndef AHB_APB_CLK_ASYNC
            /* set to 204Mhz (1:1:2:2) */
            old_freq.div.ahb_div = 2;
            ret |= __set_cpufreq_hw(&old_freq);
#endif
        }
        if((old_freq.pll <= 408000000) && (new_freq.pll >= 408000000)) {
            /* set to 408Mhz (1:1:2:2) */
            old_freq.pll = 408000000;
            old_freq.div.cpu_div = 1;
            old_freq.div.axi_div = 1;
#ifndef AHB_APB_CLK_ASYNC
            old_freq.div.ahb_div = 2;
            old_freq.div.apb_div = 2;
#endif
            ret |= __set_cpufreq_hw(&old_freq);
            /* set to 408Mhz (1:2:2:2) */
            old_freq.div.axi_div = 2;
            ret |= __set_cpufreq_hw(&old_freq);
        }
        if((old_freq.pll <= 816000000) && (new_freq.pll >= 816000000)) {
            /* set to 816Mhz (1:2:2:2) */
            old_freq.pll = 816000000;
            old_freq.div.cpu_div = 1;
            old_freq.div.axi_div = 2;
#ifndef AHB_APB_CLK_ASYNC
            old_freq.div.ahb_div = 2;
            old_freq.div.apb_div = 2;
#endif
            ret |= __set_cpufreq_hw(&old_freq);
            /* set to 816Mhz (1:3:2:2) */
            old_freq.div.axi_div = 3;
            ret |= __set_cpufreq_hw(&old_freq);
        }
        if((old_freq.pll <= 1200000000) && (new_freq.pll >= 1200000000)) {
            /* set to 1200Mhz (1:3:2:2) */
            old_freq.pll = 1200000000;
            old_freq.div.cpu_div = 1;
            old_freq.div.axi_div = 3;
#ifndef AHB_APB_CLK_ASYNC
            old_freq.div.ahb_div = 2;
            old_freq.div.apb_div = 2;
#endif
            ret |= __set_cpufreq_hw(&old_freq);
            /* set to 1200Mhz (1:4:2:2) */
            old_freq.div.axi_div = 4;
            ret |= __set_cpufreq_hw(&old_freq);
        }

        /* adjust to target frequency */
        ret |= __set_cpufreq_hw(&new_freq);
    }
    else if(new_freq.pll < old_freq.pll) {
        if((old_freq.pll > 1200000000) && (new_freq.pll <= 1200000000)) {
            /* set to 1200Mhz (1:3:2:2) */
            old_freq.pll = 1200000000;
            old_freq.div.cpu_div = 1;
            old_freq.div.axi_div = 3;
#ifndef AHB_APB_CLK_ASYNC
            old_freq.div.ahb_div = 2;
            old_freq.div.apb_div = 2;
#endif
            ret |= __set_cpufreq_hw(&old_freq);
        }
        if((old_freq.pll > 816000000) && (new_freq.pll <= 816000000)) {
            /* set to 816Mhz (1:3:2:2) */
            old_freq.pll = 816000000;
            old_freq.div.cpu_div = 1;
            old_freq.div.axi_div = 3;
#ifndef AHB_APB_CLK_ASYNC
            old_freq.div.ahb_div = 2;
            old_freq.div.apb_div = 2;
#endif
            ret |= __set_cpufreq_hw(&old_freq);
            /* set to 816Mhz (1:2:2:2) */
            old_freq.div.axi_div = 2;
            ret |= __set_cpufreq_hw(&old_freq);
        }
        if((old_freq.pll > 408000000) && (new_freq.pll <= 408000000)) {
            /* set to 408Mhz (1:2:2:2) */
            old_freq.pll = 408000000;
            old_freq.div.cpu_div = 1;
            old_freq.div.axi_div = 2;
#ifndef AHB_APB_CLK_ASYNC
            old_freq.div.ahb_div = 2;
            old_freq.div.apb_div = 2;
#endif
            ret |= __set_cpufreq_hw(&old_freq);
            /* set to 816Mhz (1:1:2:2) */
            old_freq.div.axi_div = 1;
            ret |= __set_cpufreq_hw(&old_freq);
        }
        if((old_freq.pll > 204000000) && (new_freq.pll <= 204000000)) {
            /* set to 204Mhz (1:1:2:2) */
            old_freq.pll = 204000000;
            old_freq.div.cpu_div = 1;
            old_freq.div.axi_div = 1;
#ifndef AHB_APB_CLK_ASYNC
            old_freq.div.ahb_div = 2;
            old_freq.div.apb_div = 2;
#endif
            ret |= __set_cpufreq_hw(&old_freq);
            /* set to 204Mhz (1:1:1:2) */
#ifndef AHB_APB_CLK_ASYNC
            old_freq.div.ahb_div = 1;
            ret |= __set_cpufreq_hw(&old_freq);
#endif
        }

        /* adjust to target frequency */
        ret |= __set_cpufreq_hw(&new_freq);
    }

    if(ret) {
        unsigned int    frequency;

        CPUFREQ_ERR("try to set target frequency failed!\n");

        /* try to restore frequency configuration */
        frequency = clk_get_rate(clk_cpu);
        frequency /= 4;
        clk_set_rate(clk_axi, frequency);
#ifndef AHB_APB_CLK_ASYNC
        frequency /= 2;
        clk_set_rate(clk_ahb, frequency);
        frequency /= 2;
        clk_set_rate(clk_apb, frequency);
#endif

        clk_set_rate(clk_pll, old->pll);
        frequency = old->pll / old->div.cpu_div;
        clk_set_rate(clk_cpu, frequency);
        frequency /= old->div.axi_div;
        clk_set_rate(clk_axi, frequency);
#ifndef AHB_APB_CLK_ASYNC
        frequency /= old->div.ahb_div;
        clk_set_rate(clk_ahb, frequency);
        frequency /= old->div.apb_div;
        clk_set_rate(clk_apb, frequency);
#endif

        CPUFREQ_ERR(KERN_ERR "no compatible settings cpu freq for %d\n", new_freq.pll);
        return -1;
    }

    return 0;
}


/*
*********************************************************************************************************
*                           sunxi_cpufreq_settarget
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
static int sunxi_cpufreq_settarget(struct cpufreq_policy *policy, struct sunxi_cpu_freq_t *cpu_freq)
{
    struct cpufreq_freqs    freqs;
    struct sunxi_cpu_freq_t cpu_new;
    int                     i;

    #ifdef CONFIG_CPU_FREQ_DVFS
    unsigned int    new_vdd;
    #endif

    /* show current cpu frequency configuration, just for debug */
	sunxi_cpufreq_show("cur", &cpu_cur);

    /* get new cpu frequency configuration */
	cpu_new = *cpu_freq;
	sunxi_cpufreq_show("new", &cpu_new);

    /* notify that cpu clock will be adjust if needed */
	if (policy) {
        freqs.cpu = policy->cpu;
	    freqs.old = cpu_cur.pll / 1000;
	    freqs.new = cpu_new.pll / 1000;
#ifdef CONFIG_SMP
        /* notifiers */
        for_each_cpu(i, policy->cpus) {
            freqs.cpu = i;
            cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
        }
#else
        cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
#endif
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
                freqs.cpu = policy->cpu;
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
            freqs.cpu = policy->cpu;
	        freqs.old = freqs.new;
	        freqs.new = cpu_cur.pll / 1000;
#ifdef CONFIG_SMP
            /* notifiers */
            for_each_cpu(i, policy->cpus) {
                freqs.cpu = i;
                cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
            }
#else
            cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
#endif
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
#ifdef CONFIG_SMP
        /*
         * Note that loops_per_jiffy is not updated on SMP systems in
         * cpufreq driver. So, update the per-CPU loops_per_jiffy value
         * on frequency transition. We need to update all dependent cpus
         */
        for_each_cpu(i, policy->cpus) {
            per_cpu(cpu_data, i).loops_per_jiffy =
                 cpufreq_scale(per_cpu(cpu_data, i).loops_per_jiffy, freqs.old, freqs.new);
            freqs.cpu = i;
            cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
        }
#else
        cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
#endif
	}

	CPUFREQ_DBG("%s: finished\n", __func__);
	return 0;
}


/*
 * adjust the frequency that cpu is currently running;
 * policy:  cpu frequency policy;
 * freq:    target frequency to be set, based on khz;
 * relation:    method for selecting the target requency;
 * return:  result, return 0 if set target frequency successed, else, return -EINVAL;
 * notes:   this function is called by the cpufreq core;
 */
static int sunxi_cpufreq_target(struct cpufreq_policy *policy, __u32 freq, __u32 relation)
{
    int                     ret = 0;
    unsigned int            index;
    struct sunxi_cpu_freq_t freq_cfg;
#ifdef CONFIG_CPU_FREQ_SETFREQ_DEBUG
	ktime_t calltime = ktime_set(0, 0), delta, rettime;

	if (unlikely(setgetfreq_debug)) {
		calltime = ktime_get();
	}
#endif

    mutex_lock(&sunxi_cpu_lock);

#ifdef CONFIG_SMP
    /* Wait untill all CPU's are initialized */
    if (unlikely(cpus_initialized < num_online_cpus())) {
        ret = -EINVAL;
        goto out;
    }
#endif

    /* avoid repeated calls which cause a needless amout of duplicated
     * logging output (and CPU time as the calculation process is
     * done) */
	if (freq == last_target) {
        goto out;
	}

    /* try to look for a valid frequency value from cpu frequency table */
    if (cpufreq_frequency_table_target(policy, sunxi_freq_tbl, freq, relation, &index)) {
        CPUFREQ_ERR("try to look for a valid frequency for %u failed!\n", freq);
        ret = -EINVAL;
        goto out;
    }

    /* frequency is same as the value last set, need not adjust */
	if (sunxi_freq_tbl[index].frequency == last_target) {
        goto out;
	}
    freq = sunxi_freq_tbl[index].frequency;

    /* update the target frequency */
    freq_cfg.pll = sunxi_freq_tbl[index].frequency * 1000;
    freq_cfg.div = *(struct sunxi_clk_div_t *)&sunxi_freq_tbl[index].index;
    CPUFREQ_DBG("target frequency find is %u, entry %u\n", freq_cfg.pll, index);

    /* try to set target frequency */
    ret = sunxi_cpufreq_settarget(policy, &freq_cfg);
    if(!ret) {
        last_target = freq;
    }
out:
#ifdef CONFIG_CPU_FREQ_SETFREQ_DEBUG
	if (unlikely(setgetfreq_debug)) {
		rettime = ktime_get();
		delta = ktime_sub(rettime, calltime);
		setfreq_time_usecs = ktime_to_ns(delta) >> 10;
		printk("[setfreq]: %Ld usecs\n", setfreq_time_usecs);
	}
#endif
    mutex_unlock(&sunxi_cpu_lock);

    return ret;
}


/*
 * get the frequency that cpu currently is running;
 * cpu:    cpu number, all cpus use the same clock;
 * return: cpu frequency, based on khz;
 */
static unsigned int sunxi_cpufreq_get(unsigned int cpu)
{
	unsigned int current_freq = 0;
#ifdef CONFIG_CPU_FREQ_SETFREQ_DEBUG
	ktime_t calltime = ktime_set(0, 0), delta, rettime;

	if (unlikely(setgetfreq_debug)) {
		calltime = ktime_get();
	}
#endif

	current_freq = clk_get_rate(clk_cpu) / 1000;

#ifdef CONFIG_CPU_FREQ_SETFREQ_DEBUG
	if (unlikely(setgetfreq_debug)) {
		rettime = ktime_get();
		delta = ktime_sub(rettime, calltime);
		getfreq_time_usecs = ktime_to_ns(delta) >> 10;
		printk("[getfreq]: %Ld usecs\n", getfreq_time_usecs);
	}
#endif

	return current_freq;
}


/*
 * get the frequency that cpu average is running;
 * cpu:    cpu number, all cpus use the same clock;
 * return: cpu frequency, based on khz;
 */
static unsigned int sunxi_cpufreq_getavg(struct cpufreq_policy *policy, unsigned int cpu)
{
    return clk_get_rate(clk_cpu) / 1000;
}


/*
 * get a valid frequency from cpu frequency table;
 * target_freq:	target frequency to be judge, based on KHz;
 * return: cpu frequency, based on khz;
 */
static unsigned int __get_valid_freq(unsigned int target_freq)
{
    struct cpufreq_frequency_table *tmp = &sunxi_freq_tbl[0];

    while(tmp->frequency != CPUFREQ_TABLE_END){
        if((tmp+1)->frequency <= target_freq)
            tmp++;
        else
            break;
    }

    return tmp->frequency;
}


/*
 * init cpu max/min frequency from sysconfig;
 * return: 0 - init cpu max/min successed, !0 - init cpu max/min failed;
 */
static int __init_freq_syscfg(void)
{
    int val, ret = 0;

    if (script_parser_fetch("dvfs_table", "max_freq", &val, sizeof(int))) {
        CPUFREQ_ERR("get cpu max frequency from sysconfig failed\n");
        ret = -1;
        goto fail;
    }
    cpu_freq_max = val;

    if (script_parser_fetch("dvfs_table", "min_freq", &val, sizeof(int))) {
        CPUFREQ_ERR("get cpu min frequency from sysconfig failed\n");
        ret = -1;
        goto fail;
    }
    cpu_freq_min = val;

    if(cpu_freq_max > SUNXI_CPUFREQ_MAX || cpu_freq_max < SUNXI_CPUFREQ_MIN
        || cpu_freq_min < SUNXI_CPUFREQ_MIN || cpu_freq_min > SUNXI_CPUFREQ_MAX){
        CPUFREQ_ERR("cpu max or min frequency from sysconfig is more than range\n");
        ret = -1;
        goto fail;
    }

    if(cpu_freq_min > cpu_freq_max){
        CPUFREQ_ERR("cpu min frequency can not be more than cpu max frequency\n");
        ret = -1;
        goto fail;
    }

    /* get valid max/min frequency from cpu frequency table */
    cpu_freq_max = __get_valid_freq(cpu_freq_max / 1000);
    cpu_freq_min = __get_valid_freq(cpu_freq_min / 1000);

    return 0;

fail:
    /* use default cpu max/min frequency */
    cpu_freq_max = SUNXI_CPUFREQ_MAX / 1000;
    cpu_freq_min = SUNXI_CPUFREQ_MIN / 1000;

    return ret;
}


/*
 * cpu frequency initialise a policy;
 * policy:  cpu frequency policy;
 * result:  return 0 if init ok, else, return -EINVAL;
 */
static int sunxi_cpufreq_init(struct cpufreq_policy *policy)
{
    policy->cur = sunxi_cpufreq_get(0);
    policy->min = policy->cpuinfo.min_freq = cpu_freq_min;
    policy->max = policy->cpuinfo.max_freq = cpu_freq_max;
    policy->cpuinfo.max_freq = SUNXI_CPUFREQ_MAX / 1000;
    policy->cpuinfo.min_freq = SUNXI_CPUFREQ_MIN / 1000;
    policy->governor = CPUFREQ_DEFAULT_GOVERNOR;

    /* feed the latency information from the cpu driver */
    policy->cpuinfo.transition_latency = SUNXI_FREQTRANS_LATENCY;
    cpufreq_frequency_table_get_attr(sunxi_freq_tbl, policy->cpu);

#ifdef CONFIG_SMP
    /*
     * both processors share the same voltage and the same clock,
     * but have dedicated power domains. So both cores needs to be
     * scaled together and hence needs software co-ordination.
     * Use cpufreq affected_cpus interface to handle this scenario.
     */
    policy->shared_type = CPUFREQ_SHARED_TYPE_ANY;
    cpumask_or(&sunxi_cpumask, cpumask_of(policy->cpu), &sunxi_cpumask);
    cpumask_copy(policy->cpus, &sunxi_cpumask);
    cpus_initialized++;
#endif

    return 0;
}


/*
 * get current cpu frequency configuration;
 * cfg:     cpu frequency cofniguration;
 * return:  result;
 */
static int sunxi_cpufreq_getcur(struct sunxi_cpu_freq_t *cfg)
{
    unsigned int    freq, freq0;

    if(!cfg) {
        return -EINVAL;
    }

	cfg->pll = clk_get_rate(clk_pll);
    freq = clk_get_rate(clk_cpu);
    cfg->div.cpu_div = cfg->pll / freq;
    freq0 = clk_get_rate(clk_axi);
    cfg->div.axi_div = freq / freq0;
#ifndef AHB_APB_CLK_ASYNC
    freq = clk_get_rate(clk_ahb);
    cfg->div.ahb_div = freq0 / freq;
    freq0 = clk_get_rate(clk_apb);
    cfg->div.apb_div = freq /freq0;
#endif

	return 0;
}


#ifdef CONFIG_PM

/*
 * cpu frequency configuration suspend;
 */
static int sunxi_cpufreq_suspend(struct cpufreq_policy *policy)
{
    CPUFREQ_DBG("%s\n", __func__);
    return 0;
}

/*
 * cpu frequency configuration resume;
 */
static int sunxi_cpufreq_resume(struct cpufreq_policy *policy)
{
    /* invalidate last_target setting */
    last_target = ~0;
    CPUFREQ_DBG("%s\n", __func__);
    return 0;
}


#else   /* #ifdef CONFIG_PM */

#define sunxi_cpufreq_suspend   NULL
#define sunxi_cpufreq_resume    NULL

#endif  /* #ifdef CONFIG_PM */

static ssize_t show_debug_mask(struct cpufreq_policy *policy, char *buf)
{
	return sprintf(buf, "%u\n", setgetfreq_debug);	
}
static ssize_t store_debug_mask	(struct cpufreq_policy *policy, const char *buf, size_t count)		
{									
	unsigned int ret = -EINVAL;	
    int debug_mask = 0;
									
	ret = sscanf(buf, "%u", &debug_mask);
	if (ret != 1)	
		return -EINVAL;	
	setgetfreq_debug = debug_mask;
	return ret ? ret : count;
}
cpufreq_freq_attr_rw(debug_mask);


static struct freq_attr *platform_attrs[] = {
	&debug_mask,
    NULL
};
static struct cpufreq_driver sunxi_cpufreq_driver = {
    .name       = "sunxi",
    .flags      = CPUFREQ_STICKY,
    .init       = sunxi_cpufreq_init,
    .verify     = sunxi_cpufreq_verify,
    .target     = sunxi_cpufreq_target,
    .get        = sunxi_cpufreq_get,
    .getavg     = sunxi_cpufreq_getavg,
    .suspend    = sunxi_cpufreq_suspend,
    .resume     = sunxi_cpufreq_resume,
    .attr       = platform_attrs,
};


/*
 * cpu frequency driver init
 */
static int __init sunxi_cpufreq_initcall(void)
{
	int ret = 0;

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

#ifdef AHB_APB_CLK_ASYNC
    CPUFREQ_INF("set ahb apb clock async\n");
    clk_sata_pll = clk_get(NULL, "sata_pll");
    if (IS_ERR(clk_sata_pll)) {
        CPUFREQ_INF(KERN_ERR "%s: could not get clock(s)\n", __func__);
		return -ENOENT;
    }
    clk_set_parent(clk_ahb, clk_sata_pll);
    clk_set_rate(clk_ahb, 150000000);
    clk_set_rate(clk_apb,  75000000);
#endif

	CPUFREQ_INF("%s: clocks pll=%lu,cpu=%lu,axi=%lu,ahp=%lu,apb=%lu\n", __func__,
	       clk_get_rate(clk_pll), clk_get_rate(clk_cpu), clk_get_rate(clk_axi),
	       clk_get_rate(clk_ahb), clk_get_rate(clk_apb));

#ifdef CONFIG_CPU_FREQ_DVFS
    corevdd = regulator_get(NULL, "Vcore");
    if(IS_ERR(corevdd)) {
        CPUFREQ_INF("try to get regulator failed, core vdd will not changed!\n");
        corevdd = NULL;
    }
    else {
        CPUFREQ_INF("try to get regulator(0x%x) successed.\n", (__u32)corevdd);
        last_vdd = regulator_get_voltage(corevdd) / 1000;
    }
	ret = __init_vftable_syscfg();
	if(ret) {
		CPUFREQ_ERR("use default V-F Table\n");
	}
	__vftable_show();
#endif

    /* init cpu frequency from sysconfig */
    if(__init_freq_syscfg()) {
        CPUFREQ_ERR("%s, use default cpu max/min frequency, max freq: %uMHz, min freq: %uMHz\n",
                    __func__, cpu_freq_max/1000, cpu_freq_min/1000);
    }else{
        CPUFREQ_INF("%s, get cpu frequency from sysconfig, max freq: %uMHz, min freq: %uMHz\n",
                    __func__, cpu_freq_max/1000, cpu_freq_min/1000);
    }

    /* initialise current frequency configuration */
	sunxi_cpufreq_getcur(&cpu_cur);
	sunxi_cpufreq_show("cur", &cpu_cur);

    /* register cpu frequency driver */
    ret = cpufreq_register_driver(&sunxi_cpufreq_driver);

    return ret;
}
late_initcall(sunxi_cpufreq_initcall);
