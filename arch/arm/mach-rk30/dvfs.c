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
#include <linux/regulator/consumer.h>
#include <linux/delay.h>

#define DVFS_DBG(fmt, args...) {while(0);}//pr_debug(fmt, ##args)
#define DVFS_ERR(fmt, args...) pr_err(fmt, ##args)
#define DVFS_LOG(fmt, args...) pr_debug(fmt, ##args)//while(0)

#define dvfs_regulator_get(dev,id) regulator_get((dev),(id))
#define dvfs_regulator_put(regu) regulator_get((regu))
#define dvfs_regulator_set_voltage(regu,min_uV,max_uV) regulator_set_voltage((regu),(min_uV),(max_uV))
#define dvfs_regulator_get_voltage(regu) regulator_get_voltage((regu))

#define dvfs_clk_get(a,b) clk_get((a),(b))
#define dvfs_clk_get_rate_kz(a) (clk_get_rate((a))/1000)
#define dvfs_clk_set_rate(a,b) clk_set_rate((a),(b))
#define dvfs_clk_enable(a) clk_enable((a))
#define dvfs_clk_disable(a) clk_disable((a))

#define DVFS_MHZ (1000*1000)
#define DVFS_KHZ (1000)

#define DVFS_V (1000*1000)
#define DVFS_MV (1000)


static LIST_HEAD(rk_dvfs_tree);
static DEFINE_MUTEX(mutex);

extern int rk30_clk_notifier_register(struct clk *clk, struct notifier_block *nb);
extern int rk30_clk_notifier_unregister(struct clk *clk, struct notifier_block *nb);

#define PD_ON	1
#define PD_OFF	0

int is_support_dvfs(struct clk_node *dvfs_info)
{
    return (dvfs_info->vd && dvfs_info->vd->vd_dvfs_target && dvfs_info->enable_dvfs);
}
int dvfs_set_rate(struct clk *clk, unsigned long rate)
{
    int ret = 0;
    struct vd_node *vd;
   	DVFS_DBG("%s(%s(%lu))\n",__func__,clk->name,rate);
    if(!clk->dvfs_info) {
        DVFS_ERR("%s :This clk do not support dvfs!\n", __func__);
        ret = -1;
    } else {
        vd = clk->dvfs_info->vd;
        mutex_lock(&vd->dvfs_mutex);
        ret = vd->vd_dvfs_target(clk, rate);
        mutex_unlock(&vd->dvfs_mutex);
    }
    DVFS_DBG("%s(%s(%lu)),is end\n",__func__,clk->name,rate);
    return ret;
}

static int dvfs_clk_get_ref_volt(struct clk_node *dvfs_clk,int rate_khz,
                             struct cpufreq_frequency_table *clk_fv)
{
    int i = 0;
    if (rate_khz == 0||!dvfs_clk||!dvfs_clk->dvfs_table) {
        /* since no need*/
        return -1;
    }
    clk_fv->frequency = rate_khz;
    clk_fv->index = 0;
	
    for(i = 0; (dvfs_clk->dvfs_table[i].frequency != CPUFREQ_TABLE_END); i++) {
        if(dvfs_clk->dvfs_table[i].frequency >= rate_khz) {
            clk_fv->frequency = dvfs_clk->dvfs_table[i].frequency;
            clk_fv->index = dvfs_clk->dvfs_table[i].index;
           // DVFS_DBG("%s,%s rate=%ukhz(vol=%d)\n",__func__,dvfs_clk->name, clk_fv->frequency, clk_fv->index);
            return 0;
        }
    }
    clk_fv->frequency = 0;
    clk_fv->index = 0;
   // DVFS_DBG("%s get corresponding voltage error! out of bound\n", dvfs_clk->name);
    return -1;
}

