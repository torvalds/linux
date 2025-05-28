/* SPDX-License-Identifier: MIT */

/* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved. */

#ifndef __NVRM_FBSR_H__
#define __NVRM_FBSR_H__
#include <nvrm/nvtypes.h>

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

#define NV01_MEMORY_LIST_FBMEM  (0x00000082)

#define NV01_MEMORY_LIST_SYSTEM (0x00000081)

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

struct pte_desc
{
    NvU32 idr:2;
    NvU32 reserved1:14;
    NvU32 length:16;
    union {
        NvU64 pte; // PTE when IDR==0; PDE when IDR > 0
        NvU64 pde; // PTE when IDR==0; PDE when IDR > 0
    } pte_pde[]  NV_ALIGN_BYTES(8); // PTE when IDR==0; PDE when IDR > 0
};

typedef struct rpc_alloc_memory_v13_01
{
    NvHandle   hClient;
    NvHandle   hDevice;
    NvHandle   hMemory;
    NvU32      hClass;
    NvU32      flags;
    NvU32      pteAdjust;
    NvU32      format;
    NvU64      length NV_ALIGN_BYTES(8);
    NvU32      pageCount;
    struct pte_desc pteDesc;
} rpc_alloc_memory_v13_01;

#define FBSR_TYPE_DMA                                 4   // Copy using DMA. Fastest.

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
