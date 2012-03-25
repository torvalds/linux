/* arch/arm/mach-rk30/rk30_dvfs.c
 *
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

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>

#include "clock.h"
#include <mach/dvfs.h>
#include <mach/clock.h>

#if 0
#define DVFS_DBG(fmt, args...) pr_debug(fmt, ##args)
#define DVFS_ERR(fmt, args...) pr_err(fmt, ##args)
#else
#define DEBUG_RK30_DVFS
#define DVFS_DBG(fmt, args...) printk(fmt, ##args)
#define DVFS_ERR(fmt, args...) printk(fmt, ##args)
#endif

#ifndef CONFIG_ARCH_RK30
#define DVFS_TEST_OFF_BOARD
#endif



#ifdef DVFS_TEST_OFF_BOARD
/* Just for simulation */

struct regulator {
    int min_uV;
};
#if 0
static void test_regulator_put(struct regulator *regulator)
{
    kfree(regulator);
}
#endif
struct regulator regulators[100];
static struct regulator *test_regulator_get(struct device *dev, const char *id) {
    static int ret_cnt = 0;
    return &regulators[ret_cnt++];
}

static int test_regulator_set_voltage(struct regulator *regulator, int min_uV, int max_uV)
{
    regulator->min_uV = min_uV;
    return 0;
}

static int test_regulator_get_voltage(struct regulator *regulator)
{
    return regulator->min_uV;
}

int rk30_clk_set_rate(struct clk *clk, unsigned long rate);
static void dump_dbg_map(void);
int rk30_dvfs_init_test(void);
int rk30_clk_enable(struct clk *clk);
int rk30_clk_disable(struct clk *clk);

#define dvfs_regulator_get(dev,id) test_regulator_get((dev),(id))
#define dvfs_regulator_put(regu) test_regulator_get((regu))
#define dvfs_regulator_set_voltage(regu,min_uV,max_uV) test_regulator_set_voltage((regu),(min_uV),(max_uV))
#define dvfs_regulator_get_voltage(regu) test_regulator_get_voltage((regu))

/* clock */
#define dvfs_clk_get(a,b) rk30_clk_get((a),(b))
#define dvfs_clk_set_rate(a,b) rk30_clk_set_rate((a),(b))
#define dvfs_clk_enable(a) rk30_clk_enable((a))
#define dvfs_clk_disable(a) rk30_clk_disable((a))

#else
/* board runing */
#include <linux/regulator/consumer.h>

#define dvfs_regulator_get(dev,id) regulator_get((dev),(id))
#define dvfs_regulator_put(regu) regulator_get((regu))
#define dvfs_regulator_set_voltage(regu,min_uV,max_uV) regulator_set_voltage((regu),(min_uV),(max_uV))
#define dvfs_regulator_get_voltage(regu) regulator_get_voltage((regu))

#define dvfs_clk_get(a,b) clk_get((a),(b))
#define dvfs_clk_set_rate(a,b) clk_set_rate((a),(b))
#define dvfs_clk_enable(a) clk_enable((a))
#define dvfs_clk_disable(a) clk_disable((a))
#endif


static LIST_HEAD(rk_dvfs_tree);
static DEFINE_MUTEX(mutex);
/*
int dvfs_target_core(struct clk *clk, unsigned int rate);
int dvfs_target(struct clk *clk, unsigned int rate);
int dvfs_clk_set_rate(struct clk *clk, unsigned long rate);
*/
extern int rk30_clk_notifier_register(struct clk *clk, struct notifier_block *nb);
extern int rk30_clk_notifier_unregister(struct clk *clk, struct notifier_block *nb);

#define FV_TABLE_END 0
#define PD_ON	1
#define PD_OFF	0


static void dvfs_clk_scale_volt(struct clk_node *dvfs_clk, unsigned int volt);
static int dvfs_clk_get_volt(struct clk_node *dvfs_clk, unsigned long rate,
                             struct cpufreq_frequency_table *clk_fv);

/**
 * **************************FUNCTIONS***********************************
 */

#ifdef DEBUG_RK30_DVFS
/**
 * dump_dbg_map() : Draw all informations of dvfs while debug
 */
