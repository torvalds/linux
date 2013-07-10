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

#define MHz	(1000 * 1000)
static LIST_HEAD(rk_dvfs_tree);
static DEFINE_MUTEX(mutex);
static DEFINE_MUTEX(rk_dvfs_mutex);

static int dump_dbg_map(char *buf);

#define PD_ON	1
#define PD_OFF	0
#define DVFS_STR_DISABLE(on) ((on)?"enable":"disable")

#define get_volt_up_delay(new_volt, old_volt)	\
	((new_volt) > (old_volt) ? (((new_volt) - (old_volt)) >> 9) : 0)



/**************************************vd regulator functions***************************************/
static void dvfs_volt_up_delay(struct vd_node *vd,int new_volt, int old_volt)
{
	int u_time;
	if(new_volt<=old_volt)
		return;
	if(vd->volt_time_flag>0)	
		u_time=regulator_set_voltage_time(vd->regulator,old_volt,new_volt);
	else
		u_time=-1;		
	if(u_time<0)// regulator is not suported time,useing default time
	{
		DVFS_DBG("%s:vd %s is not suported getting delay time,so we use default\n",
				__FUNCTION__,vd->name);
		u_time=((new_volt) - (old_volt)) >> 9;
	}
	DVFS_DBG("%s:vd %s volt %d to %d delay %d us\n",__FUNCTION__,vd->name,
		old_volt,new_volt,u_time);
	if (u_time >= 1000) {
		mdelay(u_time / 1000);
		udelay(u_time % 1000);
		DVFS_ERR("regulator set vol delay is larger 1ms,old is %d,new is %d\n",old_volt,new_volt);
	} else if (u_time) {
		udelay(u_time);
	}			
}
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
// for clk enable case to get vd regulator info
void clk_enable_dvfs_regulator_check(struct vd_node *vd)
{
	vd->cur_volt = dvfs_regulator_get_voltage(vd->regulator);
	if(vd->cur_volt<=0)
	{
		vd->volt_set_flag = DVFS_SET_VOLT_FAILURE;
	}
	vd->volt_set_flag = DVFS_SET_VOLT_SUCCESS;
}

static void dvfs_get_vd_regulator_volt_list(struct vd_node *vd)
{
	unsigned i,selector=dvfs_regulator_count_voltages(vd->regulator);
	int sel_volt=0;
	
	if(selector>VD_VOL_LIST_CNT)
		selector=VD_VOL_LIST_CNT;
	
	mutex_lock(&mutex);
	for (i = 0; i<selector; i++) {
		sel_volt=dvfs_regulator_list_voltage(vd->regulator,i);
		if(sel_volt<=0)
		{	
			DVFS_WARNING("%s : selector=%u,but volt <=0\n",vd->name,i);
			break;
		}
		vd->volt_list[i]=sel_volt;	
		DVFS_DBG("%s:selector=%u,volt %d\n",vd->name,i,sel_volt);
	}
	vd->n_voltages=selector;
	mutex_unlock(&mutex);
}

// >= volt
static int vd_regulator_round_volt_max(struct vd_node *vd, int volt)
{
	int sel_volt;
	unsigned i;
	
	for (i = 0; i<vd->n_voltages; i++) {
		sel_volt=vd->volt_list[i];
		if(sel_volt<=0)
		{	
			DVFS_WARNING("%s:list_volt : selector=%u,but volt <=0\n",__FUNCTION__,i);
			return -1;
		}
		if(sel_volt>=volt)
		 return sel_volt;	
	}
	return -1;
}
// >=volt
static int vd_regulator_round_volt_min(struct vd_node *vd, int volt)
{
	int sel_volt;
	unsigned i;
	
	for (i = 0; i<vd->n_voltages; i++) {
		sel_volt=vd->volt_list[i];
		if(sel_volt<=0)
		{	
			DVFS_WARNING("%s:list_volt : selector=%u,but volt <=0\n",__FUNCTION__,i);
			return -1;
		}
		if(sel_volt>volt)
		{
			if(i>0)
				return vd->volt_list[i-1];
			else
				return -1;
		}	
	}
	return -1;
}

