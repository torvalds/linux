/**************************************************************************/ /*!
@File
@Title          Device Memory Management
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header file for server side component of device memory management
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

#ifndef __DEVICEMEM_SERVER_H__
#define __DEVICEMEM_SERVER_H__

#include "device.h" /* For device node */
#include "img_types.h"
#include "pvr_debug.h"
#include "pvrsrv_error.h"

#include "pmr.h"

typedef struct _DEVMEMINT_CTX_ DEVMEMINT_CTX;
typedef struct _DEVMEMINT_CTX_EXPORT_ DEVMEMINT_CTX_EXPORT;
typedef struct _DEVMEMINT_HEAP_ DEVMEMINT_HEAP;
/* FIXME: can we unify RESERVATION and MAPPING to save data structures? */
typedef struct _DEVMEMINT_RESERVATION_ DEVMEMINT_RESERVATION;
typedef struct _DEVMEMINT_MAPPING_ DEVMEMINT_MAPPING;

/*
 * DevmemServerGetImportHandle()
 *
 * For given exportable memory descriptor returns PMR handle
 *
 */
PVRSRV_ERROR
DevmemServerGetImportHandle(DEVMEM_MEMDESC *psMemDesc,
						   IMG_HANDLE *phImport);

/*
 * DevmemServerGetHeapHandle()
 *
 * For given reservation returns the Heap handle
 *
 */
PVRSRV_ERROR
DevmemServerGetHeapHandle(DEVMEMINT_RESERVATION *psReservation,
						   IMG_HANDLE *phHeap);

/*
 * DevmemIntCtxCreate()
 *
 * Create a Server-side Device Memory Context.  This is usually the
 * counterpart of the client side memory context, and indeed is
 * usually created at the same time.
 *
 * You must have one of these before creating any heaps.
 *
 * All heaps must have been destroyed before calling
 * DevmemIntCtxDestroy()
 *
 * If you call DevmemIntCtxCreate() (and it succeeds) you are promising
 * to later call DevmemIntCtxDestroy()
 *
 * Note that this call will cause the device MMU code to do some work
 * for creating the device memory context, but it does not guarantee
 * that a page catalogue will have been created, as this may be
 * deferred until first allocation.
 *
 * Caller to provide storage for a pointer to the DEVMEM_CTX object
 * that will be created by this call.
 */
extern PVRSRV_ERROR
DevmemIntCtxCreate(
                 PVRSRV_DEVICE_NODE *psDeviceNode,
                 /* devnode / perproc etc */

                 DEVMEMINT_CTX **ppsDevmemCtxPtr,
                 IMG_HANDLE *hPrivData
                 );
/*
 * DevmemIntCtxDestroy()
 *
 * Undoes a prior DevmemIntCtxCreate or DevmemIntCtxImport.
 */
extern PVRSRV_ERROR
DevmemIntCtxDestroy(
                  DEVMEMINT_CTX *psDevmemCtx
                  );

/*
 * DevmemIntCtxExport()
 *
 * Export a device memory context created with DevmemIntCtxCreate to another
 * process
 */
 
extern PVRSRV_ERROR
DevmemIntCtxExport(DEVMEMINT_CTX *psDevmemCtx,
                   DEVMEMINT_CTX_EXPORT **ppsExport);

/*
 * DevmemIntCtxUnexport
 *
 * Unexport an exported a device memory context.
 */
extern PVRSRV_ERROR
DevmemIntCtxUnexport(DEVMEMINT_CTX_EXPORT *psExport);

/*
 * DevmemIntCtxImport
 *
 * Import an exported a device memory context.
 */
extern PVRSRV_ERROR
DevmemIntCtxImport(DEVMEMINT_CTX_EXPORT *psExport,
				   DEVMEMINT_CTX **ppsDevmemCtxPtr,
				   IMG_HANDLE *hPrivData);

