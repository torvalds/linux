/*************************************************************************/ /*!
@File
@Title          Device Memory Management
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Server-side component of the Device Memory Management.
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
/* our exported API */
#include "devicemem_server.h"
#include "devicemem_utils.h"
#include "devicemem.h"

#include "device.h" /* For device node */
#include "img_types.h"
#include "img_defs.h"
#include "pvr_debug.h"
#include "pvrsrv_error.h"

#include "mmu_common.h"
#include "pdump_km.h"
#include "pmr.h"
#include "physmem.h"
#include "pdumpdesc.h"

#include "allocmem.h"
#include "osfunc.h"
#include "lock.h"

#define DEVMEMCTX_FLAGS_FAULT_ADDRESS_AVAILABLE (1 << 0)

struct _DEVMEMINT_CTX_
{
	PVRSRV_DEVICE_NODE *psDevNode;

	/* MMU common code needs to have a context. There's a one-to-one
	   correspondence between device memory context and MMU context,
	   but we have the abstraction here so that we don't need to care
	   what the MMU does with its context, and the MMU code need not
	   know about us at all. */
	MMU_CONTEXT *psMMUContext;

	ATOMIC_T hRefCount;

	/* This handle is for devices that require notification when a new
	   memory context is created and they need to store private data that
	   is associated with the context. */
	IMG_HANDLE hPrivData;

	/* Protects access to sProcessNotifyListHead */
	POSWR_LOCK hListLock;

	/* The following tracks UM applications that need to be notified of a
	 * page fault */
	DLLIST_NODE sProcessNotifyListHead;
	/* The following is a node for the list of registered devmem contexts */
	DLLIST_NODE sPageFaultNotifyListElem;

	/* Device virtual address of a page fault on this context */
	IMG_DEV_VIRTADDR sFaultAddress;

	/* General purpose flags */
	IMG_UINT32 ui32Flags;
};

struct _DEVMEMINT_CTX_EXPORT_
{
	DEVMEMINT_CTX *psDevmemCtx;
	PMR *psPMR;
	ATOMIC_T hRefCount;
	DLLIST_NODE sNode;
};

struct _DEVMEMINT_HEAP_
{
	struct _DEVMEMINT_CTX_ *psDevmemCtx;
	IMG_UINT32 uiLog2PageSize;
	ATOMIC_T hRefCount;
};

struct _DEVMEMINT_RESERVATION_
{
	struct _DEVMEMINT_HEAP_ *psDevmemHeap;
	IMG_DEV_VIRTADDR sBase;
	IMG_DEVMEM_SIZE_T uiLength;
};

struct _DEVMEMINT_MAPPING_
{
	struct _DEVMEMINT_RESERVATION_ *psReservation;
	PMR *psPMR;
	IMG_UINT32 uiNumPages;
};

struct _DEVMEMINT_PF_NOTIFY_
{
	IMG_UINT32  ui32PID;
	DLLIST_NODE sProcessNotifyListElem;
};

/*************************************************************************/ /*!
@Function       DevmemIntCtxAcquire
@Description    Acquire a reference to the provided device memory context.
@Return         None
*/ /**************************************************************************/
static INLINE void DevmemIntCtxAcquire(DEVMEMINT_CTX *psDevmemCtx)
{
	OSAtomicIncrement(&psDevmemCtx->hRefCount);
}

/*************************************************************************/ /*!
@Function       DevmemIntCtxRelease
@Description    Release the reference to the provided device memory context.
                If this is the last reference which was taken then the
                memory context will be freed.
@Return         None
*/ /**************************************************************************/
static INLINE void DevmemIntCtxRelease(DEVMEMINT_CTX *psDevmemCtx)
{
	if (OSAtomicDecrement(&psDevmemCtx->hRefCount) == 0)
	{
		/* The last reference has gone, destroy the context */
		PVRSRV_DEVICE_NODE *psDevNode = psDevmemCtx->psDevNode;
		DLLIST_NODE *psNode, *psNodeNext;

		/* If there are any PIDs registered for page fault notification.
		 * Loop through the registered PIDs and free each one */
		dllist_foreach_node(&(psDevmemCtx->sProcessNotifyListHead), psNode, psNodeNext)
		{
			DEVMEMINT_PF_NOTIFY *psNotifyNode =
				IMG_CONTAINER_OF(psNode, DEVMEMINT_PF_NOTIFY, sProcessNotifyListElem);
			dllist_remove_node(psNode);
			OSFreeMem(psNotifyNode);
		}

		/* If this context is in the list registered for a debugger, remove
		 * from that list */
		if (dllist_node_is_in_list(&psDevmemCtx->sPageFaultNotifyListElem))
		{
			dllist_remove_node(&psDevmemCtx->sPageFaultNotifyListElem);
		}

		if (psDevNode->pfnUnregisterMemoryContext)
		{
			psDevNode->pfnUnregisterMemoryContext(psDevmemCtx->hPrivData);
		}
		MMU_ContextDestroy(psDevmemCtx->psMMUContext);

		OSWRLockDestroy(psDevmemCtx->hListLock);

		PVR_DPF((PVR_DBG_MESSAGE, "%s: Freed memory context %p",
				 __func__, psDevmemCtx));
		OSFreeMem(psDevmemCtx);
	}
}

/*************************************************************************/ /*!
@Function       DevmemIntHeapAcquire
@Description    Acquire a reference to the provided device memory heap.
@Return         None
*/ /**************************************************************************/
static INLINE void DevmemIntHeapAcquire(DEVMEMINT_HEAP *psDevmemHeap)
{
	OSAtomicIncrement(&psDevmemHeap->hRefCount);
}

/*************************************************************************/ /*!
@Function       DevmemIntHeapRelease
@Description    Release the reference to the provided device memory heap.
                If this is the last reference which was taken then the
                memory context will be freed.
@Return         None
*/ /**************************************************************************/
static INLINE void DevmemIntHeapRelease(DEVMEMINT_HEAP *psDevmemHeap)
{
	OSAtomicDecrement(&psDevmemHeap->hRefCount);
}

PVRSRV_ERROR
DevmemIntUnpin(PMR *psPMR)
{
	PVRSRV_ERROR eError;

	/* Unpin */
	eError = PMRUnpinPMR(psPMR, IMG_FALSE);

	return eError;
}

PVRSRV_ERROR
DevmemIntUnpinInvalidate(DEVMEMINT_MAPPING *psDevmemMapping, PMR *psPMR)
{
	PVRSRV_ERROR eError;

	eError = PMRUnpinPMR(psPMR, IMG_TRUE);
	PVR_GOTO_IF_ERROR(eError, e_exit);

	/* Invalidate mapping */
	eError = MMU_ChangeValidity(psDevmemMapping->psReservation->psDevmemHeap->psDevmemCtx->psMMUContext,
	                            psDevmemMapping->psReservation->sBase,
	                            psDevmemMapping->uiNumPages,
	                            psDevmemMapping->psReservation->psDevmemHeap->uiLog2PageSize,
	                            IMG_FALSE, /* !< Choose to invalidate PT entries */
	                            psPMR);

e_exit:
	return eError;
}

PVRSRV_ERROR
DevmemIntPin(PMR *psPMR)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	/* Start the pinning */
	eError = PMRPinPMR(psPMR);

	return eError;
}

PVRSRV_ERROR
DevmemIntPinValidate(DEVMEMINT_MAPPING *psDevmemMapping, PMR *psPMR)
{
	PVRSRV_ERROR eError;
	PVRSRV_ERROR eErrorMMU = PVRSRV_OK;
	IMG_UINT32 uiLog2PageSize = psDevmemMapping->psReservation->psDevmemHeap->uiLog2PageSize;

	/* Start the pinning */
	eError = PMRPinPMR(psPMR);

	if (eError == PVRSRV_OK)
	{
		/* Make mapping valid again */
		eErrorMMU = MMU_ChangeValidity(psDevmemMapping->psReservation->psDevmemHeap->psDevmemCtx->psMMUContext,
		                            psDevmemMapping->psReservation->sBase,
		                            psDevmemMapping->uiNumPages,
		                            uiLog2PageSize,
		                            IMG_TRUE, /* !< Choose to make PT entries valid again */
		                            psPMR);
	}
	else if (eError == PVRSRV_ERROR_PMR_NEW_MEMORY)
	{
		/* If we lost the physical backing we have to map it again because
		 * the old physical addresses are not valid anymore. */
		PMR_FLAGS_T uiFlags;
		uiFlags = PMR_Flags(psPMR);

		eErrorMMU = MMU_MapPages(psDevmemMapping->psReservation->psDevmemHeap->psDevmemCtx->psMMUContext,
		                         uiFlags,
		                         psDevmemMapping->psReservation->sBase,
		                         psPMR,
		                         0,
		                         psDevmemMapping->uiNumPages,
		                         NULL,
		                         uiLog2PageSize);
	}

	/* Just overwrite eError if the mappings failed.
	 * PMR_NEW_MEMORY has to be propagated to the user. */
	if (eErrorMMU != PVRSRV_OK)
	{
		eError = eErrorMMU;
	}

	return eError;
}

