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
 * @uncore_attr_group:	Attribute group storage
 * @max_freq_khz_kobj_attr: Storage for kobject attribute max_freq_khz
 * @mix_freq_khz_kobj_attr: Storage for kobject attribute min_freq_khz
 * @initial_max_freq_khz_kobj_attr: Storage for kobject attribute initial_max_freq_khz
 * @initial_min_freq_khz_kobj_attr: Storage for kobject attribute initial_min_freq_khz
 * @current_freq_khz_kobj_attr: Storage for kobject attribute current_freq_khz
 * @domain_id_kobj_attr: Storage for kobject attribute domain_id
 * @fabric_cluster_id_kobj_attr: Storage for kobject attribute fabric_cluster_id
 * @package_id_kobj_attr: Storage for kobject attribute package_id
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

	struct attribute_group uncore_attr_group;
	struct kobj_attribute max_freq_khz_kobj_attr;
	struct kobj_attribute min_freq_khz_kobj_attr;
	struct kobj_attribute initial_max_freq_khz_kobj_attr;
	struct kobj_attribute initial_min_freq_khz_kobj_attr;
	struct kobj_attribute current_freq_khz_kobj_attr;
	struct kobj_attribute domain_id_kobj_attr;
	struct kobj_attribute fabric_cluster_id_kobj_attr;
	struct kobj_attribute package_id_kobj_attr;
	struct attribute *uncore_attrs[9];
};

#define UNCORE_DOMAIN_ID_INVALID	-1

int uncore_freq_common_init(int (*read_control_freq)(struct uncore_data *data, unsigned int *min, unsigned int *max),
			     int (*write_control_freq)(struct uncore_data *data, unsigned int input, unsigned int min_max),
			     int (*uncore_read_freq)(struct uncore_data *data, unsigned int *freq));
void uncore_freq_common_exit(void);
int uncore_freq_add_entry(struct uncore_data *data, int cpu);
void uncore_freq_remove_die_entry(struct uncore_data *data);

#endif