static void dump_dbg_map(void)
{
    int i;
    struct vd_node	*vd;
    struct pd_node	*pd, *clkparent;
    struct clk_list	*child;
    struct clk_node	*dvfs_clk;

    DVFS_DBG("-------------DVFS DEBUG-----------\n\n\n");
    DVFS_DBG("RK30 DVFS TREE:\n");
    list_for_each_entry(vd, &rk_dvfs_tree, node) {
        DVFS_DBG("|\n|- voltage domain:%s\n", vd->name);
        DVFS_DBG("|- current voltage:%d\n", vd->cur_volt);

        list_for_each_entry(pd, &vd->pd_list, node) {
            DVFS_DBG("|  |\n|  |- power domain:%s, status = %s, current volt = %d\n",
                     pd->name, (pd->pd_status == PD_ON) ? "ON" : "OFF", pd->cur_volt);

            list_for_each_entry(child, &pd->clk_list, node) {
                dvfs_clk = child->dvfs_clk;
                DVFS_DBG("|  |  |\n|  |  |- clock: %s current: rate %d, volt = %d, enable_dvfs = %s\n",
                         dvfs_clk->name, dvfs_clk->cur_freq, dvfs_clk->cur_volt, dvfs_clk->enable_dvfs == 0 ? "DISABLE" : "ENABLE");
                for (i = 0; dvfs_clk->pds[i].pd != NULL; i++) {
                    clkparent = dvfs_clk->pds[i].pd;
                    DVFS_DBG("|  |  |  |- clock parents: %s, vd_parent = %s\n", clkparent->name, clkparent->vd->name);
                }

                for (i = 0; (dvfs_clk->dvfs_table[i].frequency != FV_TABLE_END); i++) {
                    DVFS_DBG("|  |  |  |- freq = %d, volt = %d\n", dvfs_clk->dvfs_table[i].frequency, dvfs_clk->dvfs_table[i].index);

                }
            }
        }
    }
    DVFS_DBG("-------------DVFS DEBUG END------------\n");
}
#endif

int is_support_dvfs(struct clk_node *dvfs_info)
{
    return (dvfs_info->vd && dvfs_info->vd->vd_dvfs_target && dvfs_info->enable_dvfs);
}
static int rk_dvfs_clk_notifier_event(struct notifier_block *this,
                                      unsigned long event, void *ptr)
{
    struct clk_notifier_data *noti_info;
    struct clk *clk;
    struct clk_node *dvfs_clk;
    noti_info = (struct clk_notifier_data *)ptr;
    clk = noti_info->clk;
    dvfs_clk = clk->dvfs_info;

    switch (event) {
    case CLK_PRE_RATE_CHANGE:
        DVFS_DBG("%s CLK_PRE_RATE_CHANGE\n", __func__);
        break;
    case CLK_POST_RATE_CHANGE:
        DVFS_DBG("%s CLK_POST_RATE_CHANGE\n", __func__);
        break;
    case CLK_ABORT_RATE_CHANGE:
        DVFS_DBG("%s CLK_ABORT_RATE_CHANGE\n", __func__);
        break;
    case CLK_PRE_ENABLE:
        DVFS_DBG("%s CLK_PRE_ENABLE\n", __func__);
        break;
    case CLK_POST_ENABLE:
        DVFS_DBG("%s CLK_POST_ENABLE\n", __func__);
        break;
    case CLK_ABORT_ENABLE:
        DVFS_DBG("%s CLK_ABORT_ENABLE\n", __func__);
        break;
    case CLK_PRE_DISABLE:
        DVFS_DBG("%s CLK_PRE_DISABLE\n", __func__);
        break;
    case CLK_POST_DISABLE:
        DVFS_DBG("%s CLK_POST_DISABLE\n", __func__);
        dvfs_clk->cur_freq = 0;
        dvfs_clk_scale_volt(dvfs_clk, 0);
        break;
    case CLK_ABORT_DISABLE:
        DVFS_DBG("%s CLK_ABORT_DISABLE\n", __func__);

        break;
    default:
        break;
    }
    return 0;
}
static struct notifier_block rk_dvfs_clk_notifier = {
    .notifier_call = rk_dvfs_clk_notifier_event,
};
int clk_disable_dvfs(struct clk *clk)
{
    struct clk_node *dvfs_clk;
    dvfs_clk = clk->dvfs_info;
    if(dvfs_clk->enable_dvfs - 1 < 0) {
        DVFS_ERR("clk is already closed!\n");
        return -1;
    } else {
        DVFS_ERR("clk is disable now!\n");
        dvfs_clk->enable_dvfs--;
        if(0 == dvfs_clk->enable_dvfs) {
            DVFS_ERR("clk closed!\n");
            rk30_clk_notifier_unregister(clk, dvfs_clk->dvfs_nb);
            DVFS_ERR("clk unregister nb!\n");
            dvfs_clk_scale_volt(dvfs_clk, 0);
        }
    }
    dump_dbg_map();
    return 0;
}