/*************************************************************************/ /*!
@Function       DevmemServerGetImportHandle
@Description    For given exportable memory descriptor returns PMR handle.
@Return         Memory is exportable - Success
                PVRSRV_ERROR failure code
*/ /**************************************************************************/
PVRSRV_ERROR
DevmemServerGetImportHandle(DEVMEM_MEMDESC *psMemDesc,
                            IMG_HANDLE *phImport)
{
	PVRSRV_ERROR eError;

	if ((GetImportProperties(psMemDesc->psImport) & DEVMEM_PROPERTIES_EXPORTABLE) == 0)
	{
		PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_DEVICEMEM_CANT_EXPORT_SUBALLOCATION, e0);
	}

	/* A new handle means a new import tracking the PMR.
	 * Hence the source PMR memory layout should be marked fixed
	 * to make sure the importer view of the memory is the same as
	 * the exporter throughout its lifetime */
	PMR_SetLayoutFixed((PMR *)psMemDesc->psImport->hPMR, IMG_TRUE);

	*phImport = psMemDesc->psImport->hPMR;
	return PVRSRV_OK;

e0:
	return eError;
}

/*************************************************************************/ /*!
@Function       DevmemServerGetHeapHandle
@Description    For given reservation returns the Heap handle.
@Return         PVRSRV_ERROR failure code
*/ /**************************************************************************/
PVRSRV_ERROR
DevmemServerGetHeapHandle(DEVMEMINT_RESERVATION *psReservation,
                          IMG_HANDLE *phHeap)
{
	if (psReservation == NULL || phHeap == NULL)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	*phHeap = psReservation->psDevmemHeap;

	return PVRSRV_OK;
}

/*************************************************************************/ /*!
@Function       DevmemServerGetContext
@Description    For given heap returns the context.
@Return         PVRSRV_ERROR failure code
*/ /**************************************************************************/
PVRSRV_ERROR
DevmemServerGetContext(DEVMEMINT_HEAP *psDevmemHeap,
					   DEVMEMINT_CTX **ppsDevmemCtxPtr)
{
	if (psDevmemHeap == NULL || ppsDevmemCtxPtr == NULL)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	*ppsDevmemCtxPtr = psDevmemHeap->psDevmemCtx;

	return PVRSRV_OK;
}

/*************************************************************************/ /*!
@Function       DevmemServerGetPrivData
@Description    For given context returns the private data handle.
@Return         PVRSRV_ERROR failure code
*/ /**************************************************************************/
PVRSRV_ERROR
DevmemServerGetPrivData(DEVMEMINT_CTX *psDevmemCtx,
						IMG_HANDLE *phPrivData)
{
	if (psDevmemCtx == NULL || phPrivData == NULL)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	*phPrivData = psDevmemCtx->hPrivData;

	return PVRSRV_OK;
}

/*************************************************************************/ /*!
@Function       DevmemIntCtxCreate
@Description    Creates and initialises a device memory context.
@Return         valid Device Memory context handle - Success
                PVRSRV_ERROR failure code
*/ /**************************************************************************/
PVRSRV_ERROR
DevmemIntCtxCreate(CONNECTION_DATA *psConnection,
                   PVRSRV_DEVICE_NODE *psDeviceNode,
                   IMG_BOOL bKernelMemoryCtx,
                   DEVMEMINT_CTX **ppsDevmemCtxPtr,
                   IMG_HANDLE *hPrivData,
                   IMG_UINT32 *pui32CPUCacheLineSize)
{
	PVRSRV_ERROR eError;
	DEVMEMINT_CTX *psDevmemCtx;
	IMG_HANDLE hPrivDataInt = NULL;
	MMU_DEVICEATTRIBS *psMMUDevAttrs = psDeviceNode->pfnGetMMUDeviceAttributes(psDeviceNode,
	                                                                           bKernelMemoryCtx);

	PVR_DPF((PVR_DBG_MESSAGE, "%s", __func__));

	/*
	 * Ensure that we are safe to perform unaligned accesses on memory
	 * we mark write-combine, as the compiler might generate
	 * instructions operating on this memory which require this
	 * assumption to be true.
	 */
	PVR_ASSERT(OSIsWriteCombineUnalignedSafe());

	/* allocate a Devmem context */
	psDevmemCtx = OSAllocMem(sizeof(*psDevmemCtx));
	PVR_LOG_GOTO_IF_NOMEM(psDevmemCtx, eError, fail_alloc);

	OSAtomicWrite(&psDevmemCtx->hRefCount, 1);
	psDevmemCtx->psDevNode = psDeviceNode;

	/* Call down to MMU context creation */

	eError = MMU_ContextCreate(psConnection,
	                           psDeviceNode,
	                           &psDevmemCtx->psMMUContext,
	                           psMMUDevAttrs);
	PVR_LOG_GOTO_IF_ERROR(eError, "MMU_ContextCreate", fail_mmucontext);

	if (psDeviceNode->pfnRegisterMemoryContext)
	{
		eError = psDeviceNode->pfnRegisterMemoryContext(psDeviceNode, psDevmemCtx->psMMUContext, &hPrivDataInt);
		PVR_LOG_GOTO_IF_ERROR(eError, "pfnRegisterMemoryContext", fail_register);
	}

	/* Store the private data as it is required to unregister the memory context */
	psDevmemCtx->hPrivData = hPrivDataInt;
	*hPrivData = hPrivDataInt;
	*ppsDevmemCtxPtr = psDevmemCtx;

	/* Pass the CPU cache line size through the bridge to the user mode as it can't be queried in user mode.*/
	*pui32CPUCacheLineSize = OSCPUCacheAttributeSize(OS_CPU_CACHE_ATTRIBUTE_LINE_SIZE);

	/* Initialise the PID notify list */
	OSWRLockCreate(&psDevmemCtx->hListLock);
	dllist_init(&(psDevmemCtx->sProcessNotifyListHead));
	psDevmemCtx->sPageFaultNotifyListElem.psNextNode = NULL;
	psDevmemCtx->sPageFaultNotifyListElem.psPrevNode = NULL;

	/* Initialise page fault address */
	psDevmemCtx->sFaultAddress.uiAddr = 0ULL;

	/* Initialise flags */
	psDevmemCtx->ui32Flags = 0;

	return PVRSRV_OK;

fail_register:
	MMU_ContextDestroy(psDevmemCtx->psMMUContext);
fail_mmucontext:
	OSFreeMem(psDevmemCtx);
fail_alloc:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

/*************************************************************************/ /*!
@Function       DevmemIntHeapCreate
@Description    Creates and initialises a device memory heap.
@Return         valid Device Memory heap handle - Success
                PVRSRV_ERROR failure code
*/ /**************************************************************************/
PVRSRV_ERROR
DevmemIntHeapCreate(DEVMEMINT_CTX *psDevmemCtx,
                    IMG_DEV_VIRTADDR sHeapBaseAddr,
                    IMG_DEVMEM_SIZE_T uiHeapLength,
                    IMG_UINT32 uiLog2DataPageSize,
                    DEVMEMINT_HEAP **ppsDevmemHeapPtr)
{
	DEVMEMINT_HEAP *psDevmemHeap;

	PVR_DPF((PVR_DBG_MESSAGE, "%s", __func__));

	/* allocate a Devmem context */
	psDevmemHeap = OSAllocMem(sizeof(*psDevmemHeap));
	PVR_LOG_RETURN_IF_NOMEM(psDevmemHeap, "psDevmemHeap");

	psDevmemHeap->psDevmemCtx = psDevmemCtx;

	DevmemIntCtxAcquire(psDevmemHeap->psDevmemCtx);

	OSAtomicWrite(&psDevmemHeap->hRefCount, 1);

	psDevmemHeap->uiLog2PageSize = uiLog2DataPageSize;

	*ppsDevmemHeapPtr = psDevmemHeap;

	return PVRSRV_OK;
}

PVRSRV_ERROR DevmemIntAllocDefBackingPage(PVRSRV_DEVICE_NODE *psDevNode,
                                            PVRSRV_DEF_PAGE *psDefPage,
                                            IMG_INT	uiInitValue,
                                            IMG_CHAR *pcDefPageName,
                                            IMG_BOOL bInitPage)
{
	IMG_UINT32 ui32RefCnt;
	PVRSRV_ERROR eError = PVRSRV_OK;

	OSLockAcquire(psDefPage->psPgLock);

	/* We know there will not be 4G number of sparse PMR's */
	ui32RefCnt = OSAtomicIncrement(&psDefPage->atRefCounter);

	if (1 == ui32RefCnt)
	{
		IMG_DEV_PHYADDR	sDevPhysAddr = {0};

#if defined(PDUMP)
		PDUMPCOMMENT("Alloc %s page object", pcDefPageName);
#endif

		/* Allocate the dummy page required for sparse backing */
		eError = DevPhysMemAlloc(psDevNode,
		                         (1 << psDefPage->ui32Log2PgSize),
		                         0,
		                         uiInitValue,
		                         bInitPage,
#if defined(PDUMP)
		                         psDevNode->psMMUDevAttrs->pszMMUPxPDumpMemSpaceName,
		                         pcDefPageName,
		                         &psDefPage->hPdumpPg,
#endif
		                         &psDefPage->sPageHandle,
		                         &sDevPhysAddr);
		if (PVRSRV_OK != eError)
		{
			OSAtomicDecrement(&psDefPage->atRefCounter);
		}
		else
		{
			psDefPage->ui64PgPhysAddr = sDevPhysAddr.uiAddr;
		}
	}

	OSLockRelease(psDefPage->psPgLock);

	return eError;
}

void DevmemIntFreeDefBackingPage(PVRSRV_DEVICE_NODE *psDevNode,
                                   PVRSRV_DEF_PAGE *psDefPage,
                                   IMG_CHAR *pcDefPageName)
{
	IMG_UINT32 ui32RefCnt;

	ui32RefCnt = OSAtomicRead(&psDefPage->atRefCounter);

	/* For the cases where the dummy page allocation fails due to lack of memory
	 * The refcount can still be 0 even for a sparse allocation */
	if (0 != ui32RefCnt)
	{
		OSLockAcquire(psDefPage->psPgLock);

		/* We know there will not be 4G number of sparse PMR's */
		ui32RefCnt = OSAtomicDecrement(&psDefPage->atRefCounter);

		if (0 == ui32RefCnt)
		{
			PDUMPCOMMENT("Free %s page object", pcDefPageName);

			/* Free the dummy page when refcount reaches zero */
			DevPhysMemFree(psDevNode,
#if defined(PDUMP)
			               psDefPage->hPdumpPg,
#endif
			               &psDefPage->sPageHandle);

#if defined(PDUMP)
			psDefPage->hPdumpPg = NULL;
#endif
			psDefPage->ui64PgPhysAddr = MMU_BAD_PHYS_ADDR;
		}

		OSLockRelease(psDefPage->psPgLock);
	}

}

PVRSRV_ERROR
DevmemIntMapPages(DEVMEMINT_RESERVATION *psReservation,
                  PMR *psPMR,
                  IMG_UINT32 ui32PageCount,
                  IMG_UINT32 ui32PhysicalPgOffset,
                  PVRSRV_MEMALLOCFLAGS_T uiFlags,
                  IMG_DEV_VIRTADDR sDevVAddrBase)
{
	PVRSRV_ERROR eError;

	if (psReservation->psDevmemHeap->uiLog2PageSize > PMR_GetLog2Contiguity(psPMR))
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Device heap and PMR have incompatible Log2Contiguity (%u - %u). "
		         "PMR contiguity must be a multiple of the heap contiguity!",
		         __func__,
		         psReservation->psDevmemHeap->uiLog2PageSize,
		         PMR_GetLog2Contiguity(psPMR)));
		PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_INVALID_PARAMS, e0);
	}

	eError = MMU_MapPages(psReservation->psDevmemHeap->psDevmemCtx->psMMUContext,
	                      uiFlags,
	                      sDevVAddrBase,
	                      psPMR,
	                      ui32PhysicalPgOffset,
	                      ui32PageCount,
	                      NULL,
	                      psReservation->psDevmemHeap->uiLog2PageSize);

