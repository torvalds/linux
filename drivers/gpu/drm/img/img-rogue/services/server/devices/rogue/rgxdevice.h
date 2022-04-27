/*************************************************************************/ /*!
@File
@Title          RGX device node header file
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the RGX device node
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#if !defined(RGXDEVICE_H)
#define RGXDEVICE_H

#include "img_types.h"
#include "img_defs.h"
#include "pvrsrv_device_types.h"
#include "mmu_common.h"
#include "rgx_fwif_km.h"
#include "cache_ops.h"
#include "device.h"
#include "osfunc.h"
#include "rgxlayer_impl.h"
#if defined(SUPPORT_WORKLOAD_ESTIMATION)
#include "hash.h"
#endif
typedef struct _RGX_SERVER_COMMON_CONTEXT_ RGX_SERVER_COMMON_CONTEXT;

typedef struct {
	DEVMEM_MEMDESC		*psFWFrameworkMemDesc;
} RGX_COMMON_CONTEXT_INFO;


/*!
 ******************************************************************************
 * Device state flags
 *****************************************************************************/
#define RGXKM_DEVICE_STATE_ZERO_FREELIST                          (0x1)  /*!< Zeroing the physical pages of reconstructed free lists */
#define RGXKM_DEVICE_STATE_DISABLE_DW_LOGGING_EN                  (0x2)  /*!< Used to disable the Devices Watchdog logging */
#define RGXKM_DEVICE_STATE_GPU_UNITS_POWER_CHANGE_EN              (0x4)  /*!< Used for validation to inject dust requests every TA/3D kick */
#define RGXKM_DEVICE_STATE_CCB_GROW_EN                            (0x8)  /*!< Used to indicate CCB grow is permitted */
#define RGXKM_DEVICE_STATE_ENABLE_SPU_UNITS_POWER_MASK_CHANGE_EN  (0x10) /*!< Used for validation to enable SPU power state mask change */
#define RGXKM_DEVICE_STATE_MASK                                   (0x1F)

/*!
 ******************************************************************************
 * ECC RAM Fault Validation
 *****************************************************************************/
#define RGXKM_ECC_ERR_INJ_DISABLE 0
#define RGXKM_ECC_ERR_INJ_SLC     1
#define RGXKM_ECC_ERR_INJ_USC     2
#define RGXKM_ECC_ERR_INJ_TPU     3
#define RGXKM_ECC_ERR_INJ_RASCAL  4
#define RGXKM_ECC_ERR_INJ_MARS    5

#define RGXKM_ECC_ERR_INJ_INTERVAL 10U

/*!
 ******************************************************************************
 * GPU DVFS Table
 *****************************************************************************/

#define RGX_GPU_DVFS_TABLE_SIZE                      32
#define RGX_GPU_DVFS_FIRST_CALIBRATION_TIME_US       25000     /* Time required to calibrate a clock frequency the first time */
#define RGX_GPU_DVFS_TRANSITION_CALIBRATION_TIME_US  150000    /* Time required for a recalibration after a DVFS transition */
#define RGX_GPU_DVFS_PERIODIC_CALIBRATION_TIME_US    10000000  /* Time before the next periodic calibration and correlation */

/*!
 ******************************************************************************
 * Global flags for driver validation
 *****************************************************************************/
#define RGX_VAL_KZ_SIG_CHECK_NOERR_EN            (0x10U)  /*!< Enable KZ signature check. Signatures must match */
#define RGX_VAL_KZ_SIG_CHECK_ERR_EN              (0x20U)  /*!< Enable KZ signature check. Signatures must not match */
#define RGX_VAL_SIG_CHECK_ERR_EN                 (0U)     /*!< Not supported on Rogue cores */

typedef struct _GPU_FREQ_TRACKING_DATA_
{
	/* Core clock speed estimated by the driver */
	IMG_UINT32 ui32EstCoreClockSpeed;

	/* Amount of successful calculations of the estimated core clock speed */
	IMG_UINT32 ui32CalibrationCount;
} GPU_FREQ_TRACKING_DATA;

#if defined(PVRSRV_TIMER_CORRELATION_HISTORY)
#define RGX_GPU_FREQ_TRACKING_SIZE 16

typedef struct
{
	IMG_UINT64 ui64BeginCRTimestamp;
	IMG_UINT64 ui64BeginOSTimestamp;

	IMG_UINT64 ui64EndCRTimestamp;
	IMG_UINT64 ui64EndOSTimestamp;

	IMG_UINT32 ui32EstCoreClockSpeed;
	IMG_UINT32 ui32CoreClockSpeed;
} GPU_FREQ_TRACKING_HISTORY;
#endif

