/* SPDX-License-Identifier: MIT */

/* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved. */

#ifndef __NVRM_GSP_H__
#define __NVRM_GSP_H__
#include <nvrm/nvtypes.h>

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/570.144 */

#define NV2080_CTRL_CMD_FB_GET_FB_REGION_INFO_MAX_ENTRIES 16U

#define NV2080_CTRL_CMD_FB_GET_FB_REGION_INFO_MEM_TYPES           17U

typedef NvBool NV2080_CTRL_CMD_FB_GET_FB_REGION_SURFACE_MEM_TYPE_FLAG[NV2080_CTRL_CMD_FB_GET_FB_REGION_INFO_MEM_TYPES];

typedef struct NV2080_CTRL_CMD_FB_GET_FB_REGION_FB_REGION_INFO {
    NV_DECLARE_ALIGNED(NvU64 base, 8);
    NV_DECLARE_ALIGNED(NvU64 limit, 8);
    NV_DECLARE_ALIGNED(NvU64 reserved, 8);
    NvU32                                                  performance;
    NvBool                                                 supportCompressed;
    NvBool                                                 supportISO;
    NvBool                                                 bProtected;
    NV2080_CTRL_CMD_FB_GET_FB_REGION_SURFACE_MEM_TYPE_FLAG blackList;
} NV2080_CTRL_CMD_FB_GET_FB_REGION_FB_REGION_INFO;

typedef struct NV2080_CTRL_CMD_FB_GET_FB_REGION_INFO_PARAMS {
    NvU32 numFBRegions;
    NV_DECLARE_ALIGNED(NV2080_CTRL_CMD_FB_GET_FB_REGION_FB_REGION_INFO fbRegion[NV2080_CTRL_CMD_FB_GET_FB_REGION_INFO_MAX_ENTRIES], 8);
} NV2080_CTRL_CMD_FB_GET_FB_REGION_INFO_PARAMS;

#define NV0080_CTRL_GR_CAPS_TBL_SIZE            23

#define NV2080_GPU_MAX_GID_LENGTH             (0x000000100ULL)

typedef struct NV2080_CTRL_GPU_GET_GID_INFO_PARAMS {
    NvU32 index;
    NvU32 flags;
    NvU32 length;
    NvU8  data[NV2080_GPU_MAX_GID_LENGTH];
} NV2080_CTRL_GPU_GET_GID_INFO_PARAMS;

typedef struct NV2080_CTRL_BIOS_GET_SKU_INFO_PARAMS {
    NvU32 BoardID;
    char  chipSKU[9];
    char  chipSKUMod[5];
    NvU32 skuConfigVersion;
    char  project[5];
    char  projectSKU[5];
    char  CDP[6];
    char  projectSKUMod[2];
    NvU32 businessCycle;
} NV2080_CTRL_BIOS_GET_SKU_INFO_PARAMS;

#define MAX_GPC_COUNT           32

typedef struct NV0080_CTRL_GPU_GET_SRIOV_CAPS_PARAMS {
    NvU32  totalVFs;
    NvU32  firstVfOffset;
    NvU32  vfFeatureMask;
    NV_DECLARE_ALIGNED(NvU64 FirstVFBar0Address, 8);
    NV_DECLARE_ALIGNED(NvU64 FirstVFBar1Address, 8);
    NV_DECLARE_ALIGNED(NvU64 FirstVFBar2Address, 8);
    NV_DECLARE_ALIGNED(NvU64 bar0Size, 8);
    NV_DECLARE_ALIGNED(NvU64 bar1Size, 8);
    NV_DECLARE_ALIGNED(NvU64 bar2Size, 8);
    NvBool b64bitBar0;
    NvBool b64bitBar1;
    NvBool b64bitBar2;
    NvBool bSriovEnabled;
    NvBool bSriovHeavyEnabled;
    NvBool bEmulateVFBar0TlbInvalidationRegister;
    NvBool bClientRmAllocatedCtxBuffer;
    NvBool bNonPowerOf2ChannelCountSupported;
    NvBool bVfResizableBAR1Supported;
} NV0080_CTRL_GPU_GET_SRIOV_CAPS_PARAMS;

#include "engine.h"

#define NVGPU_ENGINE_CAPS_MASK_BITS                32

