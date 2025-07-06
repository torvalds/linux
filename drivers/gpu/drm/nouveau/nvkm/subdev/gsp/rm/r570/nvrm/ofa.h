/* SPDX-License-Identifier: MIT */

/* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved. */

#ifndef __NVRM_OFA_H__
#define __NVRM_OFA_H__
#include <nvrm/nvtypes.h>

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/570.144 */

typedef struct
{
    NvU32 size;
    NvU32 prohibitMultipleInstances;  // Prohibit multiple allocations of OFA?
    NvU32 engineInstance;
} NV_OFA_ALLOCATION_PARAMETERS;
#endif
