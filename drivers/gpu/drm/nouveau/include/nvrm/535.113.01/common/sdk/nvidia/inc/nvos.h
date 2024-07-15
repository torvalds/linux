#ifndef __src_common_sdk_nvidia_inc_nvos_h__
#define __src_common_sdk_nvidia_inc_nvos_h__

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 1993-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#define NVOS02_FLAGS_PHYSICALITY                                   7:4
#define NVOS02_FLAGS_PHYSICALITY_CONTIGUOUS                        (0x00000000)
#define NVOS02_FLAGS_PHYSICALITY_NONCONTIGUOUS                     (0x00000001)
#define NVOS02_FLAGS_LOCATION                                      11:8
#define NVOS02_FLAGS_LOCATION_PCI                                  (0x00000000)
#define NVOS02_FLAGS_LOCATION_AGP                                  (0x00000001)
#define NVOS02_FLAGS_LOCATION_VIDMEM                               (0x00000002)
#define NVOS02_FLAGS_COHERENCY                                     15:12
#define NVOS02_FLAGS_COHERENCY_UNCACHED                            (0x00000000)
#define NVOS02_FLAGS_COHERENCY_CACHED                              (0x00000001)
#define NVOS02_FLAGS_COHERENCY_WRITE_COMBINE                       (0x00000002)
#define NVOS02_FLAGS_COHERENCY_WRITE_THROUGH                       (0x00000003)
#define NVOS02_FLAGS_COHERENCY_WRITE_PROTECT                       (0x00000004)
#define NVOS02_FLAGS_COHERENCY_WRITE_BACK                          (0x00000005)
#define NVOS02_FLAGS_ALLOC                                         17:16
#define NVOS02_FLAGS_ALLOC_NONE                                    (0x00000001)
#define NVOS02_FLAGS_GPU_CACHEABLE                                 18:18
#define NVOS02_FLAGS_GPU_CACHEABLE_NO                              (0x00000000)
#define NVOS02_FLAGS_GPU_CACHEABLE_YES                             (0x00000001)

#define NVOS02_FLAGS_KERNEL_MAPPING                                19:19
#define NVOS02_FLAGS_KERNEL_MAPPING_NO_MAP                         (0x00000000)
#define NVOS02_FLAGS_KERNEL_MAPPING_MAP                            (0x00000001)
#define NVOS02_FLAGS_ALLOC_NISO_DISPLAY                            20:20
#define NVOS02_FLAGS_ALLOC_NISO_DISPLAY_NO                         (0x00000000)
#define NVOS02_FLAGS_ALLOC_NISO_DISPLAY_YES                        (0x00000001)

#define NVOS02_FLAGS_ALLOC_USER_READ_ONLY                          21:21
#define NVOS02_FLAGS_ALLOC_USER_READ_ONLY_NO                       (0x00000000)
#define NVOS02_FLAGS_ALLOC_USER_READ_ONLY_YES                      (0x00000001)

#define NVOS02_FLAGS_ALLOC_DEVICE_READ_ONLY                        22:22
#define NVOS02_FLAGS_ALLOC_DEVICE_READ_ONLY_NO                     (0x00000000)
#define NVOS02_FLAGS_ALLOC_DEVICE_READ_ONLY_YES                    (0x00000001)

#define NVOS02_FLAGS_PEER_MAP_OVERRIDE                             23:23
#define NVOS02_FLAGS_PEER_MAP_OVERRIDE_DEFAULT                     (0x00000000)
#define NVOS02_FLAGS_PEER_MAP_OVERRIDE_REQUIRED                    (0x00000001)

#define NVOS02_FLAGS_ALLOC_TYPE_SYNCPOINT                          24:24
#define NVOS02_FLAGS_ALLOC_TYPE_SYNCPOINT_APERTURE                 (0x00000001)

