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
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/stat.h>
#include <linux/of.h>
#include <linux/opp.h>
#include "dvfs.h"

#define MHz	(1000 * 1000)
static LIST_HEAD(rk_dvfs_tree);
static DEFINE_MUTEX(rk_dvfs_mutex);

static void dvfs_volt_up_delay(struct vd_node *vd, int new_volt, int old_volt)
{
	int u_time;
	
	if(new_volt <= old_volt)
		return;
	if(vd->volt_time_flag > 0)	
		u_time = regulator_set_voltage_time(vd->regulator, old_volt, new_volt);
	else
		u_time = -1;		
	if(u_time < 0) {// regulator is not suported time,useing default time
		DVFS_DBG("%s:vd %s is not suported getting delay time,so we use default\n",
				__func__, vd->name);
		u_time = ((new_volt) - (old_volt)) >> 9;
	}
	
	DVFS_DBG("%s: vd %s volt %d to %d delay %d us\n", 
		__func__, vd->name, old_volt, new_volt, u_time);
	
	if (u_time >= 1000) {
		mdelay(u_time / 1000);
		udelay(u_time % 1000);
		DVFS_WARNING("%s: regulator set vol delay is larger 1ms,old is %d,new is %d\n",
			__func__, old_volt, new_volt);
	} else if (u_time) {
		udelay(u_time);
	}			
}

static int dvfs_regulator_set_voltage_readback(struct regulator *regulator, int min_uV, int max_uV)
{
	int ret = 0, read_back = 0;
	
	ret = dvfs_regulator_set_voltage(regulator, max_uV, max_uV);
	if (ret < 0) {
		DVFS_ERR("%s: now read back to check voltage\n", __func__);

		/* read back to judge if it is already effect */
		mdelay(2);
		read_back = dvfs_regulator_get_voltage(regulator);
		if (read_back == max_uV) {
			DVFS_ERR("%s: set ERROR but already effected, volt=%d\n", __func__, read_back);
			ret = 0;
		} else {
			DVFS_ERR("%s: set ERROR AND NOT effected, volt=%d\n", __func__, read_back);
		}
	}
	
	return ret;
}

static int dvfs_scale_volt_direct(struct vd_node *vd_clk, int volt_new)
{
	int ret = 0;
	
	DVFS_DBG("%s: volt=%d(old=%d)\n", __func__, volt_new, vd_clk->cur_volt);
	
	if (IS_ERR_OR_NULL(vd_clk)) {
		DVFS_ERR("%s: vd_node error\n", __func__);
		return -EINVAL;
	}

	if (!IS_ERR_OR_NULL(vd_clk->regulator)) {
		ret = dvfs_regulator_set_voltage_readback(vd_clk->regulator, volt_new, volt_new);
		dvfs_volt_up_delay(vd_clk,volt_new, vd_clk->cur_volt);
		if (ret < 0) {
			vd_clk->volt_set_flag = DVFS_SET_VOLT_FAILURE;
			DVFS_ERR("%s: %s set voltage up err ret = %d, Vnew = %d(was %d)mV\n",
					__func__, vd_clk->name, ret, volt_new, vd_clk->cur_volt);
			return -EAGAIN;
		}

	} else {
		DVFS_ERR("%s: invalid regulator\n", __func__);
		return -EINVAL;
	}

	vd_clk->volt_set_flag = DVFS_SET_VOLT_SUCCESS;
	vd_clk->cur_volt = volt_new;

	return 0;

}

static int dvfs_reset_volt(struct vd_node *dvfs_vd)
{
	int flag_set_volt_correct = 0;
	if (!IS_ERR_OR_NULL(dvfs_vd->regulator))
		flag_set_volt_correct = dvfs_regulator_get_voltage(dvfs_vd->regulator);
	else {
		DVFS_ERR("%s: invalid regulator\n", __func__);
		return -EINVAL;
	}
	if (flag_set_volt_correct <= 0) {
		DVFS_ERR("%s (vd:%s), try to reload volt ,by it is error again(%d)!!! stop scaling\n",
				__func__, dvfs_vd->name, flag_set_volt_correct);
		return -EAGAIN;
	}
	dvfs_vd->volt_set_flag = DVFS_SET_VOLT_SUCCESS;
	DVFS_WARNING("%s:vd(%s) try to reload volt = %d\n",
			__func__, dvfs_vd->name, flag_set_volt_correct);

	/* Reset vd's voltage */
	dvfs_vd->cur_volt = flag_set_volt_correct;

	return dvfs_vd->cur_volt;
}


// for clk enable case to get vd regulator info
static void clk_enable_dvfs_regulator_check(struct vd_node *vd)
{
	vd->cur_volt = dvfs_regulator_get_voltage(vd->regulator);
	if(vd->cur_volt <= 0){
		vd->volt_set_flag = DVFS_SET_VOLT_FAILURE;
	}
	vd->volt_set_flag = DVFS_SET_VOLT_SUCCESS;
}

static void dvfs_get_vd_regulator_volt_list(struct vd_node *vd)
{
	unsigned int i, selector = dvfs_regulator_count_voltages(vd->regulator);
	int n = 0, sel_volt = 0;
	
	if(selector > VD_VOL_LIST_CNT)
		selector = VD_VOL_LIST_CNT;

	for (i = 0; i < selector; i++) {
		sel_volt = dvfs_regulator_list_voltage(vd->regulator, i);
		if(sel_volt <= 0){	
			//DVFS_WARNING("%s: vd(%s) list volt selector=%u, but volt(%d) <=0\n",
			//	__func__, vd->name, i, sel_volt);
			continue;
		}
		vd->volt_list[n++] = sel_volt;	
		DVFS_DBG("%s: vd(%s) list volt selector=%u, n=%d, volt=%d\n", 
			__func__, vd->name, i, n, sel_volt);
	}
	
	vd->n_voltages = n;
}

