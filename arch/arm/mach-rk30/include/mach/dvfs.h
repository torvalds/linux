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
 * @vd_name:	Voltage Domain's Name
 * @cur_volt:	Voltage Domain's Current Voltage
 * @vd_list:	Point of he Voltage Domain List Node
 * @pd_list:	Head of Power Domain List Belongs to This Voltage Domain
 * @vd_voltreq_list:	Head of Voltage Request List for Voltage Domain
 */

struct vd_node {
	char	*name;
	char	*regulator_name;
	int		cur_volt;
	struct regulator	*regulator;
	struct mutex		dvfs_mutex;
	struct list_head	node;
	struct list_head	pd_list;
	vd_dvfs_target_callback	vd_dvfs_target;
};
struct vd_node_lookup {
	struct vd_node	*vd;
	char			*regulator_name;
};
/**
 * struct pd_node:	To Store All Power Domains' info per Voltage Domain
 * @pd_name:	Power Domain's Name
 * @cur_volt:	Power Domain's Current Voltage
 * @pd_list:	Point of the Power Domain List Node
 * @clk_list:	Head of Power Domain's Clocks List
 * @pd_status:		If The Power Domain On:	1 means on, 0 means off
 */
struct pd_node {
	char	*name;
	int		cur_volt;
	unsigned char	pd_status;
	struct vd_node	*vd;
	struct  clk		*pd_clk;
	struct list_head	node;
	struct list_head	clk_list;
};

struct pd_node_lookup {
	struct pd_node* pd;
};

struct clk_list{
	struct clk_node *dvfs_clk;
	struct list_head node;
};

struct pds_list {
	struct clk_list clk_list;
	struct pd_node *pd;
};

struct clk_node {
	char	*name;
	int		set_freq;//khz
	int		set_volt;
	int		enable_dvfs;
	struct clk *ck;
	struct pds_list		*pds;
	struct vd_node		*vd;
	struct cpufreq_frequency_table	*dvfs_table;
	struct notifier_block *dvfs_nb;
	struct list_head	node;
	clk_dvfs_target_callback clk_dvfs_target;
};

#ifdef CONFIG_DVFS
int rk30_dvfs_init(void);
int is_support_dvfs(struct clk_node *dvfs_info);
int dvfs_set_rate(struct clk *clk, unsigned long rate);
int clk_enable_dvfs(struct clk *clk);
int clk_disable_dvfs(struct clk *clk);
int cpufreq_dvfs_init(struct clk *ck, struct cpufreq_frequency_table **table, clk_dvfs_target_callback clk_dvfs_target);
void dvfs_clk_register_set_rate_callback(struct clk *clk, clk_dvfs_target_callback clk_dvfs_target);
struct cpufreq_frequency_table *dvfs_get_freq_volt_table(struct clk *clk);
int dvfs_set_freq_volt_table(struct clk *clk, struct cpufreq_frequency_table *table);
int rk30_dvfs_init(void);
#else
int rk30_dvfs_init(void){};
int is_support_dvfs(struct clk_node *dvfs_info){};
int dvfs_set_rate(struct clk *clk, unsigned long rate){};
int clk_enable_dvfs(struct clk *clk){};
int clk_disable_dvfs(struct clk *clk){};
int cpufreq_dvfs_init(struct clk *ck, struct cpufreq_frequency_table **table, clk_dvfs_target_callback clk_dvfs_target){};
void dvfs_clk_register_set_rate_callback(struct clk *clk, clk_dvfs_target_callback clk_dvfs_target){};
struct cpufreq_frequency_table *dvfs_get_freq_volt_table(struct clk *clk){};
int dvfs_set_freq_volt_table(struct clk *clk, struct cpufreq_frequency_table *table);
int rk30_dvfs_init(void){};




#endif


#endif
