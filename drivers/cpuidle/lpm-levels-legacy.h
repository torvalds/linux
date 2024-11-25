/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Copyright (c) 2014-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <soc/qcom/pm-legacy.h>
#include <soc/qcom/spm.h>

#define NR_LPM_LEVELS 8

extern bool use_psci;

struct lpm_lookup_table {
	uint32_t modes;
	const char *mode_name;
};

struct power_params {
	uint32_t latency_us;		/* Enter + Exit latency */
	uint32_t ss_power;		/* Steady state power */
	uint32_t energy_overhead;	/* Enter + exit over head */
	uint32_t time_overhead_us;	/* Enter + exit overhead */
	uint32_t residencies[NR_LPM_LEVELS];
	uint32_t max_residency;
};

struct lpm_cpu_level {
	const char *name;
	enum msm_pm_sleep_mode mode;
	bool use_bc_timer;
	struct power_params pwr;
	unsigned int psci_id;
	bool is_reset;
	bool jtag_save_restore;
	bool hyp_psci;
	int reset_level;
};

struct lpm_cpu {
	struct lpm_cpu_level levels[NR_LPM_LEVELS];
	int nlevels;
	unsigned int psci_mode_shift;
	unsigned int psci_mode_mask;
	struct lpm_cluster *parent;
};

struct lpm_level_avail {
	bool idle_enabled;
	bool suspend_enabled;
	struct kobject *kobj;
	struct kobj_attribute idle_enabled_attr;
	struct kobj_attribute suspend_enabled_attr;
	void *data;
	int idx;
	bool cpu_node;
};

struct lpm_cluster_level {
	const char *level_name;
	int *mode;			/* SPM mode to enter */
	int min_child_level;
	struct cpumask num_cpu_votes;
	struct power_params pwr;
	bool notify_rpm;
	bool disable_dynamic_routing;
	bool sync_level;
	bool last_core_only;
	struct lpm_level_avail available;
	unsigned int psci_id;
	bool is_reset;
	int reset_level;
	bool no_cache_flush;
};

struct low_power_ops {
	struct msm_spm_device *spm;
	int (*set_mode)(struct low_power_ops *ops, int mode,
				struct lpm_cluster_level *level);
	enum msm_pm_l2_scm_flag tz_flag;
};

struct lpm_cluster {
	struct list_head list;
	struct list_head child;
	const char *cluster_name;
	const char **name;
	unsigned long aff_level; /* Affinity level of the node */
	struct low_power_ops *lpm_dev;
	int ndevices;
	struct lpm_cluster_level levels[NR_LPM_LEVELS];
	int nlevels;
	enum msm_pm_l2_scm_flag l2_flag;
	int min_child_level;
	int default_level;
	int last_level;
	struct lpm_cpu *cpu;
	struct cpuidle_driver *drv;
	spinlock_t sync_lock;
	struct cpumask child_cpus;
	struct cpumask num_children_in_sync;
	struct lpm_cluster *parent;
	struct lpm_stats *stats;
	unsigned int psci_mode_shift;
	unsigned int psci_mode_mask;
	bool no_saw_devices;
};

int set_l2_mode(struct low_power_ops *ops, int mode,
				struct lpm_cluster_level *level);
int set_system_mode(struct low_power_ops *ops, int mode,
				struct lpm_cluster_level *level);
int set_l3_mode(struct low_power_ops *ops, int mode,
				struct lpm_cluster_level *level);
void lpm_suspend_wake_time(uint64_t wakeup_time);

struct lpm_cluster *lpm_of_parse_cluster(struct platform_device *pdev);
void free_cluster_node(struct lpm_cluster *cluster);
void cluster_dt_walkthrough(struct lpm_cluster *cluster);

int create_cluster_lvl_nodes(struct lpm_cluster *p, struct kobject *kobj);
bool lpm_cpu_mode_allow(unsigned int cpu,
		unsigned int mode, bool from_idle);
bool lpm_cluster_mode_allow(struct lpm_cluster *cluster,
		unsigned int mode, bool from_idle);
uint32_t *get_per_cpu_max_residency(int cpu);
extern struct lpm_cluster *lpm_root_node;