int clk_enable_dvfs(struct clk *clk)
{
    struct regulator *regulator;
    struct clk_node *dvfs_clk;
    struct cpufreq_frequency_table clk_fv;

    if(!clk->dvfs_info) {
        DVFS_ERR("This clk(%s) not support dvfs!\n", clk->name);
        return -1;
    }

    dvfs_clk = clk->dvfs_info;
    DVFS_ERR("dvfs clk enable dvfs %s\n", dvfs_clk->name);
    if(0 == dvfs_clk->enable_dvfs) {
        dvfs_clk->enable_dvfs++;
        if(!dvfs_clk->vd->regulator) {
            regulator = dvfs_regulator_get(NULL, dvfs_clk->vd->regulator_name);
            if(regulator)
                dvfs_clk->vd->regulator = regulator;
            else
                dvfs_clk->vd->regulator = NULL;
        }
        if(dvfs_clk->dvfs_nb) {
            // must unregister when clk disable
            rk30_clk_notifier_register(clk, dvfs_clk->dvfs_nb);
        }

        if(!clk || IS_ERR(clk)) {
            DVFS_ERR("%s get clk %s error\n", __func__, dvfs_clk->name);
            return -1;
        }
        //DVFS_DBG("%s get clk %s rate = %lu\n", __func__, clk->name, clk->rate);
		if(dvfs_clk->cur_freq == 0)
	        dvfs_clk_get_volt(dvfs_clk, clk->rate, &clk_fv);
		else
			dvfs_clk_get_volt(dvfs_clk, dvfs_clk->cur_freq, &clk_fv);
        dvfs_clk->cur_volt = clk_fv.index;
        dvfs_clk->cur_freq = clk_fv.frequency;
        dvfs_clk_scale_volt(dvfs_clk, dvfs_clk->cur_volt);
		dump_dbg_map();

    } else {
        DVFS_ERR("dvfs already enable clk enable = %d!\n", dvfs_clk->enable_dvfs);
        dvfs_clk->enable_dvfs++;
    }
    return 0;
}

int dvfs_set_rate(struct clk *clk, unsigned long rate)
{
    int ret = 0;
    struct vd_node *vd;
    DVFS_DBG("%s dvfs start\n", clk->name);
    if(!clk->dvfs_info) {
        DVFS_ERR("%s :This clk do not support dvfs!\n", __func__);
        ret = -1;
    } else {
        vd = clk->dvfs_info->vd;
        mutex_lock(&vd->dvfs_mutex);
        ret = vd->vd_dvfs_target(clk, rate);
        mutex_unlock(&vd->dvfs_mutex);
    }
    return ret;
}

/**
 * get correspond voltage khz
 */
static int dvfs_clk_get_volt(struct clk_node *dvfs_clk, unsigned long rate,
                             struct cpufreq_frequency_table *clk_fv)
{
    int i = 0;
    if (rate == 0) {
        /* since no need*/
        return -1;
    }
    clk_fv->frequency = rate;
    clk_fv->index = 0;
    for(i = 0; (dvfs_clk->dvfs_table[i].frequency != FV_TABLE_END); i++) {
        if(dvfs_clk->dvfs_table[i].frequency >= rate) {
            clk_fv->frequency = dvfs_clk->dvfs_table[i].frequency;
            clk_fv->index = dvfs_clk->dvfs_table[i].index;
            DVFS_DBG("%s dvfs_clk_get_volt rate=%u hz ref vol=%d uV\n", dvfs_clk->name, clk_fv->frequency, clk_fv->index);
            return 0;
        }
    }
    clk_fv->frequency = 0;
    clk_fv->index = 0;
    DVFS_ERR("%s get corresponding voltage error! out of bound\n", dvfs_clk->name);
    return -1;
}

