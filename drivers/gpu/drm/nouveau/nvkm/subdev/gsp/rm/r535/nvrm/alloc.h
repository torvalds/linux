/* SPDX-License-Identifier: MIT */

/* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved. */

#ifndef __NVRM_ALLOC_H__
#define __NVRM_ALLOC_H__
#include <nvrm/nvtypes.h>

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

typedef struct rpc_gsp_rm_alloc_v03_00
{
    NvHandle   hClient;
    NvHandle   hParent;
    NvHandle   hObject;
    NvU32      hClass;
    NvU32      status;
    NvU32      paramsSize;
    NvU32      flags;
    NvU8       reserved[4];
    NvU8       params[];
} rpc_gsp_rm_alloc_v03_00;

typedef struct NVOS00_PARAMETERS_v03_00
{
    NvHandle   hRoot;
    NvHandle   hObjectParent;
    NvHandle   hObjectOld;
    NvV32      status;
} NVOS00_PARAMETERS_v03_00;

typedef struct rpc_free_v03_00
{
    NVOS00_PARAMETERS_v03_00 params;
} rpc_free_v03_00;
#endif
