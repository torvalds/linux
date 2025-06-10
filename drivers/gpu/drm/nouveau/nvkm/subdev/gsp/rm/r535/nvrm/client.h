/* SPDX-License-Identifier: MIT */

/* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved. */

#ifndef __NVRM_CLIENT_H__
#define __NVRM_CLIENT_H__
#include <nvrm/nvtypes.h>

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

#define NV01_ROOT        (0x0U) /* finn: Evaluated from "NV0000_ALLOC_PARAMETERS_MESSAGE_ID" */

#define NV_PROC_NAME_MAX_LENGTH 100U

typedef struct NV0000_ALLOC_PARAMETERS {
    NvHandle hClient; /* CORERM-2934: hClient must remain the first member until all allocations use these params */
    NvU32    processID;
    char     processName[NV_PROC_NAME_MAX_LENGTH];
} NV0000_ALLOC_PARAMETERS;
#endif
