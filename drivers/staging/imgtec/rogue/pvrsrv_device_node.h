/* SPDX-License-Identifier: GPL-2.0 */

/*  --------------------------------------------------------------------------------------------------------
 *  File: pvrsrv_device_node.h
 *  --------------------------------------------------------------------------------------------------------
 */

#ifndef __PVRSRV_DEVICE_NODE_H__
#define __PVRSRV_DEVICE_NODE_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------------------------------------
 *  Include Files
 * ---------------------------------------------------------------------------------------------------------
 */

#include "powervr/sync_external.h"
#include "ra.h"
#include "devicemem_heapcfg.h"
#include "pvrsrv_device.h"
#include "mmu_common.h"
#include "img_types.h"
#include "lock_types.h"


/* ---------------------------------------------------------------------------------------------------------
 *  Macros Definition
 * ---------------------------------------------------------------------------------------------------------
 */


/* ---------------------------------------------------------------------------------------------------------
 *  Types and Structures Definition
 * ---------------------------------------------------------------------------------------------------------
 */

typedef struct _PG_HANDLE_ {
	union {
		void *pvHandle;
		IMG_UINT64 ui64Handle;
	} u;
	/*Order of the corresponding allocation */
	IMG_UINT32	ui32Order;
} PG_HANDLE;

typedef PVRSRV_ERROR (*FN_CREATERAMBACKEDPMR)(struct _PVRSRV_DEVICE_NODE_ *psDevNode,
										IMG_DEVMEM_SIZE_T uiSize,
										IMG_DEVMEM_SIZE_T uiChunkSize,
										IMG_UINT32 ui32NumPhysChunks,
										IMG_UINT32 ui32NumVirtChunks,
										IMG_UINT32 *pui32MappingTable,
										IMG_UINT32 uiLog2PageSize,
										PVRSRV_MEMALLOCFLAGS_T uiFlags,
										const IMG_CHAR *pszAnnotation,
										PMR **ppsPMRPtr);


typedef struct _PVRSRV_POWER_DEV_TAG_ PVRSRV_POWER_DEV;

typedef enum _PVRSRV_DEVICE_STATE_ {
	PVRSRV_DEVICE_STATE_UNDEFINED = 0,
	PVRSRV_DEVICE_STATE_INIT,
	PVRSRV_DEVICE_STATE_ACTIVE,
	PVRSRV_DEVICE_STATE_DEINIT,
	PVRSRV_DEVICE_STATE_BAD,
} PVRSRV_DEVICE_STATE;

typedef struct _PVRSRV_DEVICE_IDENTIFIER_ {
	/* Pdump memory and register bank names */
	IMG_CHAR				*pszPDumpDevName;
	IMG_CHAR				*pszPDumpRegName;
} PVRSRV_DEVICE_IDENTIFIER;

typedef struct _DEVICE_MEMORY_INFO_ {
	/* heap count.  Doesn't include additional heaps from PVRSRVCreateDeviceMemHeap */
	IMG_UINT32				ui32HeapCount;

    /* Blueprints for creating new device memory contexts */
	IMG_UINT32              uiNumHeapConfigs;
	DEVMEM_HEAP_CONFIG      *psDeviceMemoryHeapConfigArray;
	DEVMEM_HEAP_BLUEPRINT   *psDeviceMemoryHeap;
} DEVICE_MEMORY_INFO;

/*********************************************************************/ /*!
 @Function      AllocUFOCallback
 @Description   Device specific callback for allocation of an UFO block

 @Input			psDeviceNode			Pointer to device node to allocate
										the UFO for.
 @Output		ppsMemDesc				Pointer to pointer for the memdesc of
										the allocation
 @Output		pui32SyncAddr			FW Base address of the UFO block
 @Output		puiSyncPrimBlockSize	Size of the UFO block

 @Return		PVRSRV_OK if allocation was successful
 */