#define NVGPU_ENGINE_CAPS_MASK_ARRAY_MAX           ((RM_ENGINE_TYPE_LAST-1)/NVGPU_ENGINE_CAPS_MASK_BITS + 1)

#define NV2080_GPU_MAX_NAME_STRING_LENGTH                  (0x0000040U)

typedef struct VIRTUAL_DISPLAY_GET_NUM_HEADS_PARAMS 
{
    NvU32 numHeads;
    NvU32 maxNumHeads;
} VIRTUAL_DISPLAY_GET_NUM_HEADS_PARAMS;

typedef struct VIRTUAL_DISPLAY_GET_MAX_RESOLUTION_PARAMS 
{
    NvU32 headIndex;
    NvU32 maxHResolution;
    NvU32 maxVResolution;
} VIRTUAL_DISPLAY_GET_MAX_RESOLUTION_PARAMS;

#define MAX_GROUP_COUNT 2

typedef struct
{
    NvU32 ecidLow;
    NvU32 ecidHigh;
    NvU32 ecidExtended;
} EcidManufacturingInfo;

typedef struct
{
    NvU64 nonWprHeapOffset;
    NvU64 frtsOffset;
} FW_WPR_LAYOUT_OFFSET;

typedef struct GspStaticConfigInfo_t
{
    NvU8 grCapsBits[NV0080_CTRL_GR_CAPS_TBL_SIZE];
    NV2080_CTRL_GPU_GET_GID_INFO_PARAMS gidInfo;
    NV2080_CTRL_BIOS_GET_SKU_INFO_PARAMS SKUInfo;
    NV2080_CTRL_CMD_FB_GET_FB_REGION_INFO_PARAMS fbRegionInfoParams;

    NV0080_CTRL_GPU_GET_SRIOV_CAPS_PARAMS sriovCaps;
    NvU32 sriovMaxGfid;

    NvU32 engineCaps[NVGPU_ENGINE_CAPS_MASK_ARRAY_MAX];

    NvBool poisonFuseEnabled;

    NvU64 fb_length;
    NvU64 fbio_mask;
    NvU32 fb_bus_width;
    NvU32 fb_ram_type;
    NvU64 fbp_mask;
    NvU32 l2_cache_size;

    NvU8 gpuNameString[NV2080_GPU_MAX_NAME_STRING_LENGTH];
    NvU8 gpuShortNameString[NV2080_GPU_MAX_NAME_STRING_LENGTH];
    NvU16 gpuNameString_Unicode[NV2080_GPU_MAX_NAME_STRING_LENGTH];
    NvBool bGpuInternalSku;
    NvBool bIsQuadroGeneric;
    NvBool bIsQuadroAd;
    NvBool bIsNvidiaNvs;
    NvBool bIsVgx;
    NvBool bGeforceSmb;
    NvBool bIsTitan;
    NvBool bIsTesla;
    NvBool bIsMobile;
    NvBool bIsGc6Rtd3Allowed;
    NvBool bIsGc8Rtd3Allowed;
    NvBool bIsGcOffRtd3Allowed;
    NvBool bIsGcoffLegacyAllowed;
    NvBool bIsMigSupported;

    /* "Total Board Power" refers to power requirement of GPU,
     * while in GC6 state. Majority of this power will be used
     * to keep V-RAM active to preserve its content.
     * Some energy maybe consumed by Always-on components on GPU chip.
     * This power will be provided by 3.3v voltage rail.
     */
    NvU16  RTD3GC6TotalBoardPower;

    /* PERST# (i.e. PCI Express Reset) is a sideband signal
     * generated by the PCIe Host to indicate the PCIe devices,
     * that the power-rails and the reference-clock are stable.
     * The endpoint device typically uses this signal as a global reset.
     */
    NvU16  RTD3GC6PerstDelay;

    NvU64 bar1PdeBase;
    NvU64 bar2PdeBase;

    NvBool bVbiosValid;
    NvU32 vbiosSubVendor;
    NvU32 vbiosSubDevice;

    NvBool bPageRetirementSupported;

    NvBool bSplitVasBetweenServerClientRm;

    NvBool bClRootportNeedsNosnoopWAR;

    VIRTUAL_DISPLAY_GET_NUM_HEADS_PARAMS displaylessMaxHeads;
    VIRTUAL_DISPLAY_GET_MAX_RESOLUTION_PARAMS displaylessMaxResolution;
    NvU64 displaylessMaxPixels;

    // Client handle for internal RMAPI control.
    NvHandle hInternalClient;

    // Device handle for internal RMAPI control.
    NvHandle hInternalDevice;

    // Subdevice handle for internal RMAPI control.
    NvHandle hInternalSubdevice;

    NvBool bSelfHostedMode;
    NvBool bAtsSupported;

    NvBool bIsGpuUefi;
    NvBool bIsEfiInit;

    EcidManufacturingInfo ecidInfo[MAX_GROUP_COUNT];

    FW_WPR_LAYOUT_OFFSET fwWprLayoutOffset;
} GspStaticConfigInfo;