static int dvfs_pd_get_newvolt_for_clk(struct pd_node *pd,struct clk_node *dvfs_clk)
{
	struct clk_list	*child;
	int volt_max = 0;
	
	if(!pd||!dvfs_clk)
		return 0;

	if(dvfs_clk->set_volt>=pd->cur_volt)
	{
		return dvfs_clk->set_volt;
	}
	
	list_for_each_entry(child, &pd->clk_list, node){
		//DVFS_DBG("%s ,pd(%s),dvfs(%s),volt(%u)\n",__func__,pd->name,dvfs_clk->name,dvfs_clk->set_volt);
		volt_max = max(volt_max,child->dvfs_clk->set_volt);
	}
	return volt_max;
}

void dvfs_update_clk_pds_volt(struct clk_node *dvfs_clk)
{
	struct pd_node	 *pd;
	int i;
	if(!dvfs_clk)
	 return;
	for(i = 0; (dvfs_clk->pds[i].pd != NULL); i++) {
		 pd = dvfs_clk->pds[i].pd;
		// DVFS_DBG("%s dvfs(%s),pd(%s)\n",__func__,dvfs_clk->name,pd->name);
		 pd->cur_volt=dvfs_pd_get_newvolt_for_clk(pd,dvfs_clk);
	}
}

static int dvfs_get_vd_volt_bypd(struct vd_node *vd)
{
	struct pd_node *pd;
	int 	volt_max_vd=0;	
	list_for_each_entry(pd, &vd->pd_list, node) {
		//DVFS_DBG("%s pd(%s,%u)\n",__func__,pd->name,pd->cur_volt);
	  volt_max_vd = max(volt_max_vd, pd->cur_volt);
	}
	return volt_max_vd;
}
static int dvfs_vd_get_newvolt_for_clk(struct clk_node *dvfs_clk)
{
	if(!dvfs_clk)
		return -1;
	dvfs_update_clk_pds_volt(dvfs_clk);
	return  dvfs_get_vd_volt_bypd(dvfs_clk->vd);
}
void dvfs_clk_register_set_rate_callback(struct clk *clk, clk_dvfs_target_callback clk_dvfs_target)
{
    struct clk_node *dvfs_clk = clk_get_dvfs_info(clk);
    dvfs_clk->clk_dvfs_target = clk_dvfs_target;
}
struct cpufreq_frequency_table *dvfs_get_freq_volt_table(struct clk *clk)
{
	struct clk_node *info = clk_get_dvfs_info(clk);

