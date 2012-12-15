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
#include <mach/dvfs.h>
#include <mach/clock.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/hrtimer.h>

#if 1
#define DVFS_DBG(fmt, args...) {while(0);}
#else
#define DVFS_DBG(fmt, args...) printk(KERN_DEBUG "DVFS DBG:\t"fmt, ##args)
#endif

#define DVFS_SET_VOLT_FAILURE 	1
#define DVFS_SET_VOLT_SUCCESS	0

#define DVFS_ERR(fmt, args...) printk(KERN_ERR "DVFS ERR:\t"fmt, ##args)
#define DVFS_LOG(fmt, args...) printk(KERN_DEBUG "DVFS LOG:\t"fmt, ##args)

#define dvfs_regulator_get(dev,id) regulator_get((dev),(id))
#define dvfs_regulator_put(regu) regulator_put((regu))
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
static DEFINE_MUTEX(rk_dvfs_mutex);

static int dump_dbg_map(char* buf);

#define PD_ON	1
#define PD_OFF	0

#define get_volt_up_delay(new_volt, old_volt)	\
	((new_volt) > (old_volt) ? (((new_volt) - (old_volt)) >> 9) : 0)

int dvfs_regulator_set_voltage_readback(struct regulator *regulator, int min_uV, int max_uV)
{
	int ret = 0, read_back = 0;
	ret = dvfs_regulator_set_voltage(regulator, max_uV, max_uV);
	if (ret < 0) {
		DVFS_ERR("%s now read back to check voltage\n", __func__);

		/* read back to judge if it is already effect */
		mdelay(2);
		read_back = dvfs_regulator_get_voltage(regulator); 
		if (read_back == max_uV) {
			DVFS_ERR("%s set ERROR but already effected, volt=%d\n", __func__, read_back);
			ret = 0;
		} else {
			DVFS_ERR("%s set ERROR AND NOT effected, volt=%d\n", __func__, read_back);
		}
	}
	return ret;
}

struct regulator* dvfs_get_regulator(char *regulator_name)
{
	struct vd_node *vd;
	list_for_each_entry(vd, &rk_dvfs_tree, node) {
		if (strcmp(regulator_name, vd->regulator_name) == 0) {
			return vd->regulator;
		}
	}
	return NULL;
}

int dvfs_clk_enable_limit(struct clk *clk, unsigned int min_rate, unsigned max_rate)
{
	struct clk_node* dvfs_clk;
	u32 rate = 0;
	dvfs_clk = clk->dvfs_info;

	dvfs_clk->freq_limit_en = 1;
	dvfs_clk->min_rate = min_rate;
	dvfs_clk->max_rate = max_rate;
	
	rate = clk_get_rate(clk);
	if (rate < min_rate) 
		dvfs_clk_set_rate(clk, min_rate);
	else if (rate > max_rate)
		dvfs_clk_set_rate(clk, max_rate);
	return 0;
}

int dvfs_clk_disable_limit(struct clk *clk)
{
	struct clk_node* dvfs_clk;
	dvfs_clk = clk->dvfs_info;
	
	dvfs_clk->freq_limit_en = 0;
	
	return 0;
}

int is_support_dvfs(struct clk_node *dvfs_info)
{
	return (dvfs_info->vd && dvfs_info->vd->vd_dvfs_target && dvfs_info->enable_dvfs);
}

int dvfs_set_rate(struct clk *clk, unsigned long rate)
{
	int ret = 0;
	struct vd_node *vd;
	DVFS_DBG("%s(%s(%lu))\n", __func__, clk->name, rate);
	if (!clk->dvfs_info) {
		DVFS_ERR("%s :This clk do not support dvfs!\n", __func__);
		ret = -1;
	} else {
		vd = clk->dvfs_info->vd;
		// mutex_lock(&vd->dvfs_mutex);
		mutex_lock(&rk_dvfs_mutex);
		ret = vd->vd_dvfs_target(clk, rate);
		mutex_unlock(&rk_dvfs_mutex);
		// mutex_unlock(&vd->dvfs_mutex);
	}
	DVFS_DBG("%s(%s(%lu)),is end\n", __func__, clk->name, rate);
	return ret;
}

static int dvfs_clk_get_ref_volt_depend(struct depend_list *depend, int rate_khz,
		struct cpufreq_frequency_table *clk_fv)
{
	int i = 0;
	if (rate_khz == 0 || !depend || !depend->dep_table) {
		return -1;
	}
	clk_fv->frequency = rate_khz;
	clk_fv->index = 0;

	for (i = 0; (depend->dep_table[i].frequency != CPUFREQ_TABLE_END); i++) {
		if (depend->dep_table[i].frequency >= rate_khz) {
			clk_fv->frequency = depend->dep_table[i].frequency;
			clk_fv->index = depend->dep_table[i].index;
			return 0;
		}
	}
	clk_fv->frequency = 0;
	clk_fv->index = 0;
	return -1;
}
static int dvfs_clk_get_ref_volt(struct clk_node *dvfs_clk, int rate_khz,
		struct cpufreq_frequency_table *clk_fv)
{
	int i = 0;
	if (rate_khz == 0 || !dvfs_clk || !dvfs_clk->dvfs_table) {
		/* since no need */
		return -1;
	}
	clk_fv->frequency = rate_khz;
	clk_fv->index = 0;

	for (i = 0; (dvfs_clk->dvfs_table[i].frequency != CPUFREQ_TABLE_END); i++) {
		if (dvfs_clk->dvfs_table[i].frequency >= rate_khz) {
			clk_fv->frequency = dvfs_clk->dvfs_table[i].frequency;
			clk_fv->index = dvfs_clk->dvfs_table[i].index;
			// DVFS_DBG("%s,%s rate=%ukhz(vol=%d)\n",__func__,dvfs_clk->name, 
			// clk_fv->frequency, clk_fv->index);
			return 0;
		}
	}
	clk_fv->frequency = 0;
	clk_fv->index = 0;
	// DVFS_DBG("%s get corresponding voltage error! out of bound\n", dvfs_clk->name);
	return -1;
}

static int dvfs_pd_get_newvolt_byclk(struct pd_node *pd, struct clk_node *dvfs_clk)
{
	struct clk_list	*child;
	int volt_max = 0;

	if (!pd || !dvfs_clk)
		return 0;

	if (dvfs_clk->set_volt >= pd->cur_volt) {
		return dvfs_clk->set_volt;
	}

	list_for_each_entry(child, &pd->clk_list, node) {
		// DVFS_DBG("%s ,pd(%s),dvfs(%s),volt(%u)\n",__func__,pd->name,
		// dvfs_clk->name,dvfs_clk->set_volt);
		volt_max = max(volt_max, child->dvfs_clk->set_volt);
	}
	return volt_max;
}

void dvfs_update_clk_pds_volt(struct clk_node *dvfs_clk)
{
	struct pd_node	 *pd;
	int i;
	if (!dvfs_clk)
		return;
	for (i = 0; (dvfs_clk->pds[i].pd != NULL); i++) {
		pd = dvfs_clk->pds[i].pd;
		// DVFS_DBG("%s dvfs(%s),pd(%s)\n",__func__,dvfs_clk->name,pd->name);
		pd->cur_volt = dvfs_pd_get_newvolt_byclk(pd, dvfs_clk);
	}
}

static int dvfs_vd_get_newvolt_bypd(struct vd_node *vd)
{
	struct pd_node		*pd;
	struct depend_list	*depend;
	int 	volt_max_vd = 0;
	list_for_each_entry(pd, &vd->pd_list, node) {
		// DVFS_DBG("%s pd(%s,%u)\n",__func__,pd->name,pd->cur_volt);
		volt_max_vd = max(volt_max_vd, pd->cur_volt);
	}

	/* some clks depend on this voltage domain */
	if (!list_empty(&vd->req_volt_list)) {
		list_for_each_entry(depend, &vd->req_volt_list, node2vd) {
			volt_max_vd = max(volt_max_vd, depend->req_volt);
		}
	}
	return volt_max_vd;
}

static int dvfs_vd_get_newvolt_byclk(struct clk_node *dvfs_clk)
{
	if (!dvfs_clk)
		return -1;
	dvfs_update_clk_pds_volt(dvfs_clk);
	return  dvfs_vd_get_newvolt_bypd(dvfs_clk->vd);
}

void dvfs_clk_register_set_rate_callback(struct clk *clk, clk_dvfs_target_callback clk_dvfs_target)
{
	struct clk_node *dvfs_clk = clk_get_dvfs_info(clk);
	if (IS_ERR_OR_NULL(dvfs_clk)){
		DVFS_ERR("%s %s get dvfs_clk err\n", __func__, clk->name);
		return ;
	}
	dvfs_clk->clk_dvfs_target = clk_dvfs_target;
}

