/* SPDX-License-Identifier: MIT */

/* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved. */

#ifndef __NVRM_FIFO_H__
#define __NVRM_FIFO_H__
#include <nvrm/nvtypes.h>

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/570.144 */

#define NV_MAX_SUBDEVICES             8

typedef struct NV_MEMORY_DESC_PARAMS {
    NV_DECLARE_ALIGNED(NvU64 base, 8);
    NV_DECLARE_ALIGNED(NvU64 size, 8);
    NvU32 addressSpace;
    NvU32 cacheAttrib;
} NV_MEMORY_DESC_PARAMS;

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
    NvU32    tpcConfigID; // TPC Configuration Id as supported by DTD-PG Feature
} NV_CHANNEL_ALLOC_PARAMS;

typedef NV_CHANNEL_ALLOC_PARAMS NV_CHANNELGPFIFO_ALLOCATION_PARAMETERS;

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

typedef enum {
    /*!
     * Initial state as passed in NV_CHANNEL_ALLOC_PARAMS by
     * kernel CPU-RM clients.
     */
    ERROR_NOTIFIER_TYPE_UNKNOWN = 0,
    /*! @brief Error notifier is explicitly not set.
     *
     * The corresponding hErrorContext or hEccErrorContext must be
     * NV01_NULL_OBJECT.
     */
    ERROR_NOTIFIER_TYPE_NONE,
    /*! @brief Error notifier is a ContextDma */
    ERROR_NOTIFIER_TYPE_CTXDMA,
    /*! @brief Error notifier is a NvNotification array in sysmem/vidmem */
    ERROR_NOTIFIER_TYPE_MEMORY
} ErrorNotifierType;

#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_PRIVILEGE                       1:0
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_PRIVILEGE_USER                  0x0
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_PRIVILEGE_ADMIN                 0x1
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_PRIVILEGE_KERNEL                0x2
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_ERROR_NOTIFIER_TYPE             3:2
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_ERROR_NOTIFIER_TYPE_UNKNOWN     ERROR_NOTIFIER_TYPE_UNKNOWN
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_ERROR_NOTIFIER_TYPE_NONE        ERROR_NOTIFIER_TYPE_NONE
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_ERROR_NOTIFIER_TYPE_CTXDMA      ERROR_NOTIFIER_TYPE_CTXDMA
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_ERROR_NOTIFIER_TYPE_MEMORY      ERROR_NOTIFIER_TYPE_MEMORY
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_ECC_ERROR_NOTIFIER_TYPE         5:4
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_ECC_ERROR_NOTIFIER_TYPE_UNKNOWN ERROR_NOTIFIER_TYPE_UNKNOWN
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_ECC_ERROR_NOTIFIER_TYPE_NONE    ERROR_NOTIFIER_TYPE_NONE
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_ECC_ERROR_NOTIFIER_TYPE_CTXDMA  ERROR_NOTIFIER_TYPE_CTXDMA
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_ECC_ERROR_NOTIFIER_TYPE_MEMORY  ERROR_NOTIFIER_TYPE_MEMORY
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_GSP_OWNED                       6:6
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_GSP_OWNED_NO                    0x0
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_GSP_OWNED_YES                   0x1
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_UVM_OWNED                       7:7
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_UVM_OWNED_NO                    0x0
#define NV_KERNELCHANNEL_ALLOC_INTERNALFLAGS_UVM_OWNED_YES                   0x1

typedef struct rpc_rc_triggered_v17_02
{
    NvU32      nv2080EngineType;
    NvU32      chid;
    NvU32      gfid;
    NvU32      exceptLevel;
    NvU32      exceptType;
    NvU32      scope;
    NvU16      partitionAttributionId;
    NvU32      mmuFaultAddrLo;
    NvU32      mmuFaultAddrHi;
    NvU32      mmuFaultType;
    NvBool     bCallbackNeeded;
    NvU32      rcJournalBufferSize;
    NvU8       rcJournalBuffer[];
} rpc_rc_triggered_v17_02;

#define NV2080_CTRL_GPU_MAX_CONSTRUCTED_FALCONS         0x40

typedef struct NV2080_CTRL_GPU_CONSTRUCTED_FALCON_INFO {
    NvU32 engDesc;
    NvU32 ctxAttr;
    NvU32 ctxBufferSize;
    NvU32 addrSpaceList;
    NvU32 registerBase;
} NV2080_CTRL_GPU_CONSTRUCTED_FALCON_INFO;

#define NV2080_CTRL_CMD_GPU_GET_CONSTRUCTED_FALCON_INFO (0x208001b0) /* finn: Evaluated from "(FINN_NV20_SUBDEVICE_0_GPU_INTERFACE_ID << 8) | NV2080_CTRL_GPU_GET_CONSTRUCTED_FALCON_INFO_PARAMS_MESSAGE_ID" */
typedef struct NV2080_CTRL_GPU_GET_CONSTRUCTED_FALCON_INFO_PARAMS {
    NvU32                                   numConstructedFalcons;
    NV2080_CTRL_GPU_CONSTRUCTED_FALCON_INFO constructedFalconsTable[NV2080_CTRL_GPU_MAX_CONSTRUCTED_FALCONS];
} NV2080_CTRL_GPU_GET_CONSTRUCTED_FALCON_INFO_PARAMS;

typedef struct NV2080_CTRL_CMD_INTERNAL_FIFO_TOGGLE_ACTIVE_CHANNEL_SCHEDULING_PARAMS {
    NvBool bDisableActiveChannels;
} NV2080_CTRL_CMD_INTERNAL_FIFO_TOGGLE_ACTIVE_CHANNEL_SCHEDULING_PARAMS;

#define NV2080_CTRL_CMD_INTERNAL_FIFO_TOGGLE_ACTIVE_CHANNEL_SCHEDULING (0x20800ac3) /* finn: Evaluated from "(FINN_NV20_SUBDEVICE_0_INTERNAL_INTERFACE_ID << 8) | NV2080_CTRL_CMD_INTERNAL_FIFO_TOGGLE_ACTIVE_CHANNEL_SCHEDULING_PARAMS_MESSAGE_ID" */
#endif
