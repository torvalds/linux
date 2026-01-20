/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Intel Uncore Frequency Control: Common defines and prototypes
 * Copyright (c) 2022, Intel Corporation.
 * All rights reserved.
 *
 */

#ifndef __INTEL_UNCORE_FREQ_COMMON_H
#define __INTEL_UNCORE_FREQ_COMMON_H

#include <linux/device.h>

/*
 * Define uncore agents, which are under uncore frequency control.
 * Defined in the same order as specified in the TPMI UFS Specifications.
 * It is possible that there are common uncore frequency control to more than
 * one hardware agents. So, these defines are used as a bit mask.
*/

#define AGENT_TYPE_CORE		0x01
#define	AGENT_TYPE_CACHE	0x02
#define	AGENT_TYPE_MEMORY	0x04
#define	AGENT_TYPE_IO		0x08

/**
 * struct uncore_data - Encapsulate all uncore data
 * @stored_uncore_data: Last user changed MSR 620 value, which will be restored
 *			on system resume.
 * @initial_min_freq_khz: Sampled minimum uncore frequency at driver init
 * @initial_max_freq_khz: Sampled maximum uncore frequency at driver init
 * @control_cpu:	Designated CPU for a die to read/write
 * @valid:		Mark the data valid/invalid
 * @package_id:	Package id for this instance
 * @die_id:		Die id for this instance
 * @domain_id:		Power domain id for this instance
 * @cluster_id:		cluster id in a domain
 * @instance_id:	Unique instance id to append to directory name
 * @name:		Sysfs entry name for this instance
 * @agent_type_mask:	Bit mask of all hardware agents for this domain
 * @uncore_attr_group:	Attribute group storage
 * @max_freq_khz_kobj_attr: Storage for kobject attribute max_freq_khz
 * @min_freq_khz_kobj_attr: Storage for kobject attribute min_freq_khz
 * @initial_max_freq_khz_kobj_attr: Storage for kobject attribute initial_max_freq_khz
 * @initial_min_freq_khz_kobj_attr: Storage for kobject attribute initial_min_freq_khz
 * @current_freq_khz_kobj_attr: Storage for kobject attribute current_freq_khz
 * @domain_id_kobj_attr: Storage for kobject attribute domain_id
 * @fabric_cluster_id_kobj_attr: Storage for kobject attribute fabric_cluster_id
 * @package_id_kobj_attr: Storage for kobject attribute package_id
 * @elc_low_threshold_percent_kobj_attr:
 *		Storage for kobject attribute elc_low_threshold_percent
 * @elc_high_threshold_percent_kobj_attr:
 *		Storage for kobject attribute elc_high_threshold_percent
 * @elc_high_threshold_enable_kobj_attr:
 *		Storage for kobject attribute elc_high_threshold_enable
 * @elc_floor_freq_khz_kobj_attr: Storage for kobject attribute elc_floor_freq_khz
 * @agent_types_kobj_attr: Storage for kobject attribute agent_type
 * @die_id_kobj_attr:	Attribute storage for die_id information
 * @uncore_attrs:	Attribute storage for group creation
 *
 * This structure is used to encapsulate all data related to uncore sysfs
 * settings for a die/package.
 */
struct uncore_data {
	u64 stored_uncore_data;
	u32 initial_min_freq_khz;
	u32 initial_max_freq_khz;
	int control_cpu;
	bool valid;
	int package_id;
	int die_id;
	int domain_id;
	int cluster_id;
	int instance_id;
	char name[32];
	u16  agent_type_mask;

	struct attribute_group uncore_attr_group;
	struct kobj_attribute max_freq_khz_kobj_attr;
	struct kobj_attribute min_freq_khz_kobj_attr;
	struct kobj_attribute initial_max_freq_khz_kobj_attr;
	struct kobj_attribute initial_min_freq_khz_kobj_attr;
	struct kobj_attribute current_freq_khz_kobj_attr;
	struct kobj_attribute domain_id_kobj_attr;
	struct kobj_attribute fabric_cluster_id_kobj_attr;
	struct kobj_attribute package_id_kobj_attr;
	struct kobj_attribute elc_low_threshold_percent_kobj_attr;
	struct kobj_attribute elc_high_threshold_percent_kobj_attr;
	struct kobj_attribute elc_high_threshold_enable_kobj_attr;
	struct kobj_attribute elc_floor_freq_khz_kobj_attr;
	struct kobj_attribute agent_types_kobj_attr;
	struct kobj_attribute die_id_kobj_attr;
	struct attribute *uncore_attrs[15];
};

#define UNCORE_DOMAIN_ID_INVALID	-1

enum uncore_index {
	UNCORE_INDEX_MIN_FREQ,
	UNCORE_INDEX_MAX_FREQ,
	UNCORE_INDEX_CURRENT_FREQ,
	UNCORE_INDEX_EFF_LAT_CTRL_LOW_THRESHOLD,
	UNCORE_INDEX_EFF_LAT_CTRL_HIGH_THRESHOLD,
	UNCORE_INDEX_EFF_LAT_CTRL_HIGH_THRESHOLD_ENABLE,
	UNCORE_INDEX_EFF_LAT_CTRL_FREQ,
	UNCORE_INDEX_DIE_ID,
};

int uncore_freq_common_init(int (*read)(struct uncore_data *data, unsigned int *value,
					enum uncore_index index),
			    int (*write)(struct uncore_data *data, unsigned int input,
					 enum uncore_index index));
void uncore_freq_common_exit(void);
int uncore_freq_add_entry(struct uncore_data *data, int cpu);
void uncore_freq_remove_die_entry(struct uncore_data *data);

#endif
