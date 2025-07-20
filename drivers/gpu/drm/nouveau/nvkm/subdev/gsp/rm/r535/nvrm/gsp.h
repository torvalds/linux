/* SPDX-License-Identifier: MIT */

/* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved. */

#ifndef __NVRM_GSP_H__
#define __NVRM_GSP_H__
#include <nvrm/nvtypes.h>

/* Excerpt of RM headers from https://github.com/NVIDIA/open-gpu-kernel-modules/tree/535.113.01 */

#define NV2080_CTRL_CMD_FB_GET_FB_REGION_INFO_MAX_ENTRIES 16U

#define NV2080_CTRL_CMD_FB_GET_FB_REGION_INFO_MEM_TYPES   17U

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

typedef struct NV2080_CTRL_GPU_GET_FERMI_GPC_INFO_PARAMS {
    NvU32 gpcMask;
} NV2080_CTRL_GPU_GET_FERMI_GPC_INFO_PARAMS;

typedef struct NV2080_CTRL_GPU_GET_FERMI_TPC_INFO_PARAMS {
    NvU32 gpcId;
    NvU32 tpcMask;
} NV2080_CTRL_GPU_GET_FERMI_TPC_INFO_PARAMS;

typedef struct NV2080_CTRL_GPU_GET_FERMI_ZCULL_INFO_PARAMS {
    NvU32 gpcId;
    NvU32 zcullMask;
} NV2080_CTRL_GPU_GET_FERMI_ZCULL_INFO_PARAMS;

typedef struct NV2080_CTRL_BIOS_GET_SKU_INFO_PARAMS {
    NvU32 BoardID;
    char  chipSKU[4];
    char  chipSKUMod[2];
    char  project[5];
    char  projectSKU[5];
    char  CDP[6];
    char  projectSKUMod[2];
    NvU32 businessCycle;
} NV2080_CTRL_BIOS_GET_SKU_INFO_PARAMS;

typedef enum
{
    COMPUTE_BRANDING_TYPE_NONE,
    COMPUTE_BRANDING_TYPE_TESLA,
} COMPUTE_BRANDING_TYPE;

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
} NV0080_CTRL_GPU_GET_SRIOV_CAPS_PARAMS;

#include "engine.h"

#define NVGPU_ENGINE_CAPS_MASK_BITS                32

#define NVGPU_ENGINE_CAPS_MASK_ARRAY_MAX           ((RM_ENGINE_TYPE_LAST-1)/NVGPU_ENGINE_CAPS_MASK_BITS + 1)

typedef struct GspSMInfo_t
{
    NvU32 version;
    NvU32 regBankCount;
    NvU32 regBankRegCount;
    NvU32 maxWarpsPerSM;
    NvU32 maxThreadsPerWarp;
    NvU32 geomGsObufEntries;
    NvU32 geomXbufEntries;
    NvU32 maxSPPerSM;
    NvU32 rtCoreCount;
} GspSMInfo;

typedef enum NV2080_CTRL_CMD_GR_CTXSW_PREEMPTION_BIND_BUFFERS {
    NV2080_CTRL_CMD_GR_CTXSW_PREEMPTION_BIND_BUFFERS_MAIN = 0,
    NV2080_CTRL_CMD_GR_CTXSW_PREEMPTION_BIND_BUFFERS_SPILL = 1,
    NV2080_CTRL_CMD_GR_CTXSW_PREEMPTION_BIND_BUFFERS_PAGEPOOL = 2,
    NV2080_CTRL_CMD_GR_CTXSW_PREEMPTION_BIND_BUFFERS_BETACB = 3,
    NV2080_CTRL_CMD_GR_CTXSW_PREEMPTION_BIND_BUFFERS_RTV = 4,
    NV2080_CTRL_CMD_GR_CTXSW_PREEMPTION_BIND_BUFFERS_CONTEXT_POOL = 5,
    NV2080_CTRL_CMD_GR_CTXSW_PREEMPTION_BIND_BUFFERS_CONTEXT_POOL_CONTROL = 6,
    NV2080_CTRL_CMD_GR_CTXSW_PREEMPTION_BIND_BUFFERS_CONTEXT_POOL_CONTROL_CPU = 7,
    NV2080_CTRL_CMD_GR_CTXSW_PREEMPTION_BIND_BUFFERS_END = 8,
} NV2080_CTRL_CMD_GR_CTXSW_PREEMPTION_BIND_BUFFERS;

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

