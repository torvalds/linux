/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QCOM_SIMPLE_LPM_H__
#define __QCOM_SIMPLE_LPM_H__

#define MAX_CLUSTER_STATES	4

extern bool simple_sleep_disabled;

struct qcom_simple_cluster_node {
	struct simple_lpm_cluster *cluster;
	struct kobject *kobj;
	int state_idx;
	struct kobj_attribute disable_attr;
	struct attribute_group *attr_group;
	struct attribute **attrs;
};

struct simple_lpm_cpu {
	int cpu;
	int enable;
	int last_idx;
	struct notifier_block nb;
	struct cpuidle_driver *drv;
	struct cpuidle_device *dev;
	ktime_t next_wakeup;
	ktime_t now;
};

struct simple_lpm_cluster {
	struct device *dev;
	struct generic_pm_domain *genpd;
	struct qcom_simple_cluster_node *dev_node[MAX_CLUSTER_STATES];
	struct kobject *dev_kobj;
	struct notifier_block genpd_nb;
	bool state_allowed[MAX_CLUSTER_STATES];
	struct list_head list;
	spinlock_t lock;
	bool initialized;
};

struct simple_cluster_governor {
	void (*select)(struct simple_lpm_cpu *cpu_gov);
	void (*enable)(void);
	void (*disable)(void);
	void (*reflect)(void);
};

DECLARE_PER_CPU(struct simple_lpm_cpu, lpm_cpu_data);

extern struct list_head cluster_dev_list;
extern u64 cur_div;
extern u64 cluster_cur_div;

void update_simple_cluster_select(struct simple_lpm_cpu *cpu_gov);
int create_simple_gov_global_sysfs_nodes(void);
void remove_simple_gov_global_sysfs_nodes(void);
void register_cluster_simple_governor_ops(struct simple_cluster_governor *ops);
void unregister_cluster_simple_governor_ops(struct simple_cluster_governor *ops);
void remove_simple_cluster_sysfs_nodes(struct simple_lpm_cluster *simple_cluster_gov);
int create_simple_cluster_sysfs_nodes(struct simple_lpm_cluster *simple_cluster_gov);
int qcom_cluster_lpm_simple_governor_init(void);
void qcom_cluster_lpm_simple_governor_deinit(void);
#endif /* __QCOM_SIMPLE_LPM_H__ */
