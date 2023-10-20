/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2023 Intel Corporation */
#ifndef _ADF_CFG_SERVICES_H_
#define _ADF_CFG_SERVICES_H_

#include "adf_cfg_strings.h"

enum adf_services {
	SVC_CY = 0,
	SVC_CY2,
	SVC_DC,
	SVC_DCC,
	SVC_SYM,
	SVC_ASYM,
	SVC_DC_ASYM,
	SVC_ASYM_DC,
	SVC_DC_SYM,
	SVC_SYM_DC,
	SVC_COUNT
};

extern const char *const adf_cfg_services[SVC_COUNT];

#endif
