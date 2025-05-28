/* SPDX-License-Identifier: MIT */

/* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved. */

#ifndef __NVRM_FBSR_H__
#define __NVRM_FBSR_H__
#include <nvrm/nvtypes.h>

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/570.144 */

#define NV2080_CTRL_CMD_INTERNAL_FBSR_INIT (0x20800ac2) /* finn: Evaluated from "(FINN_NV20_SUBDEVICE_0_INTERNAL_INTERFACE_ID << 8) | NV2080_CTRL_INTERNAL_FBSR_INIT_PARAMS_MESSAGE_ID" */
typedef struct NV2080_CTRL_INTERNAL_FBSR_INIT_PARAMS {
    NvHandle hClient;
    NvHandle hSysMem;
    NvBool   bEnteringGcoffState;
    NV_DECLARE_ALIGNED(NvU64 sysmemAddrOfSuspendResumeData, 8);
} NV2080_CTRL_INTERNAL_FBSR_INIT_PARAMS;

#endif
