/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2023 Intel Corporation */
#ifndef _ADF_CFG_SERVICES_H_
#define _ADF_CFG_SERVICES_H_

#include "adf_cfg_strings.h"

struct adf_accel_dev;

enum adf_services {
	SVC_ASYM = 0,
	SVC_SYM,
	SVC_DC,
	SVC_DCC,
	SVC_BASE_COUNT
};

enum adf_composed_services {
	SVC_SYM_ASYM = SVC_BASE_COUNT,
	SVC_SYM_DC,
	SVC_ASYM_DC,
};

enum {
	ADF_ONE_SERVICE = 1,
	ADF_TWO_SERVICES,
	ADF_THREE_SERVICES,
};

#define MAX_NUM_CONCURR_SVC	ADF_THREE_SERVICES

int adf_parse_service_string(struct adf_accel_dev *accel_dev, const char *in,
			     size_t in_len, char *out, size_t out_len);
int adf_get_service_enabled(struct adf_accel_dev *accel_dev);

#endif
