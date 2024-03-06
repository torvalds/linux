/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_GSC_TYPES_H_
#define _XE_GSC_TYPES_H_

#include <linux/workqueue.h>

#include "xe_uc_fw_types.h"

struct xe_bo;
struct xe_exec_queue;

/**
 * struct xe_gsc - GSC
 */
struct xe_gsc {
	/** @fw: Generic uC firmware management */
	struct xe_uc_fw fw;

	/** @security_version: SVN found in the fetched blob */
	u32 security_version;

	/** @private: Private data for use by the GSC FW */
	struct xe_bo *private;

	/** @q: Default queue used for submissions to GSC FW */
	struct xe_exec_queue *q;

	/** @wq: workqueue to handle jobs for delayed load and proxy handling */
	struct workqueue_struct *wq;

	/** @work: delayed load and proxy handling work */
	struct work_struct work;
};

#endif