e0:
	return eError;
}

PVRSRV_ERROR
DevmemIntUnmapPages(DEVMEMINT_RESERVATION *psReservation,
                    IMG_DEV_VIRTADDR sDevVAddrBase,
                    IMG_UINT32 ui32PageCount)
{
	/* Unmap the pages and mark them invalid in the MMU PTE */
	MMU_UnmapPages(psReservation->psDevmemHeap->psDevmemCtx->psMMUContext,
	               0,
	               sDevVAddrBase,
	               ui32PageCount,
	               NULL,
	               psReservation->psDevmemHeap->uiLog2PageSize,
	               0);

	return PVRSRV_OK;
}

PVRSRV_ERROR
DevmemIntMapPMR(DEVMEMINT_HEAP *psDevmemHeap,
                DEVMEMINT_RESERVATION *psReservation,
                PMR *psPMR,
                PVRSRV_MEMALLOCFLAGS_T uiMapFlags,
                DEVMEMINT_MAPPING **ppsMappingPtr)
{
	PVRSRV_ERROR eError;
	DEVMEMINT_MAPPING *psMapping;
	/* number of pages (device pages) that allocation spans */
	IMG_UINT32 ui32NumDevPages;
	/* device virtual address of start of allocation */
	IMG_DEV_VIRTADDR sAllocationDevVAddr;
	/* and its length */
	IMG_DEVMEM_SIZE_T uiAllocationSize;
	IMG_UINT32 uiLog2HeapContiguity = psDevmemHeap->uiLog2PageSize;
	IMG_BOOL bIsSparse = IMG_FALSE, bNeedBacking = IMG_FALSE;
	PVRSRV_DEVICE_NODE *psDevNode;
	PMR_FLAGS_T uiPMRFlags;
	PVRSRV_DEF_PAGE *psDefPage;
	IMG_CHAR *pszPageName;

	if (uiLog2HeapContiguity > PMR_GetLog2Contiguity(psPMR))
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Device heap and PMR have incompatible contiguity (%u - %u). "
		         "Heap contiguity must be a multiple of the heap contiguity!",
		         __func__,
		         uiLog2HeapContiguity,
		         PMR_GetLog2Contiguity(psPMR) ));
		PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_INVALID_PARAMS, e0);
	}
	psDevNode = psDevmemHeap->psDevmemCtx->psDevNode;

	/* allocate memory to record the mapping info */
	psMapping = OSAllocMem(sizeof(*psMapping));
	PVR_LOG_GOTO_IF_NOMEM(psMapping, eError, e0);

	uiAllocationSize = psReservation->uiLength;

	ui32NumDevPages = 0xffffffffU & ( ( (uiAllocationSize - 1) >> uiLog2HeapContiguity) + 1);
	PVR_ASSERT((IMG_DEVMEM_SIZE_T) ui32NumDevPages << uiLog2HeapContiguity == uiAllocationSize);

	eError = PMRLockSysPhysAddresses(psPMR);
	PVR_GOTO_IF_ERROR(eError, e2);

	sAllocationDevVAddr = psReservation->sBase;

	/*Check if the PMR that needs to be mapped is sparse */
	bIsSparse = PMR_IsSparse(psPMR);
	if (bIsSparse)
	{
		/*Get the flags*/
		uiPMRFlags = PMR_Flags(psPMR);
		bNeedBacking = PVRSRV_IS_SPARSE_DUMMY_BACKING_REQUIRED(uiPMRFlags);

		if (bNeedBacking)
		{
			IMG_INT uiInitValue;

			if (PVRSRV_IS_SPARSE_ZERO_BACKING_REQUIRED(uiPMRFlags))
			{
				psDefPage = &psDevmemHeap->psDevmemCtx->psDevNode->sDevZeroPage;
				uiInitValue = PVR_ZERO_PAGE_INIT_VALUE;
				pszPageName = DEV_ZERO_PAGE;
			}
			else
			{
				psDefPage = &psDevmemHeap->psDevmemCtx->psDevNode->sDummyPage;
				uiInitValue = PVR_DUMMY_PAGE_INIT_VALUE;
				pszPageName = DUMMY_PAGE;
			}

			/* Error is logged with in the function if any failures.
			 * As the allocation fails we need to fail the map request and
			 * return appropriate error
			 *
			 * Allocation of dummy/zero page is done after locking the pages for PMR physically
			 * By implementing this way, the best case path of dummy/zero page being most likely to be
			 * allocated after physically locking down pages, is considered.
			 * If the dummy/zero page allocation fails, we do unlock the physical address and the impact
			 * is a bit more in on demand mode of operation */
			eError = DevmemIntAllocDefBackingPage(psDevNode,
			                                      psDefPage,
			                                      uiInitValue,
			                                      pszPageName,
			                                      IMG_TRUE);
			PVR_GOTO_IF_ERROR(eError, e3);
		}

		/* N.B. We pass mapping permission flags to MMU_MapPages and let
		 * it reject the mapping if the permissions on the PMR are not compatible. */
		eError = MMU_MapPages(psDevmemHeap->psDevmemCtx->psMMUContext,
		                      uiMapFlags,
		                      sAllocationDevVAddr,
		                      psPMR,
		                      0,
		                      ui32NumDevPages,
		                      NULL,
		                      uiLog2HeapContiguity);
		PVR_GOTO_IF_ERROR(eError, e4);
	}
	else
	{
		eError = MMU_MapPMRFast(psDevmemHeap->psDevmemCtx->psMMUContext,
		                        sAllocationDevVAddr,
		                        psPMR,
		                        (IMG_DEVMEM_SIZE_T) ui32NumDevPages << uiLog2HeapContiguity,
		                        uiMapFlags,
		                        uiLog2HeapContiguity);
		PVR_GOTO_IF_ERROR(eError, e3);
	}

	psMapping->psReservation = psReservation;
	psMapping->uiNumPages = ui32NumDevPages;
	psMapping->psPMR = psPMR;

	/* Don't bother with refcount on reservation, as a reservation
	   only ever holds one mapping, so we directly increment the
	   refcount on the heap instead */
	DevmemIntHeapAcquire(psMapping->psReservation->psDevmemHeap);

	*ppsMappingPtr = psMapping;

	return PVRSRV_OK;