/*********************************************************************/
typedef PVRSRV_ERROR (*AllocUFOBlockCallback)(struct _PVRSRV_DEVICE_NODE_ *psDeviceNode,
														DEVMEM_MEMDESC **ppsMemDesc,
														IMG_UINT32 *pui32SyncAddr,
														IMG_UINT32 *puiSyncPrimBlockSize);

/*********************************************************************/ /*!
 @Function		FreeUFOCallback
 @Description   Device specific callback for freeing of an UFO

 @Input			psDeviceNode	Pointer to device node that the UFO block was
								allocated from.
 @Input			psMemDesc		Pointer to pointer for the memdesc of
								the UFO block to free.
 */
/*********************************************************************/
typedef void (*FreeUFOBlockCallback)(struct _PVRSRV_DEVICE_NODE_ *psDeviceNode,
											DEVMEM_MEMDESC *psMemDesc);

typedef struct __DUMMY_PAGE__ {
	/*Page handle for the dummy page allocated (UMA/LMA)*/
	PG_HANDLE	sDummyPageHandle;
	POS_LOCK	psDummyPgLock;
	ATOMIC_T	atRefCounter;
	/*Dummy page size in terms of log2 */
	IMG_UINT32	ui32Log2DummyPgSize;
	IMG_UINT64	ui64DummyPgPhysAddr;
#if defined(PDUMP)
#define DUMMY_PAGE	("DUMMY_PAGE")
	IMG_HANDLE hPdumpDummyPg;
#endif
} PVRSRV_DUMMY_PAGE ;

