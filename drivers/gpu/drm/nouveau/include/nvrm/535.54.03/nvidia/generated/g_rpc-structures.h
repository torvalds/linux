#ifndef __src_nvidia_generated_g_rpc_structures_h__
#define __src_nvidia_generated_g_rpc_structures_h__

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.54.03 */

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

typedef struct rpc_unloading_guest_driver_v1F_07
{
    NvBool     bInPMTransition;
    NvBool     bGc6Entering;
    NvU32      newLevel;
} rpc_unloading_guest_driver_v1F_07;

typedef struct rpc_run_cpu_sequencer_v17_00
{
    NvU32      bufferSizeDWord;
    NvU32      cmdIndex;
    NvU32      regSaveArea[8];
    NvU32      commandBuffer[];
} rpc_run_cpu_sequencer_v17_00;

typedef struct rpc_os_error_log_v17_00
{
    NvU32      exceptType;
    NvU32      runlistId;
    NvU32      chid;
    char       errString[0x100];
} rpc_os_error_log_v17_00;

#endif