	if(!info||!info->dvfs_table) {
	   return NULL;
	}
	mutex_lock(&mutex);
	return info->dvfs_table;
	mutex_unlock(&mutex);
}
int dvfs_set_freq_volt_table(struct clk *clk, struct cpufreq_frequency_table *table)
{
	struct clk_node *info = clk_get_dvfs_info(clk);
	if(!table || !info)
	    return -1;

	mutex_lock(&mutex);
	info->dvfs_table = table;
	mutex_unlock(&mutex);
	return 0;
}
int clk_enable_dvfs(struct clk *clk)
{
    struct regulator *regulator;
    struct clk_node *dvfs_clk;
	struct cpufreq_frequency_table clk_fv;
 	if(!clk){
        DVFS_ERR("clk enable dvfs error\n");
        return -1;
    }	
	dvfs_clk=clk_get_dvfs_info(clk);
    if(!dvfs_clk||!dvfs_clk->vd) {
        DVFS_ERR("%s clk(%s) not support dvfs!\n",__func__,clk->name);
        return -1;
    }
    if(dvfs_clk->enable_dvfs==0){
      
		if(!dvfs_clk->vd->regulator) {
			regulator=NULL;
			if(dvfs_clk->vd->regulator_name)
				regulator = dvfs_regulator_get(NULL,dvfs_clk->vd->regulator_name);
			if(regulator)
			{		
				//DVFS_DBG("dvfs_regulator_get(%s)\n",dvfs_clk->vd->regulator_name);
				dvfs_clk->vd->regulator = regulator;
			}
			else
			{
				dvfs_clk->vd->regulator = NULL;
				dvfs_clk->enable_dvfs=0;
				DVFS_ERR("%s can't get regulator in %s\n",dvfs_clk->name,__func__);
				return -1;
			}	
		}
		else
		{
			dvfs_clk->vd->cur_volt = dvfs_regulator_get_voltage(dvfs_clk->vd->regulator);
			//DVFS_DBG("%s(%s) vd volt=%u\n",__func__,dvfs_clk->name,dvfs_clk->vd->cur_volt);
		}
		
		dvfs_clk->set_freq=dvfs_clk_get_rate_kz(clk);
		//DVFS_DBG("%s ,%s get freq%u!\n",__func__,dvfs_clk->name,dvfs_clk->set_freq);

		if(dvfs_clk_get_ref_volt(dvfs_clk,dvfs_clk->set_freq,&clk_fv))
		{
			dvfs_clk->enable_dvfs=0;
			return -1;
		}
		dvfs_clk->set_volt=clk_fv.index;
		//DVFS_DBG("%s,%s,freq%u(ref vol %u)\n",__func__,dvfs_clk->name,
			//	 dvfs_clk->set_freq,dvfs_clk->set_volt);
#if 0
        if(dvfs_clk->dvfs_nb) {
            // must unregister when clk disable
            rk30_clk_notifier_register(clk, dvfs_clk->dvfs_nb);
        }
#endif
		dvfs_vd_get_newvolt_for_clk(dvfs_clk);
        dvfs_clk->enable_dvfs++;	
    } else {
        DVFS_ERR("dvfs already enable clk enable = %d!\n", dvfs_clk->enable_dvfs);
        dvfs_clk->enable_dvfs++;
    }
    return 0;
}

