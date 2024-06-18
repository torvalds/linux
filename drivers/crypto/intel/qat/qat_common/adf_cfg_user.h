/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2020 Intel Corporation */
#ifndef ADF_CFG_USER_H_
#define ADF_CFG_USER_H_

#include "adf_cfg_common.h"
#include "adf_cfg_strings.h"

struct adf_user_cfg_key_val {
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	char val[ADF_CFG_MAX_VAL_LEN_IN_BYTES];
	union {
		struct adf_user_cfg_key_val *next;
		__u64 padding3;
	};
	enum adf_cfg_val_type type;
} __packed;

struct adf_user_cfg_section {
	char name[ADF_CFG_MAX_SECTION_LEN_IN_BYTES];
	union {
		struct adf_user_cfg_key_val *params;
		__u64 padding1;
	};
	union {
		struct adf_user_cfg_section *next;
		__u64 padding3;
	};
} __packed;

struct adf_user_cfg_ctl_data {
	union {
		struct adf_user_cfg_section *config_section;
		__u64 padding;
	};
	__u8 device_id;
} __packed;
#endif