// >=volt
int vd_regulator_round_volt(struct vd_node *vd, int volt,int flags)
{
	if(!vd->n_voltages)
		return -1;
	if(flags==VD_LIST_RELATION_L)
		return vd_regulator_round_volt_min(vd,volt);
	else
		return vd_regulator_round_volt_max(vd,volt);	
}
EXPORT_SYMBOL(vd_regulator_round_volt);


static void dvfs_table_round_volt(struct clk_node  *dvfs_clk)
{
	int i,test_volt;

	if(!dvfs_clk->dvfs_table||!dvfs_clk->vd||IS_ERR_OR_NULL(dvfs_clk->vd->regulator))
		return;
	mutex_lock(&mutex);
	for (i = 0; (dvfs_clk->dvfs_table[i].frequency != CPUFREQ_TABLE_END); i++) {

		test_volt=vd_regulator_round_volt(dvfs_clk->vd,dvfs_clk->dvfs_table[i].index,VD_LIST_RELATION_H);
		if(test_volt<=0)
		{	
			DVFS_WARNING("clk %s:round_volt : is %d,but list <=0\n",dvfs_clk->name,dvfs_clk->dvfs_table[i].index);
			break;
		}
		DVFS_DBG("clk %s:round_volt %d to %d\n",dvfs_clk->name,dvfs_clk->dvfs_table[i].index,test_volt);
		dvfs_clk->dvfs_table[i].index=test_volt;		
	}
	mutex_unlock(&mutex);
}
void dvfs_vd_get_regulator_volt_time_info(struct vd_node *vd)
{
	if(vd->volt_time_flag<=0)// check regulator support get uping vol timer
	{
		vd->volt_time_flag=dvfs_regulator_set_voltage_time(vd->regulator,vd->cur_volt,vd->cur_volt+200*1000);
		if(vd->volt_time_flag<0)
		{
			DVFS_DBG("%s,vd %s volt_time is no support\n",__FUNCTION__,vd->name);
		}
		else
		{
			DVFS_DBG("%s,vd %s volt_time is support,up 200mv need delay %d us\n",__FUNCTION__,vd->name,vd->volt_time_flag);

		}	
	}
}

void dvfs_vd_get_regulator_mode_info(struct vd_node *vd)
{
	//REGULATOR_MODE_FAST
	if(vd->mode_flag<=0)// check regulator support get uping vol timer
	{
		vd->mode_flag=dvfs_regulator_get_mode(vd->regulator);
		if(vd->mode_flag==REGULATOR_MODE_FAST||vd->mode_flag==REGULATOR_MODE_NORMAL
			||vd->mode_flag==REGULATOR_MODE_IDLE||vd->mode_flag==REGULATOR_MODE_STANDBY)
		{
			if(dvfs_regulator_set_mode(vd->regulator,vd->mode_flag)<0)
			{
				vd->mode_flag=0;// check again
			}
			
		}
		if(vd->mode_flag>0)
		{
			DVFS_DBG("%s,vd %s mode(now is %d) support\n",__FUNCTION__,vd->name,vd->mode_flag);
		}
		else
		{
			DVFS_DBG("%s,vd %s mode is not support now check\n",__FUNCTION__,vd->name);

		}
		
	}
}
struct regulator *dvfs_get_regulator(char *regulator_name) 
{
	struct vd_node *vd;
	list_for_each_entry(vd, &rk_dvfs_tree, node) {
		if (strcmp(regulator_name, vd->regulator_name) == 0) {
			return vd->regulator;
		}
	}
	return NULL;
}

/**************************************dvfs clocks functions***************************************/
int dvfs_clk_enable_limit(struct clk *clk, unsigned int min_rate, unsigned max_rate)
{
	struct clk_node *dvfs_clk;
	u32 rate = 0, ret = 0;
	dvfs_clk = clk_get_dvfs_info(clk);
	if (IS_ERR_OR_NULL(dvfs_clk)) {
		DVFS_ERR("%s: can not get dvfs clk(%s)\n", __func__, clk->name);
		return -1;

	}

	if (dvfs_clk->vd && dvfs_clk->vd->vd_dvfs_target){
		mutex_lock(&rk_dvfs_mutex);

		dvfs_clk->freq_limit_en = 1;
		dvfs_clk->min_rate = min_rate;
		dvfs_clk->max_rate = max_rate;
		rate = clk_get_rate(clk);
		ret = dvfs_clk->vd->vd_dvfs_target(clk, rate);
		clk->last_set_rate = rate;

		mutex_unlock(&rk_dvfs_mutex);
	}

	DVFS_DBG("%s: clk(%s) last_set_rate=%d; [min_rate, max_rate]=[%d, %d]\n",
			__func__, clk->name, clk->last_set_rate, dvfs_clk->min_rate, dvfs_clk->max_rate);
	return 0;
}