int clk_disable_dvfs(struct clk *clk)
{
    struct clk_node *dvfs_clk;
    dvfs_clk = clk->dvfs_info;
    if(!dvfs_clk->enable_dvfs) {
        DVFS_DBG("clk is already closed!\n");
        return -1;
    } else {
        dvfs_clk->enable_dvfs--;
        if(0 == dvfs_clk->enable_dvfs) {
            DVFS_ERR("clk closed!\n");
            rk30_clk_notifier_unregister(clk, dvfs_clk->dvfs_nb);
            DVFS_DBG("clk unregister nb!\n");
        }
    }
    return 0;
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
        dvfs_clk->set_freq = 0;
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

static int rk_regist_vd(struct vd_node *vd)
{
    if(!vd)
        return -1;
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
    dvfs_clk->enable_dvfs = 0;
    dvfs_clk->vd = dvfs_clk->pds[0].pd->vd;
    for (i = 0; dvfs_clk->pds[i].pd != NULL; i++) {
        child = &(dvfs_clk->pds[i].clk_list);
        child->dvfs_clk = dvfs_clk;
        pd = dvfs_clk->pds[i].pd;
        list_add(&child->node, &pd->clk_list);
    }
    clk = dvfs_clk_get(NULL, dvfs_clk->name);
	dvfs_clk->ck=clk;
    clk_register_dvfs(dvfs_clk, clk);
    mutex_unlock(&mutex);
    return 0;
}

int dvfs_target_core(struct clk *clk, unsigned long rate_hz)
{
    struct clk_node *dvfs_clk;
    unsigned int volt_vd_new = 0,volt_vd_old = 0,volt_clk_old=0;
    struct cpufreq_frequency_table clk_fv = {0, 0};
    int ret = 0;
	unsigned long temp_hz;

   	if(!clk)
   	{
		DVFS_ERR("%s is not clk\n",__func__);
		return -1;
	}
 	dvfs_clk = clk_get_dvfs_info(clk);

    if(!dvfs_clk||dvfs_clk->vd->regulator == NULL) {
        DVFS_ERR("%s can't get dvfs regulater\n", clk->name);
        return -1;
    }
	
    temp_hz = rate_hz;//clk_round_rate_nolock(clk, rate_hz);
	
	//DVFS_DBG("dvfs(%s) round rate(%lu)(rount %lu)\n",dvfs_clk->name,rate_hz,temp_hz);

    /* find the clk corresponding voltage */
    if (dvfs_clk_get_ref_volt(dvfs_clk, temp_hz/1000, &clk_fv)) {
        DVFS_ERR("%s--%s:rate%lu,Get corresponding voltage error!\n",__func__,dvfs_clk->name,temp_hz);
        return -1;
    }
    volt_vd_old = dvfs_clk->vd->cur_volt;

	volt_clk_old=dvfs_clk->set_volt;

	dvfs_clk->set_volt=clk_fv.index;

    volt_vd_new = dvfs_vd_get_newvolt_for_clk(dvfs_clk);

	DVFS_LOG("dvfs--(%s),volt=%d(was %dmV),rate=%lu(was %lu),vd%u=(was%u)\n",
					dvfs_clk->name,clk_fv.index,dvfs_clk->set_volt,temp_hz,clk_get_rate(clk)
					,volt_vd_new,volt_vd_old);
    // if up the voltage
    #if 1
    if (volt_vd_old < volt_vd_new) {
        if(dvfs_regulator_set_voltage(dvfs_clk->vd->regulator, volt_vd_new, volt_vd_new) < 0) {
            DVFS_ERR("set voltage err\n");
            return -1;
        }
		dvfs_clk->vd->cur_volt=volt_vd_new;
    }
	#endif
    if(dvfs_clk->clk_dvfs_target) {
        ret = dvfs_clk->clk_dvfs_target(clk, temp_hz, clk_set_rate_locked);
    } else {
        ret = clk_set_rate_locked(clk, temp_hz);
    }
    if (ret < 0) {
		
		dvfs_clk->set_volt=volt_vd_old;
		dvfs_vd_get_newvolt_for_clk(dvfs_clk);	
        DVFS_ERR("set rate err\n");
        return -1;
    }
    dvfs_clk->set_freq	= temp_hz/1000;
	#if 1
    if (volt_vd_old > volt_vd_new){
        if(dvfs_regulator_set_voltage(dvfs_clk->vd->regulator, volt_vd_new, volt_vd_new) < 0) {
            DVFS_ERR("set voltage err\n");
			return -1;
        }
		dvfs_clk->vd->cur_volt=volt_vd_new;
    }
	#endif
    return 0;
}
#define get_volt_up_delay(new_volt,old_volt) ((new_volt)>(old_volt)?\
	(((new_volt)-(old_volt))>>10):0)