typedef struct GspStaticConfigInfo_t
{
    NvU8 grCapsBits[NV0080_CTRL_GR_CAPS_TBL_SIZE];
    NV2080_CTRL_GPU_GET_GID_INFO_PARAMS gidInfo;
    NV2080_CTRL_GPU_GET_FERMI_GPC_INFO_PARAMS gpcInfo;
    NV2080_CTRL_GPU_GET_FERMI_TPC_INFO_PARAMS tpcInfo[MAX_GPC_COUNT];
    NV2080_CTRL_GPU_GET_FERMI_ZCULL_INFO_PARAMS zcullInfo[MAX_GPC_COUNT];
    NV2080_CTRL_BIOS_GET_SKU_INFO_PARAMS SKUInfo;
    NV2080_CTRL_CMD_FB_GET_FB_REGION_INFO_PARAMS fbRegionInfoParams;
    COMPUTE_BRANDING_TYPE computeBranding;

    NV0080_CTRL_GPU_GET_SRIOV_CAPS_PARAMS sriovCaps;
    NvU32 sriovMaxGfid;

    NvU32 engineCaps[NVGPU_ENGINE_CAPS_MASK_ARRAY_MAX];

    GspSMInfo SM_info;

    NvBool poisonFuseEnabled;

    NvU64 fb_length;
    NvU32 fbio_mask;
    NvU32 fb_bus_width;
    NvU32 fb_ram_type;
    NvU32 fbp_mask;
    NvU32 l2_cache_size;

    NvU32 gfxpBufferSize[NV2080_CTRL_CMD_GR_CTXSW_PREEMPTION_BIND_BUFFERS_CONTEXT_POOL];
    NvU32 gfxpBufferAlignment[NV2080_CTRL_CMD_GR_CTXSW_PREEMPTION_BIND_BUFFERS_CONTEXT_POOL];

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
    NvBool bIsGcOffRtd3Allowed;
    NvBool bIsGcoffLegacyAllowed;

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
} GspStaticConfigInfo;

typedef struct rpc_unloading_guest_driver_v1F_07
{
    NvBool     bInPMTransition;
    NvBool     bGc6Entering;
    NvU32      newLevel;
} rpc_unloading_guest_driver_v1F_07;

typedef struct PACKED_REGISTRY_ENTRY
{
    NvU32                   nameOffset;
    NvU8                    type;
    NvU32                   data;
    NvU32                   length;
} PACKED_REGISTRY_ENTRY;

typedef struct PACKED_REGISTRY_TABLE
{
    NvU32                   size;
    NvU32                   numEntries;
    PACKED_REGISTRY_ENTRY   entries[] __counted_by(numEntries);
} PACKED_REGISTRY_TABLE;

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

typedef struct GspSystemInfo
{
    NvU64 gpuPhysAddr;
    NvU64 gpuPhysFbAddr;
    NvU64 gpuPhysInstAddr;
    NvU64 nvDomainBusDeviceFunc;
    NvU64 simAccessBufPhysAddr;
    NvU64 pcieAtomicsOpMask;
    NvU64 consoleMemSize;
    NvU64 maxUserVa;
    NvU32 pciConfigMirrorBase;
    NvU32 pciConfigMirrorSize;
    NvU8 oorArch;
    NvU64 clPdbProperties;
    NvU32 Chipset;
    NvBool bGpuBehindBridge;
    NvBool bMnocAvailable;
    NvBool bUpstreamL0sUnsupported;
    NvBool bUpstreamL1Unsupported;
    NvBool bUpstreamL1PorSupported;
    NvBool bUpstreamL1PorMobileOnly;
    NvU8   upstreamAddressValid;
    BUSINFO FHBBusInfo;
    BUSINFO chipsetIDInfo;
    ACPI_METHOD_DATA acpiMethodData;
    NvU32 hypervisorType;
    NvBool bIsPassthru;
    NvU64 sysTimerOffsetNs;
    GSP_VF_INFO gspVFInfo;
} GspSystemInfo;

