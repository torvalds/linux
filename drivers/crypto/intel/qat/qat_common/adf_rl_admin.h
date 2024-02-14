/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2023 Intel Corporation */

#ifndef ADF_RL_ADMIN_H_
#define ADF_RL_ADMIN_H_

#include <linux/types.h>

#include "adf_rl.h"

int adf_rl_send_admin_init_msg(struct adf_accel_dev *accel_dev,
			       struct rl_slice_cnt *slices_int);
int adf_rl_send_admin_add_update_msg(struct adf_accel_dev *accel_dev,
				     struct rl_sla *sla, bool is_update);
int adf_rl_send_admin_delete_msg(struct adf_accel_dev *accel_dev, u16 node_id,
				 u8 node_type);

#endif /* ADF_RL_ADMIN_H_ */