struct cpufreq_frequency_table *dvfs_get_freq_volt_table(struct clk *clk) 
{
	struct clk_node *info = clk_get_dvfs_info(clk);
	struct cpufreq_frequency_table *table;
	if (!info || !info->dvfs_table) {
		return NULL;
	}
	mutex_lock(&mutex);
	table = info->dvfs_table;
	mutex_unlock(&mutex);
	return table;
}

int dvfs_set_freq_volt_table(struct clk *clk, struct cpufreq_frequency_table *table)
{
	struct clk_node *info = clk_get_dvfs_info(clk);
	if (!table || !info)
		return -1;

	mutex_lock(&mutex);
	info->dvfs_table = table;
	mutex_unlock(&mutex);
	return 0;
}

int dvfs_set_depend_table(struct clk *clk, char *vd_name, struct cpufreq_frequency_table *table)
{
	struct vd_node		*vd;
	struct depend_list	*depend;
	struct clk_node		*info;

	info = clk_get_dvfs_info(clk);
	if (!table || !info || !vd_name) {
		DVFS_ERR("%s :DVFS SET DEPEND TABLE ERROR! table or info or name empty\n", __func__);
		return -1;
	}
	
	list_for_each_entry(vd, &rk_dvfs_tree, node) {
		if (0 == strcmp(vd->name, vd_name)) {
			DVFS_DBG("FOUND A MATCH\n");
			mutex_lock(&mutex);
			list_for_each_entry(depend, &info->depend_list, node2clk) {
				if (vd == depend->dep_vd && info == depend->dvfs_clk) {
					depend->dep_table = table;
					break;
				}
			}
			mutex_unlock(&mutex);
			return 0;
		}
	}
	DVFS_ERR("%s :DVFS SET DEPEND TABLE ERROR! can not find vd:%s\n", __func__, vd_name);

	return 0;
}

int dvfs_set_arm_logic_volt(struct dvfs_arm_table *dvfs_cpu_logic_table, 
		struct cpufreq_frequency_table *cpu_dvfs_table,
		struct cpufreq_frequency_table *dep_cpu2core_table)
{
	int i = 0;
	for (i = 0; dvfs_cpu_logic_table[i].frequency != CPUFREQ_TABLE_END; i++) {
		cpu_dvfs_table[i].frequency = dvfs_cpu_logic_table[i].frequency;
		cpu_dvfs_table[i].index = dvfs_cpu_logic_table[i].cpu_volt;

		dep_cpu2core_table[i].frequency = dvfs_cpu_logic_table[i].frequency;
		dep_cpu2core_table[i].index = dvfs_cpu_logic_table[i].logic_volt;
	}

	cpu_dvfs_table[i].frequency = CPUFREQ_TABLE_END;
	dep_cpu2core_table[i].frequency = CPUFREQ_TABLE_END;

	dvfs_set_freq_volt_table(clk_get(NULL, "cpu"), cpu_dvfs_table);
	dvfs_set_depend_table(clk_get(NULL, "cpu"), "vd_core", dep_cpu2core_table);
	return 0;
}

