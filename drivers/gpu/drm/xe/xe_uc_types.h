/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_UC_TYPES_H_
#define _XE_UC_TYPES_H_

#include "xe_gsc_types.h"
#include "xe_guc_types.h"
#include "xe_huc_types.h"
#include "xe_wopcm_types.h"

/**
 * struct xe_uc - XE micro controllers
 */
struct xe_uc {
	/** @guc: Graphics micro controller */
	struct xe_guc guc;
	/** @huc: HuC */
	struct xe_huc huc;
	/** @gsc: Graphics Security Controller */
	struct xe_gsc gsc;
	/** @wopcm: WOPCM */
	struct xe_wopcm wopcm;
};

#endif