/*
 * DevmemIntHeapCreate()
 *
 * Creates a new heap in this device memory context.  This will cause
 * a call into the MMU code to allocate various data structures for
 * managing this heap.  It will not necessarily cause any page tables
 * to be set up, as this can be deferred until first allocation.
 * (i.e. we shouldn't care - it's up to the MMU code)
 *
 * Note that the data page size must be specified (as log 2).  The
 * data page size as specified here will be communicated to the mmu
 * module, and thus may determine the page size configured in page
 * directory entries for subsequent allocations from this heap.  It is
 * essential that the page size here is less than or equal to the
 * "minimum contiguity guarantee" of any PMR that you subsequently
 * attempt to map to this heap.
 *
 * If you call DevmemIntHeapCreate() (and the call succeeds) you are
 * promising that you shall subsequently call DevmemIntHeapDestroy()
 *
 * Caller to provide storage for a pointer to the DEVMEM_HEAP object
 * that will be created by this call.
 */
extern PVRSRV_ERROR
DevmemIntHeapCreate(
                   DEVMEMINT_CTX *psDevmemCtx,
                   IMG_DEV_VIRTADDR sHeapBaseAddr,
                   IMG_DEVMEM_SIZE_T uiHeapLength,
                   IMG_UINT32 uiLog2DataPageSize,
                   DEVMEMINT_HEAP **ppsDevmemHeapPtr
                   );
/*
 * DevmemIntHeapDestroy()
 *
 * Destroys a heap previously created with DevmemIntHeapCreate()
 *
 * All allocations from his heap must have been freed before this
 * call.
 */
extern PVRSRV_ERROR
DevmemIntHeapDestroy(
                     DEVMEMINT_HEAP *psDevmemHeap
                    );

/*
 * DevmemIntMapPMR()
 *
 * Maps the given PMR to the virtual range previously allocated with
 * DevmemIntReserveRange()
 *
 * If appropriate, the PMR must have had its physical backing
 * committed, as this call will call into the MMU code to set up the
 * page tables for this allocation, which shall in turn request the
 * physical addresses from the PMR.  Alternatively, the PMR
 * implementation can choose to do so off the back of the "lock"
 * callback, which it will receive as a result (indirectly) of this
 * call.
 *
 * This function makes no promise w.r.t. the circumstances that it can
 * be called, and these would be "inherited" from the implementation
 * of the PMR.  For example if the PMR "lock" callback causes pages to
 * be pinned at that time (which may cause scheduling or disk I/O
 * etc.) then it would not be legal to "Map" the PMR in a context
 * where scheduling events are disallowed.
 *
 * If you call DevmemIntMapPMR() (and the call succeeds) then you are
 * promising that you shall later call DevmemIntUnmapPMR()
 */
extern PVRSRV_ERROR
DevmemIntMapPMR(DEVMEMINT_HEAP *psDevmemHeap,
                DEVMEMINT_RESERVATION *psReservation,
                PMR *psPMR,
                PVRSRV_MEMALLOCFLAGS_T uiMapFlags,
                DEVMEMINT_MAPPING **ppsMappingPtr);
/*
 * DevmemIntUnmapPMR()
 *
 * Reverses the mapping caused by DevmemIntMapPMR()
 */
extern PVRSRV_ERROR
DevmemIntUnmapPMR(DEVMEMINT_MAPPING *psMapping);

/*
 * DevmemIntReserveRange()
 *
 * Indicates that the specified range should be reserved from the
 * given heap.
 *
 * In turn causes the page tables to be allocated to cover the
 * specified range.
 *
 * If you call DevmemIntReserveRange() (and the call succeeds) then you
 * are promising that you shall later call DevmemIntUnreserveRange()
 */
extern PVRSRV_ERROR
DevmemIntReserveRange(DEVMEMINT_HEAP *psDevmemHeap,
                      IMG_DEV_VIRTADDR sAllocationDevVAddr,
                      IMG_DEVMEM_SIZE_T uiAllocationSize,
                      DEVMEMINT_RESERVATION **ppsReservationPtr);
