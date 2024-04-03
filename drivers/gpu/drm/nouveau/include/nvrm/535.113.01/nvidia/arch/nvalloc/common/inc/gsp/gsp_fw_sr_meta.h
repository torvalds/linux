#ifndef __src_nvidia_arch_nvalloc_common_inc_gsp_gsp_fw_sr_meta_h__
#define __src_nvidia_arch_nvalloc_common_inc_gsp_gsp_fw_sr_meta_h__

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#define GSP_FW_SR_META_MAGIC     0x8a3bb9e6c6c39d93ULL
#define GSP_FW_SR_META_REVISION  2

typedef struct
{
    //
    // Magic
    // Use for verification by Booter
    //
    NvU64 magic;  // = GSP_FW_SR_META_MAGIC;

    //
    // Revision number
    // Bumped up when we change this interface so it is not backward compatible.
    // Bumped up when we revoke GSP-RM ucode
    //
    NvU64 revision;  // = GSP_FW_SR_META_MAGIC_REVISION;

    //
    // ---- Members regarding data in SYSMEM ----------------------------
    // Consumed by Booter for DMA
    //
    NvU64 sysmemAddrOfSuspendResumeData;
    NvU64 sizeOfSuspendResumeData;

    // ---- Members for crypto ops across S/R ---------------------------

    //
    // HMAC over the entire GspFwSRMeta structure (including padding)
    // with the hmac field itself zeroed.
    //
    NvU8 hmac[32];

    // Hash over GspFwWprMeta structure
    NvU8 wprMetaHash[32];

    // Hash over GspFwHeapFreeList structure. All zeros signifies no free list.
    NvU8 heapFreeListHash[32];

    // Hash over data in WPR2 (skipping over free heap chunks; see Booter for details)
    NvU8 dataHash[32];

    //
    // Pad structure to exactly 256 bytes (1 DMA chunk).
    // Padding initialized to zero.
    //
    NvU32 padding[24];

} GspFwSRMeta;

#endif
