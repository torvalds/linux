/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2020 Intel Corporation */
#ifndef ADF_CFG_H_
#define ADF_CFG_H_

#include <linux/list.h>
#include <linux/rwsem.h>
#include <linux/debugfs.h>
#include "adf_accel_devices.h"
#include "adf_cfg_common.h"
#include "adf_cfg_strings.h"

struct adf_cfg_key_val {
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	char val[ADF_CFG_MAX_VAL_LEN_IN_BYTES];
	enum adf_cfg_val_type type;
	struct list_head list;
};

struct adf_cfg_section {
	char name[ADF_CFG_MAX_SECTION_LEN_IN_BYTES];
	struct list_head list;
	struct list_head param_head;
};

struct adf_cfg_device_data {
	struct list_head sec_list;
	struct dentry *debug;
	struct rw_semaphore lock;
};

int adf_cfg_dev_add(struct adf_accel_dev *accel_dev);
void adf_cfg_dev_remove(struct adf_accel_dev *accel_dev);
int adf_cfg_section_add(struct adf_accel_dev *accel_dev, const char *name);
void adf_cfg_del_all(struct adf_accel_dev *accel_dev);
int adf_cfg_add_key_value_param(struct adf_accel_dev *accel_dev,
				const char *section_name,
				const char *key, const void *val,
				enum adf_cfg_val_type type);
int adf_cfg_get_param_value(struct adf_accel_dev *accel_dev,
			    const char *section, const char *name, char *value);

#endif
