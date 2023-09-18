#ifndef __src_common_sdk_nvidia_inc_ctrl_ctrl2080_ctrl2080internal_h__
#define __src_common_sdk_nvidia_inc_ctrl_ctrl2080_ctrl2080internal_h__

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.54.03 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 2020-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#define NV2080_CTRL_CMD_INTERNAL_INTR_GET_KERNEL_TABLE (0x20800a5c) /* finn: Evaluated from "(FINN_NV20_SUBDEVICE_0_INTERNAL_INTERFACE_ID << 8) | NV2080_CTRL_INTERNAL_INTR_GET_KERNEL_TABLE_PARAMS_MESSAGE_ID" */

#define NV2080_CTRL_INTERNAL_INTR_MAX_TABLE_SIZE       128

typedef enum NV2080_INTR_CATEGORY {
    NV2080_INTR_CATEGORY_DEFAULT = 0,
    NV2080_INTR_CATEGORY_ESCHED_DRIVEN_ENGINE = 1,
    NV2080_INTR_CATEGORY_ESCHED_DRIVEN_ENGINE_NOTIFICATION = 2,
    NV2080_INTR_CATEGORY_RUNLIST = 3,
    NV2080_INTR_CATEGORY_RUNLIST_NOTIFICATION = 4,
    NV2080_INTR_CATEGORY_UVM_OWNED = 5,
    NV2080_INTR_CATEGORY_UVM_SHARED = 6,
    NV2080_INTR_CATEGORY_ENUM_COUNT = 7,
} NV2080_INTR_CATEGORY;

typedef struct NV2080_INTR_CATEGORY_SUBTREE_MAP {
    NvU8 subtreeStart;
    NvU8 subtreeEnd;
} NV2080_INTR_CATEGORY_SUBTREE_MAP;

typedef struct NV2080_CTRL_INTERNAL_INTR_GET_KERNEL_TABLE_ENTRY {
    NvU16 engineIdx;
    NvU32 pmcIntrMask;
    NvU32 vectorStall;
    NvU32 vectorNonStall;
} NV2080_CTRL_INTERNAL_INTR_GET_KERNEL_TABLE_ENTRY;

typedef struct NV2080_CTRL_INTERNAL_INTR_GET_KERNEL_TABLE_PARAMS {
    NvU32                                            tableLen;
    NV2080_CTRL_INTERNAL_INTR_GET_KERNEL_TABLE_ENTRY table[NV2080_CTRL_INTERNAL_INTR_MAX_TABLE_SIZE];
    NV2080_INTR_CATEGORY_SUBTREE_MAP                 subtreeMap[NV2080_INTR_CATEGORY_ENUM_COUNT];
} NV2080_CTRL_INTERNAL_INTR_GET_KERNEL_TABLE_PARAMS;

#define NV2080_CTRL_CMD_INTERNAL_FBSR_INIT (0x20800ac2) /* finn: Evaluated from "(FINN_NV20_SUBDEVICE_0_INTERNAL_INTERFACE_ID << 8) | NV2080_CTRL_INTERNAL_FBSR_INIT_PARAMS_MESSAGE_ID" */

typedef struct NV2080_CTRL_INTERNAL_FBSR_INIT_PARAMS {
    NvU32    fbsrType;
    NvU32    numRegions;
    NvHandle hClient;
    NvHandle hSysMem;
    NV_DECLARE_ALIGNED(NvU64 gspFbAllocsSysOffset, 8);
    NvBool   bEnteringGcoffState;
} NV2080_CTRL_INTERNAL_FBSR_INIT_PARAMS;

#define NV2080_CTRL_CMD_INTERNAL_FBSR_SEND_REGION_INFO (0x20800ac3) /* finn: Evaluated from "(FINN_NV20_SUBDEVICE_0_INTERNAL_INTERFACE_ID << 8) | NV2080_CTRL_INTERNAL_FBSR_SEND_REGION_INFO_PARAMS_MESSAGE_ID" */

typedef struct NV2080_CTRL_INTERNAL_FBSR_SEND_REGION_INFO_PARAMS {
    NvU32    fbsrType;
    NvHandle hClient;
    NvHandle hVidMem;
    NV_DECLARE_ALIGNED(NvU64 vidOffset, 8);
    NV_DECLARE_ALIGNED(NvU64 sysOffset, 8);
    NV_DECLARE_ALIGNED(NvU64 size, 8);
} NV2080_CTRL_INTERNAL_FBSR_SEND_REGION_INFO_PARAMS;

#endif