e4:
	if (bNeedBacking)
	{
		/*if the mapping failed, the allocated dummy ref count need
		 * to be handled accordingly */
		DevmemIntFreeDefBackingPage(psDevmemHeap->psDevmemCtx->psDevNode,
		                            psDefPage,
		                            pszPageName);
	}
e3:
	{
		PVRSRV_ERROR eError1=PVRSRV_OK;
		eError1 = PMRUnlockSysPhysAddresses(psPMR);
		PVR_LOG_IF_ERROR(eError1, "PMRUnlockSysPhysAddresses");

		*ppsMappingPtr = NULL;
	}
e2:
	OSFreeMem(psMapping);

e0:
	PVR_ASSERT (eError != PVRSRV_OK);
	return eError;
}


PVRSRV_ERROR
DevmemIntUnmapPMR(DEVMEMINT_MAPPING *psMapping)
{
	PVRSRV_ERROR eError;
	DEVMEMINT_HEAP *psDevmemHeap = psMapping->psReservation->psDevmemHeap;
	/* device virtual address of start of allocation */
	IMG_DEV_VIRTADDR sAllocationDevVAddr;
	/* number of pages (device pages) that allocation spans */
	IMG_UINT32 ui32NumDevPages;
	IMG_BOOL bIsSparse = IMG_FALSE, bNeedBacking = IMG_FALSE;
	PMR_FLAGS_T uiPMRFlags;

	ui32NumDevPages = psMapping->uiNumPages;
	sAllocationDevVAddr = psMapping->psReservation->sBase;

	/*Check if the PMR that needs to be mapped is sparse */
	bIsSparse = PMR_IsSparse(psMapping->psPMR);

	if (bIsSparse)
	{
		/*Get the flags*/
		uiPMRFlags = PMR_Flags(psMapping->psPMR);
		bNeedBacking = PVRSRV_IS_SPARSE_DUMMY_BACKING_REQUIRED(uiPMRFlags);

		if (bNeedBacking)
		{
			if (PVRSRV_IS_SPARSE_ZERO_BACKING_REQUIRED(uiPMRFlags))
			{
				DevmemIntFreeDefBackingPage(psDevmemHeap->psDevmemCtx->psDevNode,
											&psDevmemHeap->psDevmemCtx->psDevNode->sDevZeroPage,
											DEV_ZERO_PAGE);
			}
			else
			{
				DevmemIntFreeDefBackingPage(psDevmemHeap->psDevmemCtx->psDevNode,
											&psDevmemHeap->psDevmemCtx->psDevNode->sDummyPage,
											DUMMY_PAGE);
			}
		}

		MMU_UnmapPages (psDevmemHeap->psDevmemCtx->psMMUContext,
				0,
				sAllocationDevVAddr,
				ui32NumDevPages,
				NULL,
				psMapping->psReservation->psDevmemHeap->uiLog2PageSize,
				0);
	}
	else
	{
		MMU_UnmapPMRFast(psDevmemHeap->psDevmemCtx->psMMUContext,
		                 sAllocationDevVAddr,
		                 ui32NumDevPages,
		                 psMapping->psReservation->psDevmemHeap->uiLog2PageSize);
	}



	eError = PMRUnlockSysPhysAddresses(psMapping->psPMR);
	PVR_ASSERT(eError == PVRSRV_OK);

	/* Don't bother with refcount on reservation, as a reservation
	   only ever holds one mapping, so we directly decrement the
	   refcount on the heap instead */
	DevmemIntHeapRelease(psDevmemHeap);

	OSFreeMem(psMapping);

	return PVRSRV_OK;
}