int dvfs_clk_disable_limit(struct clk *clk)
{
	struct clk_node *dvfs_clk;
	u32 ret = 0;
	dvfs_clk = clk_get_dvfs_info(clk);
	if (IS_ERR_OR_NULL(dvfs_clk)) {
		DVFS_ERR("%s: can not get dvfs clk(%s)\n", __func__, clk->name);
		return -1;

	}

	if (dvfs_clk->vd && dvfs_clk->vd->vd_dvfs_target){
		mutex_lock(&rk_dvfs_mutex);
		/* To reset dvfs_clk->min_rate/max_rate */
		dvfs_set_freq_volt_table(clk, dvfs_clk->dvfs_table);

		dvfs_clk->freq_limit_en = 0;
		ret = dvfs_clk->vd->vd_dvfs_target(clk, clk->last_set_rate);

		mutex_unlock(&rk_dvfs_mutex);
	}

	DVFS_DBG("%s: clk(%s) last_set_rate=%d; [min_rate, max_rate]=[%d, %d]\n",
			__func__, clk->name, clk->last_set_rate, dvfs_clk->min_rate, dvfs_clk->max_rate);
	return 0;
}

int dvfs_vd_clk_set_rate(struct clk *clk, unsigned long rate)
{
	int ret = -1;
	struct clk_node *dvfs_info=clk_get_dvfs_info(clk);
	
	DVFS_DBG("%s(%s(%lu))\n", __func__, dvfs_info->name, rate);

	#if 0 // judge by reference func in rk
	if (dvfs_support_clk_set_rate(dvfs_info)==false) {
		DVFS_ERR("dvfs func:%s is not support!\n", __func__);
		return ret;
	}
	#endif
	
	if(dvfs_info->vd&&dvfs_info->vd->vd_dvfs_target){
		// mutex_lock(&vd->dvfs_mutex);
		mutex_lock(&rk_dvfs_mutex);
		ret = dvfs_info->vd->vd_dvfs_target(clk, rate);
		mutex_unlock(&rk_dvfs_mutex);
		// mutex_unlock(&vd->dvfs_mutex);
	}
	else
	{
		DVFS_WARNING("%s(%s),vd is no target callback\n", __func__, clk->name);	
		return -1;
	}
	DVFS_DBG("%s(%s(%lu)),is end\n", __func__, clk->name, rate);
	return ret;
}
EXPORT_SYMBOL(dvfs_vd_clk_set_rate);

int dvfs_vd_clk_disable(struct clk *clk, int on)
{
	int ret = -1;
	struct clk_node *dvfs_info=clk_get_dvfs_info(clk);	
	DVFS_DBG("%s(%s(%s,%lu))\n", __func__, dvfs_info->name, DVFS_STR_DISABLE(on),clk_get_rate(clk));


	#if 0 // judge by reference func in rk
	if (dvfs_support_clk_disable(dvfs_info)==false) {
		DVFS_ERR("dvfs func:%s is not support!\n", __func__);
		return ret;
	}
	#endif
	
	if(dvfs_info->vd&&dvfs_info->vd->vd_clk_disable_target){
		// mutex_lock(&vd->dvfs_mutex);
		mutex_lock(&rk_dvfs_mutex);
		ret = dvfs_info->vd->vd_clk_disable_target(clk, on);
		mutex_unlock(&rk_dvfs_mutex);
		// mutex_unlock(&vd->dvfs_mutex);
	}
	else
	{
		DVFS_WARNING("%s(%s),vd is no target callback\n", __func__, clk->name);	
		return -1;
	}
	DVFS_DBG("%s(%s(%s)),is end\n", __func__, dvfs_info->name, DVFS_STR_DISABLE(on));

	return ret;
}

