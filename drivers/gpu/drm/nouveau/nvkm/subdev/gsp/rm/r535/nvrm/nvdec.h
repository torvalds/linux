/* SPDX-License-Identifier: MIT */

/* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved. */

#ifndef __NVRM_NVDEC_H__
#define __NVRM_NVDEC_H__
#include <nvrm/nvtypes.h>

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

typedef struct
{
    NvU32 size;
    NvU32 prohibitMultipleInstances;
    NvU32 engineInstance;               // Select NVDEC0 or NVDEC1 or NVDEC2
} NV_BSP_ALLOCATION_PARAMETERS;
#endif
