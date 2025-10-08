/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2025 Intel Corporation */
#ifndef ADF_BANK_STATE_H_
#define ADF_BANK_STATE_H_

#include <linux/types.h>

struct adf_accel_dev;

struct ring_config {
	u64 base;
	u32 config;
	u32 head;
	u32 tail;
	u32 reserved0;
};

struct adf_bank_state {
	u32 ringstat0;
	u32 ringstat1;
	u32 ringuostat;
	u32 ringestat;
	u32 ringnestat;
	u32 ringnfstat;
	u32 ringfstat;
	u32 ringcstat0;
	u32 ringcstat1;
	u32 ringcstat2;
	u32 ringcstat3;
	u32 iaintflagen;
	u32 iaintflagreg;
	u32 iaintflagsrcsel0;
	u32 iaintflagsrcsel1;
	u32 iaintcolen;
	u32 iaintcolctl;
	u32 iaintflagandcolen;
	u32 ringexpstat;
	u32 ringexpintenable;
	u32 ringsrvarben;
	u32 reserved0;
	struct ring_config rings[ADF_ETR_MAX_RINGS_PER_BANK];
};

int adf_bank_state_restore(struct adf_accel_dev *accel_dev, u32 bank_number,
			   struct adf_bank_state *state);
int adf_bank_state_save(struct adf_accel_dev *accel_dev, u32 bank_number,
			struct adf_bank_state *state);

#endif
