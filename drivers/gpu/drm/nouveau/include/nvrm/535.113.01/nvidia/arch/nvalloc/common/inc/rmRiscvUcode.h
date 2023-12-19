#ifndef __src_nvidia_arch_nvalloc_common_inc_rmRiscvUcode_h__
#define __src_nvidia_arch_nvalloc_common_inc_rmRiscvUcode_h__

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 2018-2019 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

typedef struct {
    //
    // Version 1
    // Version 2
    // Version 3 = for Partition boot
    // Version 4 = for eb riscv boot
    // Version 5 = Support signing entire RISC-V image as "code" in code section for hopper and later.
    //
    NvU32  version;                         // structure version
    NvU32  bootloaderOffset;
    NvU32  bootloaderSize;
    NvU32  bootloaderParamOffset;
    NvU32  bootloaderParamSize;
    NvU32  riscvElfOffset;
    NvU32  riscvElfSize;
    NvU32  appVersion;                      // Changelist number associated with the image
    //
    // Manifest contains information about Monitor and it is
    // input to BR
    //
    NvU32  manifestOffset;
    NvU32  manifestSize;
    //
    // Monitor Data offset within RISCV image and size
    //
    NvU32  monitorDataOffset;
    NvU32  monitorDataSize;
    //
    // Monitor Code offset withtin RISCV image and size
    //
    NvU32  monitorCodeOffset;
    NvU32  monitorCodeSize;
    NvU32  bIsMonitorEnabled;
    //
    // Swbrom Code offset within RISCV image and size
    //
    NvU32  swbromCodeOffset;
    NvU32  swbromCodeSize;
    //
    // Swbrom Data offset within RISCV image and size
    //
    NvU32  swbromDataOffset;
    NvU32  swbromDataSize;
    //
    // Total size of FB carveout (image and reserved space).  
    //
    NvU32  fbReservedSize;
    //
    // Indicates whether the entire RISC-V image is signed as "code" in code section.
    //
    NvU32  bSignedAsCode;
} RM_RISCV_UCODE_DESC;

#endif