typedef struct rpc_os_error_log_v17_00
{
    NvU32      exceptType;
    NvU32      runlistId;
    NvU32      chid;
    char       errString[0x100];
} rpc_os_error_log_v17_00;

typedef struct rpc_run_cpu_sequencer_v17_00
{
    NvU32      bufferSizeDWord;
    NvU32      cmdIndex;
    NvU32      regSaveArea[8];
    NvU32      commandBuffer[];
} rpc_run_cpu_sequencer_v17_00;

typedef enum GSP_SEQ_BUF_OPCODE
{
    GSP_SEQ_BUF_OPCODE_REG_WRITE = 0,
    GSP_SEQ_BUF_OPCODE_REG_MODIFY,
    GSP_SEQ_BUF_OPCODE_REG_POLL,
    GSP_SEQ_BUF_OPCODE_DELAY_US,
    GSP_SEQ_BUF_OPCODE_REG_STORE,
    GSP_SEQ_BUF_OPCODE_CORE_RESET,
    GSP_SEQ_BUF_OPCODE_CORE_START,
    GSP_SEQ_BUF_OPCODE_CORE_WAIT_FOR_HALT,
    GSP_SEQ_BUF_OPCODE_CORE_RESUME,
} GSP_SEQ_BUF_OPCODE;

typedef struct
{
    NvU32 addr;
    NvU32 val;
} GSP_SEQ_BUF_PAYLOAD_REG_WRITE;

typedef struct
{
    NvU32 addr;
    NvU32 mask;
    NvU32 val;
} GSP_SEQ_BUF_PAYLOAD_REG_MODIFY;

typedef struct
{
    NvU32 addr;
    NvU32 mask;
    NvU32 val;
    NvU32 timeout;
    NvU32 error;
} GSP_SEQ_BUF_PAYLOAD_REG_POLL;

typedef struct
{
    NvU32 val;
} GSP_SEQ_BUF_PAYLOAD_DELAY_US;

typedef struct
{
    NvU32 addr;
    NvU32 index;
} GSP_SEQ_BUF_PAYLOAD_REG_STORE;

typedef struct GSP_SEQUENCER_BUFFER_CMD
{
    GSP_SEQ_BUF_OPCODE opCode;
    union
    {
        GSP_SEQ_BUF_PAYLOAD_REG_WRITE regWrite;
        GSP_SEQ_BUF_PAYLOAD_REG_MODIFY regModify;
        GSP_SEQ_BUF_PAYLOAD_REG_POLL regPoll;
        GSP_SEQ_BUF_PAYLOAD_DELAY_US delayUs;
        GSP_SEQ_BUF_PAYLOAD_REG_STORE regStore;
    } payload;
} GSP_SEQUENCER_BUFFER_CMD;

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

    // Pad structure to exactly 256 bytes.  Can replace padding with additional
    // fields without incrementing revision.  Padding initialized to 0.
    NvU8 padding[7];

    // BL to use for verification (i.e. Booter says OK to boot)
    NvU64 verified;  // 0x0 -> unverified, 0xa0a0a0a0a0a0a0a0 -> verified
} GspFwWprMeta;

#define GSP_FW_WPR_META_MAGIC     0xdc3aae21371a60b3ULL

#define GSP_FW_WPR_META_REVISION  1

typedef struct
{
    NvU32 version;   // queue version
    NvU32 size;      // bytes, page aligned
    NvU32 msgSize;   // entry size, bytes, must be power-of-2, 16 is minimum
    NvU32 msgCount;  // number of entries in queue
    NvU32 writePtr;  // message id of next slot
    NvU32 flags;     // if set it means "i want to swap RX"
    NvU32 rxHdrOff;  // Offset of msgqRxHeader from start of backing store.
    NvU32 entryOff;  // Offset of entries from start of backing store.
} msgqTxHeader;

