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
#include "pvr_debug.h"
#include "pvrsrv_error.h"

#include "mmu_common.h"
#include "pdump_km.h"
#include "pmr.h"

#include "allocmem.h"
#include "osfunc.h"
#include "lock.h"

struct _DEVMEMINT_CTX_
{
    PVRSRV_DEVICE_NODE *psDevNode;

    /* MMU common code needs to have a context.  There's a one-to-one
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
};

struct _DEVMEMINT_CTX_EXPORT_ 
{
	DEVMEMINT_CTX *psDevmemCtx;
};

struct _DEVMEMINT_HEAP_
{
    struct _DEVMEMINT_CTX_ *psDevmemCtx;
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
    IMG_UINT32 uiLog2PageSize;
};

/*************************************************************************/ /*!
@Function       _DevmemIntCtxAcquire
@Description    Acquire a reference to the provided device memory context.
@Return         None
*/ /**************************************************************************/
static INLINE IMG_VOID _DevmemIntCtxAcquire(DEVMEMINT_CTX *psDevmemCtx)
{
	OSAtomicIncrement(&psDevmemCtx->hRefCount);
}

/*************************************************************************/ /*!
@Function       _DevmemIntCtxRelease
@Description    Release the reference to the provided device memory context.
                If this is the last reference which was taken then the
                memory context will be freed.
@Return         None
*/ /**************************************************************************/
static INLINE IMG_VOID _DevmemIntCtxRelease(DEVMEMINT_CTX *psDevmemCtx)
{
	if (OSAtomicDecrement(&psDevmemCtx->hRefCount) == 0)
	{
		/* The last reference has gone, destroy the context */
		PVRSRV_DEVICE_NODE *psDevNode = psDevmemCtx->psDevNode;
	
		if (psDevNode->pfnUnregisterMemoryContext)
		{
			psDevNode->pfnUnregisterMemoryContext(psDevmemCtx->hPrivData);
		}
	    MMU_ContextDestroy(psDevmemCtx->psMMUContext);
	
		PVR_DPF((PVR_DBG_MESSAGE, "%s: Freed memory context %p", __FUNCTION__, psDevmemCtx));
		OSFreeMem(psDevmemCtx);
	}
}

/*************************************************************************/ /*!
@Function       _DevmemIntHeapAcquire
@Description    Acquire a reference to the provided device memory heap.
@Return         None
*/ /**************************************************************************/
static INLINE IMG_VOID _DevmemIntHeapAcquire(DEVMEMINT_HEAP *psDevmemHeap)
{
	OSAtomicIncrement(&psDevmemHeap->hRefCount);
}

/*************************************************************************/ /*!
@Function       _DevmemIntHeapRelease
@Description    Release the reference to the provided device memory heap.
                If this is the last reference which was taken then the
                memory context will be freed.
@Return         None
*/ /**************************************************************************/
static INLINE IMG_VOID _DevmemIntHeapRelease(DEVMEMINT_HEAP *psDevmemHeap)
{
	OSAtomicDecrement(&psDevmemHeap->hRefCount);
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

	if (psMemDesc->psImport->bExportable == IMG_FALSE)
	{
        eError = PVRSRV_ERROR_DEVICEMEM_CANT_EXPORT_SUBALLOCATION;
        goto e0;
	}

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
	*phHeap = psReservation->psDevmemHeap;
	return PVRSRV_OK;
}



/*************************************************************************/ /*!
@Function       DevmemIntCtxCreate
@Description    Creates and initialises a device memory context.
@Return         valid Device Memory context handle - Success
                PVRSRV_ERROR failure code
*/ /**************************************************************************/
PVRSRV_ERROR
DevmemIntCtxCreate(
                   PVRSRV_DEVICE_NODE *psDeviceNode,
                   DEVMEMINT_CTX **ppsDevmemCtxPtr,
                   IMG_HANDLE *hPrivData
                   )
{
    PVRSRV_ERROR eError;
    DEVMEMINT_CTX *psDevmemCtx;
    IMG_HANDLE hPrivDataInt = IMG_NULL;

	PVR_DPF((PVR_DBG_MESSAGE, "%s", __FUNCTION__));

	/* allocate a Devmem context */
    psDevmemCtx = OSAllocMem(sizeof *psDevmemCtx);
    if (psDevmemCtx == IMG_NULL)
	{
        eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		PVR_DPF ((PVR_DBG_ERROR, "%s: Alloc failed", __FUNCTION__));
        goto fail_alloc;
	}

	OSAtomicWrite(&psDevmemCtx->hRefCount, 1);
    psDevmemCtx->psDevNode = psDeviceNode;

    /* Call down to MMU context creation */

    eError = MMU_ContextCreate(psDeviceNode,
                               &psDevmemCtx->psMMUContext);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: MMU_ContextCreate failed", __FUNCTION__));
		goto fail_mmucontext;
	}