static int dvfs_clk_round_volt(struct clk_node *dvfs_clk, int volt)
{
    struct pd_node	*pd;
    struct clk_node	*dvfs_clk_tmp;
    int volt_max = 0;
    int i;

    for(i = 0; (dvfs_clk->pds[i].pd != NULL); i++) {
        pd = dvfs_clk->pds[i].pd;
        if(volt > pd->cur_volt) {
            /**
             * if dvfs_clk parent power domain's voltage is smaller then
             * this dvfs_clk's voltage ignore this power domain
             */
            volt_max = max(volt_max, volt);
            continue;
        }
        list_for_each_entry(dvfs_clk_tmp, &pd->clk_list, node) {
            /**
             * found the max voltage uninclude dvfs_clk
             */
            if(dvfs_clk_tmp != dvfs_clk) {
                volt_max = max(volt_max, dvfs_clk_tmp->cur_volt);
            }
        }
    }

    volt_max = max(volt_max, volt);
    return volt_max;
}

static void dvfs_clk_scale_volt(struct clk_node *dvfs_clk, unsigned int volt)
{
    struct vd_node *vd;
    struct pd_node *pd;
    struct clk_list	*child;
    struct clk_node	*dvfs_clk_tmp;
    int volt_max_vd = 0, volt_max_pd = 0, i;

    dvfs_clk->cur_volt = volt;//set  clk node volt
    vd = dvfs_clk->vd;// vd
    for(i = 0; (dvfs_clk->pds[i].pd != NULL); i++) {
        pd = dvfs_clk->pds[i].pd;
        volt_max_pd = 0;
        /**
         * set corresponding voltage, clk do not need to set voltage,just for
         * powerdomain
         */

        if(volt > pd->cur_volt) {
            pd->cur_volt = volt;
            pd->pd_status = (pd->cur_volt == 0) ? PD_OFF : PD_ON;
            continue;
        }

        /* set power domain voltage */
        list_for_each_entry(child, &pd->clk_list, node) {
            dvfs_clk_tmp = child->dvfs_clk;
			if(dvfs_clk_tmp->enable_dvfs){
            	volt_max_pd = max(volt_max_pd, dvfs_clk_tmp->cur_volt);
			}
        }
        pd->cur_volt = volt_max_pd;

        pd->pd_status = (volt_max_pd == 0) ? PD_OFF : PD_ON;
    }

    /* set voltage domain voltage */
    volt_max_vd = 0;
    list_for_each_entry(pd, &vd->pd_list, node) {
        volt_max_vd = max(volt_max_vd, pd->cur_volt);
    }
    vd->cur_volt = volt_max_vd;
}

int dvfs_target_set_rate_core(struct clk *clk, unsigned long rate)
{
    struct clk_node *dvfs_clk;
    int volt_new = 0, volt_old = 0;
    struct cpufreq_frequency_table clk_fv;
    int ret = 0;
    dvfs_clk = clk_get_dvfs_info(clk);

    DVFS_ERR("%s get clk %s\n", __func__, clk->name);
    if(dvfs_clk->vd->regulator == NULL) {
        DVFS_ERR("%s can't get dvfs regulater\n", clk->name);
        return -1;
    }

    /* If power domain off do scale in the notify function */
    /*
       if (rate == 0) {
       dvfs_clk->cur_freq = 0;
       dvfs_clk_scale_volt(dvfs_clk, 0);
       return 0;
       }
       */
    /* need round rate */
    DVFS_ERR("%s going to round rate = %lu\n", clk->name, rate);
    rate = clk_round_rate_nolock(clk, rate);
    DVFS_ERR("%s round get rate = %lu\n", clk->name, rate);
    /* find the clk corresponding voltage */
    if (0 != dvfs_clk_get_volt(dvfs_clk, rate, &clk_fv)) {
        DVFS_ERR("%s rate %lukhz is larger,not support\n", clk->name, rate);
        return -1;
    }
    volt_old = dvfs_clk->vd->cur_volt;
    volt_new = clk_fv.index;

    DVFS_DBG("vol_new = %d mV(was %d mV)\n", volt_new, volt_old);

    /* if up the voltage*/
    if (volt_old < volt_new) {
        if(dvfs_regulator_set_voltage(dvfs_clk->vd->regulator, volt_new, volt_new) < 0) {
            DVFS_ERR("set voltage err\n");

            return -1;
        }
        dvfs_clk->vd->cur_volt = volt_new;
        /* CPU do not use power domain, so save scale times */
        //dvfs_clk_scale_volt(dvfs_clk, clk_fv.index);
    }

    if(dvfs_clk->clk_dvfs_target) {
        ret = dvfs_clk->clk_dvfs_target(clk, rate, clk_set_rate_locked);
    } else {
        ret = clk_set_rate_locked(clk, rate);
    }
    if (ret < 0) {
        DVFS_ERR("set rate err\n");
        return -1;
    }
    dvfs_clk->cur_freq	= rate;
    dvfs_clk->cur_volt	= volt_new;

    /* if down the voltage */
    if (volt_old > volt_new) {
        if(dvfs_regulator_set_voltage(dvfs_clk->vd->regulator, volt_new, volt_new) < 0) {
            DVFS_ERR("set voltage err\n");

            return -1;
        }
        dvfs_clk->vd->cur_volt = volt_new;
        /* CPU do not use power domain, so save scale times */
        //dvfs_clk_scale_volt(dvfs_clk, clk_fv.index);
    }

    return ret;
}

