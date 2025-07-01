/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2025 Intel Corporation */
#ifndef ADF_BANK_STATE_H_
#define ADF_BANK_STATE_H_

#include <linux/types.h>

struct adf_accel_dev;
struct bank_state;

int adf_bank_state_restore(struct adf_accel_dev *accel_dev, u32 bank_number,
			   struct bank_state *state);
int adf_bank_state_save(struct adf_accel_dev *accel_dev, u32 bank_number,
			struct bank_state *state);

#endif
