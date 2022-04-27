/*************************************************************************/ /*!
@File
@Title          Device Memory Management
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Server side component for device memory management
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

#ifndef DEVICEMEM_SERVER_H
#define DEVICEMEM_SERVER_H

#include "device.h" /* For device node */
#include "img_types.h"
#include "img_defs.h"
#include "pvr_debug.h"
#include "pvrsrv_error.h"

#include "connection_server.h"
#include "pmr.h"

typedef struct _DEVMEMINT_CTX_ DEVMEMINT_CTX;
typedef struct _DEVMEMINT_CTX_EXPORT_ DEVMEMINT_CTX_EXPORT;
typedef struct _DEVMEMINT_HEAP_ DEVMEMINT_HEAP;

typedef struct _DEVMEMINT_RESERVATION_ DEVMEMINT_RESERVATION;
typedef struct _DEVMEMINT_MAPPING_ DEVMEMINT_MAPPING;
typedef struct _DEVMEMINT_PF_NOTIFY_ DEVMEMINT_PF_NOTIFY;


/*************************************************************************/ /*!
@Function       DevmemIntUnpin
@Description    This is the counterpart to DevmemPin(). It is meant to be
                called when the allocation is NOT mapped in the device virtual
                space.

@Input          psPMR           The physical memory to unpin.

@Return         PVRSRV_ERROR:   PVRSRV_OK on success and the memory is
                                registered to be reclaimed. Error otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR DevmemIntUnpin(PMR *psPMR);

/*************************************************************************/ /*!
@Function       DevmemIntUnpinInvalidate
@Description    This is the counterpart to DevmemIntPinValidate(). It is meant
                to be called for allocations that ARE mapped in the device
                virtual space and we have to invalidate the mapping.

@Input          psPMR           The physical memory to unpin.

@Return         PVRSRV_ERROR:   PVRSRV_OK on success and the memory is
                                registered to be reclaimed. Error otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR
DevmemIntUnpinInvalidate(DEVMEMINT_MAPPING *psDevmemMapping, PMR *psPMR);

/*************************************************************************/ /*!
@Function       DevmemIntPin
@Description    This is the counterpart to DevmemIntUnpin().
                Is meant to be called if there is NO device mapping present.

@Input          psPMR           The physical memory to pin.

@Return         PVRSRV_ERROR:   PVRSRV_OK on success and the allocation content
                                was successfully restored.

                                PVRSRV_ERROR_PMR_NEW_MEMORY when the content
                                could not be restored and new physical memory
                                was allocated.

                                A different error otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR DevmemIntPin(PMR *psPMR);

/*************************************************************************/ /*!
@Function       DevmemIntPinValidate
@Description    This is the counterpart to DevmemIntUnpinInvalidate().
                Is meant to be called if there is IS a device mapping present
                that needs to be taken care of.

@Input          psDevmemMapping The mapping structure used for the passed PMR.

@Input          psPMR           The physical memory to pin.

@Return         PVRSRV_ERROR:   PVRSRV_OK on success and the allocation content
                                was successfully restored.

                                PVRSRV_ERROR_PMR_NEW_MEMORY when the content
                                could not be restored and new physical memory
                                was allocated.

                                A different error otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR
DevmemIntPinValidate(DEVMEMINT_MAPPING *psDevmemMapping, PMR *psPMR);
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
 * DevmemServerGetContext()
 *
 * For given heap returns the context.
 *
 */
PVRSRV_ERROR
DevmemServerGetContext(DEVMEMINT_HEAP *psDevmemHeap,
                       DEVMEMINT_CTX **ppsDevmemCtxPtr);

/*
 * DevmemServerGetPrivData()
 *
 * For given context returns the private data handle.
 *
 */
PVRSRV_ERROR
DevmemServerGetPrivData(DEVMEMINT_CTX *psDevmemCtx,
                        IMG_HANDLE *phPrivData);