EXPORT_SYMBOL(dvfs_vd_clk_disable);

static void dvfs_table_round_clk_rate(struct clk_node  *dvfs_clk)
{
	int i;
	unsigned long temp_rate;
	int rate;
	int flags;
	
	if(!dvfs_clk->dvfs_table||dvfs_clk->clk==NULL||is_suport_round_rate(dvfs_clk->clk)<0)
		return;
	
	mutex_lock(&mutex);
	for (i = 0; (dvfs_clk->dvfs_table[i].frequency != CPUFREQ_TABLE_END); i++) {
		//ddr rate = real rate+flags
		flags=dvfs_clk->dvfs_table[i].frequency%1000;
		rate=(dvfs_clk->dvfs_table[i].frequency/1000)*1000;
		temp_rate=clk_round_rate(dvfs_clk->clk,rate*1000);
		if(temp_rate<=0)
		{	
			DVFS_WARNING("clk %s:round_clk_rate : is %d,but round <=0",dvfs_clk->name,dvfs_clk->dvfs_table[i].frequency);
			break;
		}
		
		/* Set rate unit as MHZ */
		if (temp_rate % MHz != 0)
			temp_rate = (temp_rate / MHz + 1) * MHz;

		temp_rate = (temp_rate / 1000) + flags;
		
		DVFS_DBG("clk %s round_clk_rate %d to %d\n",
			dvfs_clk->name,dvfs_clk->dvfs_table[i].frequency,(int)(temp_rate));
		
		dvfs_clk->dvfs_table[i].frequency=temp_rate;		
	}
	mutex_unlock(&mutex);
}

/***************************************************************************************************/
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
int dvfs_clk_get_ref_volt(struct clk_node *dvfs_clk, int rate_khz,
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

int dvfs_vd_get_newvolt_bypd(struct vd_node *vd)
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

int dvfs_vd_get_newvolt_byclk(struct clk_node *dvfs_clk)
{
	if (!dvfs_clk)
		return -1;
	dvfs_update_clk_pds_volt(dvfs_clk);
	return  dvfs_vd_get_newvolt_bypd(dvfs_clk->vd);
}

void dvfs_clk_register_set_rate_callback(struct clk *clk, clk_dvfs_target_callback clk_dvfs_target)
{
	struct clk_node *dvfs_clk = clk_get_dvfs_info(clk);
	if (IS_ERR_OR_NULL(dvfs_clk)) {
		DVFS_ERR("%s %s get dvfs_clk err\n", __func__, clk->name);
		return ;
	}
	dvfs_clk->clk_dvfs_target = clk_dvfs_target;
}

/************************************************ freq volt table************************************/
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
EXPORT_SYMBOL(dvfs_get_freq_volt_table);

int dvfs_set_freq_volt_table(struct clk *clk, struct cpufreq_frequency_table *table)
{
	struct clk_node *info = clk_get_dvfs_info(clk);
	int i=0;

	if (!info)
		return -1;	
	if (!table)
	{		
		info->min_rate=0;	
		info->max_rate=0;	
		return -1;
	}

	mutex_lock(&mutex);
	info->dvfs_table = table;
	if(table[0].frequency!= CPUFREQ_TABLE_END)
	{

		info->min_rate=(table[0].frequency/1000)*1000*1000;//to hz
	}
	else
	{
		info->min_rate=0;	
		info->max_rate=0;	
		mutex_unlock(&mutex);
		return -1;
	}

	for(i=0;table[i].frequency!= CPUFREQ_TABLE_END;i++)
	{
	
	}	
	info->max_rate=(table[i-1].frequency/1000)*1000*1000;

	DVFS_DBG("%s,clk %s,limit max=%lu,min=%lu\n",__FUNCTION__,info->name,info->max_rate,info->min_rate);

	mutex_unlock(&mutex);
	dvfs_table_round_clk_rate(info);
	dvfs_table_round_volt(info);
	return 0;
}
EXPORT_SYMBOL(dvfs_set_freq_volt_table);

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
				clk_enable_dvfs_regulator_check(dvfs_clk->vd);
				dvfs_get_vd_regulator_volt_list(dvfs_clk->vd);
				dvfs_vd_get_regulator_volt_time_info(dvfs_clk->vd);
				//dvfs_vd_get_regulator_mode_info(dvfs_clk->vd);
			} else {
				//dvfs_clk->vd->regulator = NULL;
				dvfs_clk->enable_dvfs = 0;
				DVFS_ERR("%s can't get regulator in %s\n", dvfs_clk->name, __func__);
				return -1;
			}
		} else {
			     clk_enable_dvfs_regulator_check(dvfs_clk->vd);
			// DVFS_DBG("%s(%s) vd volt=%u\n",__func__,dvfs_clk->name,dvfs_clk->vd->cur_volt);
		}

		dvfs_table_round_clk_rate(dvfs_clk);
		dvfs_table_round_volt(dvfs_clk);
		dvfs_clk->set_freq = dvfs_clk_get_rate_kz(clk);
		// DVFS_DBG("%s ,%s get freq%u!\n",__func__,dvfs_clk->name,dvfs_clk->set_freq);

		if (dvfs_clk_get_ref_volt(dvfs_clk, dvfs_clk->set_freq, &clk_fv)) {
			if (dvfs_clk->dvfs_table[0].frequency == CPUFREQ_TABLE_END) {
				DVFS_ERR("%s table empty\n", __func__);
				dvfs_clk->enable_dvfs = 0;
				return -1;
			} else {
				DVFS_WARNING("%s table all value are smaller than default, use default, just enable dvfs\n", __func__);
				dvfs_clk->enable_dvfs++;
				return 0;
			}
		}

		dvfs_clk->set_volt = clk_fv.index;
		dvfs_vd_get_newvolt_byclk(dvfs_clk);
		// DVFS_DBG("%s,%s,freq%u(ref vol %u)\n",__func__,dvfs_clk->name,
		//	 dvfs_clk->set_freq,dvfs_clk->set_volt);
