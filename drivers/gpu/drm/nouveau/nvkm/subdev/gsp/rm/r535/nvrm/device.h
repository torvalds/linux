/* SPDX-License-Identifier: MIT */

/* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved. */

#ifndef __NVRM_DEVICE_H__
#define __NVRM_DEVICE_H__
#include <nvrm/nvtypes.h>

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

#define NV01_DEVICE_0      (0x80U) /* finn: Evaluated from "NV0080_ALLOC_PARAMETERS_MESSAGE_ID" */

typedef struct NV0080_ALLOC_PARAMETERS {
    NvU32    deviceId;
    NvHandle hClientShare;
    NvHandle hTargetClient;
    NvHandle hTargetDevice;
    NvV32    flags;
    NV_DECLARE_ALIGNED(NvU64 vaSpaceSize, 8);
    NV_DECLARE_ALIGNED(NvU64 vaStartInternal, 8);
    NV_DECLARE_ALIGNED(NvU64 vaLimitInternal, 8);
    NvV32    vaMode;
} NV0080_ALLOC_PARAMETERS;

#define NV20_SUBDEVICE_0      (0x2080U) /* finn: Evaluated from "NV2080_ALLOC_PARAMETERS_MESSAGE_ID" */

typedef struct NV2080_ALLOC_PARAMETERS {
    NvU32 subDeviceId;
} NV2080_ALLOC_PARAMETERS;
#endif