int dvfs_target_set_rate_normal(struct clk *clk, unsigned long rate)
{
    struct clk_node *dvfs_clk;
    unsigned int volt_new = 0, volt_old = 0;
    struct cpufreq_frequency_table clk_fv = {0, 0};
    int ret = 0;

    dvfs_clk = clk_get_dvfs_info(clk);
    DVFS_ERR("%s get clk %s\n", __func__, clk->name);
    if(dvfs_clk->vd->regulator == NULL) {
        DVFS_DBG("%s can't get dvfs regulater\n", clk->name);
        return -1;
    }

    /* need round rate */
    DVFS_ERR("%s going to round rate = %lu\n", clk->name, rate);
    rate = clk_round_rate_nolock(clk, rate);
    DVFS_ERR("%s round get rate = %lu\n", clk->name, rate);
    /* find the clk corresponding voltage */
    if (dvfs_clk_get_volt(dvfs_clk, rate, &clk_fv)) {
        DVFS_DBG("dvfs_clk_get_volt:rate = Get corresponding voltage error!\n");
        return -1;
    }

    volt_old = dvfs_clk->vd->cur_volt;
    volt_new = dvfs_clk_round_volt(dvfs_clk, clk_fv.index);

    // if up the voltage
    if (volt_old < volt_new) {
        if(dvfs_regulator_set_voltage(dvfs_clk->vd->regulator, volt_new, volt_new) < 0) {
            DVFS_DBG("set voltage err\n");
            return -1;
        }
        dvfs_clk_scale_volt(dvfs_clk, clk_fv.index);
    }

    if(dvfs_clk->clk_dvfs_target) {
        ret = dvfs_clk->clk_dvfs_target(clk, rate, clk_set_rate_locked);
    } else {
        ret = clk_set_rate_locked(clk, rate);
    }
    if (ret < 0) {
        DVFS_ERR("set rate err\n");
        return -1;
    }
    dvfs_clk->cur_freq	= rate;
    dvfs_clk->cur_volt	= volt_new;

    // if down the voltage
    if (volt_old > volt_new) {
        if(dvfs_regulator_set_voltage(dvfs_clk->vd->regulator, volt_new, volt_new) < 0) {
            DVFS_DBG("set voltage err\n");
			return -1;

        }
        dvfs_clk_scale_volt(dvfs_clk, clk_fv.index);
    }

    return 0;
}


/*****************************init**************************/
/**
 * rate must be raising sequence
 */

struct cpufreq_frequency_table cpu_dvfs_table[] = {
    {.frequency	= 126000000, .index	= 800000},
    {.frequency	= 252000000, .index	= 850000},
    {.frequency	= 504000000, .index	= 900000},
    {.frequency	= 816000000, .index	= 1050000},
    {.frequency	= 1008000000, .index	= 1100000},
    {.frequency	= 1200000000, .index	= 1200000},
    {.frequency	= FV_TABLE_END},
};
struct cpufreq_frequency_table ddr_dvfs_table[] = {
    {.frequency	= 24000000, .index		= 600000},
    {.frequency	= 64000000, .index		= 700000},
    {.frequency	= 126000000, .index	= 800000},
    {.frequency	= 252000000, .index	= 850000},
    {.frequency	= 504000000, .index	= 900000},
    {.frequency	= FV_TABLE_END},
};
struct cpufreq_frequency_table gpu_dvfs_table[] = {
    {.frequency	= 64000000, .index		= 700000},
    {.frequency	= 126000000, .index	= 800000},
    {.frequency	= 360000000, .index	= 850000},
    {.frequency	= FV_TABLE_END},
};

