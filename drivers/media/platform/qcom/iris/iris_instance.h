/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_INSTANCE_H__
#define __IRIS_INSTANCE_H__

#include "iris_core.h"

/**
 * struct iris_inst - holds per video instance parameters
 *
 * @core: pointer to core structure
 * @ctx_q_lock: lock to serialize queues related ioctls
 * @lock: lock to seralise forward and reverse threads
 * @fh: reference of v4l2 file handler
 * @m2m_dev:	a reference to m2m device structure
 * @m2m_ctx:	a reference to m2m context structure
 */

struct iris_inst {
	struct iris_core		*core;
	struct mutex			ctx_q_lock;/* lock to serialize queues related ioctls */
	struct mutex			lock; /* lock to serialize forward and reverse threads */
	struct v4l2_fh			fh;
	struct v4l2_m2m_dev		*m2m_dev;
	struct v4l2_m2m_ctx		*m2m_ctx;
};

#endif