typedef struct
{
    NvU32 readPtr; // message id of last message read
} msgqRxHeader;

typedef struct {
    RmPhysAddr sharedMemPhysAddr;
    NvU32 pageTableEntryCount;
    NvLength cmdQueueOffset;
    NvLength statQueueOffset;
    NvLength locklessCmdQueueOffset;
    NvLength locklessStatQueueOffset;
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

    struct
    {
        NvU64                         pa;
        NvU64                         size;
    } profilerArgs;
} GSP_ARGUMENTS_CACHED;

#define NV2080_CTRL_GPU_SET_POWER_STATE_GPU_LEVEL_0            (0x00000000U)

#define NV2080_CTRL_GPU_SET_POWER_STATE_GPU_LEVEL_3            (0x00000003U)

typedef NvU64 LibosAddress;

typedef struct
{
    LibosAddress          id8;  // Id tag.
    LibosAddress          pa;   // Physical address.
    LibosAddress          size; // Size of memory area.
    NvU8                  kind; // See LibosMemoryRegionKind above.
    NvU8                  loc;  // See LibosMemoryRegionLoc above.
} LibosMemoryRegionInitArgument;

typedef enum {
    LIBOS_MEMORY_REGION_NONE,
    LIBOS_MEMORY_REGION_CONTIGUOUS,
    LIBOS_MEMORY_REGION_RADIX3
} LibosMemoryRegionKind;

typedef enum {
    LIBOS_MEMORY_REGION_LOC_NONE,
    LIBOS_MEMORY_REGION_LOC_SYSMEM,
    LIBOS_MEMORY_REGION_LOC_FB
} LibosMemoryRegionLoc;

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

#define GSP_FW_SR_META_MAGIC     0x8a3bb9e6c6c39d93ULL

#define GSP_FW_SR_META_REVISION  2

#define GSP_SEQUENCER_PAYLOAD_SIZE_DWORDS(opcode)                       \
    ((opcode == GSP_SEQ_BUF_OPCODE_REG_WRITE)  ? (sizeof(GSP_SEQ_BUF_PAYLOAD_REG_WRITE)  / sizeof(NvU32)) : \
     (opcode == GSP_SEQ_BUF_OPCODE_REG_MODIFY) ? (sizeof(GSP_SEQ_BUF_PAYLOAD_REG_MODIFY) / sizeof(NvU32)) : \
     (opcode == GSP_SEQ_BUF_OPCODE_REG_POLL)   ? (sizeof(GSP_SEQ_BUF_PAYLOAD_REG_POLL)   / sizeof(NvU32)) : \
     (opcode == GSP_SEQ_BUF_OPCODE_DELAY_US)   ? (sizeof(GSP_SEQ_BUF_PAYLOAD_DELAY_US)   / sizeof(NvU32)) : \
     (opcode == GSP_SEQ_BUF_OPCODE_REG_STORE)  ? (sizeof(GSP_SEQ_BUF_PAYLOAD_REG_STORE)  / sizeof(NvU32)) : \
    /* GSP_SEQ_BUF_OPCODE_CORE_RESET */                                 \
    /* GSP_SEQ_BUF_OPCODE_CORE_START */                                 \
    /* GSP_SEQ_BUF_OPCODE_CORE_WAIT_FOR_HALT */                         \
    /* GSP_SEQ_BUF_OPCODE_CORE_RESUME */                                \
    0)

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

typedef struct NV2080_CTRL_INTERNAL_INTR_GET_KERNEL_TABLE_ENTRY {
    NvU16 engineIdx;
    NvU32 pmcIntrMask;
    NvU32 vectorStall;
    NvU32 vectorNonStall;
} NV2080_CTRL_INTERNAL_INTR_GET_KERNEL_TABLE_ENTRY;

typedef struct NV2080_INTR_CATEGORY_SUBTREE_MAP {
    NvU8 subtreeStart;
    NvU8 subtreeEnd;
} NV2080_INTR_CATEGORY_SUBTREE_MAP;