static struct vd_node vd_cpu = {
    .name 			= "vd_cpu",
    .vd_dvfs_target	= dvfs_target_set_rate_core,
};

static struct vd_node vd_core = {
    .name 			= "vd_core",
    .vd_dvfs_target	= dvfs_target_set_rate_normal,
};

static struct vd_node vd_rtc = {
    .name 			= "vd_rtc",
    .vd_dvfs_target	= NULL,
};

#define LOOKUP_VD(_pvd, _regulator_name)	\
{	\
	.vd				= _pvd,	\
	.regulator_name	= _regulator_name,	\
}
static struct vd_node_lookup rk30_vds[] = {
    LOOKUP_VD(&vd_cpu, "cpu"),
    LOOKUP_VD(&vd_core, "core"),
    LOOKUP_VD(&vd_rtc, "rtc"),
};

static struct pd_node pd_a9_0 = {
    .name 			= "pd_a9_0",
    .vd				= &vd_cpu,
};
static struct pd_node pd_a9_1 = {
    .name 			= "pd_a9_1",
    .vd				= &vd_cpu,
};
static struct pd_node pd_debug = {
    .name 			= "pd_debug",
    .vd				= &vd_cpu,
};
static struct pd_node pd_scu = {
    .name 			= "pd_scu",
    .vd				= &vd_cpu,
};
static struct pd_node pd_video = {
    .name 			= "pd_video",
    .vd				= &vd_core,
};
static struct pd_node pd_vio = {
    .name 			= "pd_vio",
    .vd				= &vd_core,
};
static struct pd_node pd_gpu = {
    .name 			= "pd_gpu",
    .vd				= &vd_core,
};
static struct pd_node pd_peri = {
    .name 			= "pd_peri",
    .vd				= &vd_core,
};
static struct pd_node pd_cpu = {
    .name 			= "pd_cpu",
    .vd				= &vd_core,
};
static struct pd_node pd_alive = {
    .name 			= "pd_alive",
    .vd				= &vd_core,
};
static struct pd_node pd_rtc = {
    .name 			= "pd_rtc",
    .vd				= &vd_rtc,
};
#define LOOKUP_PD(_ppd)	\
{	\
	.pd	= _ppd,	\
}
static struct pd_node_lookup rk30_pds[] = {
    LOOKUP_PD(&pd_a9_0),
    LOOKUP_PD(&pd_a9_1),
    LOOKUP_PD(&pd_debug),
    LOOKUP_PD(&pd_scu),
    LOOKUP_PD(&pd_video),
    LOOKUP_PD(&pd_vio),
    LOOKUP_PD(&pd_gpu),
    LOOKUP_PD(&pd_peri),
    LOOKUP_PD(&pd_cpu),
    LOOKUP_PD(&pd_alive),
    LOOKUP_PD(&pd_rtc),
};

#define CLK_PDS(_ppd) \
{	\
	.pd	= _ppd,	\
}

static struct pds_list cpu_pds[] = {
    CLK_PDS(&pd_a9_0),
    CLK_PDS(&pd_a9_1),
    CLK_PDS(NULL),
};
static struct pds_list ddr_pds[] = {
    CLK_PDS(&pd_cpu),
    CLK_PDS(NULL),
};
static struct pds_list gpu_pds[] = {
    CLK_PDS(&pd_gpu),
    CLK_PDS(NULL),
};

#define RK_CLKS(_clk_name, _ppds, _dvfs_table, _dvfs_nb)	\
{	\
	.name	= _clk_name,	\
	.pds		= _ppds,	\
	.dvfs_table = _dvfs_table,	\
	.dvfs_nb	= _dvfs_nb,	\
}
static struct clk_node rk30_clks[] = {
    RK_CLKS("cpu", cpu_pds, cpu_dvfs_table, &rk_dvfs_clk_notifier),
    RK_CLKS("ddr", ddr_pds, ddr_dvfs_table, &rk_dvfs_clk_notifier),
    RK_CLKS("gpu", gpu_pds, gpu_dvfs_table, &rk_dvfs_clk_notifier),
};
/**
 * first scale regulator volt
 */