PVRSRV_ERROR
DevmemIntReserveRange(DEVMEMINT_HEAP *psDevmemHeap,
                      IMG_DEV_VIRTADDR sAllocationDevVAddr,
                      IMG_DEVMEM_SIZE_T uiAllocationSize,
                      DEVMEMINT_RESERVATION **ppsReservationPtr)
{
	PVRSRV_ERROR eError;
	DEVMEMINT_RESERVATION *psReservation;

	/* allocate memory to record the reservation info */
	psReservation = OSAllocMem(sizeof(*psReservation));
	PVR_LOG_GOTO_IF_NOMEM(psReservation, eError, e0);

	psReservation->sBase = sAllocationDevVAddr;
	psReservation->uiLength = uiAllocationSize;

	eError = MMU_Alloc(psDevmemHeap->psDevmemCtx->psMMUContext,
	                   uiAllocationSize,
	                   &uiAllocationSize,
	                   0, /* IMG_UINT32 uiProtFlags */
	                   0, /* alignment is n/a since we supply devvaddr */
	                   &sAllocationDevVAddr,
	                   psDevmemHeap->uiLog2PageSize);
	PVR_GOTO_IF_ERROR(eError, e1);

	/* since we supplied the virt addr, MMU_Alloc shouldn't have
	   chosen a new one for us */
	PVR_ASSERT(sAllocationDevVAddr.uiAddr == psReservation->sBase.uiAddr);

	DevmemIntHeapAcquire(psDevmemHeap);

	psReservation->psDevmemHeap = psDevmemHeap;
	*ppsReservationPtr = psReservation;

	return PVRSRV_OK;

	/*
	 *  error exit paths follow
	 */

e1:
	OSFreeMem(psReservation);

e0:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

PVRSRV_ERROR
DevmemIntUnreserveRange(DEVMEMINT_RESERVATION *psReservation)
{
	IMG_DEV_VIRTADDR sBase        = psReservation->sBase;
	IMG_UINT32 uiLength           = psReservation->uiLength;
	IMG_UINT32 uiLog2DataPageSize = psReservation->psDevmemHeap->uiLog2PageSize;

	MMU_Free(psReservation->psDevmemHeap->psDevmemCtx->psMMUContext,
	         sBase,
	         uiLength,
	         uiLog2DataPageSize);

	DevmemIntHeapRelease(psReservation->psDevmemHeap);
	OSFreeMem(psReservation);

    return PVRSRV_OK;
}


PVRSRV_ERROR
DevmemIntHeapDestroy(DEVMEMINT_HEAP *psDevmemHeap)
{
	if (OSAtomicRead(&psDevmemHeap->hRefCount) != 1)
	{
		PVR_DPF((PVR_DBG_ERROR, "BUG!  %s called but has too many references (%d) "
		         "which probably means allocations have been made from the heap and not freed",
		         __func__,
		         OSAtomicRead(&psDevmemHeap->hRefCount)));

		/*
		 * Try again later when you've freed all the memory
		 *
		 * Note:
		 * We don't expect the application to retry (after all this call would
		 * succeed if the client had freed all the memory which it should have
		 * done before calling this function). However, given there should be
		 * an associated handle, when the handle base is destroyed it will free
		 * any allocations leaked by the client and then it will retry this call,
		 * which should then succeed.
		 */
		return PVRSRV_ERROR_RETRY;
	}

	PVR_ASSERT(OSAtomicRead(&psDevmemHeap->hRefCount) == 1);

	DevmemIntCtxRelease(psDevmemHeap->psDevmemCtx);

	PVR_DPF((PVR_DBG_MESSAGE, "%s: Freed heap %p", __func__, psDevmemHeap));
	OSFreeMem(psDevmemHeap);

	return PVRSRV_OK;
}

PVRSRV_ERROR
DevmemIntChangeSparse(DEVMEMINT_HEAP *psDevmemHeap,
                      PMR *psPMR,
                      IMG_UINT32 ui32AllocPageCount,
                      IMG_UINT32 *pai32AllocIndices,
                      IMG_UINT32 ui32FreePageCount,
                      IMG_UINT32 *pai32FreeIndices,
                      SPARSE_MEM_RESIZE_FLAGS uiSparseFlags,
                      PVRSRV_MEMALLOCFLAGS_T uiFlags,
                      IMG_DEV_VIRTADDR sDevVAddrBase,
                      IMG_UINT64 sCpuVAddrBase)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	IMG_UINT32 uiLog2PMRContiguity = PMR_GetLog2Contiguity(psPMR);
	IMG_UINT32 uiLog2HeapContiguity = psDevmemHeap->uiLog2PageSize;
	IMG_UINT32 uiOrderDiff = uiLog2PMRContiguity - uiLog2HeapContiguity;
	IMG_UINT32 uiPagesPerOrder = 1 << uiOrderDiff;

	IMG_UINT32 *pai32MapIndices = pai32AllocIndices;
	IMG_UINT32 *pai32UnmapIndices = pai32FreeIndices;
	IMG_UINT32 uiMapPageCount = ui32AllocPageCount;
	IMG_UINT32 uiUnmapPageCount = ui32FreePageCount;

	/* Special case:
	 * Adjust indices if we map into a heap that uses smaller page sizes
	 * than the physical allocation itself.
	 * The incoming parameters are all based on the page size of the PMR
	 * but the mapping functions expects parameters to be in terms of heap page sizes. */
	if (uiOrderDiff != 0)
	{
		IMG_UINT32 uiPgIdx, uiPgOffset;

		uiMapPageCount = (uiMapPageCount << uiOrderDiff);
		uiUnmapPageCount = (uiUnmapPageCount << uiOrderDiff);

		pai32MapIndices = OSAllocMem(uiMapPageCount * sizeof(*pai32MapIndices));
		PVR_GOTO_IF_NOMEM(pai32MapIndices, eError, e0);

		pai32UnmapIndices = OSAllocMem(uiUnmapPageCount * sizeof(*pai32UnmapIndices));
		if (!pai32UnmapIndices)
		{
			OSFreeMem(pai32MapIndices);
			PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_OUT_OF_MEMORY, e0);
		}

		/* Every chunk index needs to be translated from physical indices
		 * into heap based indices. */
		for (uiPgIdx = 0; uiPgIdx < ui32AllocPageCount; uiPgIdx++)
		{
			for (uiPgOffset = 0; uiPgOffset < uiPagesPerOrder; uiPgOffset++)
			{
				pai32MapIndices[uiPgIdx*uiPagesPerOrder + uiPgOffset] =
						pai32AllocIndices[uiPgIdx]*uiPagesPerOrder + uiPgOffset;
			}
		}

		for (uiPgIdx = 0; uiPgIdx < ui32FreePageCount; uiPgIdx++)
		{
			for (uiPgOffset = 0; uiPgOffset < uiPagesPerOrder; uiPgOffset++)
			{
				pai32UnmapIndices[uiPgIdx*uiPagesPerOrder + uiPgOffset] =
						pai32FreeIndices[uiPgIdx]*uiPagesPerOrder + uiPgOffset;
			}
		}
	}

	/*
	 * The order of steps in which this request is done is given below. The order of
	 * operations is very important in this case:
	 *
	 * 1. The parameters are validated in function PMR_ChangeSparseMem below.
	 *    A successful response indicates all the parameters are correct.
	 *    In failure case we bail out from here without processing further.
	 * 2. On success, get the PMR specific operations done. this includes page alloc, page free
	 *    and the corresponding PMR status changes.
	 *    when this call fails, it is ensured that the state of the PMR before is
	 *    not disturbed. If it succeeds, then we can go ahead with the subsequent steps.
	 * 3. Invalidate the GPU page table entries for the pages to be freed.
	 * 4. Write the GPU page table entries for the pages that got allocated.
	 * 5. Change the corresponding CPU space map.
	 *
	 * The above steps can be selectively controlled using flags.
	 */
	if (uiSparseFlags & (SPARSE_REMAP_MEM | SPARSE_RESIZE_BOTH))
	{
		/* Do the PMR specific changes first */
		eError = PMR_ChangeSparseMem(psPMR,
		                             ui32AllocPageCount,
		                             pai32AllocIndices,
		                             ui32FreePageCount,
		                             pai32FreeIndices,
		                             uiSparseFlags);
		if (PVRSRV_OK != eError)
		{
			PVR_DPF((PVR_DBG_MESSAGE,
					"%s: Failed to do PMR specific changes.",
					__func__));
			goto e1;
		}

		/* Invalidate the page table entries for the free pages.
		 * Optimisation later would be not to touch the ones that gets re-mapped */
		if ((0 != ui32FreePageCount) && (uiSparseFlags & SPARSE_RESIZE_FREE))
		{
			PMR_FLAGS_T uiPMRFlags;

			/*Get the flags*/
			uiPMRFlags = PMR_Flags(psPMR);

			if (SPARSE_REMAP_MEM != (uiSparseFlags & SPARSE_REMAP_MEM))
			{
				/* Unmap the pages and mark them invalid in the MMU PTE */
				MMU_UnmapPages (psDevmemHeap->psDevmemCtx->psMMUContext,
				                uiFlags,
				                sDevVAddrBase,
				                uiUnmapPageCount,
				                pai32UnmapIndices,
				                uiLog2HeapContiguity,
				                uiPMRFlags);
			}
		}

		/* Wire the pages tables that got allocated */
		if ((0 != ui32AllocPageCount) && (uiSparseFlags & SPARSE_RESIZE_ALLOC))
		{
			/* Map the pages and mark them Valid in the MMU PTE */
			eError = MMU_MapPages (psDevmemHeap->psDevmemCtx->psMMUContext,
			                       uiFlags,
			                       sDevVAddrBase,
			                       psPMR,
			                       0,
			                       uiMapPageCount,
			                       pai32MapIndices,
			                       uiLog2HeapContiguity);
			if (PVRSRV_OK != eError)
			{
				PVR_DPF((PVR_DBG_MESSAGE,
						"%s: Failed to map alloc indices.",
						__func__));
				goto e1;
			}
		}

		/* Currently only used for debug */
		if (SPARSE_REMAP_MEM == (uiSparseFlags & SPARSE_REMAP_MEM))
		{
			eError = MMU_MapPages(psDevmemHeap->psDevmemCtx->psMMUContext,
			                      uiFlags,
			                      sDevVAddrBase,
			                      psPMR,
			                      0,
			                      uiMapPageCount,
			                      pai32UnmapIndices,
			                      uiLog2HeapContiguity);
			if (PVRSRV_OK != eError)
			{
				PVR_DPF((PVR_DBG_MESSAGE,
						"%s: Failed to map Free indices.",
						__func__));
				goto e1;
			}
		}
	}

#ifndef PVRSRV_UNMAP_ON_SPARSE_CHANGE
	/* Do the changes in sparse on to the CPU virtual map accordingly */
	if (uiSparseFlags & SPARSE_MAP_CPU_ADDR)
	{
		if (sCpuVAddrBase != 0)
		{
			eError = PMR_ChangeSparseMemCPUMap(psPMR,
			                                   sCpuVAddrBase,
			                                   ui32AllocPageCount,
			                                   pai32AllocIndices,
			                                   ui32FreePageCount,
			                                   pai32FreeIndices);
			if (PVRSRV_OK != eError)
			{
				PVR_DPF((PVR_DBG_MESSAGE,
						"%s: Failed to map to CPU addr space.",
						__func__));
				goto e0;
			}
		}
	}