int clk_enable_dvfs(struct clk *clk)
{
	struct clk_node *dvfs_clk;
	struct cpufreq_frequency_table clk_fv;
	if (!clk) {
		DVFS_ERR("clk enable dvfs error\n");
		return -1;
	}
	dvfs_clk = clk_get_dvfs_info(clk);
	if (!dvfs_clk || !dvfs_clk->vd) {
		DVFS_ERR("%s clk(%s) not support dvfs!\n", __func__, clk->name);
		return -1;
	}
	if (dvfs_clk->enable_dvfs == 0) {

		if (IS_ERR_OR_NULL(dvfs_clk->vd->regulator)) {
			//regulator = NULL;
			if (dvfs_clk->vd->regulator_name)
				dvfs_clk->vd->regulator = dvfs_regulator_get(NULL, dvfs_clk->vd->regulator_name);
			if (!IS_ERR_OR_NULL(dvfs_clk->vd->regulator)) {
				// DVFS_DBG("dvfs_regulator_get(%s)\n",dvfs_clk->vd->regulator_name);
				dvfs_clk->vd->cur_volt = dvfs_regulator_get_voltage(dvfs_clk->vd->regulator);
			} else {
				//dvfs_clk->vd->regulator = NULL;
				dvfs_clk->enable_dvfs = 0;
				DVFS_ERR("%s can't get regulator in %s\n", dvfs_clk->name, __func__);
				return -1;
			}
		} else {
			dvfs_clk->vd->cur_volt = dvfs_regulator_get_voltage(dvfs_clk->vd->regulator);
			// DVFS_DBG("%s(%s) vd volt=%u\n",__func__,dvfs_clk->name,dvfs_clk->vd->cur_volt);
		}

		dvfs_clk->set_freq = dvfs_clk_get_rate_kz(clk);
		// DVFS_DBG("%s ,%s get freq%u!\n",__func__,dvfs_clk->name,dvfs_clk->set_freq);
		
		if (dvfs_clk_get_ref_volt(dvfs_clk, dvfs_clk->set_freq, &clk_fv)) {
			if (dvfs_clk->dvfs_table[0].frequency == CPUFREQ_TABLE_END) {
				DVFS_ERR("%s table empty\n", __func__);
				dvfs_clk->enable_dvfs = 0;
				return -1;
			} else {
				DVFS_ERR("WARNING: %s table all value are smaller than default, use default, just enable dvfs\n", __func__);
				dvfs_clk->enable_dvfs++;
				return 0;
			}
		}

		dvfs_clk->set_volt = clk_fv.index;
		// DVFS_DBG("%s,%s,freq%u(ref vol %u)\n",__func__,dvfs_clk->name,
		//	 dvfs_clk->set_freq,dvfs_clk->set_volt);
#if 0
		if (dvfs_clk->dvfs_nb) {
			// must unregister when clk disable
			rk30_clk_notifier_register(clk, dvfs_clk->dvfs_nb);
		}
#endif
		dvfs_vd_get_newvolt_byclk(dvfs_clk);
		if(dvfs_clk->vd->cur_volt<dvfs_clk->set_volt) {
			int ret;
			mutex_lock(&rk_dvfs_mutex);
			ret = dvfs_regulator_set_voltage_readback(dvfs_clk->vd->regulator, 
					dvfs_clk->set_volt, dvfs_clk->set_volt);
			if (ret < 0) {
				dvfs_clk->vd->volt_set_flag = DVFS_SET_VOLT_FAILURE;
				dvfs_clk->enable_dvfs = 0;
				DVFS_ERR("dvfs enable clk %s,set volt error \n", dvfs_clk->name);
				mutex_unlock(&rk_dvfs_mutex);
				return -1;
			}
			dvfs_clk->vd->volt_set_flag = DVFS_SET_VOLT_SUCCESS;
			mutex_unlock(&rk_dvfs_mutex);
		}
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
	if (!dvfs_clk->enable_dvfs) {
		DVFS_DBG("clk is already closed!\n");
		return -1;
	} else {
		dvfs_clk->enable_dvfs--;
		if (0 == dvfs_clk->enable_dvfs) {
			DVFS_ERR("clk closed!\n");
#if 0
			clk_notifier_unregister(clk, dvfs_clk->dvfs_nb);
			DVFS_DBG("clk unregister nb!\n");
#endif
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

static struct clk_node *dvfs_get_dvfs_clk_byname(char *name)
{
	struct vd_node *vd;
	struct pd_node *pd;
	struct clk_list	*child;
	list_for_each_entry(vd, &rk_dvfs_tree, node) {
		list_for_each_entry(pd, &vd->pd_list, node) {
			list_for_each_entry(child, &pd->clk_list, node) {
				if (0 == strcmp(child->dvfs_clk->name, name)) {
					return child->dvfs_clk;
				}
			}
		}
	}
	return NULL;
}
static int rk_regist_vd(struct vd_node *vd)
{
	if (!vd)
		return -1;
	mutex_lock(&mutex);
	//mutex_init(&vd->dvfs_mutex);
	list_add(&vd->node, &rk_dvfs_tree);
	INIT_LIST_HEAD(&vd->pd_list);
	INIT_LIST_HEAD(&vd->req_volt_list);

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

	if (!dvfs_clk)
		return -1;

	if (!dvfs_clk->pds)
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
	dvfs_clk->clk = clk;
	clk_register_dvfs(dvfs_clk, clk);
	INIT_LIST_HEAD(&dvfs_clk->depend_list);
	mutex_unlock(&mutex);
	return 0;
}

static int rk_regist_depends(struct depend_lookup *dep_node)
{
	struct depend_list	*depend_list;
	struct clk_node		*dvfs_clk;

	if (!dep_node) {
		DVFS_ERR("%s : DVFS BAD depend node!\n", __func__);
		return -1;
	}

	if (!dep_node->clk_name || !dep_node->dep_vd) {
		DVFS_ERR("%s : DVFS BAD depend members!\n", __func__);
		return -1;
	}

	depend_list = &dep_node->dep_list;
	dvfs_clk = dvfs_get_dvfs_clk_byname(dep_node->clk_name);

	mutex_lock(&mutex);

	depend_list->dvfs_clk = dvfs_clk;
	depend_list->dep_vd = dep_node->dep_vd;
	depend_list->dep_table = dep_node->dep_table;

	list_add(&depend_list->node2clk, &dvfs_clk->depend_list);
	list_add(&depend_list->node2vd, &depend_list->dep_vd->req_volt_list);

	mutex_unlock(&mutex);
	return 0;
}
#if 0
static int dvfs_set_depend_pre(struct clk_node *dvfs_clk, unsigned long rate_old, unsigned long rate_new)
{
	struct depend_list	*depend;
	struct cpufreq_frequency_table	clk_fv;
	int ret = -1;
	int volt = 0;
	struct regulator *regulator;

	if (rate_old >= rate_new) {
		return 0;
	}
	list_for_each_entry(depend, &dvfs_clk->depend_list, node2clk) {
		ret = dvfs_clk_get_ref_volt_depend(depend, rate_new / 1000, &clk_fv);
		if (ret != 0) {
			DVFS_ERR("%s LOGIC DVFS CAN NOT GET REF VOLT!, frequency too large!\n", __func__);
			return -1;
		}

		if (!depend->dep_vd->regulator) {
			DVFS_DBG("%s regulator empty\n", __func__);
			regulator = dvfs_regulator_get(NULL, depend->dep_vd->regulator_name);
			if (!regulator) {
				DVFS_ERR("%s get regulator err\n", __func__);
				return -1;
			}

			depend->dep_vd->regulator = regulator;
		}
		if (IS_ERR_OR_NULL(depend->dep_vd->regulator)) {
			DVFS_ERR("%s vd's(%s) regulator not NULL but error\n", __func__, depend->dep_vd->name);
			return -1;
		}

		if (clk_fv.index == dvfs_regulator_get_voltage(depend->dep_vd->regulator)) {
			depend->req_volt = clk_fv.index;
			DVFS_DBG("%s same voltage\n", __func__);
			return 0;
		}

		depend->req_volt = clk_fv.index;
		volt = dvfs_vd_get_newvolt_bypd(depend->dep_vd);
		DVFS_DBG("%s setting voltage = %d\n", __func__, volt);
		ret = dvfs_regulator_set_voltage_readback(depend->dep_vd->regulator, volt, volt);
		if (0 != ret) {
			DVFS_ERR("%s set voltage = %d ERROR, ret = %d\n", __func__, volt, ret);
			return -1;
		}
		udelay(200);
		DVFS_DBG("%s set voltage = %d OK, ret = %d\n", __func__, volt, ret);
		if (ret != 0) {
			DVFS_ERR("%s err, ret = %d\n", __func__, ret);
			return -1;
		}
	}

	return 0;
}

static int dvfs_set_depend_post(struct clk_node *dvfs_clk, unsigned long rate_old, unsigned long rate_new)
{
	struct depend_list	*depend;
	struct cpufreq_frequency_table	clk_fv;
	int ret = -1;
	int volt = 0;
	struct regulator *regulator;

	if (rate_old <= rate_new) 
		return 0;
	list_for_each_entry(depend, &dvfs_clk->depend_list, node2clk) {
		ret = dvfs_clk_get_ref_volt_depend(depend, rate_new / 1000, &clk_fv);
		if (ret != 0) {
			DVFS_ERR("%s LOGIC DVFS CAN NOT GET REF VOLT!, frequency too large!\n", __func__);
			return -1;
		}

		if (!depend->dep_vd->regulator) {
			DVFS_DBG("%s regulator empty\n", __func__);
			regulator = dvfs_regulator_get(NULL, depend->dep_vd->regulator_name);
			if (!regulator) {
				DVFS_ERR("%s get regulator err\n", __func__);
				return -1;
			}

			depend->dep_vd->regulator = regulator;
		}
		if (IS_ERR_OR_NULL(depend->dep_vd->regulator)) {
			DVFS_ERR("%s vd's(%s) regulator not NULL but error\n", __func__, depend->dep_vd->name);
			return -1;
		}

		if (clk_fv.index == dvfs_regulator_get_voltage(depend->dep_vd->regulator)) {
			depend->req_volt = clk_fv.index;
			DVFS_DBG("%s same voltage\n", __func__);
			return 0;
		}

		depend->req_volt = clk_fv.index;
		volt = dvfs_vd_get_newvolt_bypd(depend->dep_vd);
		DVFS_DBG("%s setting voltage = %d\n", __func__, volt);
		ret = dvfs_regulator_set_voltage_readback(depend->dep_vd->regulator, volt, volt);
		if (0 != ret) {
			DVFS_ERR("%s set voltage = %d ERROR, ret = %d\n", __func__, volt, ret);
			return -1;
		}
		udelay(200);
		DVFS_DBG("%s set voltage = %d OK, ret = %d\n", __func__, volt, ret);
		if (ret != 0) {
			DVFS_ERR("%s err, ret = %d\n", __func__, ret);
			return -1;
		}
	}

	return 0;
}
#endif

#define ARM_HIGHER_LOGIC	(150 * 1000)
#define LOGIC_HIGHER_ARM	(100 * 1000)

int check_volt_correct(int volt_old, int *volt_new, int volt_dep_old, int *volt_dep_new, 
		int clk_biger_than_dep, int dep_biger_than_clk)
{
	int up_boundary = 0, low_boundary = 0;
	DVFS_DBG("%d %d\n", clk_biger_than_dep, dep_biger_than_clk);
	up_boundary = volt_old + dep_biger_than_clk;
	low_boundary = volt_old - clk_biger_than_dep;
	
	if (volt_dep_old < low_boundary || volt_dep_old > up_boundary) {
		DVFS_ERR("%s current volt out of bondary volt=%d(old=%d), volt_dep=%d(dep_old=%d), up_bnd=%d(dn=%d)\n",
				__func__, *volt_new, volt_old, *volt_dep_new, volt_dep_old, up_boundary, low_boundary);
		return -1;
	}
	
	up_boundary = *volt_new + dep_biger_than_clk;
	low_boundary = *volt_new - clk_biger_than_dep;
	
	if (*volt_dep_new < low_boundary || *volt_dep_new > up_boundary) {

		if (*volt_dep_new < low_boundary) {
			*volt_dep_new = low_boundary;
			
		} else if (*volt_dep_new > up_boundary) {
			*volt_new = *volt_dep_new - dep_biger_than_clk;
		}
		DVFS_LOG("%s target volt out of bondary volt=%d(old=%d), volt_dep=%d(dep_old=%d), up_bnd=%d(dn=%d)\n",
				__func__, *volt_new, volt_old, *volt_dep_new, volt_dep_old, up_boundary, low_boundary);		
		return 0;
	}
	return 0;

}
int dvfs_scale_volt(struct vd_node *vd_clk, struct vd_node *vd_dep, 
		int volt_old, int volt_new, int volt_dep_old, int volt_dep_new, int clk_biger_than_dep, int dep_biger_than_clk)
{
	struct regulator *regulator, *regulator_dep;
	int volt = 0, volt_dep = 0, step = 0, step_dep = 0;
	int volt_pre = 0, volt_dep_pre = 0;
	int ret = 0;

	DVFS_DBG("ENTER %s, volt=%d(old=%d), volt_dep=%d(dep_old=%d)\n", __func__, volt_new, volt_old, volt_dep_new, volt_dep_old);
	regulator = vd_clk->regulator;
	regulator_dep = vd_dep->regulator;

	if (IS_ERR_OR_NULL(regulator) || IS_ERR(regulator_dep)) {	
		DVFS_ERR("%s dvfs_clk->vd->regulator or depend->dep_vd->regulator == NULL\n", __func__);
		return -1;
	}

	volt = volt_old;
	volt_dep = volt_dep_old;

	step = volt_new - volt_old > 0 ? 1 : (-1);
	step_dep = volt_dep_new - volt_dep_old > 0 ? 1 : (-1);

	DVFS_DBG("step=%d step_dep=%d %d\n", step, step_dep, step * step_dep);

	DVFS_DBG("Volt_new=%d(old=%d), volt_dep_new=%d(dep_old=%d)\n",
			volt_new, volt_old, volt_dep_new, volt_dep_old);
	do {
		volt_pre = volt;
		volt_dep_pre = volt_dep;
		if (step * step_dep < 0) {
			// target is between volt_old and volt_dep_old, just
			// need one step
			DVFS_DBG("step * step_dep < 0\n");
			volt = volt_new;
			volt_dep = volt_dep_new;
		
		} else if (step > 0) {
			// up voltage
			DVFS_DBG("step > 0\n");

			if (volt > volt_dep) {
				if (volt_dep == volt_dep_new) {
					volt = volt_dep + clk_biger_than_dep;
				} else {
					volt_dep = volt + dep_biger_than_clk;
				}
			} else {
				if (volt == volt_new) {
					volt_dep = volt + dep_biger_than_clk;
				} else {
					volt = volt_dep + clk_biger_than_dep;
				}
			}
			volt = volt > volt_new ? volt_new : volt;
			volt_dep = volt_dep > volt_dep_new ? volt_dep_new : volt_dep;

		} else if (step < 0) {
			// down voltage
			DVFS_DBG("step < 0\n");
			if (volt > volt_dep) {
				if (volt == volt_new) {
					volt_dep = volt - clk_biger_than_dep;
				} else {
					volt = volt_dep - dep_biger_than_clk;
				}
			} else {
				if (volt_dep == volt_dep_new) {
					volt = volt_dep - dep_biger_than_clk;
				} else {
					volt_dep = volt - clk_biger_than_dep;
				}
			}
			volt = volt < volt_new ? volt_new : volt;
			volt_dep = volt_dep < volt_dep_new ? volt_dep_new : volt_dep;

		} else {
			DVFS_ERR("Oops, some bugs here:Volt_new=%d(old=%d), volt_dep_new=%d(dep_old=%d)\n", 
					volt_new, volt_old, volt_dep_new, volt_dep_old);
			goto fail;
		}

		DVFS_DBG("\t\tNOW:Volt=%d, volt_dep=%d\n", volt, volt_dep);

		if (vd_clk->cur_volt != volt) {
			ret = dvfs_regulator_set_voltage_readback(regulator, volt, volt);
			udelay(get_volt_up_delay(volt, volt_pre));
			if (ret < 0) {
				DVFS_ERR("%s %s set voltage up err ret = %d, Vnew = %d(was %d)mV\n", 
						__func__, vd_clk->name, ret, volt_new, volt_old);
				goto fail;
			}
			vd_clk->cur_volt = volt;
		}

		if (vd_dep->cur_volt != volt_dep) {
			ret = dvfs_regulator_set_voltage_readback(regulator_dep, volt_dep, volt_dep);
			udelay(get_volt_up_delay(volt_dep, volt_dep_pre));
			if (ret < 0) {
				DVFS_ERR("depend %s %s set voltage up err ret = %d, Vnew = %d(was %d)mV\n", 
						__func__, vd_dep->name, ret, volt_dep_new, volt_dep_old);
				goto fail;
			}
			vd_dep->cur_volt = volt_dep;
		}

	} while (volt != volt_new || volt_dep!= volt_dep_new);
	
	vd_clk->volt_set_flag = DVFS_SET_VOLT_SUCCESS;
	vd_clk->cur_volt = volt_new;
	
	return 0;
fail:
	DVFS_ERR("+++++++++++++++++FAIL AREA\n");	
	vd_clk->cur_volt = volt_old;
	vd_dep->cur_volt = volt_dep_old;
	vd_clk->volt_set_flag = DVFS_SET_VOLT_FAILURE;
	ret = dvfs_regulator_set_voltage_readback(regulator, volt_old, volt_old);
	if (ret < 0) {
		vd_clk->volt_set_flag = DVFS_SET_VOLT_FAILURE;
		DVFS_ERR("%s %s set callback voltage err ret = %d, Vnew = %d(was %d)mV\n", 
				__func__, vd_clk->name, ret, volt_new, volt_old);
	}
	
	ret = dvfs_regulator_set_voltage_readback(regulator_dep, volt_dep_old, volt_dep_old);
	if (ret < 0) {
		vd_dep->volt_set_flag = DVFS_SET_VOLT_FAILURE;
		DVFS_ERR("%s %s set callback voltage err ret = %d, Vnew = %d(was %d)mV\n", 
				__func__, vd_dep->name, ret, volt_dep_new, volt_dep_old);
	}

	return -1;
}

int dvfs_scale_volt_direct(struct vd_node *vd_clk, int volt_new)
{
	int ret = 0;
	DVFS_DBG("ENTER %s, volt=%d(old=%d)\n", __func__, volt_new, vd_clk->cur_volt);
	if (IS_ERR_OR_NULL(vd_clk)) {
		DVFS_ERR("%s vd_node error\n", __func__);
		return -1;
	}

	DVFS_DBG("ENTER %s, volt=%d(old=%d)\n", __func__, volt_new, vd_clk->cur_volt);
	if (!IS_ERR_OR_NULL(vd_clk->regulator)) {
		ret = dvfs_regulator_set_voltage_readback(vd_clk->regulator, volt_new, volt_new);
		udelay(get_volt_up_delay(volt_new, vd_clk->cur_volt));
		if (ret < 0) {
			vd_clk->volt_set_flag = DVFS_SET_VOLT_FAILURE;
			DVFS_ERR("%s %s set voltage up err ret = %d, Vnew = %d(was %d)mV\n", 
					__func__, vd_clk->name, ret, volt_new, vd_clk->cur_volt);
			return -1;
		}

	} else {
		DVFS_ERR("%s up volt dvfs_clk->vd->regulator == NULL\n", __func__);
		return -1;
	}

	vd_clk->volt_set_flag = DVFS_SET_VOLT_SUCCESS;
	vd_clk->cur_volt = volt_new;

	return 0;

}

int dvfs_scale_volt_bystep(struct vd_node *vd_clk, struct vd_node *vd_dep, int volt_new, int volt_dep_new, 
		int clk_biger_than_dep, int dep_biger_than_clk)

{
	int ret = 0;
	int volt_old = 0, volt_dep_old = 0;

	volt_old = vd_clk->cur_volt;
	volt_dep_old = vd_dep->cur_volt;

	DVFS_DBG("ENTER %s, volt=%d(old=%d) vd_dep=%d(dep_old=%d)\n", __func__, 
			volt_new, volt_old, volt_dep_new, volt_dep_old);

	if (check_volt_correct(volt_old, &volt_new, volt_dep_old, &volt_dep_new, 
				clk_biger_than_dep, dep_biger_than_clk) < 0) {
		DVFS_ERR("CURRENT VOLT INCORRECT, VD=%s, VD_DEP=%s\n", vd_clk->name, vd_dep->name);
		return -1;
	}
	DVFS_DBG("ENTER %s, volt=%d(old=%d), volt_dep=%d(dep_old=%d)\n", __func__, 
			volt_new, volt_old, volt_dep_new, volt_dep_old);
	ret = dvfs_scale_volt(vd_clk, vd_dep, volt_old, volt_new, volt_dep_old, volt_dep_new, 
			clk_biger_than_dep, dep_biger_than_clk);
	if (ret < 0) {
		vd_clk->volt_set_flag = DVFS_SET_VOLT_FAILURE;
		DVFS_ERR("set volt error\n");
		return -1;
	}

	return 0;
}

int dvfs_reset_volt(struct vd_node *dvfs_vd)
{	
	int flag_set_volt_correct = 0;
	if (!IS_ERR_OR_NULL(dvfs_vd->regulator))
		flag_set_volt_correct = dvfs_regulator_get_voltage(dvfs_vd->regulator);
	else {
		DVFS_ERR("dvfs regulator is ERROR\n");
		return -1;
	}
	if (flag_set_volt_correct <= 0) {
		DVFS_ERR("%s (clk:%s), try to reload arm_volt error %d!!! stop scaling\n", 
				__func__, dvfs_vd->name, flag_set_volt_correct);
		return -1;
	}
	dvfs_vd->volt_set_flag = DVFS_SET_VOLT_SUCCESS;
	DVFS_ERR("%s (clk:%s), try to reload arm_volt! arm_volt_correct = %d\n", 
			__func__, dvfs_vd->name, flag_set_volt_correct);

	/* Reset vd's voltage */
	dvfs_vd->cur_volt = flag_set_volt_correct;

	return dvfs_vd->cur_volt;
}

int dvfs_get_depend_volt(struct clk_node *dvfs_clk, struct vd_node *dvfs_vd_dep, int rate_new)
{
	struct depend_list	*depend;
	struct cpufreq_frequency_table clk_fv_dep;
	int ret = 0;

	DVFS_DBG("ENTER %s, rate_new=%d\n", __func__, rate_new);
	list_for_each_entry(depend, &dvfs_clk->depend_list, node2clk) {
		DVFS_DBG("--round depend clk:%s(depend:%s)\n", depend->dvfs_clk->name, depend->dep_vd->name);
		// this place just consider ONE depend voltage domain,
		// multi-depends must have some differece
		clk_fv_dep.index = 0;
		if (depend->dep_vd == dvfs_vd_dep) {
			ret = dvfs_clk_get_ref_volt_depend(depend, rate_new / 1000, &clk_fv_dep);
			if (ret < 0) {
				DVFS_ERR("%s get dvfs_ref_volt_depend error\n", __func__);
				return -1;
			}
			depend->req_volt = clk_fv_dep.index;
			return depend->req_volt;
		}
	}

	DVFS_ERR("%s can not find vd node %s\n", __func__, dvfs_vd_dep->name);
	return -1;
}
static struct clk_node *dvfs_clk_cpu;
static struct vd_node vd_core;
int dvfs_target_cpu(struct clk *clk, unsigned long rate_hz)
{
	struct clk_node *dvfs_clk;
	int volt_new = 0, volt_dep_new = 0, clk_volt_store = 0;
	struct cpufreq_frequency_table clk_fv;
	int ret = 0;
	unsigned long rate_new, rate_old;
	
	if (!clk) {
		DVFS_ERR("%s is not a clk\n", __func__);
		return -1;
	}
	dvfs_clk = clk_get_dvfs_info(clk);
	DVFS_DBG("enter %s: clk(%s) rate = %lu Hz\n", __func__, dvfs_clk->name, rate_hz);

	if (!dvfs_clk || dvfs_clk->vd == NULL || IS_ERR_OR_NULL(dvfs_clk->vd->regulator)) {
		DVFS_ERR("dvfs(%s) is not register regulator\n", dvfs_clk->name);
		return -1;
	}

	if (dvfs_clk->vd->volt_set_flag == DVFS_SET_VOLT_FAILURE) {
		/* It means the last time set voltage error */
		ret = dvfs_reset_volt(dvfs_clk->vd);
		if (ret < 0) {
			return -1;
		}
	}

	/* Check limit rate */
	if (dvfs_clk->freq_limit_en) {
		if (rate_hz < dvfs_clk->min_rate) {
			rate_hz = dvfs_clk->min_rate;
		} else if (rate_hz > dvfs_clk->max_rate) {
			rate_hz = dvfs_clk->max_rate;
		}
	}
		
	/* need round rate */
	rate_old = clk_get_rate(clk);
	rate_new = clk_round_rate_nolock(clk, rate_hz);
	if(rate_new == rate_old)
		return 0;
	DVFS_DBG("dvfs(%s) round rate (%lu)(rount %lu) old (%lu)\n", 
			dvfs_clk->name, rate_hz, rate_new, rate_old);

	/* find the clk corresponding voltage */
	if (0 != dvfs_clk_get_ref_volt(dvfs_clk, rate_new / 1000, &clk_fv)) {
		DVFS_ERR("dvfs(%s) rate %luhz is larger,not support\n", dvfs_clk->name, rate_hz);
		return -1;
	}
	clk_volt_store = dvfs_clk->set_volt;
	dvfs_clk->set_volt = clk_fv.index;
	volt_new = dvfs_vd_get_newvolt_byclk(dvfs_clk);

	/* if up the rate */
	if (rate_new > rate_old) {
		if (!list_empty(&dvfs_clk->depend_list)) {
			// update depend's req_volt
			ret = dvfs_get_depend_volt(dvfs_clk, &vd_core, rate_new);
			if (ret <= 0)
				goto fail_roll_back;
			
			volt_dep_new = dvfs_vd_get_newvolt_bypd(&vd_core);
			if (volt_dep_new <= 0) 
				goto fail_roll_back;
			ret = dvfs_scale_volt_direct(dvfs_clk->vd, volt_new);

			//ret = dvfs_scale_volt_bystep(dvfs_clk->vd, &vd_core, volt_new, volt_dep_new, 
			//		ARM_HIGHER_LOGIC, LOGIC_HIGHER_ARM); 
			if (ret < 0) 
				goto fail_roll_back;
		} else {
			ret = dvfs_scale_volt_direct(dvfs_clk->vd, volt_new);
			if (ret < 0) 
				goto fail_roll_back;
		}
	}

	/* scale rate */
	if (dvfs_clk->clk_dvfs_target) {
		ret = dvfs_clk->clk_dvfs_target(clk, rate_new, clk_set_rate_locked);
	} else {
		ret = clk_set_rate_locked(clk, rate_new);
	}

	if (ret < 0) {
		DVFS_ERR("%s set rate err\n", __func__);
		goto fail_roll_back;
	}
	dvfs_clk->set_freq	= rate_new / 1000;

	DVFS_DBG("dvfs %s set rate %lu ok\n", dvfs_clk->name, clk_get_rate(clk));

	/* if down the rate */
	if (rate_new < rate_old) {
		if (!list_empty(&dvfs_clk->depend_list)) {
			// update depend's req_volt
			ret = dvfs_get_depend_volt(dvfs_clk, &vd_core, rate_new);
			if (ret <= 0)
				goto out;
			
			volt_dep_new = dvfs_vd_get_newvolt_bypd(&vd_core);
			if (volt_dep_new <= 0) 
				goto out;
			ret = dvfs_scale_volt_direct(dvfs_clk->vd, volt_new);

			//ret = dvfs_scale_volt_bystep(dvfs_clk->vd, &vd_core, volt_new, volt_dep_new, 
			//		ARM_HIGHER_LOGIC, LOGIC_HIGHER_ARM); 
			if (ret < 0) 
				goto out;
		} else {
			ret = dvfs_scale_volt_direct(dvfs_clk->vd, volt_new);
			if (ret < 0) 
				goto out;
		}
	}

	return ret;
fail_roll_back:
	dvfs_clk->set_volt = clk_volt_store;
	ret = dvfs_get_depend_volt(dvfs_clk, &vd_core, rate_old);
	if (ret <= 0) {
		DVFS_ERR("%s dvfs_get_depend_volt error when roll back!\n", __func__);
	}
out:
	return -1;
}

int dvfs_target_core(struct clk *clk, unsigned long rate_hz)
{
	struct clk_node *dvfs_clk;
	int volt_new = 0, volt_dep_new = 0, clk_volt_store = 0;
	
	struct cpufreq_frequency_table clk_fv;
	
	int ret = 0;
	unsigned long rate_new, rate_old;
		
	if (!clk) {
		DVFS_ERR("%s is not a clk\n", __func__);
		return -1;
	}
	dvfs_clk = clk_get_dvfs_info(clk);
	DVFS_DBG("enter %s: clk(%s) rate = %lu Hz\n", __func__, dvfs_clk->name, rate_hz);

	if (!dvfs_clk || dvfs_clk->vd == NULL || IS_ERR_OR_NULL(dvfs_clk->vd->regulator)) {
		DVFS_ERR("dvfs(%s) is not register regulator\n", dvfs_clk->name);
		return -1;
	}

	if (dvfs_clk->vd->volt_set_flag == DVFS_SET_VOLT_FAILURE) {
		/* It means the last time set voltage error */
		ret = dvfs_reset_volt(dvfs_clk->vd);
		if (ret < 0) {
			return -1;
		}
	}

	/* Check limit rate */
	if (dvfs_clk->freq_limit_en) {
		if (rate_hz < dvfs_clk->min_rate) {
			rate_hz = dvfs_clk->min_rate;
		} else if (rate_hz > dvfs_clk->max_rate) {
			rate_hz = dvfs_clk->max_rate;
		}
	}
		
	/* need round rate */
	rate_old = clk_get_rate(clk);
	rate_new = clk_round_rate_nolock(clk, rate_hz);
	if(rate_new == rate_old)
		return 0;
	DVFS_DBG("dvfs(%s) round rate (%lu)(rount %lu) old (%lu)\n", 
			dvfs_clk->name, rate_hz, rate_new, rate_old);

	/* find the clk corresponding voltage */
	if (0 != dvfs_clk_get_ref_volt(dvfs_clk, rate_new / 1000, &clk_fv)) {
		DVFS_ERR("dvfs(%s) rate %luhz is larger,not support\n", dvfs_clk->name, rate_hz);
		return -1;
	}
	clk_volt_store = dvfs_clk->set_volt;
	dvfs_clk->set_volt = clk_fv.index;
	volt_new = dvfs_vd_get_newvolt_byclk(dvfs_clk);

	/* if up the rate */
	if (rate_new > rate_old) {
		DVFS_DBG("-----------------------------rate_new > rate_old\n");
		volt_dep_new = dvfs_vd_get_newvolt_byclk(dvfs_clk_cpu);

		if (volt_dep_new < 0) 
			goto fail_roll_back;

		ret = dvfs_scale_volt_direct(dvfs_clk->vd, volt_new);
		//ret = dvfs_scale_volt_bystep(dvfs_clk->vd, dvfs_clk_cpu->vd, volt_new, volt_dep_new, 
		//			LOGIC_HIGHER_ARM, ARM_HIGHER_LOGIC); 
		if (ret < 0) 
			goto fail_roll_back;
	}

	/* scale rate */
	if (dvfs_clk->clk_dvfs_target) {
		ret = dvfs_clk->clk_dvfs_target(clk, rate_new, clk_set_rate_locked);
	} else {
		
		ret = clk_set_rate_locked(clk, rate_new);
	}

	if (ret < 0) {
		DVFS_ERR("%s set rate err\n", __func__);
		goto fail_roll_back;
	}
	dvfs_clk->set_freq	= rate_new / 1000;

	DVFS_DBG("dvfs %s set rate %lu ok\n", dvfs_clk->name, clk_get_rate(clk));

	/* if down the rate */
	if (rate_new < rate_old) {
		DVFS_DBG("-----------------------------rate_new < rate_old\n");
		volt_dep_new = dvfs_vd_get_newvolt_byclk(dvfs_clk_cpu);

		if (volt_dep_new < 0) 
			goto out;

		ret = dvfs_scale_volt_direct(dvfs_clk->vd, volt_new);
		//ret = dvfs_scale_volt_bystep(dvfs_clk->vd, dvfs_clk_cpu->vd, volt_new, volt_dep_new, 
		//			LOGIC_HIGHER_ARM, ARM_HIGHER_LOGIC); 
		if (ret < 0) 
			goto out;
	}

	return ret;
fail_roll_back:	
	dvfs_clk->set_volt = clk_volt_store;
	ret = dvfs_get_depend_volt(dvfs_clk, &vd_core, rate_old);
	if (ret <= 0) {
		DVFS_ERR("%s dvfs_get_depend_volt error when roll back!\n", __func__);
	}

out:
	return -1;
}

/*****************************init**************************/
/**
 * rate must be raising sequence
 */
static struct cpufreq_frequency_table cpu_dvfs_table[] = {
	// {.frequency	= 48 * DVFS_KHZ, .index = 920*DVFS_MV},
	// {.frequency	= 126 * DVFS_KHZ, .index	= 970 * DVFS_MV},
	// {.frequency	= 252 * DVFS_KHZ, .index	= 1040 * DVFS_MV},
	// {.frequency	= 504 * DVFS_KHZ, .index	= 1050 * DVFS_MV},
	{.frequency	= 816 * DVFS_KHZ, .index	= 1050 * DVFS_MV},
	// {.frequency	= 1008 * DVFS_KHZ, .index	= 1100 * DVFS_MV},
	{.frequency	= CPUFREQ_TABLE_END},
};

static struct cpufreq_frequency_table ddr_dvfs_table[] = {
	// {.frequency = 100 * DVFS_KHZ, .index = 1100 * DVFS_MV},
	{.frequency = 200 * DVFS_KHZ, .index = 1000 * DVFS_MV},
	{.frequency = 300 * DVFS_KHZ, .index = 1050 * DVFS_MV},
	{.frequency = 400 * DVFS_KHZ, .index = 1100 * DVFS_MV},
	{.frequency = 500 * DVFS_KHZ, .index = 1150 * DVFS_MV},
	{.frequency = 600 * DVFS_KHZ, .index = 1200 * DVFS_MV},
	{.frequency = CPUFREQ_TABLE_END},
};

static struct cpufreq_frequency_table gpu_dvfs_table[] = {
	{.frequency = 90 * DVFS_KHZ, .index = 1100 * DVFS_MV},
	{.frequency = 180 * DVFS_KHZ, .index = 1150 * DVFS_MV},
	{.frequency = 300 * DVFS_KHZ, .index = 1100 * DVFS_MV},
	{.frequency = 400 * DVFS_KHZ, .index = 1150 * DVFS_MV},
	{.frequency = 500 * DVFS_KHZ, .index = 1200 * DVFS_MV},
	{.frequency = CPUFREQ_TABLE_END},
};

static struct cpufreq_frequency_table peri_aclk_dvfs_table[] = {
	{.frequency = 100 * DVFS_KHZ, .index = 1000 * DVFS_MV},
	{.frequency = 200 * DVFS_KHZ, .index = 1050 * DVFS_MV},
	{.frequency = 300 * DVFS_KHZ, .index = 1070 * DVFS_MV},
	{.frequency = 500 * DVFS_KHZ, .index = 1100 * DVFS_MV},
	{.frequency = CPUFREQ_TABLE_END},
};

static struct cpufreq_frequency_table vpu_dvfs_table[] = {
	{.frequency = 266 * DVFS_KHZ, .index = 1100 * DVFS_MV},
	{.frequency = 300 * DVFS_KHZ, .index = 1100 * DVFS_MV},
	{.frequency = 400 * DVFS_KHZ, .index = 1200 * DVFS_MV},
	{.frequency = CPUFREQ_TABLE_END},
};

static struct cpufreq_frequency_table dep_cpu2core_table[] = {
	// {.frequency = 252 * DVFS_KHZ, .index    = 1025 * DVFS_MV},
	// {.frequency = 504 * DVFS_KHZ, .index    = 1025 * DVFS_MV},
	{.frequency = 816 * DVFS_KHZ, .index    = 1050 * DVFS_MV},//logic 1.050V
	// {.frequency = 1008 * DVFS_KHZ,.index    = 1050 * DVFS_MV},
	// {.frequency = 1200 * DVFS_KHZ,.index    = 1050 * DVFS_MV},
	// {.frequency = 1272 * DVFS_KHZ,.index    = 1050 * DVFS_MV},//logic 1.050V
	// {.frequency = 1416 * DVFS_KHZ,.index    = 1100 * DVFS_MV},//logic 1.100V
	// {.frequency = 1512 * DVFS_KHZ,.index    = 1125 * DVFS_MV},//logic 1.125V
	// {.frequency = 1608 * DVFS_KHZ,.index    = 1175 * DVFS_MV},//logic 1.175V
	{.frequency	= CPUFREQ_TABLE_END},
};

static struct vd_node vd_cpu = {
	.name 		= "vd_cpu",
	.regulator_name	= "vdd_cpu",
	.volt_set_flag	= DVFS_SET_VOLT_FAILURE,
	.vd_dvfs_target	= dvfs_target_cpu,
};

static struct vd_node vd_core = {
	.name 		= "vd_core",
	.regulator_name = "vdd_core",
	.volt_set_flag	= DVFS_SET_VOLT_FAILURE,
	.vd_dvfs_target	= dvfs_target_core,
};

static struct vd_node vd_rtc = {
	.name 		= "vd_rtc",
	.regulator_name	= "vdd_rtc",
	.volt_set_flag	= DVFS_SET_VOLT_FAILURE,
	.vd_dvfs_target	= NULL,
};

static struct vd_node *rk30_vds[] = {&vd_cpu, &vd_core, &vd_rtc};

static struct pd_node pd_a9_0 = {
	.name 			= "pd_a9_0",
	.vd			= &vd_cpu,
};
static struct pd_node pd_a9_1 = {
	.name 			= "pd_a9_1",
	.vd			= &vd_cpu,
};
static struct pd_node pd_debug = {
	.name 			= "pd_debug",
	.vd			= &vd_cpu,
};
static struct pd_node pd_scu = {
	.name 			= "pd_scu",
	.vd			= &vd_cpu,
};
static struct pd_node pd_video = {
	.name 			= "pd_video",
	.vd			= &vd_core,
};
static struct pd_node pd_vio = {
	.name 			= "pd_vio",
	.vd			= &vd_core,
};
static struct pd_node pd_gpu = {
	.name 			= "pd_gpu",
	.vd			= &vd_core,
};
static struct pd_node pd_peri = {
	.name 			= "pd_peri",
	.vd			= &vd_core,
};
static struct pd_node pd_cpu = {
	.name 			= "pd_cpu",
	.vd			= &vd_core,
};
static struct pd_node pd_alive = {
	.name 			= "pd_alive",
	.vd			= &vd_core,
};
static struct pd_node pd_rtc = {
	.name 			= "pd_rtc",
	.vd			= &vd_rtc,
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

static struct pds_list aclk_periph_pds[] = {
	CLK_PDS(&pd_peri),
	CLK_PDS(NULL),
};

static struct pds_list aclk_vepu_pds[] = {
	CLK_PDS(&pd_video),
	CLK_PDS(NULL),
};

#define RK_CLKS(_clk_name, _ppds, _dvfs_table, _dvfs_nb) \
{ \
	.name	= _clk_name, \
	.pds = _ppds,\
	.dvfs_table = _dvfs_table,	\
	.dvfs_nb	= _dvfs_nb,	\
}

static struct clk_node rk30_clks[] = {
	RK_CLKS("cpu", cpu_pds, cpu_dvfs_table, &rk_dvfs_clk_notifier),
	RK_CLKS("ddr", ddr_pds, ddr_dvfs_table, &rk_dvfs_clk_notifier),
	RK_CLKS("gpu", gpu_pds, gpu_dvfs_table, &rk_dvfs_clk_notifier),
	RK_CLKS("aclk_vepu", aclk_vepu_pds, vpu_dvfs_table, &rk_dvfs_clk_notifier),
	//RK_CLKS("aclk_periph", aclk_periph_pds, peri_aclk_dvfs_table, &rk_dvfs_clk_notifier),
};

#define RK_DEPPENDS(_clk_name, _pvd, _dep_table) \
{ \
	.clk_name	= _clk_name, \
	.dep_vd 	= _pvd,\
	.dep_table 	= _dep_table,	\
}

static struct depend_lookup rk30_depends[] = {
	RK_DEPPENDS("cpu", &vd_core, dep_cpu2core_table),
	//RK_DEPPENDS("gpu", &vd_cpu, NULL),
	//RK_DEPPENDS("gpu", &vd_cpu, NULL),
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
	for (i = 0; i < ARRAY_SIZE(rk30_depends); i++) {
		rk_regist_depends(&rk30_depends[i]);
	}
	dvfs_clk_cpu = dvfs_get_dvfs_clk_byname("cpu");
	return 0;
}

/*******************************AVS AREA****************************************/
/*
 * To use AVS function, you must call avs_init in machine_rk30_board_init(void)(board-rk30-sdk.c)
 * And then call(vdd_log):
 *	regulator_set_voltage(dcdc, 1100000, 1100000);
 *	avs_init_val_get(1,1100000,"wm8326 init");
 *	udelay(600);
 *	avs_set_scal_val(AVS_BASE);
 * in wm831x_post_init(board-rk30-sdk-wm8326.c)
 * AVS_BASE can use 172
 */

static int avs_scale_volt = 0;
static int avs_get_scal_val(int vol);

int dvfs_avs_scale_table(struct clk *clk, char *depend_vd_name)
{
	/* if depend_vd_name == NULL scale clk table
	 * else scale clk's depend table, named depend_vd_name
	 * */
	struct vd_node		*vd;
	struct depend_list	*depend;
	struct clk_node *info = clk_get_dvfs_info(clk);
	struct cpufreq_frequency_table	*table = NULL;
	int i;

	if (NULL == depend_vd_name) {
		table = info->dvfs_table;
	} else {
		list_for_each_entry(vd, &rk_dvfs_tree, node) {
			if (0 == strcmp(vd->name, depend_vd_name)) {
				DVFS_DBG("%s FOUND A MATCH vd\n", __func__);
				mutex_lock(&mutex);
				list_for_each_entry(depend, &info->depend_list, node2clk) {
					if (vd == depend->dep_vd && info == depend->dvfs_clk) {
						DVFS_DBG("%s FOUND A MATCH table\n", __func__);
						table = depend->dep_table;
						break;
					}
				}
				mutex_unlock(&mutex);
			}
		}
	}

	if (table == NULL) {
		DVFS_ERR("%s can not find match table\n", __func__);
		return -1;
	}
	if (avs_scale_volt != 0) {
		DVFS_DBG("AVS scale %s, depend name = %s, voltage = %d\n",
				info->name, depend_vd_name, avs_scale_volt);
		for (i = 0; table[i].frequency != CPUFREQ_TABLE_END; i++) {
			table[i].index = avs_get_scal_val(table[i].index);
		}
	}
	return 0;
}

static void __iomem *rk30_nandc_base;

#define nandc_readl(offset)	readl_relaxed(rk30_nandc_base + offset)
#define nandc_writel(v, offset) do { writel_relaxed(v, rk30_nandc_base + offset); dsb(); } while (0)
static u8 rk30_get_avs_val(void)
{
	u32 nanc_save_reg[4];
	unsigned long flags;
	u32 paramet = 0;
	u32 count = 100;
	preempt_disable();
	local_irq_save(flags);

	nanc_save_reg[0] = nandc_readl(0);
	nanc_save_reg[1] = nandc_readl(0x130);
	nanc_save_reg[2] = nandc_readl(0x134);
	nanc_save_reg[3] = nandc_readl(0x158);

	nandc_writel(nanc_save_reg[0] | 0x1 << 14, 0);
	nandc_writel(0x5, 0x130);

	nandc_writel(7, 0x158);
	nandc_writel(1, 0x134);

	while(count--) {
		paramet = nandc_readl(0x138);
		if((paramet & 0x1))
			break;
		udelay(1);
	};
	paramet = (paramet >> 1) & 0xff;
	nandc_writel(nanc_save_reg[0], 0);
	nandc_writel(nanc_save_reg[1], 0x130);
	nandc_writel(nanc_save_reg[2], 0x134);
	nandc_writel(nanc_save_reg[3], 0x158);

	local_irq_restore(flags);
	preempt_enable();
	return (u8)paramet;

}
#define init_avs_times 10
#define init_avs_st_num 5

struct init_avs_st {
	int is_set;
	u8 paramet[init_avs_times];
	int vol;
	char *s;
};

static struct init_avs_st init_avs_paramet[init_avs_st_num];

void avs_init_val_get(int index, int vol, char *s)
{
	int i;
	if(index >= init_avs_times)
		return;
	init_avs_paramet[index].vol = vol;
	init_avs_paramet[index].s = s;
	init_avs_paramet[index].is_set++;
	for(i = 0; i < init_avs_times; i++) {
		init_avs_paramet[index].paramet[i] = rk30_get_avs_val();
		mdelay(1);
	}
}

void avs_init(void)
{
	memset(&init_avs_paramet[0].is_set, 0, sizeof(init_avs_paramet));
	rk30_nandc_base = ioremap(RK2928_NANDC_PHYS, RK2928_NANDC_SIZE);
	//avs_init_val_get(0,1150000,"board_init");
}

#define VOL_DYN_STEP (12500)  //mv
#define AVS_VAL_PER_STEP (4)  //mv

static u8 avs_init_get_min_val(void)
{
	int i, j;
	u8 min_avs = 0xff;
	for(i = 0; i < init_avs_st_num; i++) {
		if(init_avs_paramet[i].is_set && init_avs_paramet[i].vol == (1100 * 1000)) {
			for(j = 0; j < init_avs_times; j++)
				min_avs = (u8)min(min_avs, init_avs_paramet[i].paramet[j]);
		}

	}
	return min_avs;
}

static int avs_get_scal_val(int vol)
{
	vol += avs_scale_volt;
	if(vol < 1000 * 1000)
		vol = 1000 * 1000;
	if(vol > 1400 * 1000)
		vol = 1400 * 1000;
	return vol;
}
#if 0
u8 avs_test_date[] = {172, 175, 176, 179, 171, 168, 165, 162, 199, 0};
u8 avs_test_date_cunt = 0;
#endif
int avs_set_scal_val(u8 avs_base)
{
	u8 avs_test = avs_init_get_min_val();
	s8 step = 0;

	if (avs_base < avs_test) {
		DVFS_DBG("AVS down voltage, ignore\n");
		return 0;
	}
	step = (avs_base - avs_test) / AVS_VAL_PER_STEP;
	step = (avs_base > avs_test) ? (step + 1) : step;
	if (step > 2)
		step += 1;
	avs_scale_volt = (step) * (VOL_DYN_STEP);

	DVFS_DBG("avs_set_scal_val test=%d,base=%d,step=%d,scale_vol=%d\n",
			avs_test, avs_base, step, avs_scale_volt);
	return 0;
}

/*************************interface to get avs value and dvfs tree*************************/
#define USE_NORMAL_TIME
#ifdef USE_NORMAL_TIME
static struct timer_list avs_timer;
#else
static struct hrtimer dvfs_hrtimer;
#endif

static u32 avs_dyn_start = 0;
static u32 avs_dyn_data_cnt;
static u8 *avs_dyn_data = NULL;
static u32 show_line_cnt = 0;
static u8 dly_min;
static u8 dly_max;

#define val_per_line (30)
#define line_pre_show (30)
#define avs_dyn_data_num (3*1000*1000)

static u32 print_avs_init(char *buf)
{
	char *s = buf;
	int i, j;

	for(j = 0; j < init_avs_st_num; j++) {
		if(init_avs_paramet[j].vol <= 0)
			continue;
		s += sprintf(s, "%s ,vol=%d,paramet following\n", 
				init_avs_paramet[j].s, init_avs_paramet[j].vol);
		for(i = 0; i < init_avs_times; i++) {
			s += sprintf(s, "%d ", init_avs_paramet[j].paramet[i]);
		}

		s += sprintf(s, "\n");
	}
	return (s - buf);
}

static ssize_t avs_init_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	return print_avs_init(buf);
}

static ssize_t avs_init_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n)
{

	return n;
}
static ssize_t avs_now_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	return sprintf(buf, "%d\n", rk30_get_avs_val());
}

static ssize_t avs_now_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n)
{
	return n;
}
static ssize_t avs_dyn_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	char *s = buf;
	u32 i;

	if(avs_dyn_data==NULL)
		return (s - buf);
		
	if(avs_dyn_start) {
		int start_cnt;
		int end_cnt;
		end_cnt = (avs_dyn_data_cnt ? (avs_dyn_data_cnt - 1) : 0);
		if(end_cnt > (line_pre_show * val_per_line))
			start_cnt = end_cnt - (line_pre_show * val_per_line);
		else
			start_cnt = 0;

		dly_min = avs_dyn_data[start_cnt];
		dly_max = avs_dyn_data[start_cnt];

		//s += sprintf(s,"data start=%d\n",i);
		for(i = start_cnt; i <= end_cnt;) {
			s += sprintf(s, "%d", avs_dyn_data[i]);
			dly_min = min(dly_min, avs_dyn_data[i]);
			dly_max = max(dly_max, avs_dyn_data[i]);
			i++;
			if(!(i % val_per_line)) {
				s += sprintf(s, "\n");
			} else
				s += sprintf(s, " ");
		}

		s += sprintf(s, "\n");

		s += sprintf(s, "new data is from=%d to %d\n", start_cnt, end_cnt);
		//s += sprintf(s,"\n max=%d,min=%d,totolcnt=%d,line=%d\n",dly_max,dly_min,avs_dyn_data_cnt,show_line_cnt);


	} else {
		if(show_line_cnt == 0) {
			dly_min = avs_dyn_data[0];
			dly_max = avs_dyn_data[0];
		}


		for(i = show_line_cnt * (line_pre_show * val_per_line); i < avs_dyn_data_cnt;) {
			s += sprintf(s, "%d", avs_dyn_data[i]);
			dly_min = min(dly_min, avs_dyn_data[i]);
			dly_max = max(dly_max, avs_dyn_data[i]);
			i++;
			if(!(i % val_per_line)) {
				s += sprintf(s, "\n");
			} else
				s += sprintf(s, " ");
			if(i >= ((show_line_cnt + 1)*line_pre_show * val_per_line))
				break;
		}

		s += sprintf(s, "\n");

		s += sprintf(s, "max=%d,min=%d,totolcnt=%d,line=%d\n", 
				dly_max, dly_min, avs_dyn_data_cnt, show_line_cnt);
		show_line_cnt++;
		if(((show_line_cnt * line_pre_show)*val_per_line) >= avs_dyn_data_cnt) {

			show_line_cnt = 0;

			s += sprintf(s, "data is over\n");
		}
	}
	return (s - buf);
}

