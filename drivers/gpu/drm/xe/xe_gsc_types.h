/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_GSC_TYPES_H_
#define _XE_GSC_TYPES_H_

#include "xe_uc_fw_types.h"

/**
 * struct xe_gsc - GSC
 */
struct xe_gsc {
	/** @fw: Generic uC firmware management */
	struct xe_uc_fw fw;

	/** @security_version: SVN found in the fetched blob */
	u32 security_version;
};

#endif