#endif

e1:
	if (pai32MapIndices != pai32AllocIndices)
	{
		OSFreeMem(pai32MapIndices);
	}
	if (pai32UnmapIndices != pai32FreeIndices)
	{
		OSFreeMem(pai32UnmapIndices);
	}
e0:
	return eError;
}

/*************************************************************************/ /*!
@Function       DevmemIntCtxDestroy
@Description    Destroy that created by DevmemIntCtxCreate
@Input          psDevmemCtx   Device Memory context
@Return         cannot fail.
*/ /**************************************************************************/
PVRSRV_ERROR
DevmemIntCtxDestroy(DEVMEMINT_CTX *psDevmemCtx)
{
	/*
	   We can't determine if we should be freeing the context here
	   as a refcount!=1 could be due to either the fact that heap(s)
	   remain with allocations on them, or that this memory context
	   has been exported.
	   As the client couldn't do anything useful with this information
	   anyway and the fact that the refcount will ensure we only
	   free the context when _all_ references have been released
	   don't bother checking and just return OK regardless.
	   */
	DevmemIntCtxRelease(psDevmemCtx);
	return PVRSRV_OK;
}

PVRSRV_ERROR DevmemIntIsVDevAddrValid(CONNECTION_DATA * psConnection,
                                      PVRSRV_DEVICE_NODE *psDevNode,
                                      DEVMEMINT_CTX *psDevMemContext,
                                      IMG_DEV_VIRTADDR sDevAddr)
{
	IMG_UINT32 i, j, uiLog2HeapPageSize = 0;
	DEVICE_MEMORY_INFO *psDinfo = &psDevNode->sDevMemoryInfo;
	DEVMEM_HEAP_CONFIG *psConfig = psDinfo->psDeviceMemoryHeapConfigArray;

	IMG_BOOL bFound = IMG_FALSE;

	for (i = 0;
		 i < psDinfo->uiNumHeapConfigs && !bFound;
		 i++)
	{
		for (j = 0;
			 j < psConfig[i].uiNumHeaps  && !bFound;
			 j++)
		{
			IMG_DEV_VIRTADDR uiBase =
					psConfig[i].psHeapBlueprintArray[j].sHeapBaseAddr;
			IMG_DEVMEM_SIZE_T uiSize =
					psConfig[i].psHeapBlueprintArray[j].uiHeapLength;

			if ((sDevAddr.uiAddr >= uiBase.uiAddr) &&
				(sDevAddr.uiAddr < (uiBase.uiAddr + uiSize)))
			{
				uiLog2HeapPageSize =
						psConfig[i].psHeapBlueprintArray[j].uiLog2DataPageSize;
				bFound = IMG_TRUE;
			}
		}
	}

	if (uiLog2HeapPageSize == 0)
	{
		return PVRSRV_ERROR_INVALID_GPU_ADDR;
	}

	return MMU_IsVDevAddrValid(psDevMemContext->psMMUContext,
	                           uiLog2HeapPageSize,
	                           sDevAddr) ? PVRSRV_OK : PVRSRV_ERROR_INVALID_GPU_ADDR;
}