int dvfs_target_cpu(struct clk *clk, unsigned long rate_hz)
{
    struct clk_node *dvfs_clk;
    int volt_new = 0, volt_old = 0;
    struct cpufreq_frequency_table clk_fv;
    int ret = 0;
	unsigned long temp_hz;

	if(!clk)
	{
		DVFS_ERR("%s is not clk\n",__func__);
		return -1;
	}
 	dvfs_clk = clk_get_dvfs_info(clk);
	
    if(!dvfs_clk||dvfs_clk->vd->regulator == NULL) {
        DVFS_ERR("dvfs(%s) is not register regulator\n", dvfs_clk->name);
        return -1;
    }
    /* need round rate */
    temp_hz = clk_round_rate_nolock(clk, rate_hz);
	
	DVFS_DBG("dvfs(%s) round rate (%lu)(rount %lu)\n",dvfs_clk->name,rate_hz,temp_hz);

    /* find the clk corresponding voltage */
    if (0 != dvfs_clk_get_ref_volt(dvfs_clk, temp_hz/1000, &clk_fv)) {
        DVFS_ERR("dvfs(%s) rate %luhz is larger,not support\n",dvfs_clk->name, rate_hz);
        return -1;
    }
    volt_old = dvfs_clk->vd->cur_volt;
    volt_new = clk_fv.index;

    DVFS_LOG("%s--(%s),volt=%d(was %dmV),rate=%lu(was %lu),vd=%u(was %u)\n",__func__,
						dvfs_clk->name,volt_new,volt_old,temp_hz,clk_get_rate(clk)
						,volt_new,volt_old);
    /* if up the voltage*/
    if (volt_old < volt_new) {
        if(dvfs_clk->vd->regulator&&dvfs_regulator_set_voltage(dvfs_clk->vd->regulator,volt_new, volt_new) < 0) {
            DVFS_ERR("set voltage err\n");
            return -1;
        }
        dvfs_clk->vd->cur_volt = volt_new;
		DVFS_LOG("%s set volt ok up\n",dvfs_clk->name); 
		udelay(get_volt_up_delay(volt_new,volt_old));
		//DVFS_DBG("get_volt_up_delay%u",get_volt_up_delay(volt_new,volt_old));
    }
	
    if(dvfs_clk->clk_dvfs_target) {
        ret = dvfs_clk->clk_dvfs_target(clk, temp_hz, clk_set_rate_locked);
    } else {
        ret = clk_set_rate_locked(clk, temp_hz);
    }
    if (ret < 0) {
        DVFS_ERR("set rate err\n");
        return -1;
    }	
    dvfs_clk->set_freq	= temp_hz/1000;
	
	DVFS_LOG("dvfs %s set rate%lu ok\n",dvfs_clk->name,clk_get_rate(clk));
	
    /* if down the voltage */
    if (volt_old > volt_new) {
        if(dvfs_clk->vd->regulator&&dvfs_regulator_set_voltage(dvfs_clk->vd->regulator, volt_new, volt_new) < 0) {
            DVFS_ERR("set voltage err\n");
            return -1;
        }
        dvfs_clk->vd->cur_volt = volt_new;
		DVFS_LOG("dvfs %s set volt ok dn\n",dvfs_clk->name);
      
    }
    return ret;
}



/*****************************init**************************/
/**
* rate must be raising sequence
*/
static struct cpufreq_frequency_table cpu_dvfs_table[] = {
	//{.frequency	= 48*DVFS_KHZ, .index = 920*DVFS_MV},
	//{.frequency	= 126*DVFS_KHZ, .index	= 970*DVFS_MV},
	// {.frequency	= 252*DVFS_KHZ, .index	= 1040*DVFS_MV},
	// {.frequency	= 504*DVFS_KHZ, .index	= 1060*DVFS_MV},
	{.frequency	= 816*DVFS_KHZ, .index	= 1080*DVFS_MV},
	//  {.frequency	= 1008*DVFS_KHZ, .index	= 1100*DVFS_MV},
	{.frequency	= CPUFREQ_TABLE_END},
};
static struct cpufreq_frequency_table ddr_dvfs_table[] = {
	//{.frequency = 100*DVFS_KHZ, .index = 1100*DVFS_MV},
	{.frequency = 200*DVFS_KHZ, .index = 1000*DVFS_MV},
	{.frequency = 300*DVFS_KHZ, .index = 1050*DVFS_MV},
	{.frequency = 400*DVFS_KHZ, .index = 1100*DVFS_MV},
	{.frequency = 500*DVFS_KHZ, .index = 1150*DVFS_MV},
	{.frequency = 600*DVFS_KHZ, .index = 1200*DVFS_MV},
	{.frequency = CPUFREQ_TABLE_END},  
};
static struct cpufreq_frequency_table gpu_dvfs_table[] = {
	  {.frequency = 100*DVFS_KHZ, .index = 1000*DVFS_MV},
	  {.frequency = 200*DVFS_KHZ, .index = 1050*DVFS_MV},
	  {.frequency = 300*DVFS_KHZ, .index = 1100*DVFS_MV},
	  {.frequency = 400*DVFS_KHZ, .index = 1150*DVFS_MV},
	  {.frequency = 500*DVFS_KHZ, .index = 1200*DVFS_MV},
	  {.frequency = CPUFREQ_TABLE_END},  
};

