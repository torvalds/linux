/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2021 Intel Corporation
 */

#ifndef _ABI_GUC_ERRORS_ABI_H
#define _ABI_GUC_ERRORS_ABI_H

enum intel_guc_response_status {
	INTEL_GUC_RESPONSE_STATUS_SUCCESS = 0x0,
	INTEL_GUC_RESPONSE_STATUS_GENERIC_FAIL = 0xF000,
};

#endif /* _ABI_GUC_ERRORS_ABI_H */
