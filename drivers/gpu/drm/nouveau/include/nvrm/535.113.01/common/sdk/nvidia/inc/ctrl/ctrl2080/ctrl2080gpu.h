#ifndef __src_common_sdk_nvidia_inc_ctrl_ctrl2080_ctrl2080gpu_h__
#define __src_common_sdk_nvidia_inc_ctrl_ctrl2080_ctrl2080gpu_h__

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 2006-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#define NV2080_GPU_MAX_NAME_STRING_LENGTH                  (0x0000040U)

#define NV2080_CTRL_GPU_SET_POWER_STATE_GPU_LEVEL_0            (0x00000000U)

#define NV2080_CTRL_GPU_SET_POWER_STATE_GPU_LEVEL_3            (0x00000003U)

typedef struct NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ENTRY {
    NV_DECLARE_ALIGNED(NvU64 gpuPhysAddr, 8);
    NV_DECLARE_ALIGNED(NvU64 gpuVirtAddr, 8);
    NV_DECLARE_ALIGNED(NvU64 size, 8);
    NvU32 physAttr;
    NvU16 bufferId;
    NvU8  bInitialize;
    NvU8  bNonmapped;
} NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ENTRY;

#define NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ID_MAIN                         0U
#define NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ID_PM                           1U
#define NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ID_PATCH                        2U
#define NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ID_BUFFER_BUNDLE_CB             3U
#define NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ID_PAGEPOOL                     4U
#define NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ID_ATTRIBUTE_CB                 5U
#define NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ID_RTV_CB_GLOBAL                6U
#define NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ID_GFXP_POOL                    7U
#define NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ID_GFXP_CTRL_BLK                8U
#define NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ID_FECS_EVENT                   9U
#define NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ID_PRIV_ACCESS_MAP              10U
#define NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ID_UNRESTRICTED_PRIV_ACCESS_MAP 11U
#define NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ID_GLOBAL_PRIV_ACCESS_MAP       12U

#define NV2080_CTRL_GPU_PROMOTE_CONTEXT_MAX_ENTRIES                        16U

#define NV2080_CTRL_CMD_GPU_PROMOTE_CTX                                    (0x2080012bU) /* finn: Evaluated from "(FINN_NV20_SUBDEVICE_0_GPU_INTERFACE_ID << 8) | NV2080_CTRL_GPU_PROMOTE_CTX_PARAMS_MESSAGE_ID" */

typedef struct NV2080_CTRL_GPU_PROMOTE_CTX_PARAMS {
    NvU32    engineType;
    NvHandle hClient;
    NvU32    ChID;
    NvHandle hChanClient;
    NvHandle hObject;
    NvHandle hVirtMemory;
    NV_DECLARE_ALIGNED(NvU64 virtAddress, 8);
    NV_DECLARE_ALIGNED(NvU64 size, 8);
    NvU32    entryCount;
    // C form: NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ENTRY promoteEntry[NV2080_CTRL_GPU_PROMOTE_CONTEXT_MAX_ENTRIES];
    NV_DECLARE_ALIGNED(NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ENTRY promoteEntry[NV2080_CTRL_GPU_PROMOTE_CONTEXT_MAX_ENTRIES], 8);
} NV2080_CTRL_GPU_PROMOTE_CTX_PARAMS;

typedef struct NV2080_CTRL_GPU_GET_FERMI_GPC_INFO_PARAMS {
    NvU32 gpcMask;
} NV2080_CTRL_GPU_GET_FERMI_GPC_INFO_PARAMS;

typedef struct NV2080_CTRL_GPU_GET_FERMI_TPC_INFO_PARAMS {
    NvU32 gpcId;
    NvU32 tpcMask;
} NV2080_CTRL_GPU_GET_FERMI_TPC_INFO_PARAMS;

typedef struct NV2080_CTRL_GPU_GET_FERMI_ZCULL_INFO_PARAMS {
    NvU32 gpcId;
    NvU32 zcullMask;
} NV2080_CTRL_GPU_GET_FERMI_ZCULL_INFO_PARAMS;

#define NV2080_GPU_MAX_GID_LENGTH             (0x000000100ULL)

typedef struct NV2080_CTRL_GPU_GET_GID_INFO_PARAMS {
    NvU32 index;
    NvU32 flags;
    NvU32 length;
    NvU8  data[NV2080_GPU_MAX_GID_LENGTH];
} NV2080_CTRL_GPU_GET_GID_INFO_PARAMS;

#endif