static struct cpufreq_frequency_table peri_aclk_dvfs_table[] = {
	  {.frequency = 100*DVFS_KHZ, .index = 1000*DVFS_MV},
	  {.frequency = 200*DVFS_KHZ, .index = 1050*DVFS_MV},
	  {.frequency = 300*DVFS_KHZ, .index = 1070*DVFS_MV},
	  {.frequency = 500*DVFS_KHZ, .index = 1100*DVFS_MV},
	  {.frequency = CPUFREQ_TABLE_END},  
};

static struct vd_node vd_cpu = {
    .name 			= "vd_cpu",
	.regulator_name	= "vdd_cpu",
    .vd_dvfs_target	= dvfs_target_cpu,
};

static struct vd_node vd_core = {
    .name 			= "vd_core",
	.regulator_name = "vdd_core",
    .vd_dvfs_target	= dvfs_target_core,
};

static struct vd_node vd_rtc = {
    .name 			= "vd_rtc",
	.regulator_name	= "vdd_rtc",
    .vd_dvfs_target	= NULL,
};

static struct vd_node *rk30_vds[] = {&vd_cpu,&vd_core,&vd_rtc};

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

#define RK_CLKS(_clk_name, _ppds, _dvfs_table, _dvfs_nb) \
	{ \
	.name	= _clk_name, \
	.pds = _ppds,\
	.dvfs_table = _dvfs_table,	\
	.dvfs_nb	= _dvfs_nb,	\
	}

static struct pds_list aclk_periph_pds[] = {
    CLK_PDS(&pd_peri),
    CLK_PDS(NULL),
};


static struct clk_node rk30_clks[] = {
    RK_CLKS("cpu", cpu_pds, cpu_dvfs_table, &rk_dvfs_clk_notifier),
    RK_CLKS("ddr", ddr_pds, ddr_dvfs_table, &rk_dvfs_clk_notifier),
    RK_CLKS("gpu", gpu_pds, gpu_dvfs_table, &rk_dvfs_clk_notifier),
   RK_CLKS("aclk_periph", aclk_periph_pds, peri_aclk_dvfs_table,&rk_dvfs_clk_notifier),
};

int rk30_dvfs_init(void)
{
    int i = 0;
    for (i = 0; i < ARRAY_SIZE(rk30_vds); i++) {
        rk_regist_vd(rk30_vds[i]);
    }
    for (i = 0; i < ARRAY_SIZE(rk30_pds); i++) {
        rk_regist_pd(&rk30_pds[i]);
    }
    for (i = 0; i < ARRAY_SIZE(rk30_clks); i++) {
        rk_regist_clk(&rk30_clks[i]);
    }
	
    return 0;
}

#if 1
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
                         dvfs_clk->name, dvfs_clk->set_freq, dvfs_clk->set_volt, dvfs_clk->enable_dvfs == 0 ? "DISABLE" : "ENABLE");
                for (i = 0; dvfs_clk->pds[i].pd != NULL; i++) {
                    clkparent = dvfs_clk->pds[i].pd;
                    DVFS_DBG("|  |  |  |- clock parents: %s, vd_parent = %s\n", clkparent->name, clkparent->vd->name);
                }

                for (i = 0; (dvfs_clk->dvfs_table[i].frequency != CPUFREQ_TABLE_END); i++) {
                    DVFS_DBG("|  |  |  |- freq = %d, volt = %d\n", dvfs_clk->dvfs_table[i].frequency, dvfs_clk->dvfs_table[i].index);

                }
            }
        }
    }
    DVFS_DBG("-------------DVFS DEBUG END------------\n");
}
#endif