// >= volt
static int vd_regulator_round_volt_max(struct vd_node *vd, int volt)
{
	int sel_volt;
	int i;
	
	for (i = 0; i < vd->n_voltages; i++) {
		sel_volt = vd->volt_list[i];
		if(sel_volt <= 0){	
			DVFS_WARNING("%s: selector=%u, but volt <=0\n", 
				__func__, i);
			continue;
		}
		if(sel_volt >= volt)
			return sel_volt;	
	}
	return -EINVAL;
}

// >=volt
static int vd_regulator_round_volt_min(struct vd_node *vd, int volt)
{
	int sel_volt;
	int i;
	
	for (i = 0; i < vd->n_voltages; i++) {
		sel_volt = vd->volt_list[i];
		if(sel_volt <= 0){	
			DVFS_WARNING("%s: selector=%u, but volt <=0\n", 
				__func__, i);
			continue;
		}
		if(sel_volt > volt){
			if(i > 0)
				return vd->volt_list[i-1];
			else
				return -EINVAL;
		}	
	}
	
	return -EINVAL;
}

// >=volt
static int vd_regulator_round_volt(struct vd_node *vd, int volt, int flags)
{
	if(!vd->n_voltages)
		return -EINVAL;
	if(flags == VD_LIST_RELATION_L)
		return vd_regulator_round_volt_min(vd, volt);
	else
		return vd_regulator_round_volt_max(vd, volt);	
}

static void dvfs_table_round_volt(struct dvfs_node *clk_dvfs_node)
{
	int i, test_volt;

	if(!clk_dvfs_node->dvfs_table || !clk_dvfs_node->vd || 
		IS_ERR_OR_NULL(clk_dvfs_node->vd->regulator))
		return;

	for (i = 0; (clk_dvfs_node->dvfs_table[i].frequency != CPUFREQ_TABLE_END); i++) {

		test_volt = vd_regulator_round_volt(clk_dvfs_node->vd, clk_dvfs_node->dvfs_table[i].index, VD_LIST_RELATION_H);
		if(test_volt <= 0)
		{	
			DVFS_WARNING("%s: clk(%s) round volt(%d) but list <=0\n",
				__func__, clk_dvfs_node->name, clk_dvfs_node->dvfs_table[i].index);
			break;
		}
		DVFS_DBG("clk %s:round_volt %d to %d\n",
			clk_dvfs_node->name, clk_dvfs_node->dvfs_table[i].index, test_volt);
		
		clk_dvfs_node->dvfs_table[i].index=test_volt;		
	}
}

static void dvfs_vd_get_regulator_volt_time_info(struct vd_node *vd)
{
	if(vd->volt_time_flag <= 0){// check regulator support get uping vol timer
		vd->volt_time_flag = dvfs_regulator_set_voltage_time(vd->regulator, vd->cur_volt, vd->cur_volt+200*1000);
		if(vd->volt_time_flag < 0){
			DVFS_DBG("%s,vd %s volt_time is no support\n",
				__func__, vd->name);
		}
		else{
			DVFS_DBG("%s,vd %s volt_time is support,up 200mv need delay %d us\n",
				__func__, vd->name, vd->volt_time_flag);
		}	
	}
}
#if 0
static void dvfs_vd_get_regulator_mode_info(struct vd_node *vd)
{
	//REGULATOR_MODE_FAST
	if(vd->mode_flag <= 0){// check regulator support get uping vol timer{
		vd->mode_flag = dvfs_regulator_get_mode(vd->regulator);
		if(vd->mode_flag==REGULATOR_MODE_FAST || vd->mode_flag==REGULATOR_MODE_NORMAL
			|| vd->mode_flag == REGULATOR_MODE_IDLE || vd->mode_flag==REGULATOR_MODE_STANDBY){
			
			if(dvfs_regulator_set_mode(vd->regulator, vd->mode_flag) < 0){
				vd->mode_flag = 0;// check again
			}
		}
		if(vd->mode_flag > 0){
			DVFS_DBG("%s,vd %s mode(now is %d) support\n",
				__func__, vd->name, vd->mode_flag);
		}
		else{
			DVFS_DBG("%s,vd %s mode is not support now check\n",
				__func__, vd->name);
		}
	}
}

struct regulator *dvfs_get_regulator1(char *regulator_name) 
{
	struct vd_node *vd;
	list_for_each_entry(vd, &rk_dvfs_tree, node) {
		if (strcmp(regulator_name, vd->regulator_name) == 0) {
			return vd->regulator;
		}
	}
	return NULL;
}
#endif

static int dvfs_get_rate_range(struct dvfs_node *clk_dvfs_node)
{
	struct cpufreq_frequency_table *table;
	int i = 0;

	if (!clk_dvfs_node)
		return -1;

	clk_dvfs_node->min_rate = 0;
	clk_dvfs_node->max_rate = 0;

	table = clk_dvfs_node->dvfs_table;
	for (i = 0; table[i].frequency != CPUFREQ_TABLE_END; i++) {
		clk_dvfs_node->max_rate = table[i].frequency / 1000 * 1000 * 1000;
		if (i == 0)
			clk_dvfs_node->min_rate = table[i].frequency / 1000 * 1000 * 1000;
	}

	DVFS_DBG("%s: clk %s, limit rate [min, max] = [%u, %u]\n",
			__func__, clk_dvfs_node->name, clk_dvfs_node->min_rate, clk_dvfs_node->max_rate);

	return 0;
}

