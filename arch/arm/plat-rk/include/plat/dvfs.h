/* arch/arm/mach-rk30/rk30_dvfs.h
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
#ifndef _RK30_DVFS_H_
#define _RK30_DVFS_H_

#include <mach/clock.h>

//typedef int (*vd_dvfs_target_callback)(struct clk *clk, unsigned long rate);
typedef int (*dvfs_set_rate_callback)(struct clk *clk, unsigned long rate);
typedef int (*clk_dvfs_target_callback)(struct clk *clk, unsigned long rate,
                                        dvfs_set_rate_callback set_rate);
typedef int (*dvfs_clk_disable_callback)(struct clk *clk,int on);

/**
 * struct vd_node:	To Store All Voltage Domains' info
 * @name:		Voltage Domain's Name
 * @regulator_name:	Voltage domain's regulator name
 * @cur_volt:		Voltage Domain's Current Voltage
 * @regulator:		Voltage Domain's regulator point
 * @node:		Point of he Voltage Domain List Node
 * @pd_list:		Head of Power Domain List Belongs to This Voltage Domain
 * @req_volt_list:	The list of clocks requests
 * @dvfs_mutex:		Lock
 * @vd_dvfs_target:	Callback function	
 */
 #define VD_VOL_LIST_CNT (200)
 #define VD_LIST_RELATION_L 0
 #define VD_LIST_RELATION_H 1

struct vd_node {
	char			*name;
	char			*regulator_name;
	int			volt_time_flag;// =0 ,is no initing checking ,>0 ,support,<0 not support
	int			mode_flag;// =0 ,is no initing checking ,>0 ,support,<0 not support
	int			cur_volt;
	int			volt_set_flag;
	struct regulator	*regulator;
	struct list_head	node;
	struct list_head	pd_list;
	struct list_head	req_volt_list;
	//struct mutex		dvfs_mutex;
	dvfs_clk_disable_callback	vd_clk_disable_target;
	dvfs_set_rate_callback      vd_dvfs_target;
	unsigned n_voltages;
	int volt_list[VD_VOL_LIST_CNT];
};

/**
 * struct pd_node:	To Store All Power Domains' info
 * @name:		Power Domain's Name
 * @cur_volt:		Power Domain's Current Voltage
 * @pd_status:		Power Domain's status
 * @vd:			Voltage Domain the power domain belongs to
 * @pd_clk:		Look power domain as a clock
 * @node:		List node to Voltage Domain
 * @clk_list:		Head of Power Domain's Clocks List
 */
struct pd_node {
	char			*name;
	int			cur_volt;
	unsigned char		pd_status;
	struct vd_node		*vd;
	//struct clk		*pd_clk;
	struct list_head	node;
	struct list_head	clk_list;
};

struct pd_node_lookup {
	struct pd_node		*pd;
};

struct clk_list{
	struct clk_node		*dvfs_clk;
	struct list_head	node;
};

struct pds_list {
	struct clk_list		clk_list;
	struct pd_node		*pd;
};

struct depend_list {
	int			req_volt;
	struct clk_node		*dvfs_clk;
	struct vd_node		*dep_vd;
	struct list_head	node2clk;
	struct list_head	node2vd;
	struct cpufreq_frequency_table	*dep_table;
};

struct depend_lookup {
	char			*clk_name;
	struct clk_node		*dvfs_clk;
	struct vd_node		*dep_vd;
	struct depend_list	dep_list;
	struct cpufreq_frequency_table	*dep_table;
};
struct clk_disable_ctr {
	dvfs_clk_disable_callback clk_disable_target;
	struct clk_node *dvfs_clk;
};
/**
 * struct clk_node:	To Store All dvfs clocks' info
 * @name:		Dvfs clock's Name
 * @set_freq:		Dvfs clock's Current Frequency
 * @set_volt:		Dvfs clock's Current Voltage
 * @enable_dvfs:	Sign if DVFS clock enable
 * @clk:		System clk's point
 * @pds:		Power Domains dvfs clock belongs to
 * @vd:			Voltage Domains dvfs clock belongs to
 * @depend_list:	Dvfs Clock depend list
 * @dvfs_nb:		Notify list
 * @dvfs_table:		Frequency and voltage table for dvfs
 * @clk_dvfs_target:	Callback function
 */