static ssize_t avs_dyn_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n)
{
	const char *pbuf;

	if((strncmp(buf, "start", strlen("start")) == 0)) {
		if(avs_dyn_data==NULL)	
			avs_dyn_data = kmalloc(avs_dyn_data_num, GFP_KERNEL);
		if(avs_dyn_data==NULL)
			return n;
		
		pbuf = &buf[strlen("start")];
		avs_dyn_data_cnt = 0;
		show_line_cnt = 0;
		if(avs_dyn_data) {
#ifdef USE_NORMAL_TIME
			mod_timer(&avs_timer, jiffies + msecs_to_jiffies(5));
#else
			hrtimer_start(&dvfs_hrtimer, ktime_set(0, 5 * 1000 * 1000), HRTIMER_MODE_REL);
#endif
			avs_dyn_start = 1;
		}
		//sscanf(pbuf, "%d %d", &number, &voltage);
		//DVFS_DBG("---------ldo %d %d\n", number, voltage);

	} else if((strncmp(buf, "stop", strlen("stop")) == 0)) {
		pbuf = &buf[strlen("stop")];
		avs_dyn_start = 0;
		show_line_cnt = 0;
		//sscanf(pbuf, "%d %d", &number, &voltage);
		//DVFS_DBG("---------dcdc %d %d\n", number, voltage);
	}



	return n;
}