static void dvfs_table_round_clk_rate(struct dvfs_node  *clk_dvfs_node)
{
	int i, rate, temp_rate, flags;
	
	if(!clk_dvfs_node || !clk_dvfs_node->dvfs_table || !clk_dvfs_node->clk)
		return;

	for (i = 0; (clk_dvfs_node->dvfs_table[i].frequency != CPUFREQ_TABLE_END); i++) {
		//ddr rate = real rate+flags
		flags = clk_dvfs_node->dvfs_table[i].frequency%1000;
		rate = (clk_dvfs_node->dvfs_table[i].frequency/1000)*1000;
		temp_rate = clk_round_rate(clk_dvfs_node->clk, rate*1000);
		if(temp_rate <= 0){	
			DVFS_WARNING("%s: clk(%s) rate %d round return %d\n",
				__func__, clk_dvfs_node->name, clk_dvfs_node->dvfs_table[i].frequency, temp_rate);
			continue;
		}
		
		/* Set rate unit as MHZ */
		if (temp_rate % MHz != 0)
			temp_rate = (temp_rate / MHz + 1) * MHz;

		temp_rate = (temp_rate / 1000) + flags;
		
		DVFS_DBG("clk %s round_clk_rate %d to %d\n",
			clk_dvfs_node->name,clk_dvfs_node->dvfs_table[i].frequency, temp_rate);
		
		clk_dvfs_node->dvfs_table[i].frequency = temp_rate;		
	}
}

static int clk_dvfs_node_get_ref_volt(struct dvfs_node *clk_dvfs_node, int rate_khz,
		struct cpufreq_frequency_table *clk_fv)
{
	int i = 0;
	
	if (rate_khz == 0 || !clk_dvfs_node || !clk_dvfs_node->dvfs_table) {
		/* since no need */
		return -EINVAL;
	}
	clk_fv->frequency = rate_khz;
	clk_fv->index = 0;

	for (i = 0; (clk_dvfs_node->dvfs_table[i].frequency != CPUFREQ_TABLE_END); i++) {
		if (clk_dvfs_node->dvfs_table[i].frequency >= rate_khz) {
			clk_fv->frequency = clk_dvfs_node->dvfs_table[i].frequency;
			clk_fv->index = clk_dvfs_node->dvfs_table[i].index;
			 //printk("%s,%s rate=%ukhz(vol=%d)\n",__func__,clk_dvfs_node->name,
			 //clk_fv->frequency, clk_fv->index);
			return 0;
		}
	}
	clk_fv->frequency = 0;
	clk_fv->index = 0;
	//DVFS_DBG("%s get corresponding voltage error! out of bound\n", clk_dvfs_node->name);
	return -1;
}

static int dvfs_pd_get_newvolt_byclk(struct pd_node *pd, struct dvfs_node *clk_dvfs_node)
{
	int volt_max = 0;

	if (!pd || !clk_dvfs_node)
		return 0;

	if (clk_dvfs_node->set_volt >= pd->cur_volt) {
		return clk_dvfs_node->set_volt;
	}

	list_for_each_entry(clk_dvfs_node, &pd->clk_list, node) {
		// DVFS_DBG("%s ,pd(%s),dvfs(%s),volt(%u)\n",__func__,pd->name,
		// clk_dvfs_node->name,clk_dvfs_node->set_volt);
		volt_max = max(volt_max, clk_dvfs_node->set_volt);
	}
	return volt_max;
}

static void dvfs_update_clk_pds_volt(struct dvfs_node *clk_dvfs_node)
{
	struct pd_node *pd;
	
	if (!clk_dvfs_node)
		return;
	
	pd = clk_dvfs_node->pd;
	if (!pd)
		return ;
	
	pd->cur_volt = dvfs_pd_get_newvolt_byclk(pd, clk_dvfs_node);
	/*for (i = 0; (clk_dvfs_node->pds[i].pd != NULL); i++) {
		pd = clk_dvfs_node->pds[i].pd;
		// DVFS_DBG("%s dvfs(%s),pd(%s)\n",__func__,clk_dvfs_node->name,pd->name);
		pd->cur_volt = dvfs_pd_get_newvolt_byclk(pd, clk_dvfs_node);
	}*/
}

static int dvfs_vd_get_newvolt_bypd(struct vd_node *vd)
{
	int volt_max_vd = 0;
	struct pd_node *pd;
	//struct depend_list	*depend;

	if (!vd)
		return -EINVAL;
	
	list_for_each_entry(pd, &vd->pd_list, node) {
		// DVFS_DBG("%s pd(%s,%u)\n",__func__,pd->name,pd->cur_volt);
		volt_max_vd = max(volt_max_vd, pd->cur_volt);
	}

	/* some clks depend on this voltage domain */
/*	if (!list_empty(&vd->req_volt_list)) {
		list_for_each_entry(depend, &vd->req_volt_list, node2vd) {
			volt_max_vd = max(volt_max_vd, depend->req_volt);
		}
	}*/
	return volt_max_vd;
}

static int dvfs_vd_get_newvolt_byclk(struct dvfs_node *clk_dvfs_node)
{
	if (!clk_dvfs_node)
		return -1;
	dvfs_update_clk_pds_volt(clk_dvfs_node);
	return  dvfs_vd_get_newvolt_bypd(clk_dvfs_node->vd);
}

