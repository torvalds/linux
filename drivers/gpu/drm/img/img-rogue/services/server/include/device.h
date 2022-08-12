/**************************************************************************/ /*!
@File
@Title          Common Device header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Device related function templates and defines
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
*/ /***************************************************************************/

#ifndef DEVICE_H
#define DEVICE_H

#include "devicemem_heapcfg.h"
#include "mmu_common.h"
#include "ra.h"			/* RA_ARENA */
#include "pvrsrv_device.h"
#include "sync_checkpoint.h"
#include "srvkm.h"
#include "physheap.h"
#include "sync_internal.h"
#include "sysinfo.h"
#include "dllist.h"

#include "rgx_bvnc_defs_km.h"

#include "lock.h"

#include "power.h"

#if defined(SUPPORT_GPUVIRT_VALIDATION)
#include "virt_validation_defs.h"
#endif

typedef struct _PVRSRV_POWER_DEV_TAG_ *PPVRSRV_POWER_DEV;

struct SYNC_RECORD;

struct _CONNECTION_DATA_;

/*************************************************************************/ /*!
 @Function      AllocUFOBlockCallback
 @Description   Device specific callback for allocation of a UFO block

 @Input         psDeviceNode          Pointer to device node to allocate
                                      the UFO for.
 @Output        ppsMemDesc            Pointer to pointer for the memdesc of
                                      the allocation
 @Output        pui32SyncAddr         FW Base address of the UFO block
 @Output        puiSyncPrimBlockSize  Size of the UFO block

 @Return        PVRSRV_OK if allocation was successful
*/ /**************************************************************************/
typedef PVRSRV_ERROR (*AllocUFOBlockCallback)(struct _PVRSRV_DEVICE_NODE_ *psDeviceNode,
														DEVMEM_MEMDESC **ppsMemDesc,
														IMG_UINT32 *pui32SyncAddr,
														IMG_UINT32 *puiSyncPrimBlockSize);

/*************************************************************************/ /*!
 @Function      FreeUFOBlockCallback
 @Description   Device specific callback for freeing of a UFO

 @Input         psDeviceNode    Pointer to device node that the UFO block was
                                allocated from.
 @Input         psMemDesc       Pointer to pointer for the memdesc of the UFO
                                block to free.
*/ /**************************************************************************/
typedef void (*FreeUFOBlockCallback)(struct _PVRSRV_DEVICE_NODE_ *psDeviceNode,
									 DEVMEM_MEMDESC *psMemDesc);

typedef struct _PVRSRV_DEVICE_IDENTIFIER_
{
	/* Pdump memory and register bank names */
	IMG_CHAR				*pszPDumpDevName;
	IMG_CHAR				*pszPDumpRegName;

	/* Under Linux, this is the minor number of RenderNode corresponding to this Device */
	IMG_INT32				i32OsDeviceID;
	/* Services layer enumeration of the device used in pvrdebug */
	IMG_UINT32				ui32InternalID;
} PVRSRV_DEVICE_IDENTIFIER;

typedef struct _DEVICE_MEMORY_INFO_
{
	/* Heap count. Doesn't include additional heaps from PVRSRVCreateDeviceMemHeap */
	IMG_UINT32				ui32HeapCount;

	/* Blueprints for creating new device memory contexts */
	IMG_UINT32              uiNumHeapConfigs;
	DEVMEM_HEAP_CONFIG      *psDeviceMemoryHeapConfigArray;
	DEVMEM_HEAP_BLUEPRINT   *psDeviceMemoryHeap;
} DEVICE_MEMORY_INFO;

#define MMU_BAD_PHYS_ADDR (0xbadbad00badULL)
#define DUMMY_PAGE	("DUMMY_PAGE")
#define DEV_ZERO_PAGE	("DEV_ZERO_PAGE")
#define PVR_DUMMY_PAGE_INIT_VALUE	(0x0)
#define PVR_ZERO_PAGE_INIT_VALUE	(0x0)

typedef struct __DEFAULT_PAGE__
{
	/*Page handle for the page allocated (UMA/LMA)*/
	PG_HANDLE	sPageHandle;
	POS_LOCK	psPgLock;
	ATOMIC_T	atRefCounter;
	/*Default page size in terms of log2 */
	IMG_UINT32	ui32Log2PgSize;
	IMG_UINT64	ui64PgPhysAddr;
#if defined(PDUMP)
	IMG_HANDLE hPdumpPg;
#endif
} PVRSRV_DEF_PAGE;

