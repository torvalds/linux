/* SPDX-License-Identifier: MIT */

/* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved. */

#ifndef __NVRM_FIFO_H__
#define __NVRM_FIFO_H__
#include <nvrm/nvtypes.h>

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

#define NV2080_CTRL_FIFO_GET_DEVICE_INFO_TABLE_MAX_ENTRIES         32

#define NV2080_CTRL_FIFO_GET_DEVICE_INFO_TABLE_ENGINE_DATA_TYPES   16

#define NV2080_CTRL_FIFO_GET_DEVICE_INFO_TABLE_ENGINE_MAX_PBDMA    2

#define NV2080_CTRL_FIFO_GET_DEVICE_INFO_TABLE_ENGINE_MAX_NAME_LEN 16

typedef struct NV2080_CTRL_FIFO_DEVICE_ENTRY {
    NvU32 engineData[NV2080_CTRL_FIFO_GET_DEVICE_INFO_TABLE_ENGINE_DATA_TYPES];
    NvU32 pbdmaIds[NV2080_CTRL_FIFO_GET_DEVICE_INFO_TABLE_ENGINE_MAX_PBDMA];
    NvU32 pbdmaFaultIds[NV2080_CTRL_FIFO_GET_DEVICE_INFO_TABLE_ENGINE_MAX_PBDMA];
    NvU32 numPbdmas;
    char  engineName[NV2080_CTRL_FIFO_GET_DEVICE_INFO_TABLE_ENGINE_MAX_NAME_LEN];
} NV2080_CTRL_FIFO_DEVICE_ENTRY;

#define NV2080_CTRL_CMD_FIFO_GET_DEVICE_INFO_TABLE                 (0x20801112) /* finn: Evaluated from "(FINN_NV20_SUBDEVICE_0_FIFO_INTERFACE_ID << 8) | NV2080_CTRL_FIFO_GET_DEVICE_INFO_TABLE_PARAMS_MESSAGE_ID" */
typedef struct NV2080_CTRL_FIFO_GET_DEVICE_INFO_TABLE_PARAMS {
    NvU32                         baseIndex;
    NvU32                         numEntries;
    NvBool                        bMore;
    // C form: NV2080_CTRL_FIFO_DEVICE_ENTRY entries[NV2080_CTRL_FIFO_GET_DEVICE_INFO_TABLE_MAX_ENTRIES];
    NV2080_CTRL_FIFO_DEVICE_ENTRY entries[NV2080_CTRL_FIFO_GET_DEVICE_INFO_TABLE_MAX_ENTRIES];
} NV2080_CTRL_FIFO_GET_DEVICE_INFO_TABLE_PARAMS;

typedef enum
{
    /* *************************************************************************
     * Bug 3820969
     * THINK BEFORE CHANGING ENUM ORDER HERE.
     * VGPU-guest uses this same ordering. Because this enum is not versioned,
     * changing the order here WILL BREAK old-guest-on-newer-host compatibility.
     * ************************************************************************/

    // *ENG_XYZ, e.g.: ENG_GR, ENG_CE etc.,
    ENGINE_INFO_TYPE_ENG_DESC = 0,

    // HW engine ID
    ENGINE_INFO_TYPE_FIFO_TAG,

    // RM_ENGINE_TYPE_*
    ENGINE_INFO_TYPE_RM_ENGINE_TYPE,

    //
    // runlist id (meaning varies by GPU)
    // Valid only for Esched-driven engines
    //
    ENGINE_INFO_TYPE_RUNLIST,

    // NV_PFIFO_INTR_MMU_FAULT_ENG_ID_*
    ENGINE_INFO_TYPE_MMU_FAULT_ID,

    // ROBUST_CHANNEL_*
    ENGINE_INFO_TYPE_RC_MASK,

    // Reset Bit Position. On Ampere, only valid if not _INVALID
    ENGINE_INFO_TYPE_RESET,

    // Interrupt Bit Position
    ENGINE_INFO_TYPE_INTR,

    // log2(MC_ENGINE_*)
    ENGINE_INFO_TYPE_MC,

    // The DEV_TYPE_ENUM for this engine
    ENGINE_INFO_TYPE_DEV_TYPE_ENUM,

    // The particular instance of this engine type
    ENGINE_INFO_TYPE_INSTANCE_ID,

    //
    // The base address for this engine's NV_RUNLIST. Valid only on Ampere+
    // Valid only for Esched-driven engines
    //
    ENGINE_INFO_TYPE_RUNLIST_PRI_BASE,

    //
    // If this entry is a host-driven engine.
    // Update _isEngineInfoTypeValidForOnlyHostDriven when adding any new entry.
    //
    ENGINE_INFO_TYPE_IS_HOST_DRIVEN_ENGINE,

    //
    // The index into the per-engine NV_RUNLIST registers. Valid only on Ampere+
    // Valid only for Esched-driven engines
    //
    ENGINE_INFO_TYPE_RUNLIST_ENGINE_ID,

    //
    // The base address for this engine's NV_CHRAM registers. Valid only on
    // Ampere+
    //
    // Valid only for Esched-driven engines
    //
    ENGINE_INFO_TYPE_CHRAM_PRI_BASE,

    // This entry added to copy data at RMCTRL_EXPORT() call for Kernel RM
    ENGINE_INFO_TYPE_KERNEL_RM_MAX,
    // Used for iterating the engine info table by the index passed.
    ENGINE_INFO_TYPE_INVALID = ENGINE_INFO_TYPE_KERNEL_RM_MAX,

    // Size of FIFO_ENGINE_LIST.engineData
    ENGINE_INFO_TYPE_ENGINE_DATA_ARRAY_SIZE = ENGINE_INFO_TYPE_INVALID,

    // Input-only parameter for kfifoEngineInfoXlate.
    ENGINE_INFO_TYPE_PBDMA_ID

    /* *************************************************************************
     * Bug 3820969
     * THINK BEFORE CHANGING ENUM ORDER HERE.
     * VGPU-guest uses this same ordering. Because this enum is not versioned,
     * changing the order here WILL BREAK old-guest-on-newer-host compatibility.
     * ************************************************************************/
} ENGINE_INFO_TYPE;

