/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QCOM_LPM_H__
#define __QCOM_LPM_H__

#define MAX_LPM_CPUS		8
#define MAXSAMPLES		5
#define PRED_TIMER_ADD		100
#define PRED_PREMATURE_CNT	3
#define PRED_REF_STDDEV		500
#define CLUST_SMPL_INVLD_TIME	40000
#define MAX_CLUSTER_STATES	4

extern bool sleep_disabled;
extern bool prediction_disabled;

struct qcom_cluster_node {
	struct lpm_cluster *cluster;
	struct kobject *kobj;
	int state_idx;
	struct kobj_attribute disable_attr;
	struct attribute_group *attr_group;
	struct attribute **attrs;
};

struct history_lpm {
	int mode[MAXSAMPLES];
	uint32_t resi[MAXSAMPLES];
	int nsamp;
	uint32_t samples_idx;
};

struct history_ipi {
	uint32_t interval[MAXSAMPLES];
	uint32_t current_ptr;
	ktime_t cpu_idle_resched_ts;
};

struct lpm_cpu {
	int cpu;
	int enable;
	int last_idx;
	struct notifier_block nb;
	struct cpuidle_driver *drv;
	struct cpuidle_device *dev;
	ktime_t next_wakeup;
	uint64_t predicted;
	uint32_t history_invalid;
	bool predict_started;
	bool htmr_wkup;
	struct hrtimer histtimer;
	struct hrtimer biastimer;
	struct history_lpm lpm_history;
	struct history_ipi ipi_history;
	ktime_t now;
	uint64_t bias;
	int64_t next_pred_time;
	uint32_t pred_type;
	bool ipi_pending;
	spinlock_t lock;
};

struct cluster_history {
	uint64_t residency;
	int mode;
	uint64_t entry_time;
};

struct lpm_cluster {
	struct device *dev;
	uint32_t samples_idx;
	bool history_invalid;
	bool htmr_wkup;
	int entry_idx;
	int nsamp;
	struct cluster_history history[MAXSAMPLES];
	struct generic_pm_domain *genpd;
	struct qcom_cluster_node *dev_node[MAX_CLUSTER_STATES];
	struct kobject *dev_kobj;
	struct notifier_block genpd_nb;
	struct work_struct work;
	struct hrtimer histtimer;
	ktime_t entry_time;
	ktime_t next_wakeup;
	ktime_t pred_wakeup;
	ktime_t now;
	ktime_t cpu_next_wakeup[MAX_LPM_CPUS];
	bool state_allowed[MAX_CLUSTER_STATES];
	struct list_head list;
	spinlock_t lock;
	bool predicted;
	bool initialized;
};

struct cluster_governor {
	void (*select)(struct lpm_cpu *cpu_gov);
	void (*enable)(void);
	void (*disable)(void);
	void (*reflect)(void);
};

DECLARE_PER_CPU(struct lpm_cpu, lpm_cpu_data);

int qcom_cluster_lpm_governor_init(void);
void qcom_cluster_lpm_governor_deinit(void);
void update_cluster_select(struct lpm_cpu *cpu_gov);
void clear_cpu_predict_history(void);
int create_global_sysfs_nodes(void);
int create_cluster_sysfs_nodes(struct lpm_cluster *cluster_gov);
void register_cluster_governor_ops(struct cluster_governor *ops);
void remove_global_sysfs_nodes(void);
void remove_cluster_sysfs_nodes(struct lpm_cluster *cluster_gov);

#endif /* __QCOM_LPM_H__ */