#define NV2080_CTRL_INTERNAL_INTR_MAX_TABLE_SIZE       128

typedef enum NV2080_INTR_CATEGORY {
    NV2080_INTR_CATEGORY_DEFAULT = 0,
    NV2080_INTR_CATEGORY_ESCHED_DRIVEN_ENGINE = 1,
    NV2080_INTR_CATEGORY_ESCHED_DRIVEN_ENGINE_NOTIFICATION = 2,
    NV2080_INTR_CATEGORY_RUNLIST = 3,
    NV2080_INTR_CATEGORY_RUNLIST_NOTIFICATION = 4,
    NV2080_INTR_CATEGORY_UVM_OWNED = 5,
    NV2080_INTR_CATEGORY_UVM_SHARED = 6,
    NV2080_INTR_CATEGORY_ENUM_COUNT = 7,
} NV2080_INTR_CATEGORY;

#define NV2080_CTRL_CMD_INTERNAL_INTR_GET_KERNEL_TABLE (0x20800a5c) /* finn: Evaluated from "(FINN_NV20_SUBDEVICE_0_INTERNAL_INTERFACE_ID << 8) | NV2080_CTRL_INTERNAL_INTR_GET_KERNEL_TABLE_PARAMS_MESSAGE_ID" */
typedef struct NV2080_CTRL_INTERNAL_INTR_GET_KERNEL_TABLE_PARAMS {
    NvU32                                            tableLen;
    NV2080_CTRL_INTERNAL_INTR_GET_KERNEL_TABLE_ENTRY table[NV2080_CTRL_INTERNAL_INTR_MAX_TABLE_SIZE];
    NV2080_INTR_CATEGORY_SUBTREE_MAP                 subtreeMap[NV2080_INTR_CATEGORY_ENUM_COUNT];
} NV2080_CTRL_INTERNAL_INTR_GET_KERNEL_TABLE_PARAMS;

#define GSP_FW_HEAP_PARAM_SIZE_PER_GB_FB                  (96 << 10)   // All architectures

#define GSP_FW_HEAP_PARAM_CLIENT_ALLOC_SIZE      ((48 << 10) * 2048)   // Support 2048 channels

typedef union rpc_message_rpc_union_field_v03_00
{
    NvU32      spare;
    NvU32      cpuRmGfid;
} rpc_message_rpc_union_field_v03_00;

typedef rpc_message_rpc_union_field_v03_00 rpc_message_rpc_union_field_v;

typedef struct rpc_message_header_v03_00
{
    NvU32      header_version;
    NvU32      signature;
    NvU32      length;
    NvU32      function;
    NvU32      rpc_result;
    NvU32      rpc_result_private;
    NvU32      sequence;
    rpc_message_rpc_union_field_v u;
    rpc_generic_union rpc_message_data[];
} rpc_message_header_v03_00;

typedef rpc_message_header_v03_00 rpc_message_header_v;

typedef struct GSP_MSG_QUEUE_ELEMENT
{
    NvU8  authTagBuffer[16];         // Authentication tag buffer.
    NvU8  aadBuffer[16];             // AAD buffer.
    NvU32 checkSum;                  // Set to value needed to make checksum always zero.
    NvU32 seqNum;                    // Sequence number maintained by the message queue.
    NvU32 elemCount;                 // Number of message queue elements this message has.
    NV_DECLARE_ALIGNED(rpc_message_header_v rpc, 8);
} GSP_MSG_QUEUE_ELEMENT;

#define GSP_FW_HEAP_PARAM_OS_SIZE_LIBOS2                   (0 << 20)   // No FB heap usage
#define GSP_FW_HEAP_PARAM_OS_SIZE_LIBOS3                  (20 << 20)

#define GSP_FW_HEAP_PARAM_BASE_RM_SIZE_TU10X               (8 << 20)   // Turing thru Ada

#define GSP_FW_HEAP_SIZE_OVERRIDE_LIBOS2_MIN_MB                (64u)
#define GSP_FW_HEAP_SIZE_OVERRIDE_LIBOS3_BAREMETAL_MIN_MB      (84u)
#endif