// .CP :
typedef struct _PVRSRV_DEVICE_NODE_ {
	PVRSRV_DEVICE_IDENTIFIER	sDevId;

	PVRSRV_DEVICE_STATE			eDevState;
	ATOMIC_T					eHealthStatus; /* Holds values from PVRSRV_DEVICE_HEALTH_STATUS */
	ATOMIC_T					eHealthReason; /* Holds values from PVRSRV_DEVICE_HEALTH_REASON */

	IMG_HANDLE					*hDebugTable;

	/* device specific MMU attributes */
	MMU_DEVICEATTRIBS			*psMMUDevAttrs;
	/* device specific MMU firmware atrributes, used only in some devices*/
	MMU_DEVICEATTRIBS			*psFirmwareMMUDevAttrs;

	/* lock for power state transitions */
	POS_LOCK					hPowerLock;
	/* current system device power state */
	PVRSRV_SYS_POWER_STATE		eCurrentSysPowerState;
	PVRSRV_POWER_DEV			*psPowerDev;

	/*
		callbacks the device must support:
	*/

	FN_CREATERAMBACKEDPMR pfnCreateRamBackedPMR[PVRSRV_DEVICE_PHYS_HEAP_LAST];

	PVRSRV_ERROR (*pfnDevPxAlloc)(struct _PVRSRV_DEVICE_NODE_ *psDevNode, size_t uiSize,
								PG_HANDLE *psMemHandle, IMG_DEV_PHYADDR *psDevPAddr);

	void (*pfnDevPxFree)(struct _PVRSRV_DEVICE_NODE_ *psDevNode, PG_HANDLE *psMemHandle);

	PVRSRV_ERROR (*pfnDevPxMap)(struct _PVRSRV_DEVICE_NODE_ *psDevNode, PG_HANDLE *pshMemHandle,
								size_t uiSize, IMG_DEV_PHYADDR *psDevPAddr,
								void **pvPtr);

	void (*pfnDevPxUnMap)(struct _PVRSRV_DEVICE_NODE_ *psDevNode,
						  PG_HANDLE *psMemHandle, void *pvPtr);

	PVRSRV_ERROR (*pfnDevPxClean)(struct _PVRSRV_DEVICE_NODE_ *psDevNode,
								PG_HANDLE *pshMemHandle,
								IMG_UINT32 uiOffset,
								IMG_UINT32 uiLength);

	IMG_UINT32 uiMMUPxLog2AllocGran;

	void (*pfnMMUCacheInvalidate)(struct _PVRSRV_DEVICE_NODE_ *psDevNode,
									IMG_HANDLE hDeviceData,
									MMU_LEVEL eLevel,
									IMG_BOOL bUnmap);

	PVRSRV_ERROR (*pfnMMUCacheInvalidateKick)(struct _PVRSRV_DEVICE_NODE_ *psDevNode,
									IMG_UINT32 *pui32NextMMUInvalidateUpdate,
									IMG_BOOL bInterrupt);

	IMG_UINT32 (*pfnMMUCacheGetInvalidateCounter)(struct _PVRSRV_DEVICE_NODE_ *psDevNode);

	void (*pfnDumpDebugInfo)(struct _PVRSRV_DEVICE_NODE_ *psDevNode);

	PVRSRV_ERROR (*pfnUpdateHealthStatus)(struct _PVRSRV_DEVICE_NODE_ *psDevNode,
									IMG_BOOL bIsTimerPoll);

	PVRSRV_ERROR (*pfnResetHWRLogs)(struct _PVRSRV_DEVICE_NODE_ *psDevNode);

	/* Method to drain device HWPerf packets from firmware buffer to host buffer */
	PVRSRV_ERROR (*pfnServiceHWPerf)(struct _PVRSRV_DEVICE_NODE_ *psDevNode);

	PVRSRV_ERROR (*pfnDeviceVersionString)(struct _PVRSRV_DEVICE_NODE_ *psDevNode, IMG_CHAR **ppszVersionString);

	PVRSRV_ERROR (*pfnDeviceClockSpeed)(struct _PVRSRV_DEVICE_NODE_ *psDevNode, IMG_PUINT32 pui32RGXClockSpeed);

	PVRSRV_ERROR (*pfnSoftReset)(struct _PVRSRV_DEVICE_NODE_ *psDevNode, IMG_UINT64 ui64ResetValue1, IMG_UINT64 ui64ResetValue2);

#if defined(SUPPORT_KERNEL_SRVINIT) && defined(RGXFW_ALIGNCHECKS)
	PVRSRV_ERROR (*pfnAlignmentCheck)(struct _PVRSRV_DEVICE_NODE_ *psDevNode, IMG_UINT32 ui32FWAlignChecksSize, IMG_UINT32 aui32FWAlignChecks[]);
#endif
	IMG_BOOL	(*pfnCheckDeviceFeature)(struct _PVRSRV_DEVICE_NODE_ *psDevNode, IMG_UINT64 ui64FeatureMask);

	IMG_INT32	(*pfnGetDeviceFeatureValue)(struct _PVRSRV_DEVICE_NODE_ *psDevNode, IMG_UINT64 ui64FeatureMask);

	PVRSRV_DEVICE_CONFIG	*psDevConfig;

	/* device post-finalise compatibility check */
	PVRSRV_ERROR			(*pfnInitDeviceCompatCheck)(struct _PVRSRV_DEVICE_NODE_ *);

	/* information about the device's address space and heaps */
	DEVICE_MEMORY_INFO		sDevMemoryInfo;

	/* device's shared-virtual-memory heap size */
	IMG_UINT64				ui64GeneralSVMHeapSize;

	/* private device information */
	void					*pvDevice;



#if defined(SUPPORT_GPUVIRT_VALIDATION)
	RA_ARENA                *psOSidSubArena[GPUVIRT_VALIDATION_NUM_OS];
#endif


#define PVRSRV_MAX_RA_NAME_LENGTH (50)
	RA_ARENA				**apsLocalDevMemArenas;
	IMG_CHAR				**apszRANames;
	IMG_UINT32				ui32NumOfLocalMemArenas;

#if defined(SUPPORT_PVRSRV_GPUVIRT)
	IMG_CHAR				szKernelFwRAName[RGXFW_NUM_OS][PVRSRV_MAX_RA_NAME_LENGTH];
	RA_ARENA				*psKernelFwMemArena[RGXFW_NUM_OS];
	IMG_UINT32				uiKernelFwRAIdx;
	RA_BASE_T				ui64RABase[RGXFW_NUM_OS];
#endif

	IMG_UINT32				ui32RegisteredPhysHeaps;
	PHYS_HEAP				**papsRegisteredPhysHeaps;

	/*
	 * Pointers to the device's physical memory heap(s)
	 * The first entry (apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL]) will be used for allocations
	 *  where the PVRSRV_MEMALLOCFLAG_CPU_LOCAL flag is not set. Normally this will be an LMA heap
	 *  (but the device configuration could specify a UMA heap here, if desired)
	 * The second entry (apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_CPU_LOCAL]) will be used for allocations
	 *  where the PVRSRV_MEMALLOCFLAG_CPU_LOCAL flag is set. Normally this will be a UMA heap
	 *  (but the configuration could specify an LMA heap here, if desired)
	 * The third entry (apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_FW_LOCAL]) will be used for allocations
	 *  where the PVRSRV_MEMALLOCFLAG_FW_LOCAL flag is set; this is used when SUPPORT_PVRSRV_GPUVIRT is enabled
	 * The device configuration will always specify two physical heap IDs - in the event of the device
	 *  only using one physical heap, both of these IDs will be the same, and hence both pointers below
	 *  will also be the same; when SUPPORT_PVRSRV_GPUVIRT is enabled the device configuration specifies
	 *  three physical heap IDs, the last being for PVRSRV_DEVICE_PHYS_HEAP_FW_LOCAL allocations
	 */
	PHYS_HEAP				*apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_LAST];

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