#if 0
		if (dvfs_clk->dvfs_nb) {
			// must unregister when clk disable
			clk_notifier_register(clk, dvfs_clk->dvfs_nb);
		}
#endif

#if 0
		if(dvfs_clk->vd->cur_volt < dvfs_clk->set_volt) {
			int ret;
			mutex_lock(&rk_dvfs_mutex);
			ret = dvfs_regulator_set_voltage_readback(dvfs_clk->vd->regulator, dvfs_clk->set_volt, dvfs_clk->set_volt);
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
#endif
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
struct clk_node *dvfs_get_dvfs_clk_byname(char *name) 
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
int rk_regist_vd(struct vd_node *vd)
{
	if (!vd)
		return -1;
	mutex_lock(&mutex);
	//mutex_init(&vd->dvfs_mutex);
	list_add(&vd->node, &rk_dvfs_tree);
	INIT_LIST_HEAD(&vd->pd_list);
	INIT_LIST_HEAD(&vd->req_volt_list);
	vd->mode_flag=0;
	vd->volt_time_flag=0;
	vd->n_voltages=0;
	mutex_unlock(&mutex);
	return 0;
}

int rk_regist_pd(struct pd_node_lookup *pd_lookup)
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

int rk_regist_clk(struct clk_node *dvfs_clk)
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

int rk_regist_depends(struct depend_lookup *dep_node)
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

int correct_volt(int *volt_clk, int *volt_dep, int clk_biger_than_dep, int dep_biger_than_clk)
{
	int up_boundary = 0, low_boundary = 0;

	up_boundary = *volt_clk + dep_biger_than_clk;
	low_boundary = *volt_clk - clk_biger_than_dep;

	if (*volt_dep < low_boundary || *volt_dep > up_boundary) {

		if (*volt_dep < low_boundary) {
			*volt_dep = low_boundary;

		} else if (*volt_dep > up_boundary) {
			*volt_clk = *volt_dep - dep_biger_than_clk;
		}
	}

	return 0;
}

int dvfs_scale_volt(struct vd_node *vd_clk, struct vd_node *vd_dep,
		int volt_old, int volt_new, int volt_dep_old, int volt_dep_new, int clk_biger_than_dep, int dep_biger_than_clk)
{
	struct regulator *regulator, *regulator_dep;
	int volt = 0, volt_dep = 0, step = 0, step_dep = 0;
	int volt_tmp = 0, volt_dep_tmp = 0;
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
			volt_tmp = volt_dep + clk_biger_than_dep;
			volt_dep_tmp = volt + dep_biger_than_clk;

			volt = volt_tmp > volt_new ? volt_new : volt_tmp;
			volt_dep = volt_dep_tmp > volt_dep_new ? volt_dep_new : volt_dep_tmp;

		} else if (step < 0) {
			// down voltage
			DVFS_DBG("step < 0\n");

			volt_tmp = volt_dep - dep_biger_than_clk;
			volt_dep_tmp = volt - clk_biger_than_dep;

			volt = volt_tmp < volt_new ? volt_new : volt_tmp;
			volt_dep = volt_dep_tmp < volt_dep_new ? volt_dep_new : volt_dep_tmp;

		} else {
			DVFS_ERR("Oops, some bugs here:Volt_new=%d(old=%d), volt_dep_new=%d(dep_old=%d)\n",
					volt_new, volt_old, volt_dep_new, volt_dep_old);
			goto fail;
		}

		if (vd_clk->cur_volt != volt) {
			DVFS_DBG("\t\t%s:%d->%d\n", vd_clk->name, vd_clk->cur_volt, volt);
			ret = dvfs_regulator_set_voltage_readback(regulator, volt, volt);
			//udelay(get_volt_up_delay(volt, volt_pre));
			dvfs_volt_up_delay(vd_clk,volt, volt_pre);
			if (ret < 0) {
				DVFS_ERR("%s %s set voltage up err ret = %d, Vnew = %d(was %d)mV\n",
						__func__, vd_clk->name, ret, volt_new, volt_old);
				goto fail;
			}
			vd_clk->cur_volt = volt;
		}
		if (vd_dep->cur_volt != volt_dep) {
			DVFS_DBG("\t\t%s:%d->%d\n", vd_dep->name, vd_dep->cur_volt, volt_dep);
			ret = dvfs_regulator_set_voltage_readback(regulator_dep, volt_dep, volt_dep);
			//udelay(get_volt_up_delay(volt_dep, volt_dep_pre));
			dvfs_volt_up_delay(vd_dep,volt_dep, volt_dep_pre);
			if (ret < 0) {
				DVFS_ERR("depend %s %s set voltage up err ret = %d, Vnew = %d(was %d)mV\n",
						__func__, vd_dep->name, ret, volt_dep_new, volt_dep_old);
				goto fail;
			}
			vd_dep->cur_volt = volt_dep;
		}

		DVFS_DBG("\t\tNOW:Volt=%d, volt_dep=%d\n", volt, volt_dep);

	} while (volt != volt_new || volt_dep != volt_dep_new);

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
		//udelay(get_volt_up_delay(volt_new, vd_clk->cur_volt));
		dvfs_volt_up_delay(vd_clk,volt_new, vd_clk->cur_volt);
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
		int cur_clk_biger_than_dep, int cur_dep_biger_than_clk, int new_clk_biger_than_dep, int new_dep_biger_than_clk)
{

	struct regulator *regulator, *regulator_dep;
	int volt_new_corrected = 0, volt_dep_new_corrected = 0;
	int volt_old = 0, volt_dep_old = 0;
	int ret = 0;

	volt_old = vd_clk->cur_volt;
	volt_dep_old = vd_dep->cur_volt;

	DVFS_DBG("ENTER %s, volt=%d(old=%d) vd_dep=%d(dep_old=%d)\n", __func__,
			volt_new, volt_old, volt_dep_new, volt_dep_old);
	DVFS_DBG("ENTER %s, VOLT_DIFF: clk_cur=%d(clk_new=%d) dep_cur=%d(dep_new=%d)\n", __func__,
			cur_clk_biger_than_dep, new_clk_biger_than_dep, 
			cur_dep_biger_than_clk, new_dep_biger_than_clk);

	volt_new_corrected = volt_new;
	volt_dep_new_corrected = volt_dep_new;
	correct_volt(&volt_new_corrected, &volt_dep_new_corrected, cur_clk_biger_than_dep, cur_dep_biger_than_clk);
	ret = dvfs_scale_volt(vd_clk, vd_dep, volt_old, volt_new_corrected, volt_dep_old, volt_dep_new_corrected,
			cur_clk_biger_than_dep, cur_dep_biger_than_clk);
	if (ret < 0) {
		vd_clk->volt_set_flag = DVFS_SET_VOLT_FAILURE;
		DVFS_ERR("set volt error\n");
		return -1;
	}

	if (cur_clk_biger_than_dep != new_clk_biger_than_dep || cur_dep_biger_than_clk != new_dep_biger_than_clk) {
		regulator = vd_clk->regulator;
		regulator_dep = vd_dep->regulator;

		volt_new_corrected = volt_new;
		volt_dep_new_corrected = volt_dep_new;
		correct_volt(&volt_new_corrected, &volt_dep_new_corrected, new_clk_biger_than_dep, new_dep_biger_than_clk);

		if (vd_clk->cur_volt != volt_new_corrected) {
			DVFS_DBG("%s:%d->%d\n", vd_clk->name, vd_clk->cur_volt, volt_new_corrected);
			ret = dvfs_regulator_set_voltage_readback(regulator, volt_new_corrected, volt_new_corrected);
			//udelay(get_volt_up_delay(volt_new_corrected, vd_clk->cur_volt));
			dvfs_volt_up_delay(vd_clk,volt_new_corrected, vd_clk->cur_volt);
			if (ret < 0) {
				DVFS_ERR("%s %s set voltage up err ret = %d, Vnew = %d(was %d)mV\n",
						__func__, vd_clk->name, ret, volt_new_corrected, vd_clk->cur_volt);
				return -1;
			}
			vd_clk->cur_volt = volt_new_corrected;
		}
		if (vd_dep->cur_volt != volt_dep_new_corrected) {
			DVFS_DBG("%s:%d->%d\n", vd_clk->name, vd_clk->cur_volt, volt_dep_new_corrected);
			ret = dvfs_regulator_set_voltage_readback(regulator_dep, volt_dep_new_corrected, volt_dep_new_corrected);
			//udelay(get_volt_up_delay(volt_dep_new_corrected, vd_dep->cur_volt));
			dvfs_volt_up_delay(vd_dep,volt_dep_new_corrected, vd_dep->cur_volt);
			if (ret < 0) {
				DVFS_ERR("depend %s %s set voltage up err ret = %d, Vnew = %d(was %d)mV\n",
						__func__, vd_dep->name, ret, volt_dep_new_corrected, vd_dep->cur_volt);
				return -1;
			}
			vd_dep->cur_volt = volt_dep_new_corrected;
		}
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
		DVFS_ERR("%s (vd:%s), try to reload volt ,by it is error again(%d)!!! stop scaling\n",
				__func__, dvfs_vd->name, flag_set_volt_correct);
		return -1;
	}
	dvfs_vd->volt_set_flag = DVFS_SET_VOLT_SUCCESS;
	DVFS_WARNING("%s (vd:%s), try to reload volt = %d\n",
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
	char *s = buf;
	mutex_lock(&rk_dvfs_mutex);

	printk( "-------------DVFS TREE-----------\n\n\n");
	printk( "DVFS TREE:\n");
	list_for_each_entry(vd, &rk_dvfs_tree, node) {
		printk( "|\n|- voltage domain:%s\n", vd->name);
		printk( "|- current voltage:%d\n", vd->cur_volt);
		list_for_each_entry(depend, &vd->req_volt_list, node2vd) {
			printk( "|- request voltage:%d, clk:%s\n", depend->req_volt, depend->dvfs_clk->name);
		}

		list_for_each_entry(pd, &vd->pd_list, node) {
			printk( "|  |\n|  |- power domain:%s, status = %s, current volt = %d\n",
					pd->name, (pd->pd_status == PD_ON) ? "ON" : "OFF", pd->cur_volt);

			list_for_each_entry(child, &pd->clk_list, node) {
				dvfs_clk = child->dvfs_clk;
				printk( "|  |  |\n|  |  |- clock: %s current: rate %d, volt = %d,"
						" enable_dvfs = %s\n",
						dvfs_clk->name, dvfs_clk->set_freq, dvfs_clk->set_volt,
						dvfs_clk->enable_dvfs == 0 ? "DISABLE" : "ENABLE");
				printk( "|  |  |- clk limit:[%d, %d]; last set rate = %lu\n",
						dvfs_clk->min_rate, dvfs_clk->max_rate,
						dvfs_clk->clk->last_set_rate);

				for (i = 0; dvfs_clk->pds[i].pd != NULL; i++) {
					clkparent = dvfs_clk->pds[i].pd;
					printk( "|  |  |  |- clock parents: %s, vd_parent = %s\n",
							clkparent->name, clkparent->vd->name);
				}

				for (i = 0; (dvfs_clk->dvfs_table[i].frequency != CPUFREQ_TABLE_END); i++) {
					printk( "|  |  |  |- freq = %d, volt = %d\n",
							dvfs_clk->dvfs_table[i].frequency,
							dvfs_clk->dvfs_table[i].index);

				}

				list_for_each_entry(depend, &dvfs_clk->depend_list, node2clk) {
					printk( "|  |  |  |  |- DEPEND VD: %s\n", depend->dep_vd->name);
					for (i = 0; (depend->dep_table[i].frequency != CPUFREQ_TABLE_END); i++) {
						printk( "|  |  |  |  |- freq = %d, req_volt = %d\n",
								depend->dep_table[i].frequency,

								depend->dep_table[i].index);
					}
				}
			}
		}
	}
	printk( "-------------DVFS TREE END------------\n");
	
	mutex_unlock(&rk_dvfs_mutex);
	return s - buf;
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

static struct avs_ctr_st *avs_ctr_data=NULL;
#define init_avs_times 10
#define init_avs_st_num 5

struct init_avs_st {
	int is_set;
	u8 paramet[init_avs_times];
	int vol;
	char *s;
};

static struct init_avs_st init_avs_paramet[init_avs_st_num];

void avs_board_init(struct avs_ctr_st *data)
{
	
	avs_ctr_data=data;
}
void avs_init(void)
{
	memset(&init_avs_paramet[0].is_set, 0, sizeof(init_avs_paramet));
	if(avs_ctr_data&&avs_ctr_data->avs_init)
		avs_ctr_data->avs_init();
	avs_init_val_get(0, 1200000,"board_init");
}
static u8 rk_get_avs_val(void)
{
	
	if(avs_ctr_data&&avs_ctr_data->avs_get_val)
	{	
		return avs_ctr_data->avs_get_val();
	}
	return 0;

}
/******************************int val get**************************************/
void avs_init_val_get(int index, int vol, char *s)
{
	int i;
	if(index >= init_avs_times)
		return;
	init_avs_paramet[index].vol = vol;
	init_avs_paramet[index].s = s;
	init_avs_paramet[index].is_set++;
	printk("DVFS MSG:\tAVS Value(index=%d): ", index);
	for(i = 0; i < init_avs_times; i++) {
		init_avs_paramet[index].paramet[i] = rk_get_avs_val();
		mdelay(1);
		printk("%d ", init_avs_paramet[index].paramet[i]);
	}
	printk("\n");
}
int avs_set_scal_val(u8 avs_base)
{
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
	return sprintf(buf, "%d\n", rk_get_avs_val());
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

	if(avs_dyn_data == NULL)
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
		if(avs_dyn_data == NULL)
			avs_dyn_data = kmalloc(avs_dyn_data_num, GFP_KERNEL);
		if(avs_dyn_data == NULL)
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
		avs_dyn_data[avs_dyn_data_cnt] = rk_get_avs_val();
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
		avs_dyn_data[avs_dyn_data_cnt] = rk_get_avs_val();
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
	__ATTR(dvfs_tree,	S_IRUSR | S_IRGRP | S_IWUSR,	dvfs_tree_show,	dvfs_tree_store),
	__ATTR(avs_init,	S_IRUSR | S_IRGRP | S_IWUSR,	avs_init_show,	avs_init_store),
//	__ATTR(avs_dyn,		S_IRUSR | S_IRGRP | S_IWUSR,	avs_dyn_show,	avs_dyn_store),
	__ATTR(avs_now,		S_IRUSR | S_IRGRP | S_IWUSR,	avs_now_show,	avs_now_store),
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
