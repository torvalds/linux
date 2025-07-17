/* SPDX-License-Identifier: MIT */

/* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved. */

#ifndef __NVRM_BAR_H__
#define __NVRM_BAR_H__
#include <nvrm/nvtypes.h>

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

typedef enum
{
    NV_RPC_UPDATE_PDE_BAR_1,
    NV_RPC_UPDATE_PDE_BAR_2,
    NV_RPC_UPDATE_PDE_BAR_INVALID,
} NV_RPC_UPDATE_PDE_BAR_TYPE;

typedef struct UpdateBarPde_v15_00
{
    NV_RPC_UPDATE_PDE_BAR_TYPE barType;
    NvU64      entryValue NV_ALIGN_BYTES(8);
    NvU64      entryLevelShift NV_ALIGN_BYTES(8);
} UpdateBarPde_v15_00;

typedef struct rpc_update_bar_pde_v15_00
{
    UpdateBarPde_v15_00 info;
} rpc_update_bar_pde_v15_00;
#endif