#define NVOS02_FLAGS_MEMORY_PROTECTION                             26:25
#define NVOS02_FLAGS_MEMORY_PROTECTION_DEFAULT                     (0x00000000)
#define NVOS02_FLAGS_MEMORY_PROTECTION_PROTECTED                   (0x00000001)
#define NVOS02_FLAGS_MEMORY_PROTECTION_UNPROTECTED                 (0x00000002)

#define NVOS02_FLAGS_MAPPING                                       31:30
#define NVOS02_FLAGS_MAPPING_DEFAULT                               (0x00000000)
#define NVOS02_FLAGS_MAPPING_NO_MAP                                (0x00000001)
#define NVOS02_FLAGS_MAPPING_NEVER_MAP                             (0x00000002)

#define NV01_EVENT_CLIENT_RM                                       (0x04000000)

typedef struct
{
    NvV32    channelInstance;            // One of the n channel instances of a given channel type.
                                         // Note that core channel has only one instance
                                         // while all others have two (one per head).
    NvHandle hObjectBuffer;              // ctx dma handle for DMA push buffer
    NvHandle hObjectNotify;              // ctx dma handle for an area (of type NvNotification defined in sdk/nvidia/inc/nvtypes.h) where RM can write errors/notifications
    NvU32    offset;                     // Initial offset for put/get, usually zero.
    NvP64    pControl NV_ALIGN_BYTES(8); // pControl gives virt addr of UDISP GET/PUT regs

    NvU32    flags;
#define NV50VAIO_CHANNELDMA_ALLOCATION_FLAGS_CONNECT_PB_AT_GRAB                1:1
#define NV50VAIO_CHANNELDMA_ALLOCATION_FLAGS_CONNECT_PB_AT_GRAB_YES            0x00000000
#define NV50VAIO_CHANNELDMA_ALLOCATION_FLAGS_CONNECT_PB_AT_GRAB_NO             0x00000001

} NV50VAIO_CHANNELDMA_ALLOCATION_PARAMETERS;

typedef struct
{
    NvV32    channelInstance;            // One of the n channel instances of a given channel type.
                                         // All PIO channels have two instances (one per head).
    NvHandle hObjectNotify;              // ctx dma handle for an area (of type NvNotification defined in sdk/nvidia/inc/nvtypes.h) where RM can write errors.
    NvP64    pControl NV_ALIGN_BYTES(8); // pControl gives virt addr of control region for PIO channel
} NV50VAIO_CHANNELPIO_ALLOCATION_PARAMETERS;

typedef struct
{
    NvU32 size;
    NvU32 prohibitMultipleInstances;
    NvU32 engineInstance;               // Select NVDEC0 or NVDEC1 or NVDEC2
} NV_BSP_ALLOCATION_PARAMETERS;

typedef struct
{
    NvU32 size;
    NvU32 prohibitMultipleInstances;  // Prohibit multiple allocations of MSENC?
    NvU32 engineInstance;             // Select MSENC/NVENC0 or NVENC1 or NVENC2
} NV_MSENC_ALLOCATION_PARAMETERS;

typedef struct
{
    NvU32 size;
    NvU32 prohibitMultipleInstances;  // Prohibit multiple allocations of NVJPG?
    NvU32 engineInstance;
} NV_NVJPG_ALLOCATION_PARAMETERS;

typedef struct
{
    NvU32 size;
    NvU32 prohibitMultipleInstances;  // Prohibit multiple allocations of OFA?
} NV_OFA_ALLOCATION_PARAMETERS;

typedef struct
{
    NvU32   index;
    NvV32   flags;
    NvU64   vaSize NV_ALIGN_BYTES(8);
    NvU64   vaStartInternal NV_ALIGN_BYTES(8);
    NvU64   vaLimitInternal NV_ALIGN_BYTES(8);
    NvU32   bigPageSize;
    NvU64   vaBase NV_ALIGN_BYTES(8);
} NV_VASPACE_ALLOCATION_PARAMETERS;

#define NV_VASPACE_ALLOCATION_INDEX_GPU_NEW                                 0x00 //<! Create new VASpace, by default

#endif