#define NV2080_CTRL_CMD_CE_GET_FAULT_METHOD_BUFFER_SIZE (0x20802a08) /* finn: Evaluated from "(FINN_NV20_SUBDEVICE_0_CE_INTERFACE_ID << 8) | NV2080_CTRL_CE_GET_FAULT_METHOD_BUFFER_SIZE_PARAMS_MESSAGE_ID" */
typedef struct NV2080_CTRL_CE_GET_FAULT_METHOD_BUFFER_SIZE_PARAMS {
    NvU32 size;
} NV2080_CTRL_CE_GET_FAULT_METHOD_BUFFER_SIZE_PARAMS;

#define NV2080_CTRL_CMD_INTERNAL_MAX_CONSTRUCTED_FALCONS     0x40

typedef struct NV2080_CTRL_INTERNAL_CONSTRUCTED_FALCON_INFO {
    NvU32 engDesc;
    NvU32 ctxAttr;
    NvU32 ctxBufferSize;
    NvU32 addrSpaceList;
    NvU32 registerBase;
} NV2080_CTRL_INTERNAL_CONSTRUCTED_FALCON_INFO;

#define NV2080_CTRL_CMD_INTERNAL_GET_CONSTRUCTED_FALCON_INFO (0x20800a42) /* finn: Evaluated from "(FINN_NV20_SUBDEVICE_0_INTERNAL_INTERFACE_ID << 8) | NV2080_CTRL_INTERNAL_GET_CONSTRUCTED_FALCON_INFO_PARAMS_MESSAGE_ID" */
typedef struct NV2080_CTRL_INTERNAL_GET_CONSTRUCTED_FALCON_INFO_PARAMS {
    NvU32                                        numConstructedFalcons;
    NV2080_CTRL_INTERNAL_CONSTRUCTED_FALCON_INFO constructedFalconsTable[NV2080_CTRL_CMD_INTERNAL_MAX_CONSTRUCTED_FALCONS];
} NV2080_CTRL_INTERNAL_GET_CONSTRUCTED_FALCON_INFO_PARAMS;

#define NV_MAX_SUBDEVICES       8

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

#define NVA06F_CTRL_CMD_BIND (0xa06f0104) /* finn: Evaluated from "(FINN_KEPLER_CHANNEL_GPFIFO_A_GPFIFO_INTERFACE_ID << 8) | NVA06F_CTRL_BIND_PARAMS_MESSAGE_ID" */
typedef struct NVA06F_CTRL_BIND_PARAMS {
    NvU32 engineType;
} NVA06F_CTRL_BIND_PARAMS;

#define NVA06F_CTRL_CMD_GPFIFO_SCHEDULE (0xa06f0103) /* finn: Evaluated from "(FINN_KEPLER_CHANNEL_GPFIFO_A_GPFIFO_INTERFACE_ID << 8) | NVA06F_CTRL_GPFIFO_SCHEDULE_PARAMS_MESSAGE_ID" */
typedef struct NVA06F_CTRL_GPFIFO_SCHEDULE_PARAMS {
    NvBool bEnable;
    NvBool bSkipSubmit;
} NVA06F_CTRL_GPFIFO_SCHEDULE_PARAMS;

#define NV2080_CTRL_GPU_PROMOTE_CONTEXT_MAX_ENTRIES                        16U

typedef struct NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ENTRY {
    NV_DECLARE_ALIGNED(NvU64 gpuPhysAddr, 8);
    NV_DECLARE_ALIGNED(NvU64 gpuVirtAddr, 8);
    NV_DECLARE_ALIGNED(NvU64 size, 8);
    NvU32 physAttr;
    NvU16 bufferId;
    NvU8  bInitialize;
    NvU8  bNonmapped;
} NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ENTRY;

#define NV2080_CTRL_CMD_GPU_PROMOTE_CTX                                    (0x2080012bU) /* finn: Evaluated from "(FINN_NV20_SUBDEVICE_0_GPU_INTERFACE_ID << 8) | NV2080_CTRL_GPU_PROMOTE_CTX_PARAMS_MESSAGE_ID" */
typedef struct NV2080_CTRL_GPU_PROMOTE_CTX_PARAMS {
    NvU32    engineType;
    NvHandle hClient;
    NvU32    ChID;
    NvHandle hChanClient;
    NvHandle hObject;
    NvHandle hVirtMemory;
    NV_DECLARE_ALIGNED(NvU64 virtAddress, 8);
    NV_DECLARE_ALIGNED(NvU64 size, 8);
    NvU32    entryCount;
    // C form: NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ENTRY promoteEntry[NV2080_CTRL_GPU_PROMOTE_CONTEXT_MAX_ENTRIES];
    NV_DECLARE_ALIGNED(NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ENTRY promoteEntry[NV2080_CTRL_GPU_PROMOTE_CONTEXT_MAX_ENTRIES], 8);
} NV2080_CTRL_GPU_PROMOTE_CTX_PARAMS;

typedef struct rpc_rc_triggered_v17_02
{
    NvU32      nv2080EngineType;
    NvU32      chid;
    NvU32      exceptType;
    NvU32      scope;
    NvU16      partitionAttributionId;
} rpc_rc_triggered_v17_02;
#endif