/*
 * DevmemIntAllocDefBackingPage
 *
 * This function allocates default backing page and initializes it
 * with a given default value
 *
 */
PVRSRV_ERROR DevmemIntAllocDefBackingPage(PVRSRV_DEVICE_NODE *psDevNode,
                                            PVRSRV_DEF_PAGE *psDefPage,
                                            IMG_INT	uiInitValue,
                                            IMG_CHAR *pcDefPageName,
                                            IMG_BOOL bInitPage);
/*
 * DevmemIntFreeDefBackingPage
 *
 * Frees a given page
 */
void DevmemIntFreeDefBackingPage(PVRSRV_DEVICE_NODE *psDevNode,
                                   PVRSRV_DEF_PAGE *psDefPage,
                                   IMG_CHAR *pcDefPageName);


/*
 * DevmemIntCtxCreate()
 *
 * Create a Server-side Device Memory Context. This is usually the counterpart
 * of the client side memory context, and indeed is usually created at the
 * same time.
 *
 * You must have one of these before creating any heaps.
 *
 * All heaps must have been destroyed before calling
 * DevmemIntCtxDestroy()
 *
 * If you call DevmemIntCtxCreate() (and it succeeds) you are promising to
 * later call DevmemIntCtxDestroy()
 *
 * Note that this call will cause the device MMU code to do some work for
 * creating the device memory context, but it does not guarantee that a page
 * catalogue will have been created, as this may be deferred until the first
 * allocation.
 *
 * Caller to provide storage for a pointer to the DEVMEM_CTX object that will
 * be created by this call.
 */
PVRSRV_ERROR
DevmemIntCtxCreate(CONNECTION_DATA *psConnection,
                   PVRSRV_DEVICE_NODE *psDeviceNode,
                   /* devnode / perproc etc */
                   IMG_BOOL bKernelMemoryCtx,
                   DEVMEMINT_CTX **ppsDevmemCtxPtr,
                   IMG_HANDLE *hPrivData,
                   IMG_UINT32 *pui32CPUCacheLineSize);
/*
 * DevmemIntCtxDestroy()
 *
 * Undoes a prior DevmemIntCtxCreate or DevmemIntCtxImport.
 */
PVRSRV_ERROR
DevmemIntCtxDestroy(DEVMEMINT_CTX *psDevmemCtx);

/*
 * DevmemIntHeapCreate()
 *
 * Creates a new heap in this device memory context.  This will cause a call
 * into the MMU code to allocate various data structures for managing this
 * heap. It will not necessarily cause any page tables to be set up, as this
 * can be deferred until first allocation. (i.e. we shouldn't care - it's up
 * to the MMU code)
 *
 * Note that the data page size must be specified (as log 2). The data page
 * size as specified here will be communicated to the mmu module, and thus may
 * determine the page size configured in page directory entries for subsequent
 * allocations from this heap. It is essential that the page size here is less
 * than or equal to the "minimum contiguity guarantee" of any PMR that you
 * subsequently attempt to map to this heap.
 *
 * If you call DevmemIntHeapCreate() (and the call succeeds) you are promising
 * that you shall subsequently call DevmemIntHeapDestroy()
 *
 * Caller to provide storage for a pointer to the DEVMEM_HEAP object that will
 * be created by this call.
 */
PVRSRV_ERROR
DevmemIntHeapCreate(DEVMEMINT_CTX *psDevmemCtx,
                    IMG_DEV_VIRTADDR sHeapBaseAddr,
                    IMG_DEVMEM_SIZE_T uiHeapLength,
                    IMG_UINT32 uiLog2DataPageSize,
                    DEVMEMINT_HEAP **ppsDevmemHeapPtr);
/*
 * DevmemIntHeapDestroy()
 *
 * Destroys a heap previously created with DevmemIntHeapCreate()
 *
 * All allocations from his heap must have been freed before this
 * call.
 */
PVRSRV_ERROR
DevmemIntHeapDestroy(DEVMEMINT_HEAP *psDevmemHeap);