int dvfs_clk_enable_limit(struct dvfs_node *clk_dvfs_node, unsigned int min_rate, unsigned max_rate)
{
	u32 rate = 0, ret = 0;

	if (!clk_dvfs_node)
		return -EINVAL;
	
	if (clk_dvfs_node->vd && clk_dvfs_node->vd->vd_dvfs_target){
		mutex_lock(&clk_dvfs_node->vd->mutex);
		
		/* To reset clk_dvfs_node->min_rate/max_rate */
		dvfs_get_rate_range(clk_dvfs_node);
		clk_dvfs_node->freq_limit_en = 1;
		clk_dvfs_node->min_rate = min_rate > clk_dvfs_node->min_rate ? min_rate : clk_dvfs_node->min_rate;
		clk_dvfs_node->max_rate = max_rate < clk_dvfs_node->max_rate ? max_rate : clk_dvfs_node->max_rate;
		if (clk_dvfs_node->last_set_rate == 0)
			rate = clk_get_rate(clk_dvfs_node->clk);
		else
			rate = clk_dvfs_node->last_set_rate;
		ret = clk_dvfs_node->vd->vd_dvfs_target(clk_dvfs_node, rate);

		mutex_unlock(&clk_dvfs_node->vd->mutex);

	}

	DVFS_DBG("%s:clk(%s) last_set_rate=%u; [min_rate, max_rate]=[%u, %u]\n",
			__func__, __clk_get_name(clk_dvfs_node->clk), clk_dvfs_node->last_set_rate, 
			clk_dvfs_node->min_rate, clk_dvfs_node->max_rate);

	return 0;
}
EXPORT_SYMBOL(dvfs_clk_enable_limit);

int dvfs_clk_disable_limit(struct dvfs_node *clk_dvfs_node)
{
	u32 ret = 0;

	if (!clk_dvfs_node)
		return -EINVAL;
	
	if (clk_dvfs_node->vd && clk_dvfs_node->vd->vd_dvfs_target){
		mutex_lock(&clk_dvfs_node->vd->mutex);
		
		/* To reset clk_dvfs_node->min_rate/max_rate */
		dvfs_get_rate_range(clk_dvfs_node);
		clk_dvfs_node->freq_limit_en = 0;
		ret = clk_dvfs_node->vd->vd_dvfs_target(clk_dvfs_node, clk_dvfs_node->last_set_rate);

		mutex_unlock(&clk_dvfs_node->vd->mutex);
	}

	DVFS_DBG("%s: clk(%s) last_set_rate=%u; [min_rate, max_rate]=[%u, %u]\n",
			__func__, __clk_get_name(clk_dvfs_node->clk), clk_dvfs_node->last_set_rate, clk_dvfs_node->min_rate, clk_dvfs_node->max_rate);
	return 0;
}
EXPORT_SYMBOL(dvfs_clk_disable_limit);


int dvfs_clk_register_set_rate_callback(struct dvfs_node *clk_dvfs_node, clk_set_rate_callback clk_dvfs_target)
{
	if (!clk_dvfs_node)
		return -EINVAL;
			
	mutex_lock(&clk_dvfs_node->vd->mutex);
	clk_dvfs_node->clk_dvfs_target = clk_dvfs_target;
	mutex_unlock(&clk_dvfs_node->vd->mutex);

	return 0;
}
EXPORT_SYMBOL(dvfs_clk_register_set_rate_callback);

struct cpufreq_frequency_table *dvfs_get_freq_volt_table(struct dvfs_node *clk_dvfs_node) 
{
	struct cpufreq_frequency_table *table;

	if (!clk_dvfs_node)
		return NULL;

	mutex_lock(&clk_dvfs_node->vd->mutex);
	table = clk_dvfs_node->dvfs_table;
	mutex_unlock(&clk_dvfs_node->vd->mutex);
	
	return table;
}
EXPORT_SYMBOL(dvfs_get_freq_volt_table);

int dvfs_set_freq_volt_table(struct dvfs_node *clk_dvfs_node, struct cpufreq_frequency_table *table)
{
	if (!clk_dvfs_node)
		return -EINVAL;

	if (IS_ERR_OR_NULL(table)){
		DVFS_ERR("%s:invalid table!\n", __func__);
		return -EINVAL;
	}
	
	mutex_lock(&clk_dvfs_node->vd->mutex);
	clk_dvfs_node->dvfs_table = table;
	dvfs_get_rate_range(clk_dvfs_node);
	dvfs_table_round_clk_rate(clk_dvfs_node);
	dvfs_table_round_volt(clk_dvfs_node);
	mutex_unlock(&clk_dvfs_node->vd->mutex);

	return 0;
}
EXPORT_SYMBOL(dvfs_set_freq_volt_table);