struct clk_node {
	char			*name;
	int			set_freq;	//KHZ
	int			set_volt;	//MV
	int			enable_dvfs;
	int			freq_limit_en;	//sign if use limit frequency
	unsigned int		min_rate;	//limit min frequency
	unsigned int		max_rate;	//limit max frequency
	struct clk 		*clk;
	struct pds_list		*pds;
	struct vd_node		*vd;
	struct list_head	depend_list;
	struct notifier_block	*dvfs_nb;
	struct cpufreq_frequency_table	*dvfs_table;
	clk_dvfs_target_callback	clk_dvfs_target;
	struct clk_disable_ctr *disable_ctr;
};

struct dvfs_arm_table {
	unsigned int	frequency; /* kHz - doesn't need to be in ascending
				    * order */
	unsigned int	cpu_volt;     /* any */

	unsigned int	logic_volt;
};


#define DVFS_MHZ (1000*1000)
#define DVFS_KHZ (1000)

#define DVFS_V (1000*1000)
#define DVFS_MV (1000)
#if 0
#define DVFS_DBG(fmt, args...) printk(KERN_DEBUG "DVFS DBG:\t"fmt, ##args)
#else
#define DVFS_DBG(fmt, args...) {while(0);}
#endif

#define DVFS_ERR(fmt, args...) printk(KERN_ERR "DVFS ERR:\t"fmt, ##args)
#define DVFS_LOG(fmt, args...) printk(KERN_DEBUG "DVFS LOG:\t"fmt, ##args)
#define DVFS_WARNING(fmt, args...) printk(KERN_WARNING "DVFS WARNING:\t"fmt, ##args)



#define DVFS_SET_VOLT_FAILURE 	1
#define DVFS_SET_VOLT_SUCCESS	0


#define dvfs_regulator_get(dev,id) regulator_get((dev),(id))
#define dvfs_regulator_put(regu) regulator_put((regu))
#define dvfs_regulator_set_voltage(regu,min_uV,max_uV) regulator_set_voltage((regu),(min_uV),(max_uV))
#define dvfs_regulator_get_voltage(regu) regulator_get_voltage((regu))
#define dvfs_regulator_set_voltage_time(regu, old_uV, new_uV) regulator_set_voltage_time((regu), (old_uV), (new_uV))
#define dvfs_regulator_set_mode(regu, mode) regulator_set_mode((regu), (mode))
#define dvfs_regulator_get_mode(regu) regulator_get_mode((regu))
#define dvfs_regulator_list_voltage(regu,selector) regulator_list_voltage((regu),(selector))
#define dvfs_regulator_count_voltages(regu) regulator_count_voltages((regu))

#define dvfs_clk_get(a,b) clk_get((a),(b))
#define dvfs_clk_get_rate_kz(a) (clk_get_rate((a))/1000)
#define dvfs_clk_set_rate(a,b) clk_set_rate((a),(b))




typedef void (*avs_init_fn)(void);
typedef u8 (*avs_get_val_fn)(void);
struct avs_ctr_st {
	avs_init_fn		avs_init;
	avs_get_val_fn		avs_get_val;
};




#ifdef CONFIG_DVFS
/***********************************************************************************/
int dvfs_clk_get_ref_volt(struct clk_node *dvfs_clk, int rate_khz,
		struct cpufreq_frequency_table *clk_fv);
struct clk_node *clk_get_dvfs_info(struct clk *clk);
int dvfs_reset_volt(struct vd_node *dvfs_vd);
int dvfs_vd_get_newvolt_byclk(struct clk_node *dvfs_clk);
int dvfs_get_depend_volt(struct clk_node *dvfs_clk, struct vd_node *dvfs_vd_dep, int rate_new);
int dvfs_vd_get_newvolt_bypd(struct vd_node *vd);
int dvfs_scale_volt_bystep(struct vd_node *vd_clk, struct vd_node *vd_dep, int volt_new, int volt_dep_new,
		int cur_clk_biger_than_dep, int cur_dep_biger_than_clk, int new_clk_biger_than_dep, int new_dep_biger_than_clk);