typedef struct
{
    NvU16               deviceID;           // deviceID
    NvU16               vendorID;           // vendorID
    NvU16               subdeviceID;        // subsystem deviceID
    NvU16               subvendorID;        // subsystem vendorID
    NvU8                revisionID;         // revision ID
} BUSINFO;

#define NV0073_CTRL_SYSTEM_ACPI_ID_MAP_MAX_DISPLAYS             (16U)

typedef struct DOD_METHOD_DATA
{
    NV_STATUS status;
    NvU32     acpiIdListLen;
    NvU32     acpiIdList[NV0073_CTRL_SYSTEM_ACPI_ID_MAP_MAX_DISPLAYS];
} DOD_METHOD_DATA;

typedef struct JT_METHOD_DATA
{
    NV_STATUS status;
    NvU32     jtCaps;
    NvU16     jtRevId;
    NvBool    bSBIOSCaps;
} JT_METHOD_DATA;

typedef struct MUX_METHOD_DATA_ELEMENT
{
    NvU32       acpiId;
    NvU32       mode;
    NV_STATUS   status;
} MUX_METHOD_DATA_ELEMENT;

#define NV0073_CTRL_SYSTEM_ACPI_ID_MAP_MAX_DISPLAYS             (16U)

typedef struct MUX_METHOD_DATA
{
    NvU32                       tableLen;
    MUX_METHOD_DATA_ELEMENT     acpiIdMuxModeTable[NV0073_CTRL_SYSTEM_ACPI_ID_MAP_MAX_DISPLAYS];
    MUX_METHOD_DATA_ELEMENT     acpiIdMuxPartTable[NV0073_CTRL_SYSTEM_ACPI_ID_MAP_MAX_DISPLAYS];
    MUX_METHOD_DATA_ELEMENT     acpiIdMuxStateTable[NV0073_CTRL_SYSTEM_ACPI_ID_MAP_MAX_DISPLAYS];    
} MUX_METHOD_DATA;

typedef struct CAPS_METHOD_DATA
{
    NV_STATUS status;
    NvU32     optimusCaps;
} CAPS_METHOD_DATA;

typedef struct ACPI_METHOD_DATA
{
    NvBool                                               bValid;
    DOD_METHOD_DATA                                      dodMethodData;
    JT_METHOD_DATA                                       jtMethodData;
    MUX_METHOD_DATA                                      muxMethodData;
    CAPS_METHOD_DATA                                     capsMethodData;
} ACPI_METHOD_DATA;

typedef struct GSP_VF_INFO
{
    NvU32  totalVFs;
    NvU32  firstVFOffset;
    NvU64  FirstVFBar0Address;
    NvU64  FirstVFBar1Address;
    NvU64  FirstVFBar2Address;
    NvBool b64bitBar0;
    NvBool b64bitBar1;
    NvBool b64bitBar2;
} GSP_VF_INFO;

typedef struct
{
    // Link capabilities
    NvU32 linkCap;
} GSP_PCIE_CONFIG_REG;