PVRSRV_ERROR
DevmemIntFlushDevSLCRange(DEVMEMINT_CTX *psDevMemContext,
                          IMG_DEV_VIRTADDR sDevVAddr,
                          IMG_DEVMEM_SIZE_T uiSize,
                          IMG_BOOL bInvalidate)
{
	PVRSRV_DEVICE_NODE *psDevNode = psDevMemContext->psDevNode;
	MMU_CONTEXT *psMMUContext = psDevMemContext->psMMUContext;

	if (psDevNode->pfnDevSLCFlushRange)
	{
		return psDevNode->pfnDevSLCFlushRange(psDevNode,
		                                      psMMUContext,
		                                      sDevVAddr,
		                                      uiSize,
		                                      bInvalidate);
	}

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

PVRSRV_ERROR
DevmemIntInvalidateFBSCTable(DEVMEMINT_CTX *psDevMemContext,
                             IMG_UINT64 ui64FBSCEntryMask)
{
	PVRSRV_DEVICE_NODE *psDevNode = psDevMemContext->psDevNode;
	MMU_CONTEXT *psMMUContext = psDevMemContext->psMMUContext;

	if (psDevNode->pfnInvalFBSCTable)
	{
		return psDevNode->pfnInvalFBSCTable(psDevNode,
		                                    psMMUContext,
		                                    ui64FBSCEntryMask);
	}

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

PVRSRV_ERROR DevmemIntGetFaultAddress(CONNECTION_DATA *psConnection,
                                      PVRSRV_DEVICE_NODE *psDevNode,
                                      DEVMEMINT_CTX *psDevMemContext,
                                      IMG_DEV_VIRTADDR *psFaultAddress)
{
	if ((psDevMemContext->ui32Flags & DEVMEMCTX_FLAGS_FAULT_ADDRESS_AVAILABLE) == 0)
	{
		return PVRSRV_ERROR_RESOURCE_UNAVAILABLE;
	}

	*psFaultAddress = psDevMemContext->sFaultAddress;
	psDevMemContext->ui32Flags &= ~DEVMEMCTX_FLAGS_FAULT_ADDRESS_AVAILABLE;

	return PVRSRV_OK;
}

static POSWR_LOCK g_hExportCtxListLock;
static DLLIST_NODE g_sExportCtxList;

PVRSRV_ERROR
DevmemIntInit(void)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	dllist_init(&g_sExportCtxList);

	eError = OSWRLockCreate(&g_hExportCtxListLock);

	return eError;
}

PVRSRV_ERROR
DevmemIntDeInit(void)
{
	PVR_ASSERT(dllist_is_empty(&g_sExportCtxList));

	OSWRLockDestroy(g_hExportCtxListLock);

	return PVRSRV_OK;
}

PVRSRV_ERROR
DevmemIntExportCtx(DEVMEMINT_CTX *psContext,
                   PMR *psPMR,
                   DEVMEMINT_CTX_EXPORT **ppsContextExport)
{
	DEVMEMINT_CTX_EXPORT *psCtxExport;

	psCtxExport = OSAllocMem(sizeof(DEVMEMINT_CTX_EXPORT));
	PVR_LOG_RETURN_IF_NOMEM(psCtxExport, "psCtxExport");

	DevmemIntCtxAcquire(psContext);
	PMRRefPMR(psPMR);
	/* Now that the source PMR is exported, the layout
	 * can't change as there could be outstanding importers
	 * This is to make sure both exporter and importers view of
	 * the memory is same */
	PMR_SetLayoutFixed(psPMR, IMG_TRUE);
	psCtxExport->psDevmemCtx = psContext;
	psCtxExport->psPMR = psPMR;
	OSWRLockAcquireWrite(g_hExportCtxListLock);
	dllist_add_to_tail(&g_sExportCtxList, &psCtxExport->sNode);
	OSWRLockReleaseWrite(g_hExportCtxListLock);

	*ppsContextExport = psCtxExport;

	return PVRSRV_OK;
}

PVRSRV_ERROR
DevmemIntUnexportCtx(DEVMEMINT_CTX_EXPORT *psContextExport)
{
	PMRUnrefPMR(psContextExport->psPMR);
	DevmemIntCtxRelease(psContextExport->psDevmemCtx);
	OSWRLockAcquireWrite(g_hExportCtxListLock);
	dllist_remove_node(&psContextExport->sNode);
	OSWRLockReleaseWrite(g_hExportCtxListLock);
	OSFreeMem(psContextExport);

	/* Unable to find exported context, return error */
	return PVRSRV_OK;
}

PVRSRV_ERROR
DevmemIntAcquireRemoteCtx(PMR *psPMR,
                          DEVMEMINT_CTX **ppsContext,
                          IMG_HANDLE *phPrivData)
{
	PDLLIST_NODE psListNode, psListNodeNext;
	DEVMEMINT_CTX_EXPORT *psCtxExport;

	OSWRLockAcquireRead(g_hExportCtxListLock);
	/* Find context from list using PMR as key */
	dllist_foreach_node(&g_sExportCtxList, psListNode, psListNodeNext)
	{
		psCtxExport = IMG_CONTAINER_OF(psListNode, DEVMEMINT_CTX_EXPORT, sNode);
		if (psCtxExport->psPMR == psPMR)
		{
			OSWRLockReleaseRead(g_hExportCtxListLock);
			DevmemIntCtxAcquire(psCtxExport->psDevmemCtx);
			*ppsContext = psCtxExport->psDevmemCtx;
			*phPrivData = psCtxExport->psDevmemCtx->hPrivData;

			/* PMR should have been already exported to import it
			 * If a PMR is exported, its immutable and the same is
			 * checked here */
			PVR_ASSERT(IMG_TRUE == PMR_IsMemLayoutFixed(psPMR));

			return PVRSRV_OK;
		}
	}
	OSWRLockReleaseRead(g_hExportCtxListLock);

	/* Unable to find exported context, return error */
	PVR_DPF((PVR_DBG_ERROR,
			"%s: Failed to acquire remote context. Could not retrieve context with given PMR",
			__func__));
	return PVRSRV_ERROR_INVALID_PARAMS;
}

/*************************************************************************/ /*!
@Function       DevmemIntRegisterPFNotify
@Description    Registers a PID to be notified when a page fault occurs on a
                specific device memory context.
@Input          psDevmemCtx    The context to be notified about.
@Input          ui32PID        The PID of the process that would like to be
                               notified.
@Input          bRegister      If true, register. If false, de-register.
@Return         PVRSRV_ERROR.
*/ /**************************************************************************/
PVRSRV_ERROR DevmemIntRegisterPFNotifyKM(DEVMEMINT_CTX *psDevmemCtx,
                                         IMG_INT32     ui32PID,
                                         IMG_BOOL      bRegister)
{
	PVRSRV_DEVICE_NODE *psDevNode;
	DLLIST_NODE         *psNode, *psNodeNext;
	DEVMEMINT_PF_NOTIFY *psNotifyNode;
	IMG_BOOL            bPresent = IMG_FALSE;

	PVR_LOG_RETURN_IF_INVALID_PARAM(psDevmemCtx, "psDevmemCtx");

	psDevNode = psDevmemCtx->psDevNode;

	if (bRegister)
	{
		OSWRLockAcquireRead(psDevmemCtx->hListLock);
		/* If this is the first PID in the list, the device memory context
		 * needs to be registered for notification */
		if (dllist_is_empty(&psDevmemCtx->sProcessNotifyListHead))
		{
			OSWRLockReleaseRead(psDevmemCtx->hListLock);
			dllist_add_to_tail(&psDevNode->sMemoryContextPageFaultNotifyListHead,
			                   &psDevmemCtx->sPageFaultNotifyListElem);
		}
		else
		{
			OSWRLockReleaseRead(psDevmemCtx->hListLock);
		}
	}

	/* Loop through the registered PIDs and check whether this one is
	 * present */
	OSWRLockAcquireRead(psDevmemCtx->hListLock);
	dllist_foreach_node(&(psDevmemCtx->sProcessNotifyListHead), psNode, psNodeNext)
	{
		psNotifyNode = IMG_CONTAINER_OF(psNode, DEVMEMINT_PF_NOTIFY, sProcessNotifyListElem);

		if (psNotifyNode->ui32PID == ui32PID)
		{
			bPresent = IMG_TRUE;
			break;
		}
	}
	OSWRLockReleaseRead(psDevmemCtx->hListLock);

	if (bRegister)
	{
		if (bPresent)
		{
			PVR_DPF((PVR_DBG_ERROR,
			         "%s: Trying to register a PID that is already registered",
			         __func__));
			return PVRSRV_ERROR_PID_ALREADY_REGISTERED;
		}

		psNotifyNode = OSAllocMem(sizeof(*psNotifyNode));
		if (psNotifyNode == NULL)
		{
			PVR_DPF((PVR_DBG_ERROR,
			         "%s: Unable to allocate memory for the notify list",
			          __func__));
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}
		psNotifyNode->ui32PID = ui32PID;
		OSWRLockAcquireWrite(psDevmemCtx->hListLock);
		dllist_add_to_tail(&(psDevmemCtx->sProcessNotifyListHead), &(psNotifyNode->sProcessNotifyListElem));
		OSWRLockReleaseWrite(psDevmemCtx->hListLock);
	}
	else
	{
		if (!bPresent)
		{
			PVR_DPF((PVR_DBG_ERROR,
			         "%s: Trying to unregister a PID that is not registered",
			         __func__));
			return PVRSRV_ERROR_PID_NOT_REGISTERED;
		}
		dllist_remove_node(psNode);
		psNotifyNode = IMG_CONTAINER_OF(psNode, DEVMEMINT_PF_NOTIFY, sProcessNotifyListElem);
		OSFreeMem(psNotifyNode);
	}

	if (!bRegister)
	{
		/* If the last process in the list is being unregistered, then also
		 * unregister the device memory context from the notify list. */
		OSWRLockAcquireWrite(psDevmemCtx->hListLock);
		if (dllist_is_empty(&psDevmemCtx->sProcessNotifyListHead))
		{
			dllist_remove_node(&psDevmemCtx->sPageFaultNotifyListElem);
		}
		OSWRLockReleaseWrite(psDevmemCtx->hListLock);
	}

	return PVRSRV_OK;
}

/*************************************************************************/ /*!
@Function       DevmemIntPFNotify
@Description    Notifies any processes that have registered themselves to be
                notified when a page fault happens on a specific device memory
                context.
@Input          *psDevNode           The device node.
@Input          ui64FaultedPCAddress The page catalogue address that faulted.
@Input          sFaultAddress        The address that triggered the fault.
@Return         PVRSRV_ERROR
*/ /**************************************************************************/
PVRSRV_ERROR DevmemIntPFNotify(PVRSRV_DEVICE_NODE *psDevNode,
                               IMG_UINT64         ui64FaultedPCAddress,
                               IMG_DEV_VIRTADDR   sFaultAddress)
{
	DLLIST_NODE         *psNode, *psNodeNext;
	DEVMEMINT_PF_NOTIFY *psNotifyNode;
	PVRSRV_ERROR        eError;
	DEVMEMINT_CTX       *psDevmemCtx = NULL;
	IMG_BOOL            bFailed = IMG_FALSE;

	OSWRLockAcquireRead(psDevNode->hMemoryContextPageFaultNotifyListLock);
	if (dllist_is_empty(&(psDevNode->sMemoryContextPageFaultNotifyListHead)))
	{
		OSWRLockReleaseRead(psDevNode->hMemoryContextPageFaultNotifyListLock);
		return PVRSRV_OK;
	}

	dllist_foreach_node(&(psDevNode->sMemoryContextPageFaultNotifyListHead), psNode, psNodeNext)
	{
		DEVMEMINT_CTX *psThisContext =
			IMG_CONTAINER_OF(psNode, DEVMEMINT_CTX, sPageFaultNotifyListElem);
		IMG_DEV_PHYADDR sPCDevPAddr;

		eError = MMU_AcquireBaseAddr(psThisContext->psMMUContext, &sPCDevPAddr);
		if (eError != PVRSRV_OK)
		{
			PVR_LOG_ERROR(eError, "MMU_AcquireBaseAddr");
			OSWRLockReleaseRead(psDevNode->hMemoryContextPageFaultNotifyListLock);
			return eError;
		}

		if (sPCDevPAddr.uiAddr == ui64FaultedPCAddress)
		{
			psDevmemCtx = psThisContext;
			break;
		}
	}
	OSWRLockReleaseRead(psDevNode->hMemoryContextPageFaultNotifyListLock);

	if (psDevmemCtx == NULL)
	{
		/* Not found, just return */
		return PVRSRV_OK;
	}
	OSWRLockAcquireRead(psDevmemCtx->hListLock);

	/*
	 * Store the first occurrence of a page fault address,
	 * until that address is consumed by a client.
	 */
	if ((psDevmemCtx->ui32Flags & DEVMEMCTX_FLAGS_FAULT_ADDRESS_AVAILABLE) == 0)
	{
		psDevmemCtx->sFaultAddress = sFaultAddress;
		psDevmemCtx->ui32Flags |= DEVMEMCTX_FLAGS_FAULT_ADDRESS_AVAILABLE;
	}

	/* Loop through each registered PID and send a signal to the process */
	dllist_foreach_node(&(psDevmemCtx->sProcessNotifyListHead), psNode, psNodeNext)
	{
		psNotifyNode = IMG_CONTAINER_OF(psNode, DEVMEMINT_PF_NOTIFY, sProcessNotifyListElem);

		eError = OSDebugSignalPID(psNotifyNode->ui32PID);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
			         "%s: Unable to signal process for PID: %u",
			         __func__,
			         psNotifyNode->ui32PID));

			PVR_ASSERT(!"Unable to signal process");

			bFailed = IMG_TRUE;
		}
	}
	OSWRLockReleaseRead(psDevmemCtx->hListLock);

	if (bFailed)
	{
		return PVRSRV_ERROR_SIGNAL_FAILED;
	}

	return PVRSRV_OK;
}

