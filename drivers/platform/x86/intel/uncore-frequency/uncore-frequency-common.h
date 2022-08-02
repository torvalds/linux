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
 * @name:		Sysfs entry name for this instance
 * @uncore_attr_group:	Attribute group storage
 * @max_freq_khz_dev_attr: Storage for device attribute max_freq_khz
 * @mix_freq_khz_dev_attr: Storage for device attribute min_freq_khz
 * @initial_max_freq_khz_dev_attr: Storage for device attribute initial_max_freq_khz
 * @initial_min_freq_khz_dev_attr: Storage for device attribute initial_min_freq_khz
 * @current_freq_khz_dev_attr: Storage for device attribute current_freq_khz
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
	char name[32];

	struct attribute_group uncore_attr_group;
	struct device_attribute max_freq_khz_dev_attr;
	struct device_attribute min_freq_khz_dev_attr;
	struct device_attribute initial_max_freq_khz_dev_attr;
	struct device_attribute initial_min_freq_khz_dev_attr;
	struct device_attribute current_freq_khz_dev_attr;
	struct attribute *uncore_attrs[6];
};

int uncore_freq_common_init(int (*read_control_freq)(struct uncore_data *data, unsigned int *min, unsigned int *max),
			     int (*write_control_freq)(struct uncore_data *data, unsigned int input, unsigned int min_max),
			     int (*uncore_read_freq)(struct uncore_data *data, unsigned int *freq));
void uncore_freq_common_exit(void);
int uncore_freq_add_entry(struct uncore_data *data, int cpu);
void uncore_freq_remove_die_entry(struct uncore_data *data);

#endif
