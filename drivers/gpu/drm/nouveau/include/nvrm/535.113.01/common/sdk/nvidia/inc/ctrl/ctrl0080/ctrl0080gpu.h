#ifndef __src_common_sdk_nvidia_inc_ctrl_ctrl0080_ctrl0080gpu_h__
#define __src_common_sdk_nvidia_inc_ctrl_ctrl0080_ctrl0080gpu_h__

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 2004-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

typedef struct NV0080_CTRL_GPU_GET_SRIOV_CAPS_PARAMS {
    NvU32  totalVFs;
    NvU32  firstVfOffset;
    NvU32  vfFeatureMask;
    NV_DECLARE_ALIGNED(NvU64 FirstVFBar0Address, 8);
    NV_DECLARE_ALIGNED(NvU64 FirstVFBar1Address, 8);
    NV_DECLARE_ALIGNED(NvU64 FirstVFBar2Address, 8);
    NV_DECLARE_ALIGNED(NvU64 bar0Size, 8);
    NV_DECLARE_ALIGNED(NvU64 bar1Size, 8);
    NV_DECLARE_ALIGNED(NvU64 bar2Size, 8);
    NvBool b64bitBar0;
    NvBool b64bitBar1;
    NvBool b64bitBar2;
    NvBool bSriovEnabled;
    NvBool bSriovHeavyEnabled;
    NvBool bEmulateVFBar0TlbInvalidationRegister;
    NvBool bClientRmAllocatedCtxBuffer;
} NV0080_CTRL_GPU_GET_SRIOV_CAPS_PARAMS;

#endif
