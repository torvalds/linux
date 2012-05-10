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

typedef int (*vd_dvfs_target_callback)(struct clk *clk, unsigned long rate);

typedef int (*dvfs_set_rate_callback)(struct clk *clk, unsigned long rate);

typedef int (*clk_dvfs_target_callback)(struct clk *clk, unsigned long rate,
                                        dvfs_set_rate_callback set_rate);

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

struct vd_node {
	char			*name;
	char			*regulator_name;
	int			cur_volt;
	struct regulator	*regulator;
	struct list_head	node;
	struct list_head	pd_list;
	struct list_head	req_volt_list;
	//struct mutex		dvfs_mutex;
	vd_dvfs_target_callback	vd_dvfs_target;
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
	struct clk 		*clk;
	struct pds_list		*pds;
	struct vd_node		*vd;
	struct list_head	depend_list;
	struct notifier_block	*dvfs_nb;
	struct cpufreq_frequency_table	*dvfs_table;
	clk_dvfs_target_callback	clk_dvfs_target;
};

struct dvfs_arm_table {
	unsigned int	frequency; /* kHz - doesn't need to be in ascending
				    * order */
	unsigned int	cpu_volt;     /* any */

	unsigned int	logic_volt;
};

#ifdef CONFIG_DVFS
int rk30_dvfs_init(void);
int is_support_dvfs(struct clk_node *dvfs_info);
int dvfs_set_rate(struct clk *clk, unsigned long rate);
int clk_enable_dvfs(struct clk *clk);
int clk_disable_dvfs(struct clk *clk);
void dvfs_clk_register_set_rate_callback(struct clk *clk, clk_dvfs_target_callback clk_dvfs_target);
struct cpufreq_frequency_table *dvfs_get_freq_volt_table(struct clk *clk);
int dvfs_set_freq_volt_table(struct clk *clk, struct cpufreq_frequency_table *table);
int dvfs_set_depend_table(struct clk *clk, char *vd_name, struct cpufreq_frequency_table *table);
int dvfs_set_arm_logic_volt(struct dvfs_arm_table *dvfs_cpu_logic_table, struct cpufreq_frequency_table *cpu_dvfs_table, struct cpufreq_frequency_table *dep_cpu2core_table);
#else
static inline int rk30_dvfs_init(void) { return 0; }
static inline int is_support_dvfs(struct clk_node *dvfs_info) { return 0; }
static inline int dvfs_set_rate(struct clk *clk, unsigned long rate) { return 0; }
static inline int clk_enable_dvfs(struct clk *clk) { return 0; }
static inline int clk_disable_dvfs(struct clk *clk) { return 0; }
static inline void dvfs_clk_register_set_rate_callback(struct clk *clk, clk_dvfs_target_callback clk_dvfs_target) {}
static inline struct cpufreq_frequency_table *dvfs_get_freq_volt_table(struct clk *clk) { return NULL; }
static inline int dvfs_set_freq_volt_table(struct clk *clk, struct cpufreq_frequency_table *table) { return 0; }
static inline int dvfs_set_depend_table(struct clk *clk, char *vd_name, struct cpufreq_frequency_table *table) {return 0;}
int dvfs_set_arm_logic_volt(struct dvfs_arm_table *dvfs_cpu_logic_table, struct cpufreq_frequency_table *cpu_dvfs_table, struct cpufreq_frequency_table *dep_cpu2core_table){ return 0; }

#endif

#endif
