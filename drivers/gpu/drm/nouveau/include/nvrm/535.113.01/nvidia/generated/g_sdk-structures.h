#ifndef __src_nvidia_generated_g_sdk_structures_h__
#define __src_nvidia_generated_g_sdk_structures_h__
#include <nvrm/535.113.01/nvidia/kernel/inc/vgpu/rpc_headers.h>

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 2008-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

typedef struct NVOS00_PARAMETERS_v03_00
{
    NvHandle   hRoot;
    NvHandle   hObjectParent;
    NvHandle   hObjectOld;
    NvV32      status;
} NVOS00_PARAMETERS_v03_00;

typedef struct UpdateBarPde_v15_00
{
    NV_RPC_UPDATE_PDE_BAR_TYPE barType;
    NvU64      entryValue NV_ALIGN_BYTES(8);
    NvU64      entryLevelShift NV_ALIGN_BYTES(8);
} UpdateBarPde_v15_00;

#endif
