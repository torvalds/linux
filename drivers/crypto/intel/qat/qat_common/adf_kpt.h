/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2026 Intel Corporation */
#ifndef ADF_KPT_H_
#define ADF_KPT_H_

#include <linux/types.h>

#define GET_KPT_CFG_DATA(accel_dev) (&(accel_dev)->hw_device->kpt_data)
#define GET_KPT_USER_DATA(accel_dev) (&(accel_dev)->hw_device->kpt_data.user_input)

struct adf_accel_dev;

struct adf_kpt_interface_data {
	bool enable;
	bool swk_shared;
	unsigned int swk_cnt_per_fn;
	unsigned int swk_cnt_per_pasid;
	unsigned int swk_max_ttl;
};

struct adf_kpt_hw_data {
	unsigned int max_swk_cnt_per_fn_pasid;
	unsigned int max_swk_ttl;
	struct adf_kpt_interface_data user_input;
};

int adf_enable_kpt(struct adf_accel_dev *accel_dev);

#endif /* ADF_KPT_H_ */