int clk_enable_dvfs(struct dvfs_node *clk_dvfs_node)
{
	struct cpufreq_frequency_table clk_fv;

	if (!clk_dvfs_node)
		return -EINVAL;
	
	DVFS_DBG("%s: dvfs clk(%s) enable dvfs!\n", 
		__func__, __clk_get_name(clk_dvfs_node->clk));

	if (!clk_dvfs_node->vd) {
		DVFS_ERR("%s: dvfs node(%s) has no vd node!\n", 
			__func__, clk_dvfs_node->name);
		return -EINVAL;
	}
	mutex_lock(&clk_dvfs_node->vd->mutex);
	if (clk_dvfs_node->enable_count == 0) {
		if (IS_ERR_OR_NULL(clk_dvfs_node->vd->regulator)) {
			if (clk_dvfs_node->vd->regulator_name)
				clk_dvfs_node->vd->regulator = dvfs_regulator_get(NULL, clk_dvfs_node->vd->regulator_name);
			if (!IS_ERR_OR_NULL(clk_dvfs_node->vd->regulator)) {
				DVFS_DBG("%s: vd(%s) get regulator(%s) ok\n",
					__func__, clk_dvfs_node->vd->name, clk_dvfs_node->vd->regulator_name);
				clk_enable_dvfs_regulator_check(clk_dvfs_node->vd);
				dvfs_get_vd_regulator_volt_list(clk_dvfs_node->vd);
				dvfs_vd_get_regulator_volt_time_info(clk_dvfs_node->vd);
			} else {
				clk_dvfs_node->enable_count = 0;
				DVFS_ERR("%s: vd(%s) can't get regulator(%s)!\n", 
					__func__, clk_dvfs_node->vd->name, clk_dvfs_node->vd->regulator_name);
				mutex_unlock(&clk_dvfs_node->vd->mutex);
				return -ENXIO;
			}
		} else {
			clk_enable_dvfs_regulator_check(clk_dvfs_node->vd);
		}
		
		DVFS_DBG("%s: vd(%s) cur volt=%d\n",
			__func__, clk_dvfs_node->name, clk_dvfs_node->vd->cur_volt);

		dvfs_table_round_clk_rate(clk_dvfs_node);
		dvfs_get_rate_range(clk_dvfs_node);
		clk_dvfs_node->freq_limit_en = 1;
		dvfs_table_round_volt(clk_dvfs_node);
		clk_dvfs_node->set_freq = clk_dvfs_node_get_rate_kz(clk_dvfs_node->clk);
		
		DVFS_DBG("%s: %s get freq %u!\n", 
			__func__, clk_dvfs_node->name, clk_dvfs_node->set_freq);

		if (clk_dvfs_node_get_ref_volt(clk_dvfs_node, clk_dvfs_node->set_freq, &clk_fv)) {
			if (clk_dvfs_node->dvfs_table[0].frequency == CPUFREQ_TABLE_END) {
				DVFS_ERR("%s: table empty\n", __func__);
				clk_dvfs_node->enable_count = 0;
				mutex_unlock(&clk_dvfs_node->vd->mutex);
				return -EINVAL;
			} else {
				DVFS_WARNING("%s: clk(%s) freq table all value are smaller than default(%d), use default, just enable dvfs\n", 
					__func__, clk_dvfs_node->name, clk_dvfs_node->set_freq);
				clk_dvfs_node->enable_count++;
				mutex_unlock(&clk_dvfs_node->vd->mutex);
				return 0;
			}
		}

		clk_dvfs_node->set_volt = clk_fv.index;
		dvfs_vd_get_newvolt_byclk(clk_dvfs_node);
		DVFS_DBG("%s: %s, freq %u(ref vol %u)\n",
			__func__, clk_dvfs_node->name, clk_dvfs_node->set_freq, clk_dvfs_node->set_volt);
#if 0
		if (clk_dvfs_node->dvfs_nb) {
			// must unregister when clk disable
			clk_notifier_register(clk, clk_dvfs_node->dvfs_nb);
		}
#endif

#if 0
		if(clk_dvfs_node->vd->cur_volt < clk_dvfs_node->set_volt) {
			int ret;
			mutex_lock(&rk_dvfs_mutex);
			ret = dvfs_regulator_set_voltage_readback(clk_dvfs_node->vd->regulator, clk_dvfs_node->set_volt, clk_dvfs_node->set_volt);
			if (ret < 0) {
				clk_dvfs_node->vd->volt_set_flag = DVFS_SET_VOLT_FAILURE;
				clk_dvfs_node->enable_dvfs = 0;
				DVFS_ERR("dvfs enable clk %s,set volt error \n", clk_dvfs_node->name);
				mutex_unlock(&rk_dvfs_mutex);
				return -1;
			}
			clk_dvfs_node->vd->volt_set_flag = DVFS_SET_VOLT_SUCCESS;
			mutex_unlock(&rk_dvfs_mutex);
		}
#endif
		clk_dvfs_node->enable_count++;
	} else {
		DVFS_DBG("%s: dvfs already enable clk enable = %d!\n",
			__func__, clk_dvfs_node->enable_count);
		clk_dvfs_node->enable_count++;
	}

	mutex_unlock(&clk_dvfs_node->vd->mutex);
	
	return 0;
}
EXPORT_SYMBOL(clk_enable_dvfs);

int clk_disable_dvfs(struct dvfs_node *clk_dvfs_node)
{
	if (!clk_dvfs_node)
		return -EINVAL;

	DVFS_DBG("%s:dvfs clk(%s) disable dvfs!\n", 
		__func__, __clk_get_name(clk_dvfs_node->clk));

	mutex_lock(&clk_dvfs_node->vd->mutex);
	if (!clk_dvfs_node->enable_count) {
		DVFS_WARNING("%s:clk(%s) is already closed!\n", 
			__func__, __clk_get_name(clk_dvfs_node->clk));
		return 0;
	} else {
		clk_dvfs_node->enable_count--;
		if (0 == clk_dvfs_node->enable_count) {
			DVFS_DBG("%s:dvfs clk(%s) disable dvfs ok!\n",
				__func__, __clk_get_name(clk_dvfs_node->clk));
#if 0
			clk_notifier_unregister(clk, clk_dvfs_node->dvfs_nb);
			DVFS_DBG("clk unregister nb!\n");
#endif
		}
	}
	mutex_unlock(&clk_dvfs_node->vd->mutex);
	return 0;
}
EXPORT_SYMBOL(clk_disable_dvfs);