/*
 * DevmemIntUnreserveRange()
 *
 * Undoes the state change caused by DevmemIntReserveRage()
 */
extern PVRSRV_ERROR
DevmemIntUnreserveRange(DEVMEMINT_RESERVATION *psDevmemReservation);

/*
 * SLCFlushInvalRequest()
 *
 * Schedules an SLC Flush & Invalidate on the firmware if required.
 * If the request is performed depends on the caching attributes
 * of the allocation and hence depends on the underlying PMR
 */
extern PVRSRV_ERROR
DevmemSLCFlushInvalRequest(PVRSRV_DEVICE_NODE *psDeviceNode, PMR *psPmr);

extern PVRSRV_ERROR
DevmemIntIsVDevAddrValid(DEVMEMINT_CTX *psDevMemContext,
                         IMG_DEV_VIRTADDR sDevAddr);

#if defined(PDUMP)
/*
 * DevmemIntPDumpSaveToFileVirtual()
 *
 * Writes out PDump "SAB" commands with the data found in memory at
 * the given virtual address.
 */
/* FIXME: uiArraySize shouldn't be here, and is an
   artefact of the bridging */
extern PVRSRV_ERROR
DevmemIntPDumpSaveToFileVirtual(DEVMEMINT_CTX *psDevmemCtx,
                                IMG_DEV_VIRTADDR sDevAddrStart,
                                IMG_DEVMEM_SIZE_T uiSize,
                                IMG_UINT32 uiArraySize,
                                const IMG_CHAR *pszFilename,
								IMG_UINT32 ui32FileOffset,
								IMG_UINT32 ui32PDumpFlags);

extern IMG_UINT32
DevmemIntMMUContextID(DEVMEMINT_CTX *psDevMemContext);

extern PVRSRV_ERROR
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
						IMG_UINT32 ui32PDumpFlags);
#else	/* PDUMP */

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVSyncPrimPDumpPolKM)
#endif
static INLINE PVRSRV_ERROR
DevmemIntPDumpSaveToFileVirtual(DEVMEMINT_CTX *psDevmemCtx,
                                IMG_DEV_VIRTADDR sDevAddrStart,
                                IMG_DEVMEM_SIZE_T uiSize,
                                IMG_UINT32 uiArraySize,
                                const IMG_CHAR *pszFilename,
								IMG_UINT32 ui32FileOffset,
								IMG_UINT32 ui32PDumpFlags)
{
	PVR_UNREFERENCED_PARAMETER(psDevmemCtx);
	PVR_UNREFERENCED_PARAMETER(sDevAddrStart);
	PVR_UNREFERENCED_PARAMETER(uiSize);
	PVR_UNREFERENCED_PARAMETER(uiArraySize);
	PVR_UNREFERENCED_PARAMETER(pszFilename);
	PVR_UNREFERENCED_PARAMETER(ui32FileOffset);
	PVR_UNREFERENCED_PARAMETER(ui32PDumpFlags);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVSyncPrimPDumpPolKM)
#endif
static INLINE PVRSRV_ERROR
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
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	PVR_UNREFERENCED_PARAMETER(pszFileName);
	PVR_UNREFERENCED_PARAMETER(ui32FileOffset);
	PVR_UNREFERENCED_PARAMETER(ui32Width);
	PVR_UNREFERENCED_PARAMETER(ui32Height);
	PVR_UNREFERENCED_PARAMETER(ui32StrideInBytes);
	PVR_UNREFERENCED_PARAMETER(sDevBaseAddr);
	PVR_UNREFERENCED_PARAMETER(psDevMemContext);
	PVR_UNREFERENCED_PARAMETER(ui32Size);
	PVR_UNREFERENCED_PARAMETER(ePixelFormat);
	PVR_UNREFERENCED_PARAMETER(ui32AddrMode);
	PVR_UNREFERENCED_PARAMETER(ui32PDumpFlags);
	return PVRSRV_OK;
}
#endif	/* PDUMP */
#endif /* ifndef __DEVICEMEM_SERVER_H__ */
