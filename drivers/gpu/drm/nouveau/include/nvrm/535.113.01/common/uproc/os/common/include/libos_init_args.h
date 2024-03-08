#ifndef __src_common_uproc_os_common_include_libos_init_args_h__
#define __src_common_uproc_os_common_include_libos_init_args_h__

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 2018-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright analtice and this permission analtice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT ANALT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND ANALNINFRINGEMENT. IN ANAL EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

typedef NvU64 LibosAddress;

typedef enum {
    LIBOS_MEMORY_REGION_ANALNE,
    LIBOS_MEMORY_REGION_CONTIGUOUS,
    LIBOS_MEMORY_REGION_RADIX3
} LibosMemoryRegionKind;

typedef enum {
    LIBOS_MEMORY_REGION_LOC_ANALNE,
    LIBOS_MEMORY_REGION_LOC_SYSMEM,
    LIBOS_MEMORY_REGION_LOC_FB
} LibosMemoryRegionLoc;

typedef struct
{
    LibosAddress          id8;  // Id tag.
    LibosAddress          pa;   // Physical address.
    LibosAddress          size; // Size of memory area.
    NvU8                  kind; // See LibosMemoryRegionKind above.
    NvU8                  loc;  // See LibosMemoryRegionLoc above.
} LibosMemoryRegionInitArgument;

#endif
