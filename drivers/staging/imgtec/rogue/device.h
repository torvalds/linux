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

#ifndef __DEVICE_H__
#define __DEVICE_H__


#include "devicemem_heapcfg.h"
#include "mmu_common.h"	
#include "ra.h"  		/* RA_ARENA */
#include "pvrsrv_device.h"
#include "srvkm.h"
#include "physheap.h"
#include <powervr/sync_external.h>
#include "sysinfo.h"
#include "dllist.h"
#include "cache_km.h"

#include "lock.h"

#if defined(SUPPORT_GPUVIRT_VALIDATION)
#include "virt_validation_defs.h"
#endif

#if defined(SUPPORT_BUFFER_SYNC)
struct pvr_buffer_sync_context;
#endif

typedef struct _PVRSRV_POWER_DEV_TAG_ PVRSRV_POWER_DEV;

#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING)
struct SYNC_RECORD;
#endif

/*********************************************************************/ /*!
 @Function      AllocUFOCallback
 @Description   Device specific callback for allocation of an UFO block

 @Input         psDeviceNode          Pointer to device node to allocate
                                      the UFO for.
 @Output        ppsMemDesc            Pointer to pointer for the memdesc of
                                      the allocation
 @Output        pui32SyncAddr         FW Base address of the UFO block
 @Output        puiSyncPrimBlockSize  Size of the UFO block

 @Return        PVRSRV_OK if allocation was successful
 */
/*********************************************************************/
typedef PVRSRV_ERROR (*AllocUFOBlockCallback)(struct _PVRSRV_DEVICE_NODE_ *psDeviceNode,
														DEVMEM_MEMDESC **ppsMemDesc,
														IMG_UINT32 *pui32SyncAddr,
														IMG_UINT32 *puiSyncPrimBlockSize);

/*********************************************************************/ /*!
 @Function      FreeUFOCallback
 @Description   Device specific callback for freeing of an UFO

 @Input         psDeviceNode    Pointer to device node that the UFO block was
                                allocated from.
 @Input         psMemDesc       Pointer to pointer for the memdesc of
                                the UFO block to free.
 */
/*********************************************************************/
typedef void (*FreeUFOBlockCallback)(struct _PVRSRV_DEVICE_NODE_ *psDeviceNode,
									 DEVMEM_MEMDESC *psMemDesc);

typedef struct _PVRSRV_DEVICE_IDENTIFIER_
{
	/* Pdump memory and register bank names */
	IMG_CHAR				*pszPDumpDevName;
	IMG_CHAR				*pszPDumpRegName;
} PVRSRV_DEVICE_IDENTIFIER;

typedef struct _DEVICE_MEMORY_INFO_
{
	/* heap count.  Doesn't include additional heaps from PVRSRVCreateDeviceMemHeap */
	IMG_UINT32				ui32HeapCount;

    /* Blueprints for creating new device memory contexts */
    IMG_UINT32              uiNumHeapConfigs;
    DEVMEM_HEAP_CONFIG      *psDeviceMemoryHeapConfigArray;
    DEVMEM_HEAP_BLUEPRINT   *psDeviceMemoryHeap;
} DEVICE_MEMORY_INFO;


typedef struct _PG_HANDLE_
{
	union
	{
		void *pvHandle;
		IMG_UINT64 ui64Handle;
	}u;
	/*Order of the corresponding allocation */
	IMG_UINT32	ui32Order;
} PG_HANDLE;

#define MMU_BAD_PHYS_ADDR (0xbadbad00badULL)
typedef struct __DUMMY_PAGE__
{
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
	PVRSRV_DEVICE_HEALTH_STATUS_OK = 0,
	PVRSRV_DEVICE_HEALTH_STATUS_NOT_RESPONDING,
	PVRSRV_DEVICE_HEALTH_STATUS_DEAD
} PVRSRV_DEVICE_HEALTH_STATUS;

typedef enum _PVRSRV_DEVICE_HEALTH_REASON_
{
	PVRSRV_DEVICE_HEALTH_REASON_NONE = 0,
	PVRSRV_DEVICE_HEALTH_REASON_ASSERTED,
	PVRSRV_DEVICE_HEALTH_REASON_POLL_FAILING,
	PVRSRV_DEVICE_HEALTH_REASON_TIMEOUTS,
	PVRSRV_DEVICE_HEALTH_REASON_QUEUE_CORRUPT,
	PVRSRV_DEVICE_HEALTH_REASON_QUEUE_STALLED
} PVRSRV_DEVICE_HEALTH_REASON;

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

typedef struct _PVRSRV_DEVICE_NODE_
{
	PVRSRV_DEVICE_IDENTIFIER	sDevId;

	PVRSRV_DEVICE_STATE			eDevState;
	ATOMIC_T					eHealthStatus; /* Holds values from PVRSRV_DEVICE_HEALTH_STATUS */
	ATOMIC_T					eHealthReason; /* Holds values from PVRSRV_DEVICE_HEALTH_REASON */

	IMG_HANDLE						*hDebugTable;

	/* device specific MMU attributes */
   	MMU_DEVICEATTRIBS      *psMMUDevAttrs;
	/* device specific MMU firmware atrributes, used only in some devices*/
	MMU_DEVICEATTRIBS      *psFirmwareMMUDevAttrs;

	/* lock for power state transitions */
	POS_LOCK				hPowerLock;
	/* current system device power state */
	PVRSRV_SYS_POWER_STATE	eCurrentSysPowerState;
	PVRSRV_POWER_DEV		*psPowerDev;

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
	PVRSRV_ERROR			(*pfnInitDeviceCompatCheck) (struct _PVRSRV_DEVICE_NODE_*);

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

PVRSRV_ERROR IMG_CALLCONV PVRSRVDeviceFinalise(PVRSRV_DEVICE_NODE *psDeviceNode,
											   IMG_BOOL bInitSuccessful);

PVRSRV_ERROR IMG_CALLCONV PVRSRVDevInitCompatCheck(PVRSRV_DEVICE_NODE *psDeviceNode);

PVRSRV_ERROR IMG_CALLCONV RGXClientConnectCompatCheck_ClientAgainstFW(PVRSRV_DEVICE_NODE * psDeviceNode, IMG_UINT32 ui32ClientBuildOptions);

	
#endif /* __DEVICE_H__ */

/******************************************************************************
 End of file (device.h)
******************************************************************************/