typedef enum _PVRSRV_DEVICE_STATE_
{
	PVRSRV_DEVICE_STATE_UNDEFINED = 0,
	PVRSRV_DEVICE_STATE_INIT,
	PVRSRV_DEVICE_STATE_ACTIVE,
	PVRSRV_DEVICE_STATE_DEINIT,
	PVRSRV_DEVICE_STATE_BAD,
} PVRSRV_DEVICE_STATE;

typedef enum _PVRSRV_DEVICE_HEALTH_STATUS_
{
	PVRSRV_DEVICE_HEALTH_STATUS_UNDEFINED = 0,
	PVRSRV_DEVICE_HEALTH_STATUS_OK,
	PVRSRV_DEVICE_HEALTH_STATUS_NOT_RESPONDING,
	PVRSRV_DEVICE_HEALTH_STATUS_DEAD,
	PVRSRV_DEVICE_HEALTH_STATUS_FAULT
} PVRSRV_DEVICE_HEALTH_STATUS;

typedef enum _PVRSRV_DEVICE_HEALTH_REASON_
{
	PVRSRV_DEVICE_HEALTH_REASON_NONE = 0,
	PVRSRV_DEVICE_HEALTH_REASON_ASSERTED,
	PVRSRV_DEVICE_HEALTH_REASON_POLL_FAILING,
	PVRSRV_DEVICE_HEALTH_REASON_TIMEOUTS,
	PVRSRV_DEVICE_HEALTH_REASON_QUEUE_CORRUPT,
	PVRSRV_DEVICE_HEALTH_REASON_QUEUE_STALLED,
	PVRSRV_DEVICE_HEALTH_REASON_IDLING,
	PVRSRV_DEVICE_HEALTH_REASON_RESTARTING,
	PVRSRV_DEVICE_HEALTH_REASON_MISSING_INTERRUPTS
} PVRSRV_DEVICE_HEALTH_REASON;

typedef enum _PVRSRV_DEVICE_DEBUG_DUMP_STATUS_
{
	PVRSRV_DEVICE_DEBUG_DUMP_NONE = 0,
	PVRSRV_DEVICE_DEBUG_DUMP_CAPTURE
} PVRSRV_DEVICE_DEBUG_DUMP_STATUS;

#ifndef DI_GROUP_DEFINED
#define DI_GROUP_DEFINED
typedef struct DI_GROUP DI_GROUP;
#endif
#ifndef DI_ENTRY_DEFINED
#define DI_ENTRY_DEFINED
typedef struct DI_ENTRY DI_ENTRY;
#endif

typedef struct _PVRSRV_DEVICE_DEBUG_INFO_
{
	DI_GROUP *psGroup;
	DI_ENTRY *psDumpDebugEntry;
#ifdef SUPPORT_RGX
	DI_ENTRY *psFWTraceEntry;
#ifdef SUPPORT_FIRMWARE_GCOV
	DI_ENTRY *psFWGCOVEntry;
#endif
	DI_ENTRY *psFWMappingsEntry;
#if defined(SUPPORT_VALIDATION) || defined(SUPPORT_RISCV_GDB)
	DI_ENTRY *psRiscvDmiDIEntry;
	IMG_UINT64 ui64RiscvDmi;
#endif
#endif /* SUPPORT_RGX */
#ifdef SUPPORT_VALIDATION
	DI_ENTRY *psRGXRegsEntry;
#endif /* SUPPORT_VALIDATION */
#ifdef SUPPORT_POWER_VALIDATION_VIA_DEBUGFS
	DI_ENTRY *psPowMonEntry;
#endif
#ifdef SUPPORT_POWER_SAMPLING_VIA_DEBUGFS
	DI_ENTRY *psPowerDataEntry;
#endif
} PVRSRV_DEVICE_DEBUG_INFO;

#if defined(PVRSRV_DEBUG_LISR_EXECUTION)
#define RGX_LISR_INIT							(0U)
#define RGX_LISR_DEVICE_NOT_POWERED				(1U)
#define RGX_LISR_NOT_TRIGGERED_BY_HW			(2U)
#define RGX_LISR_FW_IRQ_COUNTER_NOT_UPDATED		(3U)
#define RGX_LISR_PROCESSED						(4U)

typedef IMG_UINT32 LISR_STATUS;

