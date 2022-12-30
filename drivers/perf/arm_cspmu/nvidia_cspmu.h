/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 */

/* Support for NVIDIA specific attributes. */

#ifndef __NVIDIA_CSPMU_H__
#define __NVIDIA_CSPMU_H__

#include "arm_cspmu.h"

/* Allocate NVIDIA descriptor. */
int nv_cspmu_init_ops(struct arm_cspmu *cspmu);

#endif /* __NVIDIA_CSPMU_H__ */
