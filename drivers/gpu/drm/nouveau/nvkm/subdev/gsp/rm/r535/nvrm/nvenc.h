/* SPDX-License-Identifier: MIT */

/* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved. */

#ifndef __NVRM_NVENC_H__
#define __NVRM_NVENC_H__
#include <nvrm/nvtypes.h>

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

typedef struct
{
    NvU32 size;
    NvU32 prohibitMultipleInstances;  // Prohibit multiple allocations of MSENC?
    NvU32 engineInstance;             // Select MSENC/NVENC0 or NVENC1 or NVENC2
} NV_MSENC_ALLOCATION_PARAMETERS;
#endif
