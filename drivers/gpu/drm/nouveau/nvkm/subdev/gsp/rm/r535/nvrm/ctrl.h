/* SPDX-License-Identifier: MIT */

/* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved. */

#ifndef __NVRM_CTRL_H__
#define __NVRM_CTRL_H__
#include <nvrm/nvtypes.h>

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

typedef struct rpc_gsp_rm_control_v03_00
{
    NvHandle   hClient;
    NvHandle   hObject;
    NvU32      cmd;
    NvU32      status;
    NvU32      paramsSize;
    NvU32      flags;
    NvU8       params[];
} rpc_gsp_rm_control_v03_00;
#endif