typedef struct _LISR_EXECUTION_INFO_
{
	/* status of last LISR invocation */
	LISR_STATUS ui32Status;

	/* snapshot from the last LISR invocation */
#if defined(RGX_FW_IRQ_OS_COUNTERS)
	IMG_UINT32 aui32InterruptCountSnapshot[RGX_NUM_OS_SUPPORTED];
#else
	IMG_UINT32 aui32InterruptCountSnapshot[RGXFW_THREAD_NUM];
#endif

	/* time of the last LISR invocation */
	IMG_UINT64 ui64Clockns;
} LISR_EXECUTION_INFO;

#define UPDATE_LISR_DBG_STATUS(status)		psDeviceNode->sLISRExecutionInfo.ui32Status = (status)
#define UPDATE_LISR_DBG_SNAPSHOT(idx, val)	psDeviceNode->sLISRExecutionInfo.aui32InterruptCountSnapshot[idx] = (val)
#define UPDATE_LISR_DBG_TIMESTAMP()			psDeviceNode->sLISRExecutionInfo.ui64Clockns = OSClockns64()
#define UPDATE_LISR_DBG_COUNTER()			psDeviceNode->ui64nLISR++
#define UPDATE_MISR_DBG_COUNTER()			psDeviceNode->ui64nMISR++
#else
#define UPDATE_LISR_DBG_STATUS(status)
#define UPDATE_LISR_DBG_SNAPSHOT(idx, val)
#define UPDATE_LISR_DBG_TIMESTAMP()
#define UPDATE_LISR_DBG_COUNTER()
#define UPDATE_MISR_DBG_COUNTER()
#endif /* defined(PVRSRV_DEBUG_LISR_EXECUTION) */