typedef struct _RGX_GPU_DVFS_TABLE_
{
	/* Beginning of current calibration period (in us) */
	IMG_UINT64 ui64CalibrationCRTimestamp;
	IMG_UINT64 ui64CalibrationOSTimestamp;

	/* Calculated calibration period (in us) */
	IMG_UINT64 ui64CalibrationCRTimediff;
	IMG_UINT64 ui64CalibrationOSTimediff;

	/* Current calibration period (in us) */
	IMG_UINT32 ui32CalibrationPeriod;

	/* System layer frequency table and frequency tracking data */
	IMG_UINT32 ui32FreqIndex;
	IMG_UINT32 aui32GPUFrequency[RGX_GPU_DVFS_TABLE_SIZE];
	GPU_FREQ_TRACKING_DATA asTrackingData[RGX_GPU_DVFS_TABLE_SIZE];

#if defined(PVRSRV_TIMER_CORRELATION_HISTORY)
	IMG_UINT32 ui32HistoryIndex;
	GPU_FREQ_TRACKING_HISTORY asTrackingHistory[RGX_GPU_FREQ_TRACKING_SIZE];
#endif
} RGX_GPU_DVFS_TABLE;


/*!
 ******************************************************************************
 * GPU utilisation statistics
 *****************************************************************************/

typedef struct _RGXFWIF_GPU_UTIL_STATS_
{
	IMG_BOOL   bValid;                /* If TRUE, statistics are valid.
	                                     FALSE if the driver couldn't get reliable stats. */
	IMG_UINT64 ui64GpuStatActive;     /* GPU active statistic */
	IMG_UINT64 ui64GpuStatBlocked;    /* GPU blocked statistic */
	IMG_UINT64 ui64GpuStatIdle;       /* GPU idle statistic */
	IMG_UINT64 ui64GpuStatCumulative; /* Sum of active/blocked/idle stats */
	IMG_UINT64 ui64TimeStamp;         /* Timestamp of the most recent sample of the GPU stats */
} RGXFWIF_GPU_UTIL_STATS;


typedef struct _RGX_REG_CONFIG_
{
	IMG_BOOL               bEnabled;
	RGXFWIF_REG_CFG_TYPE   eRegCfgTypeToPush;
	IMG_UINT32             ui32NumRegRecords;
	POS_LOCK               hLock;
} RGX_REG_CONFIG;

typedef struct _PVRSRV_STUB_PBDESC_ PVRSRV_STUB_PBDESC;

typedef struct
{
	IMG_UINT32			ui32DustCount1;
	IMG_UINT32			ui32DustCount2;
	IMG_BOOL			bToggle;
} RGX_DUST_STATE;

typedef struct _PVRSRV_DEVICE_FEATURE_CONFIG_
{
	IMG_UINT64 ui64ErnsBrns;
	IMG_UINT64 ui64Features;
	IMG_UINT32 ui32B;
	IMG_UINT32 ui32V;
	IMG_UINT32 ui32N;
	IMG_UINT32 ui32C;
	IMG_UINT32 ui32FeaturesValues[RGX_FEATURE_WITH_VALUES_MAX_IDX];
	IMG_UINT32 ui32MAXDustCount;
	IMG_UINT32 ui32SLCSizeInBytes;
	IMG_PCHAR  pszBVNCString;
}PVRSRV_DEVICE_FEATURE_CONFIG;

/* This is used to get the value of a specific feature.
 * Note that it will assert if the feature is disabled or value is invalid. */
