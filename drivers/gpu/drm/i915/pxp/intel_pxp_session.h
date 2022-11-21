/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_SESSION_H__
#define __INTEL_PXP_SESSION_H__

#include <linux/types.h>

struct work_struct;

void intel_pxp_session_work(struct work_struct *work);

#endif /* __INTEL_PXP_SESSION_H__ */