static ssize_t dvfs_tree_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n)
{
	return n;
}
static ssize_t dvfs_tree_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	return dump_dbg_map(buf);

}

static void avs_timer_fn(unsigned long data)
{
	int i;
	for(i = 0; i < 1; i++) {
		if(avs_dyn_data_cnt >= avs_dyn_data_num)
			return;
		avs_dyn_data[avs_dyn_data_cnt] = rk30_get_avs_val();
		avs_dyn_data_cnt++;
	}
	if(avs_dyn_start)
		mod_timer(&avs_timer, jiffies + msecs_to_jiffies(10));
}
#if 0
struct hrtimer dvfs_hrtimer;
static enum hrtimer_restart dvfs_hrtimer_timer_func(struct hrtimer *timer)
{
	int i;
	for(i = 0; i < 1; i++) {
		if(avs_dyn_data_cnt >= avs_dyn_data_num)
			return HRTIMER_NORESTART;
		avs_dyn_data[avs_dyn_data_cnt] = rk30_get_avs_val();
		avs_dyn_data_cnt++;
	}
	if(avs_dyn_start)
		hrtimer_start(timer, ktime_set(0, 1 * 1000 * 1000), HRTIMER_MODE_REL);

}
#endif
/*********************************************************************************/
static struct kobject *dvfs_kobj;
struct dvfs_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf);
	ssize_t (*store)(struct kobject *kobj, struct kobj_attribute *attr,
			const char *buf, size_t n);
};