	if (psDeviceNode->pfnRegisterMemoryContext)
	{
		eError = psDeviceNode->pfnRegisterMemoryContext(psDeviceNode, psDevmemCtx->psMMUContext, &hPrivDataInt);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to register MMU context", __FUNCTION__));
			goto fail_register;
		}
	}

	/* Store the private data as it is required to unregister the memory context */
	psDevmemCtx->hPrivData = hPrivDataInt;
	*hPrivData = hPrivDataInt;
    *ppsDevmemCtxPtr = psDevmemCtx;

	return PVRSRV_OK;

fail_register:
    MMU_ContextDestroy(psDevmemCtx->psMMUContext);
fail_mmucontext:
	OSFREEMEM(psDevmemCtx);
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
DevmemIntHeapCreate(
                    DEVMEMINT_CTX *psDevmemCtx,
                    IMG_DEV_VIRTADDR sHeapBaseAddr,
                    IMG_DEVMEM_SIZE_T uiHeapLength,
                    IMG_UINT32 uiLog2DataPageSize,
                    DEVMEMINT_HEAP **ppsDevmemHeapPtr
                    )
{
    PVRSRV_ERROR eError;
    DEVMEMINT_HEAP *psDevmemHeap;

	PVR_DPF((PVR_DBG_MESSAGE, "%s: DevmemIntHeap_Create", __FUNCTION__));

	/* allocate a Devmem context */
	psDevmemHeap = OSAllocMem(sizeof *psDevmemHeap);
    if (psDevmemHeap == IMG_NULL)
	{
        eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		PVR_DPF ((PVR_DBG_ERROR, "%s: Alloc failed", __FUNCTION__));
        goto fail_alloc;
	}

    psDevmemHeap->psDevmemCtx = psDevmemCtx;

	_DevmemIntCtxAcquire(psDevmemHeap->psDevmemCtx);

	OSAtomicWrite(&psDevmemHeap->hRefCount, 1);

    *ppsDevmemHeapPtr = psDevmemHeap;

	return PVRSRV_OK;

fail_alloc:
    return eError;
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

	/* allocate memory to record the mapping info */
	psMapping = OSAllocMem(sizeof *psMapping);
    if (psMapping == IMG_NULL)
	{
        eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		PVR_DPF ((PVR_DBG_ERROR, "DevmemIntMapPMR: Alloc failed"));
        goto e0;
	}

    uiAllocationSize = psReservation->uiLength;


    ui32NumDevPages = 0xffffffffU & (((uiAllocationSize - 1)
                                      >> GET_LOG2_PAGESIZE()) + 1);
    PVR_ASSERT(ui32NumDevPages << GET_LOG2_PAGESIZE() == uiAllocationSize);

    eError = PMRLockSysPhysAddresses(psPMR,
    		GET_LOG2_PAGESIZE());
    if (eError != PVRSRV_OK)
	{
        goto e2;
	}

    sAllocationDevVAddr = psReservation->sBase;

    /*  N.B.  We pass mapping permission flags to MMU_MapPMR and let
       it reject the mapping if the permissions on the PMR are not compatible. */

    eError = MMU_MapPMR (psDevmemHeap->psDevmemCtx->psMMUContext,
                         sAllocationDevVAddr,
                         psPMR,
                         ui32NumDevPages << GET_LOG2_PAGESIZE(),
                         uiMapFlags,
                         GET_LOG2_PAGESIZE());
    PVR_ASSERT(eError == PVRSRV_OK);

    psMapping->psReservation = psReservation;
    psMapping->uiNumPages = ui32NumDevPages;
    psMapping->uiLog2PageSize = GET_LOG2_PAGESIZE();
    psMapping->psPMR = psPMR;
    /* Don't bother with refcount on reservation, as a reservation
       only ever holds one mapping, so we directly increment the
       refcount on the heap instead */
    _DevmemIntHeapAcquire(psMapping->psReservation->psDevmemHeap);

    *ppsMappingPtr = psMapping;

    return PVRSRV_OK;

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
    DEVMEMINT_HEAP *psDevmemHeap;
    /* device virtual address of start of allocation */
    IMG_DEV_VIRTADDR sAllocationDevVAddr;
    /* number of pages (device pages) that allocation spans */
    IMG_UINT32 ui32NumDevPages;

    psDevmemHeap = psMapping->psReservation->psDevmemHeap;

    ui32NumDevPages = psMapping->uiNumPages;
    sAllocationDevVAddr = psMapping->psReservation->sBase;


    MMU_UnmapPages (psDevmemHeap->psDevmemCtx->psMMUContext,
                    sAllocationDevVAddr,
                    ui32NumDevPages,
                    GET_LOG2_PAGESIZE());

    eError = PMRUnlockSysPhysAddresses(psMapping->psPMR);
    PVR_ASSERT(eError == PVRSRV_OK);

    /* Don't bother with refcount on reservation, as a reservation
       only ever holds one mapping, so we directly decrement the
       refcount on the heap instead */
    _DevmemIntHeapRelease(psDevmemHeap);

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
	psReservation = OSAllocMem(sizeof *psReservation);
    if (psReservation == IMG_NULL)
	{
        eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		PVR_DPF ((PVR_DBG_ERROR, "DevmemIntReserveRange: Alloc failed"));
        goto e0;
	}

    psReservation->sBase = sAllocationDevVAddr;
    psReservation->uiLength = uiAllocationSize;


    eError = MMU_Alloc (psDevmemHeap->psDevmemCtx->psMMUContext,
                        uiAllocationSize,
                        &uiAllocationSize,
                        0, /* IMG_UINT32 uiProtFlags */
                        0, /* alignment is n/a since we supply devvaddr */
                        &sAllocationDevVAddr,
                        GET_LOG2_PAGESIZE());
    if (eError != PVRSRV_OK)
    {
        goto e1;
    }

    /* since we supplied the virt addr, MMU_Alloc shouldn't have
       chosen a new one for us */
    PVR_ASSERT(sAllocationDevVAddr.uiAddr == psReservation->sBase.uiAddr);

	_DevmemIntHeapAcquire(psDevmemHeap);

    psReservation->psDevmemHeap = psDevmemHeap;
    *ppsReservationPtr = psReservation;

    return PVRSRV_OK;

    /*
      error exit paths follow
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

    MMU_Free (psReservation->psDevmemHeap->psDevmemCtx->psMMUContext,
              psReservation->sBase,
              psReservation->uiLength,
              GET_LOG2_PAGESIZE());

	_DevmemIntHeapRelease(psReservation->psDevmemHeap);
	OSFreeMem(psReservation);

    return PVRSRV_OK;
}

PVRSRV_ERROR
DevmemIntHeapDestroy(
                     DEVMEMINT_HEAP *psDevmemHeap
                     )
{
    if (OSAtomicRead(&psDevmemHeap->hRefCount) != 1)
    {
        PVR_DPF((PVR_DBG_ERROR, "BUG!  %s called but has too many references (%d) "
                 "which probably means allocations have been made from the heap and not freed",
                 __FUNCTION__,
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

	_DevmemIntCtxRelease(psDevmemHeap->psDevmemCtx);

	PVR_DPF((PVR_DBG_MESSAGE, "%s: Freed heap %p", __FUNCTION__, psDevmemHeap));
	OSFreeMem(psDevmemHeap);

	return PVRSRV_OK;
}


/*************************************************************************/ /*!
@Function       DevmemIntCtxDestroy
@Description    Destroy that created by DevmemIntCtxCreate
@Input          psDevmemCtx   Device Memory context
@Return         cannot fail.
*/ /**************************************************************************/
PVRSRV_ERROR
DevmemIntCtxDestroy(
                    DEVMEMINT_CTX *psDevmemCtx
                    )
{
	/*
		We can't determine if we should be freeing the context here
		as it refcount!=1 could be due to either the fact that heap(s)
		remain with allocations on them, or that this memory context
		has been exported.
		As the client couldn’t do anything useful with this information
		anyway and the fact that the refcount will ensure we only
		free the context when _all_ references have been released
		don't bother checking and just return OK regardless.
	*/
	_DevmemIntCtxRelease(psDevmemCtx);
	return PVRSRV_OK;
}

/*************************************************************************/ /*!
@Function       DevmemIntCtxExport
@Description    Exports a device memory context.
@Return         valid Device Memory context handle - Success
                PVRSRV_ERROR failure code
*/ /**************************************************************************/
PVRSRV_ERROR
DevmemIntCtxExport(DEVMEMINT_CTX *psDevmemCtx,
                   DEVMEMINT_CTX_EXPORT **ppsExport)
{
	DEVMEMINT_CTX_EXPORT *psExport;

	psExport = OSAllocMem(sizeof(*psExport));
	if (psExport == IMG_NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	_DevmemIntCtxAcquire(psDevmemCtx);
	psExport->psDevmemCtx = psDevmemCtx;
	
	*ppsExport = psExport;
	return PVRSRV_OK;
}

/*************************************************************************/ /*!
@Function       DevmemIntCtxUnexport
@Description    Unexport an exported a device memory context.
@Return         None
*/ /**************************************************************************/
PVRSRV_ERROR
DevmemIntCtxUnexport(DEVMEMINT_CTX_EXPORT *psExport)
{
	_DevmemIntCtxRelease(psExport->psDevmemCtx);
	OSFreeMem(psExport);
	return PVRSRV_OK;
}

/*************************************************************************/ /*!
@Function       DevmemIntCtxImport
@Description    Import an exported a device memory context.
@Return         valid Device Memory context handle - Success
                PVRSRV_ERROR failure code
*/ /**************************************************************************/
PVRSRV_ERROR
DevmemIntCtxImport(DEVMEMINT_CTX_EXPORT *psExport,
				   DEVMEMINT_CTX **ppsDevmemCtxPtr,
				   IMG_HANDLE *hPrivData)
{
	DEVMEMINT_CTX *psDevmemCtx = psExport->psDevmemCtx;

	_DevmemIntCtxAcquire(psDevmemCtx);

	*ppsDevmemCtxPtr = psDevmemCtx;
	*hPrivData = psDevmemCtx->hPrivData;

	return PVRSRV_OK;
}

/*************************************************************************/ /*!
@Function       DevmemSLCFlushInvalRequest
@Description    Requests a SLC Flush and Invalidate
@Input          psDeviceNode    Device node
@Input          psPmr           PMR
@Return         PVRSRV_OK
*/ /**************************************************************************/
PVRSRV_ERROR
DevmemSLCFlushInvalRequest(PVRSRV_DEVICE_NODE *psDeviceNode,
							PMR *psPmr)
{

	/* invoke SLC flush and invalidate request */
	psDeviceNode->pfnSLCCacheInvalidateRequest(psDeviceNode, psPmr);

	return PVRSRV_OK;
}

PVRSRV_ERROR DevmemIntIsVDevAddrValid(DEVMEMINT_CTX *psDevMemContext,
                                      IMG_DEV_VIRTADDR sDevAddr)
{
    return MMU_IsVDevAddrValid(psDevMemContext->psMMUContext,
                               GET_LOG2_PAGESIZE(),
                               sDevAddr) ? PVRSRV_OK : PVRSRV_ERROR_INVALID_GPU_ADDR;
}

#if defined (PDUMP)
IMG_UINT32 DevmemIntMMUContextID(DEVMEMINT_CTX *psDevMemContext)
{
	IMG_UINT32 ui32MMUContextID;
	MMU_AcquirePDumpMMUContext(psDevMemContext->psMMUContext, &ui32MMUContextID);
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
										&uiPDumpMMUCtx);

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

	MMU_ReleasePDumpMMUContext(psDevmemCtx->psMMUContext);
    return PVRSRV_OK;
}


PVRSRV_ERROR
DevmemIntPDumpBitmap(PVRSRV_DEVICE_NODE *psDeviceNode,
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

	eError = MMU_AcquirePDumpMMUContext(psDevMemContext->psMMUContext, &ui32ContextID);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "DevmemIntPDumpBitmap: Failed to acquire MMU context"));
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
	MMU_ReleasePDumpMMUContext(psDevMemContext->psMMUContext);

	return eError;
}
#endif