static int dvfs_target(struct dvfs_node *clk_dvfs_node, unsigned long rate)
{
	struct cpufreq_frequency_table clk_fv;
	unsigned long old_rate = 0, new_rate = 0, volt_new = 0, clk_volt_store = 0;
	struct clk *clk = clk_dvfs_node->clk;
	int ret;

	if (!clk)
		return -EINVAL;

	if (!clk_dvfs_node->enable_count){
		DVFS_WARNING("%s:dvfs(%s) is disable\n", 
			__func__, clk_dvfs_node->name);
		return 0;
	}
	
	if (clk_dvfs_node->vd->volt_set_flag == DVFS_SET_VOLT_FAILURE) {
		/* It means the last time set voltage error */
		ret = dvfs_reset_volt(clk_dvfs_node->vd);
		if (ret < 0) {
			return -EAGAIN;
		}
	}

	/* Check limit rate */
	//if (clk_dvfs_node->freq_limit_en) {
		if (rate < clk_dvfs_node->min_rate) {
			rate = clk_dvfs_node->min_rate;
		} else if (rate > clk_dvfs_node->max_rate) {
			rate = clk_dvfs_node->max_rate;
		}
	//}

	new_rate = clk_round_rate(clk, rate);
	old_rate = clk_get_rate(clk);
	if (new_rate == old_rate)
		return 0;

	DVFS_DBG("enter %s: clk(%s) new_rate = %lu Hz, old_rate =  %lu Hz\n", 
		__func__, clk_dvfs_node->name, rate, old_rate);	

	/* find the clk corresponding voltage */
	ret = clk_dvfs_node_get_ref_volt(clk_dvfs_node, new_rate / 1000, &clk_fv);
	if (ret) {
		DVFS_ERR("%s:dvfs clk(%s) rate %luhz is larger,not support\n",
			__func__, clk_dvfs_node->name, new_rate);
		return ret;
	}
	clk_volt_store = clk_dvfs_node->set_volt;
	clk_dvfs_node->set_volt = clk_fv.index;
	volt_new = dvfs_vd_get_newvolt_byclk(clk_dvfs_node);
	DVFS_DBG("%s:%s new rate=%lu(was=%lu),new volt=%lu,(was=%d)\n",
		__func__, clk_dvfs_node->name, new_rate, old_rate, volt_new,clk_dvfs_node->vd->cur_volt);

	/* if up the rate */
	if (new_rate > old_rate) {
		ret = dvfs_scale_volt_direct(clk_dvfs_node->vd, volt_new);
		if (ret)
			goto fail_roll_back;
	}

	/* scale rate */
	if (clk_dvfs_node->clk_dvfs_target) {
		ret = clk_dvfs_node->clk_dvfs_target(clk, new_rate);
	} else {
		ret = clk_set_rate(clk, new_rate);
	}

	if (ret) {
		DVFS_ERR("%s:clk(%s) set rate err\n", 
			__func__, __clk_get_name(clk));
		goto fail_roll_back;
	}
	clk_dvfs_node->set_freq = new_rate / 1000;

	DVFS_DBG("%s:dvfs clk(%s) set rate %lu ok\n", 
		__func__, clk_dvfs_node->name, clk_get_rate(clk));

	/* if down the rate */
	if (new_rate < old_rate) {
		ret = dvfs_scale_volt_direct(clk_dvfs_node->vd, volt_new);
		if (ret)
			goto out;
	}
	
	clk_dvfs_node->last_set_rate = new_rate;
	
	return 0;
fail_roll_back:
	clk_dvfs_node->set_volt = clk_volt_store;
out:
	return ret;
}

unsigned long dvfs_clk_get_rate(struct dvfs_node *clk_dvfs_node)
{
	return clk_get_rate(clk_dvfs_node->clk);
}
EXPORT_SYMBOL_GPL(dvfs_clk_get_rate);

int dvfs_clk_enable(struct dvfs_node *clk_dvfs_node)
{
	return clk_enable(clk_dvfs_node->clk);
}
EXPORT_SYMBOL_GPL(dvfs_clk_enable);

void dvfs_clk_disable(struct dvfs_node *clk_dvfs_node)
{
	return clk_disable(clk_dvfs_node->clk);
}
EXPORT_SYMBOL_GPL(dvfs_clk_disable);

struct dvfs_node *clk_get_dvfs_node(char *clk_name)
{
	struct vd_node *vd;
	struct pd_node *pd;
	struct dvfs_node *clk_dvfs_node;

	mutex_lock(&rk_dvfs_mutex);
	list_for_each_entry(vd, &rk_dvfs_tree, node) {
		mutex_lock(&vd->mutex);
		list_for_each_entry(pd, &vd->pd_list, node) {
			list_for_each_entry(clk_dvfs_node, &pd->clk_list, node) {
				if (0 == strcmp(clk_dvfs_node->name, clk_name)) {
					mutex_unlock(&vd->mutex);
					mutex_unlock(&rk_dvfs_mutex);
					return clk_dvfs_node;
				}
			}
		}
		mutex_unlock(&vd->mutex);
	}
	mutex_unlock(&rk_dvfs_mutex);
	
	return NULL;	
}
EXPORT_SYMBOL_GPL(clk_get_dvfs_node);

void clk_put_dvfs_node(struct dvfs_node *clk_dvfs_node)
{
	return;
}
EXPORT_SYMBOL_GPL(clk_put_dvfs_node);

int dvfs_clk_prepare_enable(struct dvfs_node *clk_dvfs_node)
{
	return clk_prepare_enable(clk_dvfs_node->clk);
}
EXPORT_SYMBOL_GPL(dvfs_clk_prepare_enable);