typedef struct GspSystemInfo
{
    NvU64 gpuPhysAddr;
    NvU64 gpuPhysFbAddr;
    NvU64 gpuPhysInstAddr;
    NvU64 gpuPhysIoAddr;
    NvU64 nvDomainBusDeviceFunc;
    NvU64 simAccessBufPhysAddr;
    NvU64 notifyOpSharedSurfacePhysAddr;
    NvU64 pcieAtomicsOpMask;
    NvU64 consoleMemSize;
    NvU64 maxUserVa;
    NvU32 pciConfigMirrorBase;
    NvU32 pciConfigMirrorSize;
    NvU32 PCIDeviceID;
    NvU32 PCISubDeviceID;
    NvU32 PCIRevisionID;
    NvU32 pcieAtomicsCplDeviceCapMask;
    NvU8 oorArch;
    NvU64 clPdbProperties;
    NvU32 Chipset;
    NvBool bGpuBehindBridge;
    NvBool bFlrSupported;
    NvBool b64bBar0Supported;
    NvBool bMnocAvailable;
    NvU32  chipsetL1ssEnable;
    NvBool bUpstreamL0sUnsupported;
    NvBool bUpstreamL1Unsupported;
    NvBool bUpstreamL1PorSupported;
    NvBool bUpstreamL1PorMobileOnly;
    NvBool bSystemHasMux;
    NvU8   upstreamAddressValid;
    BUSINFO FHBBusInfo;
    BUSINFO chipsetIDInfo;
    ACPI_METHOD_DATA acpiMethodData;
    NvU32 hypervisorType;
    NvBool bIsPassthru;
    NvU64 sysTimerOffsetNs;
    GSP_VF_INFO gspVFInfo;
    NvBool bIsPrimary;
    NvBool isGridBuild;
    GSP_PCIE_CONFIG_REG pcieConfigReg;
    NvU32 gridBuildCsp;
    NvBool bPreserveVideoMemoryAllocations;
    NvBool bTdrEventSupported;
    NvBool bFeatureStretchVblankCapable;
    NvBool bEnableDynamicGranularityPageArrays;
    NvBool bClockBoostSupported;
    NvBool bRouteDispIntrsToCPU;
    NvU64  hostPageSize;
} GspSystemInfo;

typedef struct rpc_os_error_log_v17_00
{
    NvU32      exceptType;
    NvU32      runlistId;
    NvU32      chid;
    char       errString[0x100];
    NvU32      preemptiveRemovalPreviousXid;
} rpc_os_error_log_v17_00;