static struct dvfs_attribute dvfs_attrs[] = {
	/*     node_name	permision		show_func	store_func */
#ifdef CONFIG_RK_CLOCK_PROC
	__ATTR(dvfs_tree,	S_IRUGO | S_IWUSR,	dvfs_tree_show,	dvfs_tree_store),
	//__ATTR(avs_init,	S_IRUGO | S_IWUSR,	avs_init_show,	avs_init_store),
	//__ATTR(avs_dyn,		S_IRUGO | S_IWUSR,	avs_dyn_show,	avs_dyn_store),
	//__ATTR(avs_now,		S_IRUGO | S_IWUSR,	avs_now_show,	avs_now_store),
#endif
};

static int __init dvfs_init(void)
{
	int i, ret = 0;
#ifdef USE_NORMAL_TIME
	init_timer(&avs_timer);
	//avs_timer.expires = jiffies+msecs_to_jiffies(1);
	avs_timer.function = avs_timer_fn;
	//mod_timer(&avs_timer,jiffies+msecs_to_jiffies(1));
#else
	hrtimer_init(&dvfs_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	dvfs_hrtimer.function = dvfs_hrtimer_timer_func;
	//hrtimer_start(&dvfs_hrtimer,ktime_set(0, 5*1000*1000),HRTIMER_MODE_REL);
#endif


	dvfs_kobj = kobject_create_and_add("dvfs", NULL);
	if (!dvfs_kobj)
		return -ENOMEM;
	for (i = 0; i < ARRAY_SIZE(dvfs_attrs); i++) {
		ret = sysfs_create_file(dvfs_kobj, &dvfs_attrs[i].attr);
		if (ret != 0) {
			DVFS_ERR("create index %d error\n", i);
			return ret;
		}
	}

	return ret;
}
subsys_initcall(dvfs_init);

/**
 * dump_dbg_map() : Draw all informations of dvfs while debug
 */
static int dump_dbg_map(char *buf)
{
	int i;
	struct vd_node	*vd;
	struct pd_node	*pd, *clkparent;
	struct clk_list	*child;
	struct clk_node	*dvfs_clk;
	struct depend_list *depend;
	char* s = buf;
	
	s += sprintf(s, "-------------DVFS TREE-----------\n\n\n");
	s += sprintf(s, "RK30 DVFS TREE:\n");
	list_for_each_entry(vd, &rk_dvfs_tree, node) {
		s += sprintf(s, "|\n|- voltage domain:%s\n", vd->name);
		s += sprintf(s, "|- current voltage:%d\n", vd->cur_volt);
		list_for_each_entry(depend, &vd->req_volt_list, node2vd) {
			s += sprintf(s, "|- request voltage:%d, clk:%s\n", depend->req_volt, depend->dvfs_clk->name);
		}

		list_for_each_entry(pd, &vd->pd_list, node) {
			s += sprintf(s, "|  |\n|  |- power domain:%s, status = %s, current volt = %d\n",
					pd->name, (pd->pd_status == PD_ON) ? "ON" : "OFF", pd->cur_volt);

			list_for_each_entry(child, &pd->clk_list, node) {
				dvfs_clk = child->dvfs_clk;
				s += sprintf(s, "|  |  |\n|  |  |- clock: %s current: rate %d, volt = %d, enable_dvfs = %s\n",
						dvfs_clk->name, dvfs_clk->set_freq, dvfs_clk->set_volt, 
						dvfs_clk->enable_dvfs == 0 ? "DISABLE" : "ENABLE");
				for (i = 0; dvfs_clk->pds[i].pd != NULL; i++) {
					clkparent = dvfs_clk->pds[i].pd;
					s += sprintf(s, "|  |  |  |- clock parents: %s, vd_parent = %s\n", 
							clkparent->name, clkparent->vd->name);
				}

				for (i = 0; (dvfs_clk->dvfs_table[i].frequency != CPUFREQ_TABLE_END); i++) {
					s += sprintf(s, "|  |  |  |- freq = %d, volt = %d\n", 
							dvfs_clk->dvfs_table[i].frequency, 
							dvfs_clk->dvfs_table[i].index);

				}

				list_for_each_entry(depend, &dvfs_clk->depend_list, node2clk) {
					s += sprintf(s, "|  |  |  |  |- DEPEND VD: %s\n", depend->dep_vd->name); 
					for (i = 0; (depend->dep_table[i].frequency != CPUFREQ_TABLE_END); i++) {
						s += sprintf(s, "|  |  |  |  |- freq = %d, req_volt = %d\n", 
								depend->dep_table[i].frequency, 

								depend->dep_table[i].index);
					}
				}
			}
		}
	}
	s += sprintf(s, "-------------DVFS TREE END------------\n");
	return s - buf;
}