void dvfs_clk_disable_unprepare(struct dvfs_node *clk_dvfs_node)
{
	clk_disable_unprepare(clk_dvfs_node->clk);
}
EXPORT_SYMBOL_GPL(dvfs_clk_disable_unprepare);

int dvfs_clk_set_rate(struct dvfs_node *clk_dvfs_node, unsigned long rate)
{
	int ret = -EINVAL;
	
	if (!clk_dvfs_node)
		return -EINVAL;
	
	DVFS_DBG("%s:dvfs node(%s) set rate(%lu)\n", 
		__func__, clk_dvfs_node->name, rate);
	
	#if 0 // judge by reference func in rk
	if (dvfs_support_clk_set_rate(dvfs_info)==false) {
		DVFS_ERR("dvfs func:%s is not support!\n", __func__);
		return ret;
	}
	#endif

	if (clk_dvfs_node->vd && clk_dvfs_node->vd->vd_dvfs_target) {
		mutex_lock(&clk_dvfs_node->vd->mutex);
		ret = clk_dvfs_node->vd->vd_dvfs_target(clk_dvfs_node, rate);
		mutex_unlock(&clk_dvfs_node->vd->mutex);
	} else {
		DVFS_ERR("%s:dvfs node(%s) has no vd node or target callback!\n", 
			__func__, clk_dvfs_node->name);
	}
		
	return ret;	
}
EXPORT_SYMBOL_GPL(dvfs_clk_set_rate);