typedef struct
{
    // Magic
    // BL to use for verification (i.e. Booter locked it in WPR2)
    NvU64 magic; // = 0xdc3aae21371a60b3;

    // Revision number of Booter-BL-Sequencer handoff interface
    // Bumped up when we change this interface so it is not backward compatible.
    // Bumped up when we revoke GSP-RM ucode
    NvU64 revision; // = 1;

    // ---- Members regarding data in SYSMEM ----------------------------
    // Consumed by Booter for DMA

    NvU64 sysmemAddrOfRadix3Elf;
    NvU64 sizeOfRadix3Elf;

    NvU64 sysmemAddrOfBootloader;
    NvU64 sizeOfBootloader;

    // Offsets inside bootloader image needed by Booter
    NvU64 bootloaderCodeOffset;
    NvU64 bootloaderDataOffset;
    NvU64 bootloaderManifestOffset;

    union
    {
        // Used only at initial boot
        struct
        {
            NvU64 sysmemAddrOfSignature;
            NvU64 sizeOfSignature;
        };

        //
        // Used at suspend/resume to read GspFwHeapFreeList
        // Offset relative to GspFwWprMeta FBMEM PA (gspFwWprStart)
        //
        struct
        {
            NvU32 gspFwHeapFreeListWprOffset;
            NvU32 unused0;
            NvU64 unused1;
        };
    };

    // ---- Members describing FB layout --------------------------------
    NvU64 gspFwRsvdStart;

    NvU64 nonWprHeapOffset;
    NvU64 nonWprHeapSize;

    NvU64 gspFwWprStart;

    // GSP-RM to use to setup heap.
    NvU64 gspFwHeapOffset;
    NvU64 gspFwHeapSize;

    // BL to use to find ELF for jump
    NvU64 gspFwOffset;
    // Size is sizeOfRadix3Elf above.

    NvU64 bootBinOffset;
    // Size is sizeOfBootloader above.

    NvU64 frtsOffset;
    NvU64 frtsSize;

    NvU64 gspFwWprEnd;

    // GSP-RM to use for fbRegionInfo?
    NvU64 fbSize;

    // ---- Other members -----------------------------------------------

    // GSP-RM to use for fbRegionInfo?
    NvU64 vgaWorkspaceOffset;
    NvU64 vgaWorkspaceSize;

    // Boot count.  Used to determine whether to load the firmware image.
    NvU64 bootCount;

    // This union is organized the way it is to start at an 8-byte boundary and achieve natural
    // packing of the internal struct fields.
    union
    {
        struct
        {
            // TODO: the partitionRpc* fields below do not really belong in this
            //       structure. The values are patched in by the partition bootstrapper
            //       when GSP-RM is booted in a partition, and this structure was a
            //       convenient place for the bootstrapper to access them. These should
            //       be moved to a different comm. mechanism between the bootstrapper
            //       and the GSP-RM tasks.

            // Shared partition RPC memory (physical address)
            NvU64 partitionRpcAddr;

            // Offsets relative to partitionRpcAddr
            NvU16 partitionRpcRequestOffset;
            NvU16 partitionRpcReplyOffset;

            // Code section and dataSection offset and size.
            NvU32 elfCodeOffset;
            NvU32 elfDataOffset;
            NvU32 elfCodeSize;
            NvU32 elfDataSize;

            // Used during GSP-RM resume to check for revocation
            NvU32 lsUcodeVersion;
        };

        struct
        {
            // Pad for the partitionRpc* fields, plus 4 bytes
            NvU32 partitionRpcPadding[4];

            // CrashCat (contiguous) buffer size/location - occupies same bytes as the
            // elf(Code|Data)(Offset|Size) fields above.
            // TODO: move to GSP_FMC_INIT_PARAMS
            NvU64 sysmemAddrOfCrashReportQueue;
            NvU32 sizeOfCrashReportQueue;

            // Pad for the lsUcodeVersion field
            NvU32 lsUcodeVersionPadding[1];
        };
    };

    // Number of VF partitions allocating sub-heaps from the WPR heap
    // Used during boot to ensure the heap is adequately sized
    NvU8 gspFwHeapVfPartitionCount;

    // Flags to help decide GSP-FW flow.
    NvU8 flags;

    // Pad structure to exactly 256 bytes.  Can replace padding with additional
    // fields without incrementing revision.  Padding initialized to 0.
    NvU8 padding[2];

    //
    // Starts at gspFwWprEnd+frtsSize b/c FRTS is positioned
    // to end where this allocation starts (when RM requests FSP to create
    // FRTS).
    //
    NvU32 pmuReservedSize;

    // BL to use for verification (i.e. Booter says OK to boot)
    NvU64 verified;  // 0x0 -> unverified, 0xa0a0a0a0a0a0a0a0 -> verified
} GspFwWprMeta;

#define GSP_FW_WPR_META_MAGIC     0xdc3aae21371a60b3ULL

#define GSP_FW_WPR_META_REVISION  1

typedef struct {
    NvU64 sharedMemPhysAddr;
    NvU32 pageTableEntryCount;
    NvLength cmdQueueOffset;
    NvLength statQueueOffset;
} MESSAGE_QUEUE_INIT_ARGUMENTS;

typedef struct {
    NvU32 oldLevel;
    NvU32 flags;
    NvBool bInPMTransition;
} GSP_SR_INIT_ARGUMENTS;

typedef struct
{
    MESSAGE_QUEUE_INIT_ARGUMENTS      messageQueueInitArguments;
    GSP_SR_INIT_ARGUMENTS             srInitArguments;
    NvU32                             gpuInstance;
    NvBool                            bDmemStack;

    struct
    {
        NvU64                         pa;
        NvU64                         size;
    } profilerArgs;
} GSP_ARGUMENTS_CACHED;

#define NV2080_CTRL_GPU_SET_POWER_STATE_GPU_LEVEL_3            (0x00000003U)