#if defined(PDUMP)
IMG_UINT32 DevmemIntMMUContextID(DEVMEMINT_CTX *psDevMemContext)
{
	IMG_UINT32 ui32MMUContextID;
	MMU_AcquirePDumpMMUContext(psDevMemContext->psMMUContext, &ui32MMUContextID, PDUMP_FLAGS_CONTINUOUS);
	return ui32MMUContextID;
}

PVRSRV_ERROR
DevmemIntPDumpSaveToFileVirtual(DEVMEMINT_CTX *psDevmemCtx,
                                IMG_DEV_VIRTADDR sDevAddrStart,
                                IMG_DEVMEM_SIZE_T uiSize,
                                IMG_UINT32 ui32ArraySize,
                                const IMG_CHAR *pszFilename,
                                IMG_UINT32 ui32FileOffset,
                                IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 uiPDumpMMUCtx;

	PVR_UNREFERENCED_PARAMETER(ui32ArraySize);

	eError = MMU_AcquirePDumpMMUContext(psDevmemCtx->psMMUContext,
			&uiPDumpMMUCtx,
			ui32PDumpFlags);

	PVR_ASSERT(eError == PVRSRV_OK);

	/*
	   The following SYSMEM refers to the 'MMU Context', hence it
	   should be the MMU context, not the PMR, that says what the PDump
	   MemSpace tag is?
	   From a PDump P.O.V. it doesn't matter which name space we use as long
	   as that MemSpace is used on the 'MMU Context' we're dumping from
	   */
	eError = PDumpMMUSAB(psDevmemCtx->psDevNode->sDevId.pszPDumpDevName,
	                     uiPDumpMMUCtx,
	                     sDevAddrStart,
	                     uiSize,
	                     pszFilename,
	                     ui32FileOffset,
	                     ui32PDumpFlags);
	PVR_ASSERT(eError == PVRSRV_OK);

	MMU_ReleasePDumpMMUContext(psDevmemCtx->psMMUContext, ui32PDumpFlags);
	return PVRSRV_OK;
}


PVRSRV_ERROR
DevmemIntPDumpBitmap(CONNECTION_DATA * psConnection,
                     PVRSRV_DEVICE_NODE *psDeviceNode,
                     IMG_CHAR *pszFileName,
                     IMG_UINT32 ui32FileOffset,
                     IMG_UINT32 ui32Width,
                     IMG_UINT32 ui32Height,
                     IMG_UINT32 ui32StrideInBytes,
                     IMG_DEV_VIRTADDR sDevBaseAddr,
                     DEVMEMINT_CTX *psDevMemContext,
                     IMG_UINT32 ui32Size,
                     PDUMP_PIXEL_FORMAT ePixelFormat,
                     IMG_UINT32 ui32AddrMode,
                     IMG_UINT32 ui32PDumpFlags)
{
	IMG_UINT32 ui32ContextID;
	PVRSRV_ERROR eError;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	eError = MMU_AcquirePDumpMMUContext(psDevMemContext->psMMUContext, &ui32ContextID, ui32PDumpFlags);

	if (eError != PVRSRV_OK)
	{
		PVR_LOG_ERROR(eError, "MMU_AcquirePDumpMMUContext");
		return PVRSRV_ERROR_FAILED_TO_ALLOC_MMUCONTEXT_ID;
	}

	eError = PDumpBitmapKM(psDeviceNode,
	                       pszFileName,
	                       ui32FileOffset,
	                       ui32Width,
	                       ui32Height,
	                       ui32StrideInBytes,
	                       sDevBaseAddr,
	                       ui32ContextID,
	                       ui32Size,
	                       ePixelFormat,
	                       ui32AddrMode,
	                       ui32PDumpFlags);

	/* Don't care about return value */
	MMU_ReleasePDumpMMUContext(psDevMemContext->psMMUContext, ui32PDumpFlags);

	return eError;
}

PVRSRV_ERROR
DevmemIntPDumpImageDescriptor(CONNECTION_DATA * psConnection,
							  PVRSRV_DEVICE_NODE *psDeviceNode,
							  DEVMEMINT_CTX *psDevMemContext,
							  IMG_UINT32 ui32Size,
							  const IMG_CHAR *pszFileName,
							  IMG_DEV_VIRTADDR sData,
							  IMG_UINT32 ui32DataSize,
							  IMG_UINT32 ui32LogicalWidth,
							  IMG_UINT32 ui32LogicalHeight,
							  IMG_UINT32 ui32PhysicalWidth,
							  IMG_UINT32 ui32PhysicalHeight,
							  PDUMP_PIXEL_FORMAT ePixFmt,
							  IMG_MEMLAYOUT eMemLayout,
							  IMG_FB_COMPRESSION eFBCompression,
							  const IMG_UINT32 *paui32FBCClearColour,
							  PDUMP_FBC_SWIZZLE eFBCSwizzle,
							  IMG_DEV_VIRTADDR sHeader,
							  IMG_UINT32 ui32HeaderSize,
							  IMG_UINT32 ui32PDumpFlags)
{
	IMG_UINT32 ui32ContextID;
	PVRSRV_ERROR eError;

	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(ui32Size);

	eError = MMU_AcquirePDumpMMUContext(psDevMemContext->psMMUContext, &ui32ContextID, ui32PDumpFlags);
	PVR_LOG_RETURN_IF_ERROR(eError, "MMU_AcquirePDumpMMUContext");

	eError = PDumpImageDescriptor(psDeviceNode,
									ui32ContextID,
									(IMG_CHAR *)pszFileName,
									sData,
									ui32DataSize,
									ui32LogicalWidth,
									ui32LogicalHeight,
									ui32PhysicalWidth,
									ui32PhysicalHeight,
									ePixFmt,
									eMemLayout,
									eFBCompression,
									paui32FBCClearColour,
									eFBCSwizzle,
									sHeader,
									ui32HeaderSize,
									ui32PDumpFlags);
	PVR_LOG_IF_ERROR(eError, "PDumpImageDescriptor");

	/* Don't care about return value */
	(void) MMU_ReleasePDumpMMUContext(psDevMemContext->psMMUContext, ui32PDumpFlags);

	return eError;
}

PVRSRV_ERROR
DevmemIntPDumpDataDescriptor(CONNECTION_DATA * psConnection,
							 PVRSRV_DEVICE_NODE *psDeviceNode,
							 DEVMEMINT_CTX *psDevMemContext,
							 IMG_UINT32 ui32Size,
							 const IMG_CHAR *pszFileName,
							 IMG_DEV_VIRTADDR sData,
							 IMG_UINT32 ui32DataSize,
							 IMG_UINT32 ui32HeaderType,
							 IMG_UINT32 ui32ElementType,
							 IMG_UINT32 ui32ElementCount,
							 IMG_UINT32 ui32PDumpFlags)
{
	IMG_UINT32 ui32ContextID;
	PVRSRV_ERROR eError;

	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(ui32Size);

	if ((ui32HeaderType != IBIN_HEADER_TYPE) &&
		(ui32HeaderType != DATA_HEADER_TYPE))
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Invalid header type (%u)",
		         __func__,
		         ui32HeaderType));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	eError = MMU_AcquirePDumpMMUContext(psDevMemContext->psMMUContext, &ui32ContextID, ui32PDumpFlags);
	PVR_LOG_RETURN_IF_ERROR(eError, "MMU_AcquirePDumpMMUContext");

	eError = PDumpDataDescriptor(psDeviceNode,
									ui32ContextID,
									(IMG_CHAR *)pszFileName,
									sData,
									ui32DataSize,
									ui32HeaderType,
									ui32ElementType,
									ui32ElementCount,
									ui32PDumpFlags);
	PVR_LOG_IF_ERROR(eError, "PDumpDataDescriptor");

	/* Don't care about return value */
	(void) MMU_ReleasePDumpMMUContext(psDevMemContext->psMMUContext, ui32PDumpFlags);

	return eError;
}

#endif