#if defined(SUPPORT_BUFFER_SYNC)
	struct pvr_buffer_sync_context *psBufferSyncContext;
#endif

	IMG_HANDLE				hSyncServerNotify;
	POS_LOCK				hSyncServerListLock;
	DLLIST_NODE				sSyncServerSyncsList;

#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING)
	IMG_HANDLE				hSyncServerRecordNotify;
	POS_LOCK				hSyncServerRecordLock;
	DLLIST_NODE				sSyncServerRecordList;
	struct SYNC_RECORD		*apsSyncServerRecordsFreed[PVRSRV_FULL_SYNC_TRACKING_HISTORY_LEN];
	IMG_UINT32				uiSyncServerRecordFreeIdx;
#endif

	PSYNC_PRIM_CONTEXT		hSyncPrimContext;

	PVRSRV_CLIENT_SYNC_PRIM	*psSyncPrim;
	/* With this sync-prim we make sure the MMU cache is flushed
	 * before we free the page table memory */
	PVRSRV_CLIENT_SYNC_PRIM	*psMMUCacheSyncPrim;
	IMG_UINT32				ui32NextMMUInvalidateUpdate;

	IMG_HANDLE				hCmdCompNotify;
	IMG_HANDLE				hDbgReqNotify;
	IMG_HANDLE				hHtbDbgReqNotify;
	IMG_HANDLE				hAppHintDbgReqNotify;

	PVRSRV_DUMMY_PAGE		sDummyPage;

	DLLIST_NODE				sMemoryContextPageFaultNotifyListHead;

#if defined(PDUMP)
	/* 	device-level callback which is called when pdump.exe starts.
	 *	Should be implemented in device-specific init code, e.g. rgxinit.c
	 */
	PVRSRV_ERROR			(*pfnPDumpInitDevice)(struct _PVRSRV_DEVICE_NODE_ *psDeviceNode);
	/* device-level callback to return pdump ID associated to a memory context */
	IMG_UINT32				(*pfnMMUGetContextID)(IMG_HANDLE hDevMemContext);
#endif
} PVRSRV_DEVICE_NODE;

/* ---------------------------------------------------------------------------------------------------------
 *  Global Functions' Prototype
 * ---------------------------------------------------------------------------------------------------------
 */


/* ---------------------------------------------------------------------------------------------------------
 *  Inline Functions Implementation
 * ---------------------------------------------------------------------------------------------------------
 */

#ifdef __cplusplus
}
#endif

#endif /* __PVRSRV_DEVICE_NODE_H__ */

