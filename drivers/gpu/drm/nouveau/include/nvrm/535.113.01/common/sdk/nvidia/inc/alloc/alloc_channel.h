#ifndef __src_common_sdk_nvidia_inc_alloc_alloc_channel_h__
#define __src_common_sdk_nvidia_inc_alloc_alloc_channel_h__
#include <nvrm/535.113.01/common/sdk/nvidia/inc/nvlimits.h>

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

/*
 * SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

typedef struct NV_MEMORY_DESC_PARAMS {
    NV_DECLARE_ALIGNED(NvU64 base, 8);
    NV_DECLARE_ALIGNED(NvU64 size, 8);
    NvU32 addressSpace;
    NvU32 cacheAttrib;
} NV_MEMORY_DESC_PARAMS;

#define NVOS04_FLAGS_CHANNEL_TYPE                                  1:0
#define NVOS04_FLAGS_CHANNEL_TYPE_PHYSICAL                         0x00000000
#define NVOS04_FLAGS_CHANNEL_TYPE_VIRTUAL                          0x00000001  // OBSOLETE
#define NVOS04_FLAGS_CHANNEL_TYPE_PHYSICAL_FOR_VIRTUAL             0x00000002  // OBSOLETE

#define NVOS04_FLAGS_VPR                                           2:2
#define NVOS04_FLAGS_VPR_FALSE                                     0x00000000
#define NVOS04_FLAGS_VPR_TRUE                                      0x00000001

#define NVOS04_FLAGS_CC_SECURE                                     2:2
#define NVOS04_FLAGS_CC_SECURE_FALSE                               0x00000000
#define NVOS04_FLAGS_CC_SECURE_TRUE                                0x00000001

#define NVOS04_FLAGS_CHANNEL_SKIP_MAP_REFCOUNTING                  3:3
#define NVOS04_FLAGS_CHANNEL_SKIP_MAP_REFCOUNTING_FALSE            0x00000000
#define NVOS04_FLAGS_CHANNEL_SKIP_MAP_REFCOUNTING_TRUE             0x00000001

#define NVOS04_FLAGS_GROUP_CHANNEL_RUNQUEUE                       4:4
#define NVOS04_FLAGS_GROUP_CHANNEL_RUNQUEUE_DEFAULT               0x00000000
#define NVOS04_FLAGS_GROUP_CHANNEL_RUNQUEUE_ONE                   0x00000001

#define NVOS04_FLAGS_PRIVILEGED_CHANNEL                           5:5
#define NVOS04_FLAGS_PRIVILEGED_CHANNEL_FALSE                     0x00000000
#define NVOS04_FLAGS_PRIVILEGED_CHANNEL_TRUE                      0x00000001

#define NVOS04_FLAGS_DELAY_CHANNEL_SCHEDULING                     6:6
#define NVOS04_FLAGS_DELAY_CHANNEL_SCHEDULING_FALSE               0x00000000
#define NVOS04_FLAGS_DELAY_CHANNEL_SCHEDULING_TRUE                0x00000001

#define NVOS04_FLAGS_CHANNEL_DENY_PHYSICAL_MODE_CE                7:7
#define NVOS04_FLAGS_CHANNEL_DENY_PHYSICAL_MODE_CE_FALSE          0x00000000
#define NVOS04_FLAGS_CHANNEL_DENY_PHYSICAL_MODE_CE_TRUE           0x00000001

#define NVOS04_FLAGS_CHANNEL_USERD_INDEX_VALUE                    10:8

#define NVOS04_FLAGS_CHANNEL_USERD_INDEX_FIXED                    11:11
#define NVOS04_FLAGS_CHANNEL_USERD_INDEX_FIXED_FALSE              0x00000000
#define NVOS04_FLAGS_CHANNEL_USERD_INDEX_FIXED_TRUE               0x00000001

#define NVOS04_FLAGS_CHANNEL_USERD_INDEX_PAGE_VALUE               20:12

#define NVOS04_FLAGS_CHANNEL_USERD_INDEX_PAGE_FIXED               21:21
#define NVOS04_FLAGS_CHANNEL_USERD_INDEX_PAGE_FIXED_FALSE         0x00000000
#define NVOS04_FLAGS_CHANNEL_USERD_INDEX_PAGE_FIXED_TRUE          0x00000001

#define NVOS04_FLAGS_CHANNEL_DENY_AUTH_LEVEL_PRIV                 22:22
#define NVOS04_FLAGS_CHANNEL_DENY_AUTH_LEVEL_PRIV_FALSE           0x00000000
#define NVOS04_FLAGS_CHANNEL_DENY_AUTH_LEVEL_PRIV_TRUE            0x00000001

#define NVOS04_FLAGS_CHANNEL_SKIP_SCRUBBER                        23:23
#define NVOS04_FLAGS_CHANNEL_SKIP_SCRUBBER_FALSE                  0x00000000
#define NVOS04_FLAGS_CHANNEL_SKIP_SCRUBBER_TRUE                   0x00000001

#define NVOS04_FLAGS_CHANNEL_CLIENT_MAP_FIFO                      24:24
#define NVOS04_FLAGS_CHANNEL_CLIENT_MAP_FIFO_FALSE                0x00000000
#define NVOS04_FLAGS_CHANNEL_CLIENT_MAP_FIFO_TRUE                 0x00000001

#define NVOS04_FLAGS_SET_EVICT_LAST_CE_PREFETCH_CHANNEL           25:25
#define NVOS04_FLAGS_SET_EVICT_LAST_CE_PREFETCH_CHANNEL_FALSE     0x00000000
#define NVOS04_FLAGS_SET_EVICT_LAST_CE_PREFETCH_CHANNEL_TRUE      0x00000001

#define NVOS04_FLAGS_CHANNEL_VGPU_PLUGIN_CONTEXT                  26:26
#define NVOS04_FLAGS_CHANNEL_VGPU_PLUGIN_CONTEXT_FALSE            0x00000000
#define NVOS04_FLAGS_CHANNEL_VGPU_PLUGIN_CONTEXT_TRUE             0x00000001

#define NVOS04_FLAGS_CHANNEL_PBDMA_ACQUIRE_TIMEOUT                 27:27
#define NVOS04_FLAGS_CHANNEL_PBDMA_ACQUIRE_TIMEOUT_FALSE           0x00000000
#define NVOS04_FLAGS_CHANNEL_PBDMA_ACQUIRE_TIMEOUT_TRUE            0x00000001

#define NVOS04_FLAGS_GROUP_CHANNEL_THREAD                          29:28
#define NVOS04_FLAGS_GROUP_CHANNEL_THREAD_DEFAULT                  0x00000000
#define NVOS04_FLAGS_GROUP_CHANNEL_THREAD_ONE                      0x00000001
#define NVOS04_FLAGS_GROUP_CHANNEL_THREAD_TWO                      0x00000002

#define NVOS04_FLAGS_MAP_CHANNEL                                   30:30
#define NVOS04_FLAGS_MAP_CHANNEL_FALSE                             0x00000000
#define NVOS04_FLAGS_MAP_CHANNEL_TRUE                              0x00000001

#define NVOS04_FLAGS_SKIP_CTXBUFFER_ALLOC                          31:31
#define NVOS04_FLAGS_SKIP_CTXBUFFER_ALLOC_FALSE                    0x00000000
#define NVOS04_FLAGS_SKIP_CTXBUFFER_ALLOC_TRUE                     0x00000001

#define CC_CHAN_ALLOC_IV_SIZE_DWORD    3U
#define CC_CHAN_ALLOC_NONCE_SIZE_DWORD 8U

typedef struct NV_CHANNEL_ALLOC_PARAMS {

    NvHandle hObjectError; // error context DMA
    NvHandle hObjectBuffer; // no longer used
    NV_DECLARE_ALIGNED(NvU64 gpFifoOffset, 8);    // offset to beginning of GP FIFO
    NvU32    gpFifoEntries;    // number of GP FIFO entries

    NvU32    flags;


    NvHandle hContextShare; // context share handle
    NvHandle hVASpace; // VASpace for the channel

    // handle to UserD memory object for channel, ignored if hUserdMemory[0]=0
    NvHandle hUserdMemory[NV_MAX_SUBDEVICES];

    // offset to beginning of UserD within hUserdMemory[x]
    NV_DECLARE_ALIGNED(NvU64 userdOffset[NV_MAX_SUBDEVICES], 8);

    // engine type(NV2080_ENGINE_TYPE_*) with which this channel is associated
    NvU32    engineType;
    // Channel identifier that is unique for the duration of a RM session
    NvU32    cid;
    // One-hot encoded bitmask to match SET_SUBDEVICE_MASK methods
    NvU32    subDeviceId;
    NvHandle hObjectEccError; // ECC error context DMA

    NV_DECLARE_ALIGNED(NV_MEMORY_DESC_PARAMS instanceMem, 8);
    NV_DECLARE_ALIGNED(NV_MEMORY_DESC_PARAMS userdMem, 8);
    NV_DECLARE_ALIGNED(NV_MEMORY_DESC_PARAMS ramfcMem, 8);
    NV_DECLARE_ALIGNED(NV_MEMORY_DESC_PARAMS mthdbufMem, 8);

    NvHandle hPhysChannelGroup;              // reserved
    NvU32    internalFlags;                 // reserved
    NV_DECLARE_ALIGNED(NV_MEMORY_DESC_PARAMS errorNotifierMem, 8); // reserved
    NV_DECLARE_ALIGNED(NV_MEMORY_DESC_PARAMS eccErrorNotifierMem, 8); // reserved
    NvU32    ProcessID;                 // reserved
    NvU32    SubProcessID;                 // reserved
    // IV used for CPU-side encryption / GPU-side decryption.
    NvU32    encryptIv[CC_CHAN_ALLOC_IV_SIZE_DWORD];          // reserved
    // IV used for CPU-side decryption / GPU-side encryption.
    NvU32    decryptIv[CC_CHAN_ALLOC_IV_SIZE_DWORD];          // reserved
    // Nonce used CPU-side signing / GPU-side signature verification.
    NvU32    hmacNonce[CC_CHAN_ALLOC_NONCE_SIZE_DWORD];       // reserved
} NV_CHANNEL_ALLOC_PARAMS;

typedef NV_CHANNEL_ALLOC_PARAMS NV_CHANNELGPFIFO_ALLOCATION_PARAMETERS;

#endif