int rk_regist_vd(struct vd_node *vd);
int rk_regist_pd(struct pd_node_lookup *pd_lookup);
int rk_regist_clk(struct clk_node *dvfs_clk);
int rk_regist_depends(struct depend_lookup *dep_node);
struct clk_node *dvfs_get_dvfs_clk_byname(char *name);
int vd_regulator_round_volt(struct vd_node *vd, int volt,int flags);
int dvfs_vd_clk_disable_target(struct clk *clk, int on);
void dvfs_clk_disable_delay_work(struct work_struct *work);

/*********************************if not define dvfs ,the following function is need defined func{}******************************/
int dvfs_vd_clk_set_rate(struct clk *clk, unsigned long rate);
int dvfs_vd_clk_disable(struct clk *clk, int on);
int clk_enable_dvfs(struct clk *clk);
int clk_disable_dvfs(struct clk *clk);
void dvfs_clk_register_set_rate_callback(struct clk *clk, clk_dvfs_target_callback clk_dvfs_target);
struct cpufreq_frequency_table *dvfs_get_freq_volt_table(struct clk *clk);
int dvfs_set_freq_volt_table(struct clk *clk, struct cpufreq_frequency_table *table);
int dvfs_set_depend_table(struct clk *clk, char *vd_name, struct cpufreq_frequency_table *table);
int dvfs_set_arm_logic_volt(struct dvfs_arm_table *dvfs_cpu_logic_table, struct cpufreq_frequency_table *cpu_dvfs_table, struct cpufreq_frequency_table *dep_cpu2core_table);
struct regulator* dvfs_get_regulator(char *regulator_name);
int dvfs_clk_enable_limit(struct clk *clk, unsigned int min_rate, unsigned max_rate);
int dvfs_clk_disable_limit(struct clk *clk);
int dvfs_scale_volt_direct(struct vd_node *vd_clk, int volt_new);
/******************************** inline *******************************/
static inline bool dvfs_support_clk_set_rate(struct clk_node *dvfs_info)
{
	return (dvfs_info&&dvfs_info->enable_dvfs);
}
static inline bool dvfs_support_clk_disable(struct clk_node *dvfs_info)
{
	return (dvfs_info&&dvfs_info->disable_ctr&&dvfs_info->enable_dvfs);
}
/********************************avs*******************************/
void avs_init(void);
void avs_init_val_get(int index,int vol,char *s);
int avs_set_scal_val(u8 avs_base);
void avs_board_init(struct avs_ctr_st *data);

#else
static inline bool dvfs_support_clk_set_rate(struct clk_node *dvfs_info) { return 0; }
static inline bool dvfs_support_clk_disable(struct clk_node *dvfs_info) { return 0; }

static inline int dvfs_vd_clk_set_rate(struct clk *clk, unsigned long rate) { return 0; }
static inline int dvfs_vd_clk_disable(struct clk *clk, int on) { return 0; }
static inline int clk_enable_dvfs(struct clk *clk) { return 0; }
static inline int clk_disable_dvfs(struct clk *clk) { return 0; }
static inline void dvfs_clk_register_set_rate_callback(struct clk *clk, clk_dvfs_target_callback clk_dvfs_target) {}
static inline struct cpufreq_frequency_table *dvfs_get_freq_volt_table(struct clk *clk) { return NULL; }
static inline int dvfs_set_freq_volt_table(struct clk *clk, struct cpufreq_frequency_table *table) { return 0; }
static inline int dvfs_set_depend_table(struct clk *clk, char *vd_name, struct cpufreq_frequency_table *table) {return 0;}
static inline int dvfs_set_arm_logic_volt(struct dvfs_arm_table *dvfs_cpu_logic_table, struct cpufreq_frequency_table *cpu_dvfs_table, struct cpufreq_frequency_table *dep_cpu2core_table){ return 0; }
static inline struct regulator* dvfs_get_regulator(char *regulator_name){ return NULL; }
static inline int dvfs_clk_enable_limit(struct clk *clk, unsigned int min_rate, unsigned max_rate){ return 0; }
static inline int dvfs_clk_disable_limit(struct clk *clk){ return 0; };
static inline int dvfs_scale_volt_direct(struct vd_node *vd_clk, int volt_new){ return 0; };

static inline void avs_init(void){};
static inline void avs_init_val_get(int index, int vol, char *s){};
static inline int avs_set_scal_val(u8 avs_base){ return 0; };
static inline void avs_board_init(struct avs_ctr_st *data){};
#endif

#endif