static int rk_dvfs_check_regulator_volt(void)
{
    struct vd_node	*vd;
    struct pd_node	*pd;
    struct clk_list	*child;
    struct clk_node	*dvfs_clk;
    struct clk 		*clk;
    struct cpufreq_frequency_table clk_fv;
    unsigned int vmax_pd = 0, vmax_vd = 0;

    list_for_each_entry(vd, &rk_dvfs_tree, node) {
        vmax_vd = 0;
        list_for_each_entry(pd, &vd->pd_list, node) {
            vmax_pd = 0;
            list_for_each_entry(child, &pd->clk_list, node) {

                dvfs_clk = child->dvfs_clk;
                clk = dvfs_clk_get(NULL, dvfs_clk->name);
                if(!clk || IS_ERR(clk)) {
                    DVFS_ERR("%s get clk %s error\n", __func__, dvfs_clk->name);
                    continue;
                }
                //DVFS_DBG("%s get clk %s rate = %lu\n", __func__, clk->name, clk->rate);
                dvfs_clk_get_volt(dvfs_clk, clk->rate, &clk_fv);
                dvfs_clk->cur_volt = clk_fv.index;
                dvfs_clk->cur_freq = clk_fv.frequency;
                vmax_pd = max(vmax_pd, clk_fv.index);
                pd->pd_status = (vmax_pd == 0) ? PD_OFF : PD_ON;
            }
            pd->cur_volt = vmax_pd;
            vmax_vd = max(vmax_vd, vmax_pd);
        }

        vd->cur_volt = vmax_vd;
        //DVFS_DBG("%s check error: %d, %d\n", vd->name, vd->cur_volt, dvfs_regulator_get_voltage(vd->regulator));
        //if (vd->cur_volt != dvfs_regulator_get_voltage(vd->regulator)) {
        //	DVFS_ERR("%s default voltage domain value error!\n", vd->name);
        //}
    }
    return 0;
}

static int rk_regist_vd(struct vd_node_lookup *vd_lookup)
{
    struct vd_node *vd;
    if(!vd_lookup)
        return -1;
    vd = vd_lookup->vd;
    vd->regulator_name = vd_lookup->regulator_name;

    mutex_lock(&mutex);

    mutex_init(&vd->dvfs_mutex);
    list_add(&vd->node, &rk_dvfs_tree);
    INIT_LIST_HEAD(&vd->pd_list);

    mutex_unlock(&mutex);

    return 0;
}
static int rk_regist_pd(struct pd_node_lookup *pd_lookup)
{
    struct vd_node	*vd;
    struct pd_node	*pd;

    mutex_lock(&mutex);
    pd = pd_lookup->pd;

    list_for_each_entry(vd, &rk_dvfs_tree, node) {
        if (vd == pd->vd) {
            list_add(&pd->node, &vd->pd_list);
            INIT_LIST_HEAD(&pd->clk_list);
            break;
        }
    }
    mutex_unlock(&mutex);
    return 0;
}
//extern int rk30_clk_notifier_register(struct clk *clk, struct notifier_block *nb);

static int rk_regist_clk(struct clk_node *dvfs_clk)
{
    struct pd_node	*pd;
    struct clk_list	*child;
    struct clk	*clk;
    int i = 0;

    if(!dvfs_clk)
        return -1;

    if(!dvfs_clk->pds)
        return -1;

    mutex_lock(&mutex);
    // set clk unsupport dvfs
    dvfs_clk->enable_dvfs = 0;
    dvfs_clk->vd = dvfs_clk->pds[0].pd->vd;
    for (i = 0; dvfs_clk->pds[i].pd != NULL; i++) {
        child = &(dvfs_clk->pds[i].clk_list);
        child->dvfs_clk = dvfs_clk;
        pd = dvfs_clk->pds[i].pd;
        list_add(&child->node, &pd->clk_list);
    }
    clk = dvfs_clk_get(NULL, dvfs_clk->name);
    clk_register_dvfs(dvfs_clk, clk);
    mutex_unlock(&mutex);
    return 0;
}
int rk30_dvfs_init(void)
{
    int i = 0;
    for (i = 0; i < ARRAY_SIZE(rk30_vds); i++) {
        rk_regist_vd(&rk30_vds[i]);
    }
    for (i = 0; i < ARRAY_SIZE(rk30_pds); i++) {
        rk_regist_pd(&rk30_pds[i]);
    }
    for (i = 0; i < ARRAY_SIZE(rk30_clks); i++) {
        rk_regist_clk(&rk30_clks[i]);
    }
    dump_dbg_map();
    //DVFS_DBG("%s dvfs tree create finish!\n", __func__);
    //rk_dvfs_check_regulator_volt();
    return 0;
}

