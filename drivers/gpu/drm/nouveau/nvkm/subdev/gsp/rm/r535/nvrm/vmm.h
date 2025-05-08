/* SPDX-License-Identifier: MIT */

/* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved. */

#ifndef __NVRM_VMM_H__
#define __NVRM_VMM_H__
#include <nvrm/nvtypes.h>

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

#define FERMI_VASPACE_A                                     (0x000090f1)

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

#define NV_VASPACE_ALLOCATION_FLAGS_IS_EXTERNALLY_OWNED                   BIT(3)

#define SPLIT_VAS_SERVER_RM_MANAGED_VA_START   0x100000000ULL  // 4GB
#define SPLIT_VAS_SERVER_RM_MANAGED_VA_SIZE     0x20000000ULL  // 512MB

#define GMMU_FMT_MAX_LEVELS  6U

#define NV90F1_CTRL_CMD_VASPACE_COPY_SERVER_RESERVED_PDES (0x90f10106U) /* finn: Evaluated from "(FINN_FERMI_VASPACE_A_VASPACE_INTERFACE_ID << 8) | NV90F1_CTRL_VASPACE_COPY_SERVER_RESERVED_PDES_PARAMS_MESSAGE_ID" */
typedef struct NV90F1_CTRL_VASPACE_COPY_SERVER_RESERVED_PDES_PARAMS {
    /*!
     * [in] GPU sub-device handle - this API only supports unicast.
     *      Pass 0 to use subDeviceId instead.
     */
    NvHandle hSubDevice;

    /*!
     * [in] GPU sub-device ID. Ignored if hSubDevice is non-zero.
     */
    NvU32    subDeviceId;

    /*!
     * [in] Page size (VA coverage) of the level to reserve.
     *      This need not be a leaf (page table) page size - it can be
     *      the coverage of an arbitrary level (including root page directory).
     */
    NV_DECLARE_ALIGNED(NvU64 pageSize, 8);

    /*!
     * [in] First GPU virtual address of the range to reserve.
     *      This must be aligned to pageSize.
     */
    NV_DECLARE_ALIGNED(NvU64 virtAddrLo, 8);

    /*!
     * [in] Last GPU virtual address of the range to reserve.
     *      This (+1) must be aligned to pageSize.
     */
    NV_DECLARE_ALIGNED(NvU64 virtAddrHi, 8);

    /*! 
     * [in] Number of PDE levels to copy.
     */
    NvU32    numLevelsToCopy;

   /*!
     * [in] Per-level information.
     */
    struct {
        /*!
         * Physical address of this page level instance.
         */
        NV_DECLARE_ALIGNED(NvU64 physAddress, 8);

        /*!
         * Size in bytes allocated for this level instance.
         */
        NV_DECLARE_ALIGNED(NvU64 size, 8);

        /*!
         * Aperture in which this page level instance resides.
         */
        NvU32 aperture;

        /*!
         * Page shift corresponding to the level
         */
        NvU8  pageShift;
    } levels[GMMU_FMT_MAX_LEVELS];
} NV90F1_CTRL_VASPACE_COPY_SERVER_RESERVED_PDES_PARAMS;

#define NV0080_CTRL_CMD_DMA_SET_PAGE_DIRECTORY (0x801813U) /* finn: Evaluated from "(FINN_NV01_DEVICE_0_DMA_INTERFACE_
ID << 8) | NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_PARAMS_MESSAGE_ID" */

typedef struct NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_PARAMS {
    NV_DECLARE_ALIGNED(NvU64 physAddress, 8);
    NvU32    numEntries;
    NvU32    flags;
    NvHandle hVASpace;
    NvU32    chId;
    NvU32    subDeviceId; // ID+1, 0 for BC
    NvU32    pasid;
} NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_PARAMS;

#define NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_FLAGS_APERTURE                  1:0
#define NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_FLAGS_APERTURE_VIDMEM           (0x00000000U)
#define NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_FLAGS_APERTURE_SYSMEM_COH       (0x00000001U)
#define NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_FLAGS_APERTURE_SYSMEM_NONCOH    (0x00000002U)
#define NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_FLAGS_PRESERVE_PDES             2:2
#define NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_FLAGS_PRESERVE_PDES_FALSE       (0x00000000U)
#define NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_FLAGS_PRESERVE_PDES_TRUE        (0x00000001U)
#define NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_FLAGS_ALL_CHANNELS              3:3
#define NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_FLAGS_ALL_CHANNELS_FALSE        (0x00000000U)
#define NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_FLAGS_ALL_CHANNELS_TRUE         (0x00000001U)
#define NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_FLAGS_IGNORE_CHANNEL_BUSY       4:4
#define NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_FLAGS_IGNORE_CHANNEL_BUSY_FALSE (0x00000000U)
#define NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_FLAGS_IGNORE_CHANNEL_BUSY_TRUE  (0x00000001U)
#define NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_FLAGS_EXTEND_VASPACE            5:5
#define NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_FLAGS_EXTEND_VASPACE_FALSE      (0x00000000U)
#define NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_FLAGS_EXTEND_VASPACE_TRUE       (0x00000001U)

#define NV0080_CTRL_CMD_DMA_UNSET_PAGE_DIRECTORY                           (0x801814U) /* finn: Evaluated from "(FINN_NV01_DEVICE_0_DMA_INTERFACE_ID << 8) | NV0080_CTRL_DMA_UNSET_PAGE_DIRECTORY_PARAMS_MESSAGE_ID" */

typedef struct NV0080_CTRL_DMA_UNSET_PAGE_DIRECTORY_PARAMS {
    NvHandle hVASpace;
    NvU32    subDeviceId; // ID+1, 0 for BC
} NV0080_CTRL_DMA_UNSET_PAGE_DIRECTORY_PARAMS;
#endif
