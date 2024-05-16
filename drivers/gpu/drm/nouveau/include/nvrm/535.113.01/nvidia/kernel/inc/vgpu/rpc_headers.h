#ifndef __src_nvidia_kernel_inc_vgpu_rpc_headers_h__
#define __src_nvidia_kernel_inc_vgpu_rpc_headers_h__

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 2017-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#define MAX_GPC_COUNT           32

typedef enum
{
    NV_RPC_UPDATE_PDE_BAR_1,
    NV_RPC_UPDATE_PDE_BAR_2,
    NV_RPC_UPDATE_PDE_BAR_INVALID,
} NV_RPC_UPDATE_PDE_BAR_TYPE;

typedef struct VIRTUAL_DISPLAY_GET_MAX_RESOLUTION_PARAMS 
{
    NvU32 headIndex;
    NvU32 maxHResolution;
    NvU32 maxVResolution;
} VIRTUAL_DISPLAY_GET_MAX_RESOLUTION_PARAMS;

typedef struct VIRTUAL_DISPLAY_GET_NUM_HEADS_PARAMS 
{
    NvU32 numHeads;
    NvU32 maxNumHeads;
} VIRTUAL_DISPLAY_GET_NUM_HEADS_PARAMS;

#endif