int rk_regist_vd(struct vd_node *vd)
{
	if (!vd)
		return -EINVAL;

	vd->mode_flag=0;
	vd->volt_time_flag=0;
	vd->n_voltages=0;
	INIT_LIST_HEAD(&vd->pd_list);
	mutex_lock(&rk_dvfs_mutex);
	list_add(&vd->node, &rk_dvfs_tree);
	mutex_unlock(&rk_dvfs_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(rk_regist_vd);

int rk_regist_pd(struct pd_node *pd)
{
	struct vd_node	*vd;

	if (!pd)
		return -EINVAL;

	vd = pd->vd;
	if (!vd)
		return -EINVAL;

	INIT_LIST_HEAD(&pd->clk_list);
	mutex_lock(&vd->mutex);
	list_add(&pd->node, &vd->pd_list);
	mutex_unlock(&vd->mutex);
	
	return 0;
}
EXPORT_SYMBOL_GPL(rk_regist_pd);

int rk_regist_clk(struct dvfs_node *clk_dvfs_node)
{
	struct vd_node	*vd;
	struct pd_node	*pd;

	if (!clk_dvfs_node)
		return -EINVAL;

	vd = clk_dvfs_node->vd;
	pd = clk_dvfs_node->pd;
	if (!vd || !pd)
		return -EINVAL;

	mutex_lock(&vd->mutex);
	list_add(&clk_dvfs_node->node, &pd->clk_list);
	mutex_unlock(&vd->mutex);
	
	return 0;
}
EXPORT_SYMBOL_GPL(rk_regist_clk);

static int rk_convert_cpufreq_table(struct dvfs_node *dvfs_node)
{
	struct opp *opp;
	struct device *dev;
	struct cpufreq_frequency_table *table;
	int i;

	table = dvfs_node->dvfs_table;
	dev = &dvfs_node->dev;

	for (i = 0; table[i].frequency!= CPUFREQ_TABLE_END; i++){
		opp = opp_find_freq_exact(dev, table[i].frequency * 1000, true);
		if (IS_ERR(opp))
			return PTR_ERR(opp);
		table[i].index = opp_get_voltage(opp);
	}
	return 0;
}

int of_dvfs_init(void)
{
	struct vd_node *vd;
	struct pd_node *pd;
	struct device_node *dvfs_dev_node, *clk_dev_node, *vd_dev_node, *pd_dev_node;
	struct dvfs_node *dvfs_node;
	struct clk *clk;
	int ret;

	DVFS_DBG("%s\n", __func__);

	dvfs_dev_node = of_find_node_by_name(NULL, "dvfs");
	if (IS_ERR_OR_NULL(dvfs_dev_node)) {
		DVFS_ERR("%s get dvfs dev node err\n", __func__);
		return PTR_ERR(dvfs_dev_node);
	}

	for_each_available_child_of_node(dvfs_dev_node, vd_dev_node) {
		vd = kzalloc(sizeof(struct vd_node), GFP_KERNEL);
		if (!vd)
			return -ENOMEM;

		mutex_init(&vd->mutex);
		vd->name = vd_dev_node->name;
		ret = of_property_read_string(vd_dev_node, "regulator_name", &vd->regulator_name);
		if (ret) {
			DVFS_ERR("%s:vd(%s) get regulator_name err, ret:%d\n", 
				__func__, vd_dev_node->name, ret);
			kfree(vd);
			continue;
		}
		
		/*vd->regulator = regulator_get(NULL, vd->regulator_name);
		if (IS_ERR(vd->regulator)){
			DVFS_ERR("%s vd(%s) get regulator(%s) failed!\n", __func__, vd->name, vd->regulator_name);
			kfree(vd);
			continue;
		}*/

		vd->suspend_volt = 0;
		
		vd->volt_set_flag = DVFS_SET_VOLT_FAILURE;
		vd->vd_dvfs_target = dvfs_target;
		ret = rk_regist_vd(vd);
		if (ret){
			DVFS_ERR("%s:vd(%s) register err:%d\n", __func__, vd->name, ret);
			kfree(vd);
			continue;
		}

		DVFS_DBG("%s:vd(%s) register ok, regulator name:%s,suspend volt:%d\n", 
			__func__, vd->name, vd->regulator_name, vd->suspend_volt);
		
		for_each_available_child_of_node(vd_dev_node, pd_dev_node) {		
			pd = kzalloc(sizeof(struct pd_node), GFP_KERNEL);
			if (!pd)
				return -ENOMEM;

			pd->vd = vd;
			pd->name = pd_dev_node->name;
			
			ret = rk_regist_pd(pd);
			if (ret){
				DVFS_ERR("%s:pd(%s) register err:%d\n", __func__, pd->name, ret);
				kfree(pd);
				continue;
			}
			DVFS_DBG("%s:pd(%s) register ok, parent vd:%s\n", 
				__func__, pd->name, vd->name);			
			for_each_available_child_of_node(pd_dev_node, clk_dev_node) {
				dvfs_node = kzalloc(sizeof(struct dvfs_node), GFP_KERNEL);
				if (!dvfs_node)
					return -ENOMEM;
				
				dvfs_node->name = clk_dev_node->name;
				dvfs_node->pd = pd;
				dvfs_node->vd = vd;
				dvfs_node->dev.of_node = clk_dev_node;
				ret = of_init_opp_table(&dvfs_node->dev);
				if (ret) {
					DVFS_ERR("%s:clk(%s) get opp table err:%d\n", __func__, dvfs_node->name, ret);
					kfree(dvfs_node);
					continue;
				}
				
				ret = opp_init_cpufreq_table(&dvfs_node->dev, &dvfs_node->dvfs_table);
				if (ret) {
					DVFS_ERR("%s:clk(%s) get cpufreq table err:%d\n", __func__, dvfs_node->name, ret);
					kfree(dvfs_node);
					continue;
				}
				ret = rk_convert_cpufreq_table(dvfs_node);
				if (ret) {
					kfree(dvfs_node);
					continue;
				}
				
				clk = clk_get(NULL, clk_dev_node->name);
				if (IS_ERR(clk)){
					DVFS_ERR("%s:get clk(%s) err:%ld\n", __func__, dvfs_node->name, PTR_ERR(clk));
					kfree(dvfs_node);
					continue;
					
				}
				
				dvfs_node->clk = clk;
				ret = rk_regist_clk(dvfs_node);
				if (ret){
					DVFS_ERR("%s:dvfs_node(%s) register err:%d\n", __func__, dvfs_node->name, ret);
					return ret;
				}

				DVFS_DBG("%s:dvfs_node(%s) register ok, parent pd:%s\n", 
					__func__, clk_dev_node->name, pd->name);	

			}
		}	
	}
	return 0;
}

/*********************************************************************************/

/**
 * dump_dbg_map() : Draw all informations of dvfs while debug
 */
static int dump_dbg_map(char *buf)
{
	int i;
	struct vd_node	*vd;
	struct pd_node	*pd;
	struct dvfs_node	*clk_dvfs_node;
	char *s = buf;
	
	mutex_lock(&rk_dvfs_mutex);
	printk( "-------------DVFS TREE-----------\n\n\n");
	printk( "DVFS TREE:\n");

	list_for_each_entry(vd, &rk_dvfs_tree, node) {
		mutex_lock(&vd->mutex);
		printk( "|\n|- voltage domain:%s\n", vd->name);
		printk( "|- current voltage:%d\n", vd->cur_volt);

		list_for_each_entry(pd, &vd->pd_list, node) {
			printk( "|  |\n|  |- power domain:%s, status = %s, current volt = %d\n",
					pd->name, (pd->pd_status == 1) ? "ON" : "OFF", pd->cur_volt);

			list_for_each_entry(clk_dvfs_node, &pd->clk_list, node) {
				printk( "|  |  |\n|  |  |- clock: %s current: rate %d, volt = %d,"
						" enable_dvfs = %s\n",
						clk_dvfs_node->name, clk_dvfs_node->set_freq, clk_dvfs_node->set_volt,
						clk_dvfs_node->enable_count == 0 ? "DISABLE" : "ENABLE");
				printk( "|  |  |- clk limit:[%u, %u]; last set rate = %u\n",
						clk_dvfs_node->min_rate, clk_dvfs_node->max_rate,
						clk_dvfs_node->last_set_rate);

				for (i = 0; (clk_dvfs_node->dvfs_table[i].frequency != CPUFREQ_TABLE_END); i++) {
					printk( "|  |  |  |- freq = %d, volt = %d\n",
							clk_dvfs_node->dvfs_table[i].frequency,
							clk_dvfs_node->dvfs_table[i].index);

				}
			}
		}
		mutex_unlock(&vd->mutex);
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

static u8 rk_get_avs_val(void)
{
	
	if(avs_ctr_data&&avs_ctr_data->avs_get_val)
	{	
		return avs_ctr_data->avs_get_val();
	}
	return 0;

}

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
//#ifdef CONFIG_RK_CLOCK_PROC
	__ATTR(dvfs_tree,	S_IRUSR | S_IRGRP | S_IWUSR,	dvfs_tree_show,	dvfs_tree_store),
	__ATTR(avs_init,	S_IRUSR | S_IRGRP | S_IWUSR,	avs_init_show,	avs_init_store),
	__ATTR(avs_dyn,		S_IRUSR | S_IRGRP | S_IWUSR,	avs_dyn_show,	avs_dyn_store),
	__ATTR(avs_now,		S_IRUSR | S_IRGRP | S_IWUSR,	avs_now_show,	avs_now_store),
//#endif
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