/*
 * DevmemIntMapPMR()
 *
 * Maps the given PMR to the virtual range previously allocated with
 * DevmemIntReserveRange()
 *
 * If appropriate, the PMR must have had its physical backing committed, as
 * this call will call into the MMU code to set up the page tables for this
 * allocation, which shall in turn request the physical addresses from the
 * PMR. Alternatively, the PMR implementation can choose to do so off the
 * the back of the "lock" callback, which it will receive as a result
 * (indirectly) of this call.
 *
 * This function makes no promise w.r.t. the circumstances that it can be
 * called, and these would be "inherited" from the implementation of the PMR.
 * For example if the PMR "lock" callback causes pages to be pinned at that
 * time (which may cause scheduling or disk I/O etc.) then it would not be
 * legal to "Map" the PMR in a context where scheduling events are disallowed.
 *
 * If you call DevmemIntMapPMR() (and the call succeeds) then you are promising
 * that you shall later call DevmemIntUnmapPMR()
 */
PVRSRV_ERROR
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
PVRSRV_ERROR
DevmemIntUnmapPMR(DEVMEMINT_MAPPING *psMapping);

/* DevmemIntMapPages()
 *
 * Maps an arbitrary amount of pages from a PMR to a reserved range
 *
 * @input       psReservation      Reservation handle for the range
 * @input       psPMR              PMR that is mapped
 * @input       ui32PageCount      Number of consecutive pages that are
 *                                mapped
 * @input       ui32PhysicalPgOffset Logical offset in the PMR
 * @input       uiFlags            Mapping flags
 * @input       sDevVAddrBase      Virtual address base to start the
 *                                 mapping from
 */
PVRSRV_ERROR
DevmemIntMapPages(DEVMEMINT_RESERVATION *psReservation,
                  PMR *psPMR,
                  IMG_UINT32 ui32PageCount,
                  IMG_UINT32 ui32PhysicalPgOffset,
                  PVRSRV_MEMALLOCFLAGS_T uiFlags,
                  IMG_DEV_VIRTADDR sDevVAddrBase);

/* DevmemIntUnmapPages()
 *
 * Unmaps an arbitrary amount of pages from a reserved range
 *
 * @input       psReservation      Reservation handle for the range
 * @input       sDevVAddrBase      Virtual address base to start from
 * @input       ui32PageCount      Number of consecutive pages that are
 *                                 unmapped
 */
PVRSRV_ERROR
DevmemIntUnmapPages(DEVMEMINT_RESERVATION *psReservation,
                    IMG_DEV_VIRTADDR sDevVAddrBase,
                    IMG_UINT32 ui32PageCount);

/*
 * DevmemIntReserveRange()
 *
 * Indicates that the specified range should be reserved from the given heap.
 *
 * In turn causes the page tables to be allocated to cover the specified range.
 *
 * If you call DevmemIntReserveRange() (and the call succeeds) then you are
 * promising that you shall later call DevmemIntUnreserveRange()
 */
PVRSRV_ERROR
DevmemIntReserveRange(DEVMEMINT_HEAP *psDevmemHeap,
                      IMG_DEV_VIRTADDR sAllocationDevVAddr,
                      IMG_DEVMEM_SIZE_T uiAllocationSize,
                      DEVMEMINT_RESERVATION **ppsReservationPtr);
/*
 * DevmemIntUnreserveRange()
 *
 * Undoes the state change caused by DevmemIntReserveRage()
 */
PVRSRV_ERROR
DevmemIntUnreserveRange(DEVMEMINT_RESERVATION *psDevmemReservation);