typedef struct
{
    // Magic for verification by secure ucode
    NvU64 magic;  // = GSP_FW_SR_META_MAGIC;

    //
    // Revision number
    // Bumped up when we change this interface so it is not backward compatible.
    //
    NvU64 revision;  // = GSP_FW_SR_META_MAGIC_REVISION;

    // Members regarding data in SYSMEM
    NvU64 sysmemAddrOfSuspendResumeData;
    NvU64 sizeOfSuspendResumeData;

    //
    // Internal members for use by secure ucode
    // Must be exactly GSP_FW_SR_META_INTERNAL_SIZE bytes.
    //
    NvU32 internal[32];

    // Same as flags of GspFwWprMeta
    NvU32 flags;

    // Subrevision number used by secure ucode
    NvU32 subrevision;

    //
    // Pad structure to exactly 256 bytes (1 DMA chunk).
    // Padding initialized to zero.
    //
    NvU32 padding[22];
} GspFwSRMeta;

#define GSP_FW_HEAP_PARAM_OS_SIZE_LIBOS2                   (0 << 20)   // No FB heap usage

#define GSP_FW_HEAP_PARAM_OS_SIZE_LIBOS3_BAREMETAL        (22 << 20)

#define GSP_FW_HEAP_PARAM_BASE_RM_SIZE_TU10X               (8 << 20)   // Turing thru Ada

#define GSP_FW_HEAP_SIZE_OVERRIDE_LIBOS2_MIN_MB                (64u)

#define BULLSEYE_ROOT_HEAP_ALLOC_RM_DATA_SECTION_SIZE_DELTA         (12u)

#define BULLSEYE_ROOT_HEAP_ALLOC_BAREMETAL_LIBOS_HEAP_SIZE_DELTA    (70u)

#define GSP_FW_HEAP_SIZE_OVERRIDE_LIBOS3_BAREMETAL_MIN_MB \
                                    (88u + (BULLSEYE_ROOT_HEAP_ALLOC_RM_DATA_SECTION_SIZE_DELTA) + \
                                    (BULLSEYE_ROOT_HEAP_ALLOC_BAREMETAL_LIBOS_HEAP_SIZE_DELTA))

typedef struct GSP_FMC_INIT_PARAMS
{
    // CC initialization "registry keys"
    NvU32 regkeys;
} GSP_FMC_INIT_PARAMS;

typedef enum {
    GSP_DMA_TARGET_LOCAL_FB,
    GSP_DMA_TARGET_COHERENT_SYSTEM,
    GSP_DMA_TARGET_NONCOHERENT_SYSTEM,
    GSP_DMA_TARGET_COUNT
} GSP_DMA_TARGET;

typedef struct GSP_ACR_BOOT_GSP_RM_PARAMS
{
    // Physical memory aperture through which gspRmDescPa is accessed
    GSP_DMA_TARGET target;
    // Size in bytes of the GSP-RM descriptor structure
    NvU32          gspRmDescSize;
    // Physical offset in the target aperture of the GSP-RM descriptor structure
    NvU64          gspRmDescOffset;
    // Physical offset in FB to set the start of the WPR containing GSP-RM
    NvU64          wprCarveoutOffset;
    // Size in bytes of the WPR containing GSP-RM
    NvU32          wprCarveoutSize;
    // Whether to boot GSP-RM or GSP-Proxy through ACR
    NvBool         bIsGspRmBoot;
} GSP_ACR_BOOT_GSP_RM_PARAMS;

typedef struct GSP_RM_PARAMS
{
    // Physical memory aperture through which bootArgsOffset is accessed
    GSP_DMA_TARGET target;
    // Physical offset in the memory aperture that will be passed to GSP-RM
    NvU64          bootArgsOffset;
} GSP_RM_PARAMS;

typedef struct GSP_SPDM_PARAMS
{
    // Physical Memory Aperture through which all addresses are accessed
    GSP_DMA_TARGET target;

    // Physical offset in the memory aperture where SPDM payload is stored
    NvU64 payloadBufferOffset;

    // Size of the above payload buffer
    NvU32 payloadBufferSize;
} GSP_SPDM_PARAMS;

typedef struct GSP_FMC_BOOT_PARAMS
{
    GSP_FMC_INIT_PARAMS         initParams;
    GSP_ACR_BOOT_GSP_RM_PARAMS  bootGspRmParams;
    GSP_RM_PARAMS               gspRmParams;
    GSP_SPDM_PARAMS             gspSpdmParams;
} GSP_FMC_BOOT_PARAMS;

#define GSP_FW_HEAP_PARAM_BASE_RM_SIZE_GH100              (14 << 20)   // Hopper+
#endif