void dvfs_clk_set_rate_callback(struct clk *clk, clk_dvfs_target_callback clk_dvfs_target)
{
    struct clk_node *dvfs_clk = clk_get_dvfs_info(clk);
    dvfs_clk->clk_dvfs_target = clk_dvfs_target;
}
//

/*
 *cpufreq_frequency_table->index for cpufreq is index
 *cpufreq_frequency_table->index for dvfstable is volt
 */
int cpufreq_dvfs_init(struct clk *clk, struct cpufreq_frequency_table **table, clk_dvfs_target_callback clk_dvfs_target)
{

    struct cpufreq_frequency_table *freq_table;
    struct clk_node *info = clk_get_dvfs_info(clk);
    struct cpufreq_frequency_table *dvfs_table;//dvfs volt freq table
    int i = 0;
    DVFS_DBG("%s clk name %s\n", __func__, clk->name);
    if(!info) {
        return -1;
    }
    dvfs_table = info->dvfs_table;

    if(!dvfs_table) {
        return -1;
    }

    /********************************count table num****************************/
    i = 0;
    while(dvfs_table[i].frequency != FV_TABLE_END) {
        //DVFS_DBG("dvfs_table1 %lu\n",dvfs_table[i].frequency);
        i++;
    }

    freq_table = kzalloc(sizeof(struct cpufreq_frequency_table) * (i + 1), GFP_KERNEL);
    //last freq is end tab
    freq_table[i].index = i;
    freq_table[i].frequency = CPUFREQ_TABLE_END;

    //set freq table
    i = 0;
    while(dvfs_table[i].frequency != FV_TABLE_END) {
        freq_table[i].index = i;
        freq_table[i].frequency = dvfs_table[i].frequency;
        //DVFS_DBG("dvfs_table %d %lu\n",i,dvfs_table[i].frequency);
        i++;
    }
    *table = &freq_table[0];
    dvfs_clk_set_rate_callback(clk, clk_dvfs_target);
    return 0;
}

int clk_dvfs_set_dvfs_table(struct clk *clk, struct cpufreq_frequency_table *table)
{
    struct clk_node *info = clk_get_dvfs_info(clk);
    if(!table || !info)
        return -1;
    info->dvfs_table = table;
    return 0;
}
/********************************simulation cases****************************/

#ifdef DVFS_TEST_OFF_BOARD
int rk30_dvfs_init_test(void)
{
    struct clk *clk1;
    DVFS_DBG("********************************simulation cases****************************\n");
#ifdef DEBUG_RK30_DVFS
    DVFS_DBG("\n\n");
    dump_dbg_map();
#endif
    clk1 = dvfs_clk_get(NULL, "cpu");
    if (clk1) {
        dvfs_clk_set_rate(clk1, 1008000000);
        dump_dbg_map();
        dvfs_clk_set_rate(clk1, 816000000);
        dump_dbg_map();
        dvfs_clk_set_rate(clk1, 0);
        dump_dbg_map();
        dvfs_clk_set_rate(clk1, 1200000000);
        dump_dbg_map();
        dvfs_clk_set_rate(clk1, 1009000000);
        dump_dbg_map();
        dvfs_clk_set_rate(clk1, 1416000000);
        dump_dbg_map();

    } else {
        DVFS_DBG("\t\t%s:\t can not find clk cpu\n", __func__);
    }

    clk1 = dvfs_clk_get(NULL, "gpu");
    if (clk1) {
        dvfs_clk_set_rate(clk1, 120000000);
        dump_dbg_map();
        dvfs_clk_enable(clk1);
        dvfs_clk_disable(clk1);
        dump_dbg_map();
    } else {
        DVFS_DBG("\t\t%s:\t can not find clk gpu\n", __func__);
        dump_dbg_map();
    }

    clk1 = dvfs_clk_get(NULL, "arm_pll");
    if (clk1) {
        dvfs_clk_set_rate(clk1, 24000000);
        dump_dbg_map();
    } else {
        DVFS_DBG("\t\t%s:\t can not find clk arm_pll\n", __func__);
    }

    DVFS_DBG("********************************simulation cases end***************************\n");

    return 0;

}
#endif