/*************************************************************************/ /*!
@Function       DevmemIntChangeSparse
@Description    Changes the sparse allocations of a PMR by allocating and freeing
                pages and changing their corresponding CPU and GPU mappings.

@input          psDevmemHeap          Pointer to the heap we map on
@input          psPMR                 The PMR we want to map
@input          ui32AllocPageCount    Number of pages to allocate
@input          pai32AllocIndices     The logical PMR indices where pages will
                                      be allocated. May be NULL.
@input          ui32FreePageCount     Number of pages to free
@input          pai32FreeIndices      The logical PMR indices where pages will
                                      be freed. May be NULL.
@input          uiSparseFlags         Flags passed in to determine which kind
                                      of sparse change the user wanted.
                                      See devicemem_typedefs.h for details.
@input          uiFlags               Memalloc flags for this virtual range.
@input          sDevVAddrBase         The base address of the virtual range of
                                      this sparse allocation.
@input          sCpuVAddrBase         The CPU base address of this allocation.
                                      May be 0 if not existing.
@Return         PVRSRV_ERROR failure code
*/ /**************************************************************************/
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
                      IMG_UINT64 sCpuVAddrBase);

/*
 * DevmemIntFlushDevSLCRange()
 *
 * Flush specified device context's virtual address range from SLC.
 */
PVRSRV_ERROR
DevmemIntFlushDevSLCRange(DEVMEMINT_CTX *psDevmemCtx,
                          IMG_DEV_VIRTADDR sDevVAddr,
                          IMG_DEVMEM_SIZE_T uiSize,
                          IMG_BOOL bInvalidate);

/*
 * DevmemIntRGXInvalidateFBSCTable()
 *
 * Invalidate selected FBSC table indices.
 *
 */
PVRSRV_ERROR
DevmemIntInvalidateFBSCTable(DEVMEMINT_CTX *psDevmemCtx,
                             IMG_UINT64 ui64FBSCEntryMask);

PVRSRV_ERROR
DevmemIntIsVDevAddrValid(CONNECTION_DATA * psConnection,
                         PVRSRV_DEVICE_NODE *psDevNode,
                         DEVMEMINT_CTX *psDevMemContext,
                         IMG_DEV_VIRTADDR sDevAddr);

PVRSRV_ERROR
DevmemIntGetFaultAddress(CONNECTION_DATA * psConnection,
                         PVRSRV_DEVICE_NODE *psDevNode,
                         DEVMEMINT_CTX *psDevMemContext,
                         IMG_DEV_VIRTADDR *psFaultAddress);

/*************************************************************************/ /*!
@Function       DevmemIntRegisterPFNotifyKM
@Description    Registers a PID to be notified when a page fault occurs on a
                specific device memory context.
@Input          psDevmemCtx    The context to be notified about.
@Input          ui32PID        The PID of the process that would like to be
                               notified.
@Input          bRegister      If true, register. If false, de-register.
@Return         PVRSRV_ERROR
*/ /**************************************************************************/
PVRSRV_ERROR
DevmemIntRegisterPFNotifyKM(DEVMEMINT_CTX *psDevmemCtx,
                            IMG_INT32     ui32PID,
                            IMG_BOOL      bRegister);

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
                               IMG_DEV_VIRTADDR   sFaultAddress);

#if defined(PDUMP)
/*
 * DevmemIntPDumpSaveToFileVirtual()
 *
 * Writes out PDump "SAB" commands with the data found in memory at
 * the given virtual address.
 */
PVRSRV_ERROR
DevmemIntPDumpSaveToFileVirtual(DEVMEMINT_CTX *psDevmemCtx,
                                IMG_DEV_VIRTADDR sDevAddrStart,
                                IMG_DEVMEM_SIZE_T uiSize,
                                IMG_UINT32 uiArraySize,
                                const IMG_CHAR *pszFilename,
                                IMG_UINT32 ui32FileOffset,
                                IMG_UINT32 ui32PDumpFlags);

IMG_UINT32
DevmemIntMMUContextID(DEVMEMINT_CTX *psDevMemContext);

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
                     IMG_UINT32 ui32PDumpFlags);

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
                              IMG_UINT32 ui32PDumpFlags);

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
                             IMG_UINT32 ui32PDumpFlags);