#define RGX_GET_FEATURE_VALUE(psDevInfo, Feature) \
			( psDevInfo->sDevFeatureCfg.ui32FeaturesValues[RGX_FEATURE_##Feature##_IDX] )

/* This is used to check if the feature value (e.g. with an integer value) is available for the currently running BVNC or not */
#define RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, Feature) \
			( psDevInfo->sDevFeatureCfg.ui32FeaturesValues[RGX_FEATURE_##Feature##_IDX] < RGX_FEATURE_VALUE_DISABLED )

/* This is used to check if the Boolean feature (e.g. WITHOUT an integer value) is available for the currently running BVNC or not */
#define RGX_IS_FEATURE_SUPPORTED(psDevInfo, Feature) \
			BITMASK_HAS(psDevInfo->sDevFeatureCfg.ui64Features, RGX_FEATURE_##Feature##_BIT_MASK)

/* This is used to check if the ERN is available for the currently running BVNC or not */
#define RGX_IS_ERN_SUPPORTED(psDevInfo, ERN) \
			BITMASK_HAS(psDevInfo->sDevFeatureCfg.ui64ErnsBrns, HW_ERN_##ERN##_BIT_MASK)

/* This is used to check if the BRN is available for the currently running BVNC or not */
#define RGX_IS_BRN_SUPPORTED(psDevInfo, BRN) \
			BITMASK_HAS(psDevInfo->sDevFeatureCfg.ui64ErnsBrns, FIX_HW_BRN_##BRN##_BIT_MASK)

/* there is a corresponding define in rgxapi.h */
#define RGX_MAX_TIMER_QUERIES 16U

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
/*!
 * The host maintains a 512-deep cache of submitted workloads per device,
 * i.e. a global look-up table for TA, 3D and compute (depending on the RGX
 * hardware support present)
 */

/*
 * For the workload estimation return data array, the max amount of commands the
 * MTS can have is 255, therefore 512 (LOG2 = 9) is large enough to account for
 * all corner cases
 */
#define RETURN_DATA_ARRAY_SIZE_LOG2 (9)
#define RETURN_DATA_ARRAY_SIZE      ((1UL) << RETURN_DATA_ARRAY_SIZE_LOG2)
#define RETURN_DATA_ARRAY_WRAP_MASK (RETURN_DATA_ARRAY_SIZE - 1)

#define WORKLOAD_HASH_SIZE_LOG2		6
#define WORKLOAD_HASH_SIZE			((1UL) << WORKLOAD_HASH_SIZE_LOG2)
#define WORKLOAD_HASH_WRAP_MASK		(WORKLOAD_HASH_SIZE - 1)

/*!
 * Workload characteristics for supported data masters.
 * All characteristics must match for the workload estimate to be used/updated.
 */
typedef union _RGX_WORKLOAD_
{
	struct
	{
		IMG_UINT32				ui32RenderTargetSize;
		IMG_UINT32				ui32NumberOfDrawCalls;
		IMG_UINT32				ui32NumberOfIndices;
		IMG_UINT32				ui32NumberOfMRTs;
	} sTA3D;

	struct
	{
		IMG_UINT32				ui32NumberOfWorkgroups;
		IMG_UINT32				ui32NumberOfWorkitems;
	} sCompute;

	struct
	{
		IMG_UINT32				ui32Characteristic1;
		IMG_UINT32				ui32Characteristic2;
	} sTransfer;
} RGX_WORKLOAD;

/*!
 * Host data used to match the return data (actual cycles count) to the
 * submitted command packet.
 * The hash table is a per-DM circular buffer containing a key based on the
 * workload characteristics. On job completion, the oldest workload data
 * is evicted if the CB is full and the driver matches the characteristics
 * to the matching data.
 *
 * o If the driver finds a match the existing cycle estimate is averaged with
 *   the actual cycles used.
 * o Otherwise a new hash entry is created with the actual cycles for this
 *   workload.
 *
 * Subsequently if a match is found during command submission, the estimate
 * is passed to the scheduler, e.g. adjust the GPU frequency if PDVFS is enabled.
 */
typedef struct _WORKLOAD_MATCHING_DATA_
{
	POS_LOCK				psHashLock;
	HASH_TABLE				*psHashTable;		/*! existing workload cycle estimates for this DM */
	RGX_WORKLOAD			asHashKeys[WORKLOAD_HASH_SIZE];
	IMG_UINT64				aui64HashData[WORKLOAD_HASH_SIZE];
	IMG_UINT32				ui32HashArrayWO;	/*! track the most recent workload estimates */
} WORKLOAD_MATCHING_DATA;

/*!
 * A generic container for the workload matching data for GPU contexts:
 * rendering (TA, 3D), compute, etc.
 */
typedef struct _WORKEST_HOST_DATA_
{
	union
	{
		struct
		{
			WORKLOAD_MATCHING_DATA	sDataTA;	/*!< matching data for TA commands */
			WORKLOAD_MATCHING_DATA	sData3D;	/*!< matching data for 3D commands */
		} sTA3D;

		struct
		{
			WORKLOAD_MATCHING_DATA	sDataCDM;	/*!< matching data for CDM commands */
		} sCompute;

		struct
		{
			WORKLOAD_MATCHING_DATA	sDataTDM;	/*!< matching data for TDM-TQ commands */
		} sTransfer;
	} uWorkloadMatchingData;

	/*
	 * This is a per-context property, hence the TA and 3D share the same
	 * per render context counter.
	 */
	IMG_UINT32				ui32WorkEstCCBReceived;	/*!< Used to ensure all submitted work
														 estimation commands are received
														 by the host before clean up. */
} WORKEST_HOST_DATA;

/*!
 * Entries in the list of submitted workloads, used when the completed command
 * returns data to the host.
 *
 * - the matching data is needed as it holds the hash data
 * - the host data is needed for completion updates, ensuring memory is not
 *   freed while workload estimates are in-flight.
 * - the workload characteristic is used in the hash table look-up.
 */
typedef struct _WORKEST_RETURN_DATA_
{
	WORKEST_HOST_DATA		*psWorkEstHostData;
	WORKLOAD_MATCHING_DATA	*psWorkloadMatchingData;
	RGX_WORKLOAD			sWorkloadCharacteristics;
} WORKEST_RETURN_DATA;
#endif


typedef struct
{
#if defined(PDUMP)
	IMG_HANDLE      hPdumpPages;
#endif
	PG_HANDLE       sPages;
	IMG_DEV_PHYADDR sPhysAddr;
} RGX_MIPS_ADDRESS_TRAMPOLINE;


/*!
 ******************************************************************************
 * RGX Device error counts
 *****************************************************************************/
typedef struct _PVRSRV_RGXDEV_ERROR_COUNTS_
{
	IMG_UINT32 ui32WGPErrorCount;		/*!< count of the number of WGP checksum errors */
	IMG_UINT32 ui32TRPErrorCount;		/*!< count of the number of TRP checksum errors */
} PVRSRV_RGXDEV_ERROR_COUNTS;

/*!
 ******************************************************************************
 * RGX Device info
 *****************************************************************************/
typedef struct _PVRSRV_RGXDEV_INFO_
{
	PVRSRV_DEVICE_NODE		*psDeviceNode;

	PVRSRV_DEVICE_FEATURE_CONFIG	sDevFeatureCfg;

	IMG_BOOL				bDevInit2Done;

	IMG_BOOL				bFirmwareInitialised;
	IMG_BOOL				bPDPEnabled;

	IMG_HANDLE				hDbgReqNotify;

	/* Kernel mode linear address of device registers */
	void __iomem			*pvRegsBaseKM;

	IMG_HANDLE				hRegMapping;

	/* System physical address of device registers */
	IMG_CPU_PHYADDR			sRegsPhysBase;
	/* Register region size in bytes */
	IMG_UINT32				ui32RegSize;

	PVRSRV_STUB_PBDESC		*psStubPBDescListKM;

	/* Firmware memory context info */
	DEVMEM_CONTEXT			*psKernelDevmemCtx;
	DEVMEM_HEAP				*psFirmwareMainHeap;
	DEVMEM_HEAP				*psFirmwareConfigHeap;
	MMU_CONTEXT				*psKernelMMUCtx;

	void					*pvDeviceMemoryHeap;

	/* Kernel CCB */
	DEVMEM_MEMDESC			*psKernelCCBCtlMemDesc;      /*!< memdesc for Kernel CCB control */
	RGXFWIF_CCB_CTL			*psKernelCCBCtl;             /*!< kernel mapping for Kernel CCB control */
	DEVMEM_MEMDESC			*psKernelCCBMemDesc;         /*!< memdesc for Kernel CCB */
	IMG_UINT8				*psKernelCCB;                /*!< kernel mapping for Kernel CCB */
	DEVMEM_MEMDESC			*psKernelCCBRtnSlotsMemDesc; /*!< Return slot array for Kernel CCB commands */
	IMG_UINT32				*pui32KernelCCBRtnSlots;     /*!< kernel mapping for return slot array */

	/* Firmware CCB */
	DEVMEM_MEMDESC			*psFirmwareCCBCtlMemDesc;   /*!< memdesc for Firmware CCB control */
	RGXFWIF_CCB_CTL			*psFirmwareCCBCtl;          /*!< kernel mapping for Firmware CCB control */
	DEVMEM_MEMDESC			*psFirmwareCCBMemDesc;      /*!< memdesc for Firmware CCB */
	IMG_UINT8				*psFirmwareCCB;             /*!< kernel mapping for Firmware CCB */

	/* Workload Estimation Firmware CCB */
	DEVMEM_MEMDESC			*psWorkEstFirmwareCCBCtlMemDesc;   /*!< memdesc for Workload Estimation Firmware CCB control */
	RGXFWIF_CCB_CTL			*psWorkEstFirmwareCCBCtl;          /*!< kernel mapping for Workload Estimation Firmware CCB control */
	DEVMEM_MEMDESC			*psWorkEstFirmwareCCBMemDesc;      /*!< memdesc for Workload Estimation Firmware CCB */
	IMG_UINT8				*psWorkEstFirmwareCCB;             /*!< kernel mapping for Workload Estimation Firmware CCB */

#if defined(SUPPORT_POWER_SAMPLING_VIA_DEBUGFS)
	/* Counter dumping */
	DEVMEM_MEMDESC			*psCounterBufferMemDesc;      /*!< mem desc for counter dumping buffer */
	POS_LOCK				hCounterDumpingLock;          /*!< Lock for guarding access to counter dumping buffer */
#endif

	PVRSRV_MEMALLOCFLAGS_T  uiFWPoisonOnFreeFlag;           /*!< Flag for poisoning FW allocations when freed */

	IMG_BOOL				bIgnoreHWReportedBVNC;			/*!< Ignore BVNC reported by HW */

	/*
		if we don't preallocate the pagetables we must
		insert newly allocated page tables dynamically
	*/
	void					*pvMMUContextList;

	IMG_UINT32				ui32ClkGateStatusReg;
	IMG_UINT32				ui32ClkGateStatusMask;

	DEVMEM_MEMDESC			*psRGXFWCodeMemDesc;
	IMG_DEV_VIRTADDR		sFWCodeDevVAddrBase;
	IMG_UINT32			ui32FWCodeSizeInBytes;
	DEVMEM_MEMDESC			*psRGXFWDataMemDesc;
	IMG_DEV_VIRTADDR		sFWDataDevVAddrBase;
	RGX_MIPS_ADDRESS_TRAMPOLINE	*psTrampoline;

	DEVMEM_MEMDESC			*psRGXFWCorememCodeMemDesc;
	IMG_DEV_VIRTADDR		sFWCorememCodeDevVAddrBase;
	RGXFWIF_DEV_VIRTADDR		sFWCorememCodeFWAddr;
	IMG_UINT32			ui32FWCorememCodeSizeInBytes;

	DEVMEM_MEMDESC			*psRGXFWIfCorememDataStoreMemDesc;
	IMG_DEV_VIRTADDR		sFWCorememDataStoreDevVAddrBase;
	RGXFWIF_DEV_VIRTADDR		sFWCorememDataStoreFWAddr;

	DEVMEM_MEMDESC			*psRGXFWAlignChecksMemDesc;

#if defined(PDUMP)
	DEVMEM_MEMDESC			*psRGXFWSigTAChecksMemDesc;
	IMG_UINT32				ui32SigTAChecksSize;

	DEVMEM_MEMDESC			*psRGXFWSig3DChecksMemDesc;
	IMG_UINT32				ui32Sig3DChecksSize;

	DEVMEM_MEMDESC			*psRGXFWSigTDM2DChecksMemDesc;
	IMG_UINT32				ui32SigTDM2DChecksSize;

	IMG_BOOL				bDumpedKCCBCtlAlready;

	POS_SPINLOCK			hSyncCheckpointSignalSpinLock;						/*!< Guards data shared between an atomic & sleepable-context */
#endif

	POS_LOCK				hRGXFWIfBufInitLock;								/*!< trace buffer lock for initialisation phase */

	DEVMEM_MEMDESC			*psRGXFWIfTraceBufCtlMemDesc;						/*!< memdesc of trace buffer control structure */
	DEVMEM_MEMDESC			*psRGXFWIfTraceBufferMemDesc[RGXFW_THREAD_NUM];		/*!< memdesc of actual FW trace (log) buffer(s) */
	RGXFWIF_TRACEBUF		*psRGXFWIfTraceBufCtl;								/*!< structure containing trace control data and actual trace buffer */

	DEVMEM_MEMDESC			*psRGXFWIfFwSysDataMemDesc;							/*!< memdesc of the firmware-shared system data structure */
	RGXFWIF_SYSDATA			*psRGXFWIfFwSysData;								/*!< structure containing trace control data and actual trace buffer */

	DEVMEM_MEMDESC			*psRGXFWIfFwOsDataMemDesc;							/*!< memdesc of the firmware-shared os structure */
	RGXFWIF_OSDATA			*psRGXFWIfFwOsData;									/*!< structure containing trace control data and actual trace buffer */

#if defined(SUPPORT_TBI_INTERFACE)
	DEVMEM_MEMDESC			*psRGXFWIfTBIBufferMemDesc;							/*!< memdesc of actual FW TBI buffer */
	RGXFWIF_DEV_VIRTADDR	sRGXFWIfTBIBuffer;									/*!< TBI buffer data */
	IMG_UINT32				ui32FWIfTBIBufferSize;
#endif

	DEVMEM_MEMDESC			*psRGXFWIfHWRInfoBufCtlMemDesc;
	RGXFWIF_HWRINFOBUF		*psRGXFWIfHWRInfoBufCtl;

	DEVMEM_MEMDESC			*psRGXFWIfGpuUtilFWCbCtlMemDesc;
	RGXFWIF_GPU_UTIL_FWCB	*psRGXFWIfGpuUtilFWCb;

	DEVMEM_MEMDESC			*psRGXFWIfHWPerfBufMemDesc;
	IMG_BYTE				*psRGXFWIfHWPerfBuf;
	IMG_UINT32				ui32RGXFWIfHWPerfBufSize; /* in bytes */

	DEVMEM_MEMDESC			*psRGXFWIfRegCfgMemDesc;

	DEVMEM_MEMDESC			*psRGXFWIfHWPerfCountersMemDesc;

	DEVMEM_MEMDESC			*psRGXFWIfConnectionCtlMemDesc;
	RGXFWIF_CONNECTION_CTL	*psRGXFWIfConnectionCtl;

	DEVMEM_MEMDESC			*psRGXFWIfSysInitMemDesc;
	RGXFWIF_SYSINIT			*psRGXFWIfSysInit;

	DEVMEM_MEMDESC			*psRGXFWIfOsInitMemDesc;
	RGXFWIF_OSINIT			*psRGXFWIfOsInit;

	DEVMEM_MEMDESC			*psRGXFWIfRuntimeCfgMemDesc;
	RGXFWIF_RUNTIME_CFG		*psRGXFWIfRuntimeCfg;

	/* Additional guest firmware memory context info */
	DEVMEM_HEAP				*psGuestFirmwareRawHeap[RGX_NUM_OS_SUPPORTED];
	DEVMEM_MEMDESC			*psGuestFirmwareRawMemDesc[RGX_NUM_OS_SUPPORTED];

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	/* Array to store data needed for workload estimation when a workload
	   has finished and its cycle time is returned to the host.	 */
	WORKEST_RETURN_DATA     asReturnData[RETURN_DATA_ARRAY_SIZE];
	IMG_UINT32              ui32ReturnDataWO;
	POS_LOCK                hWorkEstLock;
#endif

#if defined(SUPPORT_PDVFS)
	/**
	 * Host memdesc and pointer to memory containing core clock rate in Hz.
	 * Firmware updates the memory on changing the core clock rate over GPIO.
	 * Note: Shared memory needs atomic access from Host driver and firmware,
	 * hence size should not be greater than memory transaction granularity.
	 * Currently it is chosen to be 32 bits.
	 */
	DEVMEM_MEMDESC			*psRGXFWIFCoreClkRateMemDesc;
	volatile IMG_UINT32		*pui32RGXFWIFCoreClkRate;
	/**
	 * Last sampled core clk rate.
	 */
	volatile IMG_UINT32		ui32CoreClkRateSnapshot;
#endif

	/*
	   HWPerf data for the RGX device
	 */

	POS_LOCK    hHWPerfLock;  /*! Critical section lock that protects HWPerf code
	                           *  from multiple thread duplicate init/deinit
	                           *  and loss/freeing of FW & Host resources while in
	                           *  use in another thread e.g. MSIR. */

	IMG_UINT64  ui64HWPerfFilter; /*! Event filter for FW events (settable by AppHint) */
	IMG_HANDLE  hHWPerfStream;    /*! TL Stream buffer (L2) for firmware event stream */
	IMG_UINT32  ui32L2BufMaxPacketSize;/*!< Max allowed packet size in FW HWPerf TL (L2) buffer */
	IMG_BOOL    bSuspendHWPerfL2DataCopy;  /*! Flag to indicate if copying HWPerf data is suspended */

	IMG_UINT32  ui32HWPerfHostFilter;      /*! Event filter for HWPerfHost stream (settable by AppHint) */
	POS_LOCK    hLockHWPerfHostStream;     /*! Lock guarding access to HWPerfHost stream from multiple threads */
	IMG_HANDLE  hHWPerfHostStream;         /*! TL Stream buffer for host only event stream */
	IMG_UINT32  ui32HWPerfHostBufSize;     /*! Host side buffer size in bytes */
	IMG_UINT32  ui32HWPerfHostLastOrdinal; /*! Ordinal of the last packet emitted in HWPerfHost TL stream.
	                                        *  Guarded by hLockHWPerfHostStream */
	IMG_UINT32  ui32HWPerfHostNextOrdinal; /*! Ordinal number for HWPerfHost events. Guarded by hHWPerfHostSpinLock */
	IMG_UINT8   *pui8DeferredEvents;       /*! List of HWPerfHost events yet to be emitted in the TL stream.
	                                        *  Events generated from atomic context are deferred "emitted"
											*  as the "emission" code can sleep */
	IMG_UINT16  ui16DEReadIdx;             /*! Read index in the above deferred events buffer */
	IMG_UINT16  ui16DEWriteIdx;            /*! Write index in the above deferred events buffer */
	void        *pvHostHWPerfMISR;         /*! MISR to emit pending/deferred events in HWPerfHost TL stream */
	POS_SPINLOCK hHWPerfHostSpinLock;      /*! Guards data shared between an atomic & sleepable-context */
#if defined(PVRSRV_HWPERF_HOST_DEBUG_DEFERRED_EVENTS)
	IMG_UINT32  ui32DEHighWatermark;       /*! High watermark of deferred events buffer usage. Protected by
	                                        *! hHWPerfHostSpinLock */
	/* Max number of times DeferredEmission waited for an atomic-context to "finish" packet write */
	IMG_UINT32  ui32WaitForAtomicCtxPktHighWatermark; /*! Protected by hLockHWPerfHostStream */
	/* Whether warning has been logged about an atomic-context packet loss (due to too long wait for "write" finish) */
	IMG_BOOL    bWarnedAtomicCtxPktLost;
	/* Max number of times DeferredEmission scheduled-out to give a chance to the right-ordinal packet to be emitted */
	IMG_UINT32  ui32WaitForRightOrdPktHighWatermark; /*! Protected by hLockHWPerfHostStream */
	/* Whether warning has been logged about an packet loss (due to too long wait for right ordinal to emit) */
	IMG_BOOL    bWarnedPktOrdinalBroke;
#endif

	void        *pvGpuFtraceData;

	/* Poll data for detecting firmware fatal errors */
	IMG_UINT32				aui32CrLastPollCount[RGXFW_THREAD_NUM];
	IMG_UINT32				ui32KCCBCmdsExecutedLastTime;
	IMG_BOOL				bKCCBCmdsWaitingLastTime;
	IMG_UINT32				ui32GEOTimeoutsLastTime;
	IMG_UINT32				ui32InterruptCountLastTime;
	IMG_UINT32				ui32MissingInterruptsLastTime;

	/* Client stall detection */
	IMG_UINT32				ui32StalledClientMask;

	IMG_BOOL				bWorkEstEnabled;
	IMG_BOOL				bPDVFSEnabled;

	void					*pvLISRData;
	void					*pvMISRData;
	void					*pvAPMISRData;
	RGX_ACTIVEPM_CONF		eActivePMConf;

	volatile IMG_UINT32		aui32SampleIRQCount[RGXFW_THREAD_NUM];

	DEVMEM_MEMDESC			*psRGXFaultAddressMemDesc;

	DEVMEM_MEMDESC			*psSLC3FenceMemDesc;

	/* If we do 10 deferred memory allocations per second, then the ID would wrap around after 13 years */
	IMG_UINT32				ui32ZSBufferCurrID;	/*!< ID assigned to the next deferred devmem allocation */
	IMG_UINT32				ui32FreelistCurrID;	/*!< ID assigned to the next freelist */

	POS_LOCK				hLockZSBuffer;		/*!< Lock to protect simultaneous access to ZSBuffers */
	DLLIST_NODE				sZSBufferHead;		/*!< List of on-demand ZSBuffers */
	POS_LOCK				hLockFreeList;		/*!< Lock to protect simultaneous access to Freelists */
	DLLIST_NODE				sFreeListHead;		/*!< List of growable Freelists */
	PSYNC_PRIM_CONTEXT		hSyncPrimContext;
	PVRSRV_CLIENT_SYNC_PRIM	*psPowSyncPrim;

	IMG_UINT32				ui32ActivePMReqOk;
	IMG_UINT32				ui32ActivePMReqDenied;
	IMG_UINT32				ui32ActivePMReqNonIdle;
	IMG_UINT32				ui32ActivePMReqRetry;
	IMG_UINT32				ui32ActivePMReqTotal;

	IMG_HANDLE				hProcessQueuesMISR;

	IMG_UINT32				ui32DeviceFlags;		/*!< Flags to track general device state */

	/* GPU DVFS Table */
	RGX_GPU_DVFS_TABLE		*psGpuDVFSTable;

	/* Pointer to function returning the GPU utilisation statistics since the last
	 * time the function was called. Supports different users at the same time.
	 *
	 * psReturnStats [out]: GPU utilisation statistics (active high/active low/idle/blocked)
	 *                      in microseconds since the last time the function was called
	 *                      by a specific user (identified by hGpuUtilUser)
	 *
	 * Returns PVRSRV_OK in case the call completed without errors,
	 * some other value otherwise.
	 */
	PVRSRV_ERROR (*pfnGetGpuUtilStats) (PVRSRV_DEVICE_NODE *psDeviceNode,
	                                    IMG_HANDLE hGpuUtilUser,
	                                    RGXFWIF_GPU_UTIL_STATS *psReturnStats);

	/* Pointer to function that checks if the physical GPU IRQ
	 * line has been asserted and clears it if so */
	IMG_BOOL (*pfnRGXAckIrq) (struct _PVRSRV_RGXDEV_INFO_ *psDevInfo);

	POS_LOCK				hGPUUtilLock;

	/* Register configuration */
	RGX_REG_CONFIG			sRegCongfig;

	IMG_BOOL				bRGXPowered;
	DLLIST_NODE				sMemoryContextList;

	POSWR_LOCK				hRenderCtxListLock;
	POSWR_LOCK				hComputeCtxListLock;
	POSWR_LOCK				hTransferCtxListLock;
	POSWR_LOCK				hTDMCtxListLock;
	POSWR_LOCK				hMemoryCtxListLock;
	POSWR_LOCK				hKickSyncCtxListLock;

	/* Linked list of deferred KCCB commands due to a full KCCB.
	 * Access to members sKCCBDeferredCommandsListHead and ui32KCCBDeferredCommandsCount
	 * are protected by the hLockKCCBDeferredCommandsList spin lock. */
	POS_SPINLOCK			hLockKCCBDeferredCommandsList; /*!< Protects deferred KCCB commands list */
	DLLIST_NODE				sKCCBDeferredCommandsListHead;
	IMG_UINT32				ui32KCCBDeferredCommandsCount; /*!< No of commands in the deferred list */

	/* Linked lists of contexts on this device */
	DLLIST_NODE				sRenderCtxtListHead;
	DLLIST_NODE				sComputeCtxtListHead;
	DLLIST_NODE				sTransferCtxtListHead;
	DLLIST_NODE				sTDMCtxtListHead;
	DLLIST_NODE				sKickSyncCtxtListHead;

	DLLIST_NODE				sCommonCtxtListHead;
	POSWR_LOCK				hCommonCtxtListLock;
	IMG_UINT32				ui32CommonCtxtCurrentID;	/*!< ID assigned to the next common context */

	POS_LOCK				hDebugFaultInfoLock;	/*!< Lock to protect the debug fault info list */
	POS_LOCK				hMMUCtxUnregLock;		/*!< Lock to protect list of unregistered MMU contexts */

	POS_LOCK				hNMILock; /*!< Lock to protect NMI operations */

#if defined(SUPPORT_VALIDATION)
	IMG_UINT32				ui32ValidationFlags;	/*!< Validation flags for host driver */
#endif
	RGX_DUST_STATE			sDustReqState;

	RGX_LAYER_PARAMS		sLayerParams;

	RGXFWIF_DM				eBPDM;					/*!< Current breakpoint data master */
	IMG_BOOL				bBPSet;					/*!< A Breakpoint has been set */
	POS_LOCK				hBPLock;				/*!< Lock for break point operations */

	IMG_UINT32				ui32CoherencyTestsDone;

	ATOMIC_T				iCCBSubmissionOrdinal; /* Rolling count used to indicate CCB submission order (all CCBs) */
	POS_LOCK				hCCBRecoveryLock;      /* Lock to protect pvEarliestStalledClientCCB and ui32OldestSubmissionOrdinal variables */
	void					*pvEarliestStalledClientCCB; /* Will point to cCCB command to unblock in the event of a stall */
	IMG_UINT32				ui32OldestSubmissionOrdinal; /* Earliest submission ordinal of CCB entry found so far */
	IMG_UINT32				ui32SLRHoldoffCounter;   /* Decremented each time health check is called until zero. SLR only happen when zero. */

	POS_LOCK				hCCBStallCheckLock; /* Lock used to guard against multiple threads simultaneously checking for stalled CCBs */

#if defined(SUPPORT_FIRMWARE_GCOV)
	/* Firmware gcov buffer */
	DEVMEM_MEMDESC			*psFirmwareGcovBufferMemDesc;      /*!< mem desc for Firmware gcov dumping buffer */
	IMG_UINT32				ui32FirmwareGcovSize;
#endif

#if defined(SUPPORT_VALIDATION)
	struct
	{
		IMG_UINT64 ui64RegVal;
		struct completion sRegComp;
	} sFwRegs;
#endif

	IMG_HANDLE				hTQCLISharedMem;		/*!< TQ Client Shared Mem PMR */
	IMG_HANDLE				hTQUSCSharedMem;		/*!< TQ USC Shared Mem PMR */

#if defined(SUPPORT_VALIDATION)
	IMG_UINT32				ui32TestSLRInterval; /* Don't enqueue an update sync checkpoint every nth kick */
	IMG_UINT32				ui32TestSLRCount;    /* (used to test SLR operation) */
	IMG_UINT32				ui32SLRSkipFWAddr;
#endif

#if defined(SUPPORT_SECURITY_VALIDATION)
	DEVMEM_MEMDESC			*psRGXFWIfSecureBufMemDesc;
	DEVMEM_MEMDESC			*psRGXFWIfNonSecureBufMemDesc;
#endif

	/* Timer Queries */
	IMG_UINT32				ui32ActiveQueryId;		/*!< id of the active line */
	IMG_BOOL				bSaveStart;				/*!< save the start time of the next kick on the device*/
	IMG_BOOL				bSaveEnd;				/*!< save the end time of the next kick on the device*/

	DEVMEM_MEMDESC			*psStartTimeMemDesc;    /*!< memdesc for Start Times */
	IMG_UINT64				*pui64StartTimeById;    /*!< CPU mapping of the above */

	DEVMEM_MEMDESC			*psEndTimeMemDesc;      /*!< memdesc for End Timer */
	IMG_UINT64				*pui64EndTimeById;      /*!< CPU mapping of the above */

	IMG_UINT32				aui32ScheduledOnId[RGX_MAX_TIMER_QUERIES];	/*!< kicks Scheduled on QueryId */
	DEVMEM_MEMDESC			*psCompletedMemDesc;	/*!< kicks Completed on QueryId */
	IMG_UINT32				*pui32CompletedById;	/*!< CPU mapping of the above */

#if !defined(PVRSRV_USE_BRIDGE_LOCK)
	POS_LOCK				hTimerQueryLock;		/*!< lock to protect simultaneous access to timer query members */
#endif

	PVRSRV_RGXDEV_ERROR_COUNTS sErrorCounts;		/*!< struct containing device error counts */

	IMG_UINT32				ui32HostSafetyEventMask;/*!< mask of the safety events handled by the driver */

	RGX_CONTEXT_RESET_REASON	eLastDeviceError;	/*!< device error reported to client */
#if defined(SUPPORT_VALIDATION)
	IMG_UINT32 ui32ECCRAMErrInjModule;
	IMG_UINT32 ui32ECCRAMErrInjInterval;
#endif

	IMG_UINT32              ui32Log2Non4KPgSize; /* Page size of Non4k heap in log2 form */
} PVRSRV_RGXDEV_INFO;



typedef struct _RGX_TIMING_INFORMATION_
{
	/*! GPU default core clock speed in Hz */
	IMG_UINT32			ui32CoreClockSpeed;

	/*! Active Power Management: GPU actively requests the host driver to be powered off */
	IMG_BOOL			bEnableActivePM;

	/*! Enable the GPU to power off internal Power Islands independently from the host driver */
	IMG_BOOL			bEnableRDPowIsland;

	/*! Active Power Management: Delay between the GPU idle and the request to the host */
	IMG_UINT32			ui32ActivePMLatencyms;

} RGX_TIMING_INFORMATION;

typedef struct _RGX_DATA_
{
	/*! Timing information */
	RGX_TIMING_INFORMATION	*psRGXTimingInfo;
} RGX_DATA;


/*
	RGX PDUMP register bank name (prefix)
*/
#define RGX_PDUMPREG_NAME		"RGXREG"
#define RGX_TB_PDUMPREG_NAME	"EMUREG"

#endif /* RGXDEVICE_H */