typedef struct _PVRSRV_DEVICE_NODE_
{
	PVRSRV_DEVICE_IDENTIFIER	sDevId;

	PVRSRV_DEVICE_STATE			eDevState;
	PVRSRV_DEVICE_FABRIC_TYPE	eDevFabricType;

	ATOMIC_T					eHealthStatus; /* Holds values from PVRSRV_DEVICE_HEALTH_STATUS */
	ATOMIC_T					eHealthReason; /* Holds values from PVRSRV_DEVICE_HEALTH_REASON */
	ATOMIC_T					eDebugDumpRequested; /* Holds values from PVRSRV_DEVICE_DEBUG_DUMP_STATUS */

	IMG_HANDLE					*hDebugTable;

	/* device specific MMU attributes */
	MMU_DEVICEATTRIBS      *psMMUDevAttrs;
	/* Device specific MMU firmware attributes, used only in some devices */
	MMU_DEVICEATTRIBS      *psFirmwareMMUDevAttrs;

	PHYS_HEAP              *psMMUPhysHeap;

	/* lock for power state transitions */
	POS_LOCK				hPowerLock;
	IMG_PID                 uiPwrLockOwnerPID; /* Only valid between lock and corresponding unlock
	                                              operations of hPowerLock */

	/* current system device power state */
	PVRSRV_SYS_POWER_STATE	eCurrentSysPowerState;
	PPVRSRV_POWER_DEV	psPowerDev;

    /* multicore configuration information */
    IMG_UINT32              ui32MultiCoreNumCores;      /* total cores primary + secondaries. 0 for non-multi core */
    IMG_UINT32              ui32MultiCorePrimaryId;     /* primary core id for this device */
    IMG_UINT64             *pui64MultiCoreCapabilities; /* capabilities for each core */

	/*
		callbacks the device must support:
	*/

	PVRSRV_ERROR (*pfnDevSLCFlushRange)(struct _PVRSRV_DEVICE_NODE_ *psDevNode,
										MMU_CONTEXT *psMMUContext,
										IMG_DEV_VIRTADDR sDevVAddr,
										IMG_DEVMEM_SIZE_T uiSize,
										IMG_BOOL bInvalidate);

	PVRSRV_ERROR (*pfnInvalFBSCTable)(struct _PVRSRV_DEVICE_NODE_ *psDevNode,
									  MMU_CONTEXT *psMMUContext,
									  IMG_UINT64 ui64FBSCEntries);

	PVRSRV_ERROR (*pfnValidateOrTweakPhysAddrs)(struct _PVRSRV_DEVICE_NODE_ *psDevNode,
												MMU_DEVICEATTRIBS *psDevAttrs,
												IMG_UINT64 *pui64Addr);

	void (*pfnMMUCacheInvalidate)(struct _PVRSRV_DEVICE_NODE_ *psDevNode,
								  MMU_CONTEXT *psMMUContext,
								  MMU_LEVEL eLevel,
								  IMG_BOOL bUnmap);

	PVRSRV_ERROR (*pfnMMUCacheInvalidateKick)(struct _PVRSRV_DEVICE_NODE_ *psDevNode,
	                                          IMG_UINT32 *pui32NextMMUInvalidateUpdate);

	IMG_UINT32 (*pfnMMUCacheGetInvalidateCounter)(struct _PVRSRV_DEVICE_NODE_ *psDevNode);


	void (*pfnDumpDebugInfo)(struct _PVRSRV_DEVICE_NODE_ *psDevNode);

	PVRSRV_ERROR (*pfnUpdateHealthStatus)(struct _PVRSRV_DEVICE_NODE_ *psDevNode,
	                                      IMG_BOOL bIsTimerPoll);

#if defined(SUPPORT_AUTOVZ)
	void (*pfnUpdateAutoVzWatchdog)(struct _PVRSRV_DEVICE_NODE_ *psDevNode);
#endif

	PVRSRV_ERROR (*pfnValidationGPUUnitsPowerChange)(struct _PVRSRV_DEVICE_NODE_ *psDevNode, IMG_UINT32 ui32NewState);

	PVRSRV_ERROR (*pfnResetHWRLogs)(struct _PVRSRV_DEVICE_NODE_ *psDevNode);

	PVRSRV_ERROR (*pfnVerifyBVNC)(struct _PVRSRV_DEVICE_NODE_ *psDevNode, IMG_UINT64 ui64GivenBVNC, IMG_UINT64 ui64CoreIdMask);

	/* Method to drain device HWPerf packets from firmware buffer to host buffer */
	PVRSRV_ERROR (*pfnServiceHWPerf)(struct _PVRSRV_DEVICE_NODE_ *psDevNode);

	PVRSRV_ERROR (*pfnDeviceVersionString)(struct _PVRSRV_DEVICE_NODE_ *psDevNode, IMG_CHAR **ppszVersionString);

	PVRSRV_ERROR (*pfnDeviceClockSpeed)(struct _PVRSRV_DEVICE_NODE_ *psDevNode, IMG_PUINT32 pui32RGXClockSpeed);

	PVRSRV_ERROR (*pfnSoftReset)(struct _PVRSRV_DEVICE_NODE_ *psDevNode, IMG_UINT64 ui64ResetValue1, IMG_UINT64 ui64ResetValue2);

	PVRSRV_ERROR (*pfnAlignmentCheck)(struct _PVRSRV_DEVICE_NODE_ *psDevNode, IMG_UINT32 ui32FWAlignChecksSize, IMG_UINT32 aui32FWAlignChecks[]);
	IMG_BOOL	(*pfnCheckDeviceFeature)(struct _PVRSRV_DEVICE_NODE_ *psDevNode, IMG_UINT64 ui64FeatureMask);

	IMG_INT32	(*pfnGetDeviceFeatureValue)(struct _PVRSRV_DEVICE_NODE_ *psDevNode, enum _RGX_FEATURE_WITH_VALUE_INDEX_ eFeatureIndex);

    PVRSRV_ERROR (*pfnGetMultiCoreInfo)(struct _PVRSRV_DEVICE_NODE_ *psDevNode, IMG_UINT32 ui32CapsSize,
                                        IMG_UINT32 *pui32NumCores, IMG_UINT64 *pui64Caps);

	IMG_BOOL (*pfnHasFBCDCVersion31)(struct _PVRSRV_DEVICE_NODE_ *psDevNode);

	MMU_DEVICEATTRIBS* (*pfnGetMMUDeviceAttributes)(struct _PVRSRV_DEVICE_NODE_ *psDevNode, IMG_BOOL bKernelMemoryCtx);

	PVRSRV_DEVICE_CONFIG	*psDevConfig;

	/* device post-finalise compatibility check */
	PVRSRV_ERROR			(*pfnInitDeviceCompatCheck) (struct _PVRSRV_DEVICE_NODE_*);

	/* initialise device-specific physheaps */
	PVRSRV_ERROR			(*pfnPhysMemDeviceHeapsInit) (struct _PVRSRV_DEVICE_NODE_ *);

	/* initialise fw mmu, if FW not using GPU mmu, NULL otherwise. */
	PVRSRV_ERROR			(*pfnFwMMUInit) (struct _PVRSRV_DEVICE_NODE_ *);

	/* information about the device's address space and heaps */
	DEVICE_MEMORY_INFO		sDevMemoryInfo;

	/* device's shared-virtual-memory heap max virtual address */
	IMG_UINT64				ui64GeneralSVMHeapTopVA;

	ATOMIC_T				iNumClockSpeedChanges;

	/* private device information */
	void					*pvDevice;

#if defined(SUPPORT_GPUVIRT_VALIDATION)
	RA_ARENA                *psOSSharedArena;
	RA_ARENA				*psOSidSubArena[GPUVIRT_VALIDATION_NUM_OS];
#endif

	/* FW_MAIN, FW_CONFIG and FW_GUEST heaps. Should be part of registered heaps? */
	PHYS_HEAP               *psFWMainPhysHeap;
	PHYS_HEAP               *psFWCfgPhysHeap;
	PHYS_HEAP               *apsFWPremapPhysHeap[RGX_NUM_OS_SUPPORTED];

	IMG_UINT32				ui32RegisteredPhysHeaps;
	PHYS_HEAP				**papsRegisteredPhysHeaps;

	/* PHYS_HEAP Mapping table to the platform's physical memory heap(s)
	 * used by this device. The physical heaps are created based on
	 * the PHYS_HEAP_CONFIG data from the platform's system layer at device
	 * creation time.
	 *
	 * Contains PVRSRV_PHYS_HEAP_LAST entries for all the possible physical heaps allowed in the design.
	 * It allows the system layer PhysHeaps for the device to be identified for use in creating new PMRs.
	 * See PhysHeapCreatePMR()
	 */
	PHYS_HEAP				*apsPhysHeap[PVRSRV_PHYS_HEAP_LAST];
	IMG_UINT32				ui32UserAllocHeapCount;

#if defined(SUPPORT_AUTOVZ)
	/* Phys Heap reserved for storing the MMU mappings of firmware.
	 * The memory backing up this Phys Heap must persist between driver or OS reboots */
	PHYS_HEAP               *psFwMMUReservedPhysHeap;
#endif

	/* Flag indicating if the firmware has been initialised during the
	 * 1st boot of the Host driver according to the AutoVz life-cycle. */
	IMG_BOOL				bAutoVzFwIsUp;

	struct _PVRSRV_DEVICE_NODE_	*psNext;
	struct _PVRSRV_DEVICE_NODE_	**ppsThis;

	/* Functions for notification about memory contexts */
	PVRSRV_ERROR			(*pfnRegisterMemoryContext)(struct _PVRSRV_DEVICE_NODE_	*psDeviceNode,
														MMU_CONTEXT					*psMMUContext,
														IMG_HANDLE					*hPrivData);
	void					(*pfnUnregisterMemoryContext)(IMG_HANDLE hPrivData);

	/* Functions for allocation/freeing of UFOs */
	AllocUFOBlockCallback	pfnAllocUFOBlock;	/*!< Callback for allocation of a block of UFO memory */
	FreeUFOBlockCallback	pfnFreeUFOBlock;	/*!< Callback for freeing of a block of UFO memory */

	IMG_HANDLE				hSyncServerRecordNotify;
	POS_LOCK				hSyncServerRecordLock;
	IMG_UINT32				ui32SyncServerRecordCount;
	IMG_UINT32				ui32SyncServerRecordCountHighWatermark;
	DLLIST_NODE				sSyncServerRecordList;
	struct SYNC_RECORD		*apsSyncServerRecordsFreed[PVRSRV_FULL_SYNC_TRACKING_HISTORY_LEN];
	IMG_UINT32				uiSyncServerRecordFreeIdx;

	IMG_HANDLE				hSyncCheckpointRecordNotify;
	POS_LOCK				hSyncCheckpointRecordLock;
	IMG_UINT32				ui32SyncCheckpointRecordCount;
	IMG_UINT32				ui32SyncCheckpointRecordCountHighWatermark;
	DLLIST_NODE				sSyncCheckpointRecordList;
	struct SYNC_CHECKPOINT_RECORD	*apsSyncCheckpointRecordsFreed[PVRSRV_FULL_SYNC_TRACKING_HISTORY_LEN];
	IMG_UINT32				uiSyncCheckpointRecordFreeIdx;

	IMG_HANDLE				hSyncCheckpointNotify;
	POS_SPINLOCK			hSyncCheckpointListLock; /*!< Protects sSyncCheckpointSyncsList */
	DLLIST_NODE				sSyncCheckpointSyncsList;

	PSYNC_CHECKPOINT_CONTEXT hSyncCheckpointContext;
	PSYNC_PRIM_CONTEXT		hSyncPrimContext;

	/* With this sync-prim we make sure the MMU cache is flushed
	 * before we free the page table memory */
	PVRSRV_CLIENT_SYNC_PRIM	*psMMUCacheSyncPrim;
	IMG_UINT32				ui32NextMMUInvalidateUpdate;

	IMG_HANDLE				hCmdCompNotify;
	IMG_HANDLE				hDbgReqNotify;
	IMG_HANDLE				hAppHintDbgReqNotify;
	IMG_HANDLE				hPhysHeapDbgReqNotify;

	PVRSRV_DEF_PAGE			sDummyPage;
	PVRSRV_DEF_PAGE			sDevZeroPage;

	POSWR_LOCK				hMemoryContextPageFaultNotifyListLock;
	DLLIST_NODE				sMemoryContextPageFaultNotifyListHead;

	/* System DMA capability */
	IMG_BOOL				bHasSystemDMA;
	IMG_HANDLE				hDmaTxChan;
	IMG_HANDLE				hDmaRxChan;

#if defined(PDUMP)
	/*
	 * FBC clear color register default value to use.
	 */
	IMG_UINT64				ui64FBCClearColour;

	/* Device-level callback which is called when pdump.exe starts.
	 * Should be implemented in device-specific init code, e.g. rgxinit.c
	 */
	PVRSRV_ERROR			(*pfnPDumpInitDevice)(struct _PVRSRV_DEVICE_NODE_ *psDeviceNode);
	/* device-level callback to return pdump ID associated to a memory context */
	IMG_UINT32				(*pfnMMUGetContextID)(IMG_HANDLE hDevMemContext);

	IMG_UINT8			*pui8DeferredSyncCPSignal;	/*! Deferred fence events buffer */

	IMG_UINT16			ui16SyncCPReadIdx;		/*! Read index in the above deferred fence events buffer */

	IMG_UINT16			ui16SyncCPWriteIdx;		/*! Write index in the above deferred fence events buffer */

	POS_LOCK			hSyncCheckpointSignalLock;	/*! Guards data shared between an sleepable-contexts */

	void				*pvSyncCPMISR;			/*! MISR to emit pending/deferred fence signals */

	void				*hTransition;			/*!< SyncCheckpoint PdumpTransition Cookie */

	DLLIST_NODE			sSyncCheckpointContextListHead;	/*!< List head for the sync chkpt contexts */

	POS_LOCK			hSyncCheckpointContextListLock;	/*! lock for accessing sync chkpt contexts list */

#endif

#if defined(SUPPORT_VALIDATION)
	POS_LOCK			hValidationLock;
#endif

	/* Members for linking which connections are open on this device */
	POS_LOCK                hConnectionsLock;    /*!< Lock protecting sConnections */
	DLLIST_NODE             sConnections;        /*!< The list of currently active connection objects for this device node */

#if defined(PVRSRV_DEBUG_LISR_EXECUTION)
	LISR_EXECUTION_INFO     sLISRExecutionInfo;  /*!< Information about the last execution of the LISR */
	IMG_UINT64              ui64nLISR;           /*!< Number of LISR calls seen */
	IMG_UINT64              ui64nMISR;           /*!< Number of MISR calls made */
#endif

	PVRSRV_DEVICE_DEBUG_INFO sDebugInfo;
} PVRSRV_DEVICE_NODE;