#else /* PDUMP */

#ifdef INLINE_IS_PRAGMA
#pragma inline(DevmemIntPDumpSaveToFileVirtual)
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
#pragma inline(DevmemIntPDumpBitmap)
#endif
static INLINE PVRSRV_ERROR
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
	PVR_UNREFERENCED_PARAMETER(psConnection);
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

#ifdef INLINE_IS_PRAGMA
#pragma inline(DevmemIntPDumpImageDescriptor)
#endif
static INLINE PVRSRV_ERROR
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
	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	PVR_UNREFERENCED_PARAMETER(psDevMemContext);
	PVR_UNREFERENCED_PARAMETER(ui32Size);
	PVR_UNREFERENCED_PARAMETER(pszFileName);
	PVR_UNREFERENCED_PARAMETER(sData);
	PVR_UNREFERENCED_PARAMETER(ui32DataSize);
	PVR_UNREFERENCED_PARAMETER(ui32LogicalWidth);
	PVR_UNREFERENCED_PARAMETER(ui32LogicalHeight);
	PVR_UNREFERENCED_PARAMETER(ui32PhysicalWidth);
	PVR_UNREFERENCED_PARAMETER(ui32PhysicalHeight);
	PVR_UNREFERENCED_PARAMETER(ePixFmt);
	PVR_UNREFERENCED_PARAMETER(eMemLayout);
	PVR_UNREFERENCED_PARAMETER(eFBCompression);
	PVR_UNREFERENCED_PARAMETER(paui32FBCClearColour);
	PVR_UNREFERENCED_PARAMETER(eFBCSwizzle);
	PVR_UNREFERENCED_PARAMETER(sHeader);
	PVR_UNREFERENCED_PARAMETER(ui32HeaderSize);
	PVR_UNREFERENCED_PARAMETER(ui32PDumpFlags);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(DevmemIntPDumpDataDescriptor)
#endif
static INLINE PVRSRV_ERROR
DevmemIntPDumpDataDescriptor(CONNECTION_DATA * psConnection,
                              PVRSRV_DEVICE_NODE *psDeviceNode,
                              DEVMEMINT_CTX *psDevMemContext,
                              IMG_UINT32 ui32Size,
                              const IMG_CHAR *pszFileName,
                              IMG_DEV_VIRTADDR sData,
                              IMG_UINT32 ui32DataSize,
                              IMG_UINT32 ui32ElementType,
                              IMG_UINT32 ui32ElementCount,
                              IMG_UINT32 ui32PDumpFlags)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	PVR_UNREFERENCED_PARAMETER(psDevMemContext);
	PVR_UNREFERENCED_PARAMETER(ui32Size);
	PVR_UNREFERENCED_PARAMETER(pszFileName);
	PVR_UNREFERENCED_PARAMETER(sData);
	PVR_UNREFERENCED_PARAMETER(ui32DataSize);
	PVR_UNREFERENCED_PARAMETER(ui32ElementType);
	PVR_UNREFERENCED_PARAMETER(ui32ElementCount);
	PVR_UNREFERENCED_PARAMETER(ui32PDumpFlags);
	return PVRSRV_OK;
}

#endif /* PDUMP */

PVRSRV_ERROR
DevmemIntInit(void);

PVRSRV_ERROR
DevmemIntDeInit(void);

PVRSRV_ERROR
DevmemIntExportCtx(DEVMEMINT_CTX *psContext,
                   PMR *psPMR,
                   DEVMEMINT_CTX_EXPORT **ppsContextExport);

PVRSRV_ERROR
DevmemIntUnexportCtx(DEVMEMINT_CTX_EXPORT *psContextExport);

PVRSRV_ERROR
DevmemIntAcquireRemoteCtx(PMR *psPMR,
                          DEVMEMINT_CTX **ppsContext,
                          IMG_HANDLE *phPrivData);

#endif /* DEVICEMEM_SERVER_H */