/*
 * Macros to be used instead of calling directly the pfns since these macros
 * will expand the feature passed as argument into the bitmask/index to work
 * with the macros defined in rgx_bvnc_defs_km.h
 */
#define PVRSRV_IS_FEATURE_SUPPORTED(psDevNode, Feature) \
		psDevNode->pfnCheckDeviceFeature(psDevNode, RGX_FEATURE_##Feature##_BIT_MASK)
#define PVRSRV_GET_DEVICE_FEATURE_VALUE(psDevNode, Feature) \
		psDevNode->pfnGetDeviceFeatureValue(psDevNode, RGX_FEATURE_##Feature##_IDX)

PVRSRV_ERROR PVRSRVDeviceFinalise(PVRSRV_DEVICE_NODE *psDeviceNode,
											   IMG_BOOL bInitSuccessful);

PVRSRV_ERROR PVRSRVDevInitCompatCheck(PVRSRV_DEVICE_NODE *psDeviceNode);

PVRSRV_ERROR RGXClientConnectCompatCheck_ClientAgainstFW(PVRSRV_DEVICE_NODE * psDeviceNode, IMG_UINT32 ui32ClientBuildOptions);


#endif /* DEVICE_H */

/******************************************************************************
 End of file (device.h)
******************************************************************************/
