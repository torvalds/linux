/*************************************************************************/ /*!
@File
@Title          Device Memory Management core internal
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Services internal interface to core device memory management
                functions that are shared between client and server code.
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

#ifndef SRVCLIENT_DEVICEMEM_H
#define SRVCLIENT_DEVICEMEM_H

/******************************************************************************
 *                                                                            *
 *  +------------+   +------------+      +--------------+   +--------------+  *
 *  | a   sub-   |   | a   sub-   |      |  an          |   | allocation   |  *
 *  | allocation |   | allocation |      |  allocation  |   | also mapped  |  *
 *  |            |   |            |      |  in proc 1   |   | into proc 2  |  *
 *  +------------+   +------------+      +--------------+   +--------------+  *
 *            |         |                       |                  |          *
 *         +--------------+              +--------------+   +--------------+  *
 *         | page   gran- |              | page   gran- |   | page   gran- |  *
 *         | ular mapping |              | ular mapping |   | ular mapping |  *
 *         +--------------+              +--------------+   +--------------+  *
 *                |                                   |       |               *
 *                |                                   |       |               *
 *                |                                   |       |               *
 *         +--------------+                       +--------------+            *
 *         |              |                       |              |            *
 *         | A  "P.M.R."  |                       | A  "P.M.R."  |            *
 *         |              |                       |              |            *
 *         +--------------+                       +--------------+            *
 *                                                                            *
 ******************************************************************************/

/*
    All device memory allocations are ultimately a view upon (not
    necessarily the whole of) a "PMR".

    A PMR is a "Physical Memory Resource", which may be a
    "pre-faulted" lump of physical memory, or it may be a
    representation of some physical memory that will be instantiated
    at some future time.

    PMRs always represent multiple of some power-of-2 "contiguity"
    promised by the PMR, which will allow them to be mapped in whole
    pages into the device MMU.  As memory allocations may be smaller
    than a page, these mappings may be suballocated and thus shared
    between multiple allocations in one process.  A PMR may also be
    mapped simultaneously into multiple device memory contexts
    (cross-process scenario), however, for security reasons, it is not
    legal to share a PMR "both ways" at once, that is, mapped into
    multiple processes and divided up amongst several suballocations.

    This PMR terminology is introduced here for background
    information, but is generally of little concern to the caller of
    this API.  This API handles suballocations and mappings, and the
    caller thus deals primarily with MEMORY DESCRIPTORS representing
    an allocation or suballocation, HEAPS representing ranges of
    virtual addresses in a CONTEXT.
*/

/*
   |<---------------------------context------------------------------>|
   |<-------heap------->|   |<-------heap------->|<-------heap------->|
   |<-alloc->|          |   |<-alloc->|<-alloc->||   |<-alloc->|      |
*/

#include "img_types.h"
#include "img_defs.h"
#include "devicemem_typedefs.h"
#include "pdumpdefs.h"
#include "pvrsrv_error.h"
#include "pvrsrv_memallocflags.h"

#include "pdump.h"

#include "device_connection.h"


typedef IMG_UINT32 DEVMEM_HEAPCFGID;
#define DEVMEM_HEAPCFG_FORCLIENTS 0
#define DEVMEM_HEAPCFG_META 1


/*
  In order to call the server side functions, we need a bridge handle.
  We abstract that here, as we may wish to change its form.
 */

typedef IMG_HANDLE DEVMEM_BRIDGE_HANDLE;

/*************************************************************************/ /*!
@Function       DevmemUnpin
@Description    This is the counterpart to DevmemPin(). It is meant to be
                called before repinning an allocation.

                For a detailed description see client API documentation.

@Input          phMemDesc       The MemDesc that is going to be unpinned.

@Return         PVRSRV_ERROR:   PVRSRV_OK on success and the memory is
                                registered to be reclaimed. Error otherwise.
*/ /**************************************************************************/
IMG_INTERNAL PVRSRV_ERROR
DevmemUnpin(DEVMEM_MEMDESC *psMemDesc);

/*************************************************************************/ /*!
@Function       DevmemPin
@Description    This is the counterpart to DevmemUnpin(). It is meant to be
                called after unpinning an allocation.

                For a detailed description see client API documentation.

@Input          phMemDesc       The MemDesc that is going to be pinned.

@Return         PVRSRV_ERROR:   PVRSRV_OK on success and the allocation content
                                was successfully restored.

                                PVRSRV_ERROR_PMR_NEW_MEMORY when the content
                                could not be restored and new physical memory
                                was allocated.

                                A different error otherwise.
*/ /**************************************************************************/
IMG_INTERNAL PVRSRV_ERROR
DevmemPin(DEVMEM_MEMDESC *psMemDesc);

IMG_INTERNAL PVRSRV_ERROR
DevmemGetHeapInt(DEVMEM_HEAP *psHeap,
				 IMG_HANDLE *phDevmemHeap);

IMG_INTERNAL PVRSRV_ERROR
DevmemGetSize(DEVMEM_MEMDESC *psMemDesc,
			  IMG_DEVMEM_SIZE_T* puiSize);

IMG_INTERNAL void
DevmemGetAnnotation(DEVMEM_MEMDESC *psMemDesc,
			        IMG_CHAR **pszAnnotation);

/*
 * DevmemCreateContext()
 *
 * Create a device memory context
 *
 * This must be called before any heap is created in this context
 *
 * Caller to provide bridge handle which will be recorded internally and used
 * for all future operations on items from this memory context.  Caller also
 * to provide devicenode handle, as this is used for MMU configuration and
 * also to determine the heap configuration for the auto-instantiated heaps.
 *
 * Note that when compiled in services/server, the hBridge is not used and
 * is thrown away by the "fake" direct bridge.  (This may change. It is
 * recommended that NULL be passed for the handle for now.)
 *
 * hDeviceNode and uiHeapBlueprintID shall together dictate which heap-config
 * to use.
 *
 * This will cause the server side counterpart to be created also.
 *
 * If you call DevmemCreateContext() (and the call succeeds) you are promising
 * that you will later call Devmem_ContextDestroy(), except for abnormal
 * process termination in which case it is expected it will be destroyed as
 * part of handle clean up.
 *
 * Caller to provide storage for the pointer to the newly created
 * NEWDEVMEM_CONTEXT object.
 */
PVRSRV_ERROR
DevmemCreateContext(SHARED_DEV_CONNECTION hDevConnection,
                    DEVMEM_HEAPCFGID uiHeapBlueprintID,
                    DEVMEM_CONTEXT **ppsCtxPtr);

/*
 * DevmemAcquireDevPrivData()
 *
 * Acquire the device private data for this memory context
 */
PVRSRV_ERROR
DevmemAcquireDevPrivData(DEVMEM_CONTEXT *psCtx,
                         IMG_HANDLE *hPrivData);

/*
 * DevmemReleaseDevPrivData()
 *
 * Release the device private data for this memory context
 */
PVRSRV_ERROR
DevmemReleaseDevPrivData(DEVMEM_CONTEXT *psCtx);

/*
 * DevmemDestroyContext()
 *
 * Undoes that done by DevmemCreateContext()
 */
PVRSRV_ERROR
DevmemDestroyContext(DEVMEM_CONTEXT *psCtx);

/*
 * DevmemCreateHeap()
 *
 * Create a heap in the given context.
 *
 * N.B.  Not intended to be called directly, though it can be.
 * Normally, heaps are instantiated at context creation time according
 * to the specified blueprint.  See DevmemCreateContext() for details.
 *
 * This will cause MMU code to set up data structures for the heap,
 * but may not cause page tables to be modified until allocations are
 * made from the heap.
 *
 * uiReservedRegionLength Reserved address space for static VAs shared
 * between clients and firmware
 *
 * The "Quantum" is both the device MMU page size to be configured for
 * this heap, and the unit multiples of which "quantized" allocations
 * are made (allocations smaller than this, known as "suballocations"
 * will be made from a "sub alloc RA" and will "import" chunks
 * according to this quantum)
 *
 * Where imported PMRs (or, for example, PMRs created by device class
 * buffers) are mapped into this heap, it is important that the
 * physical contiguity guarantee offered by the PMR is greater than or
 * equal to the quantum size specified here, otherwise the attempt to
 * map it will fail.  "Normal" allocations via Devmem_Allocate
 * shall automatically meet this requirement, as each "import" will
 * trigger the creation of a PMR with the desired contiguity.  The
 * supported quantum sizes in that case shall be dictated by the OS
 * specific implementation of PhysmemNewOSRamBackedPMR() (see)
 */
PVRSRV_ERROR
DevmemCreateHeap(DEVMEM_CONTEXT *psCtxPtr,
                 /* base and length of heap */
                 IMG_DEV_VIRTADDR sBaseAddress,
                 IMG_DEVMEM_SIZE_T uiLength,
                 IMG_DEVMEM_SIZE_T uiReservedRegionLength,
                 /* log2 of allocation quantum, i.e. "page" size.
                    All allocations (that go to server side) are
                    multiples of this.  We use a client-side RA to
                    make sub-allocations from this */
                 IMG_UINT32 ui32Log2Quantum,
                 /* The minimum import alignment for this heap */
                 IMG_UINT32 ui32Log2ImportAlignment,
                 /* Name of heap for debug */
                 /* N.B.  Okay to exist on caller's stack - this
                    func takes a copy if it needs it. */
                 const IMG_CHAR *pszName,
                 DEVMEM_HEAPCFGID uiHeapBlueprintID,
                 DEVMEM_HEAP **ppsHeapPtr);
/*
 * DevmemDestroyHeap()
 *
 * Reverses DevmemCreateHeap()
 *
 * N.B. All allocations must have been freed and all mappings must
 * have been unmapped before invoking this call
 */
PVRSRV_ERROR
DevmemDestroyHeap(DEVMEM_HEAP *psHeap);

/*
 * DevmemExportalignAdjustSizeAndAlign()
 * Compute the Size and Align passed to avoid suballocations
 * (used when allocation with PVRSRV_MEMALLOCFLAG_EXPORTALIGN).
 *
 * Returns PVRSRV_ERROR_INVALID_PARAMS if uiLog2Quantum has invalid value.
 */
IMG_INTERNAL PVRSRV_ERROR
DevmemExportalignAdjustSizeAndAlign(IMG_UINT32 uiLog2Quantum,
                                    IMG_DEVMEM_SIZE_T *puiSize,
                                    IMG_DEVMEM_ALIGN_T *puiAlign);

/*
 * DevmemSubAllocate()
 *
 * Makes an allocation (possibly a "suballocation", as described
 * below) of device virtual memory from this heap.
 *
 * The size and alignment of the allocation will be honoured by the RA
 * that allocates the "suballocation".  The resulting allocation will
 * be mapped into GPU virtual memory and the physical memory to back
 * it will exist, by the time this call successfully completes.
 *
 * The size must be a positive integer multiple of the alignment.
 * (i.e. the alignment specifies the alignment of both the start and
 * the end of the resulting allocation.)
 *
 * Allocations made via this API are routed through a "suballocation
 * RA" which is responsible for ensuring that small allocations can be
 * made without wasting physical memory in the server.  Furthermore,
 * such suballocations can be made entirely client side without
 * needing to go to the server unless the allocation spills into a new
 * page.
 *
 * Such suballocations cause many allocations to share the same "PMR".
 * This happens only when the flags match exactly.
 *
 */

PVRSRV_ERROR
DevmemSubAllocate(IMG_UINT8 uiPreAllocMultiplier,
                  DEVMEM_HEAP *psHeap,
                  IMG_DEVMEM_SIZE_T uiSize,
                  IMG_DEVMEM_ALIGN_T uiAlign,
                  PVRSRV_MEMALLOCFLAGS_T uiFlags,
                  const IMG_CHAR *pszText,
                  DEVMEM_MEMDESC **ppsMemDescPtr);

#define DevmemAllocate(...) \
    DevmemSubAllocate(DEVMEM_NO_PRE_ALLOCATE_MULTIPLIER, __VA_ARGS__)

PVRSRV_ERROR
DevmemAllocateExportable(SHARED_DEV_CONNECTION hDevConnection,
                         IMG_DEVMEM_SIZE_T uiSize,
                         IMG_DEVMEM_ALIGN_T uiAlign,
                         IMG_UINT32 uiLog2HeapPageSize,
                         PVRSRV_MEMALLOCFLAGS_T uiFlags,
                         const IMG_CHAR *pszText,
                         DEVMEM_MEMDESC **ppsMemDescPtr);

PVRSRV_ERROR
DeviceMemChangeSparse(DEVMEM_MEMDESC *psMemDesc,
                      IMG_UINT32 ui32AllocPageCount,
                      IMG_UINT32 *paui32AllocPageIndices,
                      IMG_UINT32 ui32FreePageCount,
                      IMG_UINT32 *pauiFreePageIndices,
                      SPARSE_MEM_RESIZE_FLAGS uiFlags);

PVRSRV_ERROR
DevmemAllocateSparse(SHARED_DEV_CONNECTION hDevConnection,
                     IMG_DEVMEM_SIZE_T uiSize,
                     IMG_DEVMEM_SIZE_T uiChunkSize,
                     IMG_UINT32 ui32NumPhysChunks,
                     IMG_UINT32 ui32NumVirtChunks,
                     IMG_UINT32 *pui32MappingTable,
                     IMG_DEVMEM_ALIGN_T uiAlign,
                     IMG_UINT32 uiLog2HeapPageSize,
                     PVRSRV_MEMALLOCFLAGS_T uiFlags,
                     const IMG_CHAR *pszText,
                     DEVMEM_MEMDESC **ppsMemDescPtr);

PVRSRV_ERROR
DevmemSubAllocateAndMap(IMG_UINT8 uiPreAllocMultiplier,
			DEVMEM_HEAP *psHeap,
			IMG_DEVMEM_SIZE_T uiSize,
			IMG_DEVMEM_ALIGN_T uiAlign,
			PVRSRV_MEMALLOCFLAGS_T uiFlags,
			const IMG_CHAR *pszText,
			DEVMEM_MEMDESC **ppsMemDescPtr,
			IMG_DEV_VIRTADDR *psDevVirtAddr);

#define DevmemAllocateAndMap(...) \
	DevmemSubAllocateAndMap(DEVMEM_NO_PRE_ALLOCATE_MULTIPLIER, __VA_ARGS__)

/*
 * DevmemFree()
 *
 * Reverses that done by DevmemSubAllocate() N.B.  The underlying
 * mapping and server side allocation _may_ not be torn down, for
 * example, if the allocation has been exported, or if multiple
 * allocations were suballocated from the same mapping, but this is
 * properly refcounted, so the caller does not have to care.
 */

IMG_BOOL
DevmemFree(DEVMEM_MEMDESC *psMemDesc);

IMG_BOOL
DevmemReleaseDevAddrAndFree(DEVMEM_MEMDESC *psMemDesc);

/*
	DevmemMapToDevice:

	Map an allocation to the device it was allocated from.
	This function _must_ be called before any call to
	DevmemAcquireDevVirtAddr is made as it binds the allocation
	to the heap.
	DevmemReleaseDevVirtAddr is used to release the reference
	to the device mapping this function created, but it doesn't
	mean that the memory will actually be unmapped from the
	device as other references to the mapping obtained via
	DevmemAcquireDevVirtAddr could still be active.
*/
PVRSRV_ERROR DevmemMapToDevice(DEVMEM_MEMDESC *psMemDesc,
							   DEVMEM_HEAP *psHeap,
							   IMG_DEV_VIRTADDR *psDevVirtAddr);

/*
	DevmemMapToDeviceAddress:

	Same as DevmemMapToDevice but the caller chooses the address
	to map to.
*/
IMG_INTERNAL PVRSRV_ERROR
DevmemMapToDeviceAddress(DEVMEM_MEMDESC *psMemDesc,
                         DEVMEM_HEAP *psHeap,
                         IMG_DEV_VIRTADDR sDevVirtAddr);

/*
	DevmemGetDevVirtAddr

	Obtain the MemDesc's device virtual address.
	This function _must_ be called after DevmemMapToDevice(Address)
	and is expected to be used be functions which didn't allocate
	the MemDesc but need to know it's address.
	It will PVR_ASSERT if no device mapping exists and 0 is returned.
 */
IMG_DEV_VIRTADDR
DevmemGetDevVirtAddr(DEVMEM_MEMDESC *psMemDesc);

/*
	DevmemAcquireDevVirtAddr

	Acquire the MemDesc's device virtual address.
	This function _must_ be called after DevmemMapToDevice
	and is expected to be used be functions which didn't allocate
	the MemDesc but need to know it's address
 */
PVRSRV_ERROR DevmemAcquireDevVirtAddr(DEVMEM_MEMDESC *psMemDesc,
                                      IMG_DEV_VIRTADDR *psDevVirtAddrRet);

/*
 * DevmemReleaseDevVirtAddr()
 *
 * give up the licence to use the device virtual address that was
 * acquired by "Acquire" or "MapToDevice"
 */
void
DevmemReleaseDevVirtAddr(DEVMEM_MEMDESC *psMemDesc);

/*
 * DevmemAcquireCpuVirtAddr()
 *
 * Acquires a license to use the cpu virtual address of this mapping.
 * Note that the memory may not have been mapped into cpu virtual
 * memory prior to this call.  On first "acquire" the memory will be
 * mapped in (if it wasn't statically mapped in) and on last put it
 * _may_ become unmapped.  Later calling "Acquire" again, _may_ cause
 * the memory to be mapped at a different address.
 */
PVRSRV_ERROR DevmemAcquireCpuVirtAddr(DEVMEM_MEMDESC *psMemDesc,
                                      void **ppvCpuVirtAddr);

/*
 * DevmemReacquireCpuVirtAddr()
 *
 * (Re)acquires license to use the cpu virtual address of this mapping
 * if (and only if) there is already a pre-existing license to use the
 * cpu virtual address for the mapping, returns NULL otherwise.
 */
void DevmemReacquireCpuVirtAddr(DEVMEM_MEMDESC *psMemDesc,
                                void **ppvCpuVirtAddr);

/*
 * DevmemReleaseDevVirtAddr()
 *
 * give up the licence to use the cpu virtual address that was granted
 * with the "Get" call.
 */
void
DevmemReleaseCpuVirtAddr(DEVMEM_MEMDESC *psMemDesc);

#if defined(SUPPORT_INSECURE_EXPORT)
/*
 * DevmemExport()
 *
 * Given a memory allocation allocated with DevmemAllocateExportable()
 * create a "cookie" that can be passed intact by the caller's own choice
 * of secure IPC to another process and used as the argument to "map"
 * to map this memory into a heap in the target processes.  N.B.  This can
 * also be used to map into multiple heaps in one process, though that's not
 * the intention.
 *
 * Note, the caller must later call Unexport before freeing the
 * memory.
 */
PVRSRV_ERROR DevmemExport(DEVMEM_MEMDESC *psMemDesc,
                          DEVMEM_EXPORTCOOKIE *psExportCookie);


void DevmemUnexport(DEVMEM_MEMDESC *psMemDesc,
					DEVMEM_EXPORTCOOKIE *psExportCookie);

PVRSRV_ERROR
DevmemImport(SHARED_DEV_CONNECTION hDevConnection,
			 DEVMEM_EXPORTCOOKIE *psCookie,
			 PVRSRV_MEMALLOCFLAGS_T uiFlags,
			 DEVMEM_MEMDESC **ppsMemDescPtr);
#endif /* SUPPORT_INSECURE_EXPORT */

/*
 * DevmemMakeLocalImportHandle()
 *
 * This is a "special case" function for making a server export cookie
 * which went through the direct bridge into an export cookie that can
 * be passed through the client bridge.
 */
PVRSRV_ERROR
DevmemMakeLocalImportHandle(SHARED_DEV_CONNECTION hDevConnection,
                            IMG_HANDLE hServerExport,
                            IMG_HANDLE *hClientExport);

/*
 * DevmemUnmakeLocalImportHandle()
 *
 * Free any resource associated with the Make operation
 */
PVRSRV_ERROR
DevmemUnmakeLocalImportHandle(SHARED_DEV_CONNECTION hDevConnection,
                              IMG_HANDLE hClientExport);

/*
 *
 * The following set of functions is specific to the heap "blueprint"
 * stuff, for automatic creation of heaps when a context is created
 *
 */


/* Devmem_HeapConfigCount: returns the number of heap configs that
   this device has.  Note that there is no acquire/release semantics
   required, as this data is guaranteed to be constant for the
   lifetime of the device node */
PVRSRV_ERROR
DevmemHeapConfigCount(SHARED_DEV_CONNECTION hDevConnection,
                      IMG_UINT32 *puiNumHeapConfigsOut);

/* Devmem_HeapCount: returns the number of heaps that a given heap
   config on this device has.  Note that there is no acquire/release
   semantics required, as this data is guaranteed to be constant for
   the lifetime of the device node */
PVRSRV_ERROR
DevmemHeapCount(SHARED_DEV_CONNECTION hDevConnection,
                IMG_UINT32 uiHeapConfigIndex,
                IMG_UINT32 *puiNumHeapsOut);
/* Devmem_HeapConfigName: return the name of the given heap config.
   The caller is to provide the storage for the returned string and
   indicate the number of bytes (including null terminator) for such
   string in the BufSz arg.  Note that there is no acquire/release
   semantics required, as this data is guaranteed to be constant for
   the lifetime of the device node.
 */
PVRSRV_ERROR
DevmemHeapConfigName(SHARED_DEV_CONNECTION hsDevConnection,
                     IMG_UINT32 uiHeapConfigIndex,
                     IMG_CHAR *pszConfigNameOut,
                     IMG_UINT32 uiConfigNameBufSz);

/* Devmem_HeapDetails: fetches all the metadata that is recorded in
   this heap "blueprint".  Namely: heap name (caller to provide
   storage, and indicate buffer size (including null terminator) in
   BufSz arg), device virtual address and length, log2 of data page
   size (will be one of 12, 14, 16, 18, 20, 21, at time of writing).
   Note that there is no acquire/release semantics required, as this
   data is guaranteed to be constant for the lifetime of the device
   node. */
PVRSRV_ERROR
DevmemHeapDetails(SHARED_DEV_CONNECTION hDevConnection,
                  IMG_UINT32 uiHeapConfigIndex,
                  IMG_UINT32 uiHeapIndex,
                  IMG_CHAR *pszHeapNameOut,
                  IMG_UINT32 uiHeapNameBufSz,
                  IMG_DEV_VIRTADDR *psDevVAddrBaseOut,
                  IMG_DEVMEM_SIZE_T *puiHeapLengthOut,
                  IMG_DEVMEM_SIZE_T *puiReservedRegionLengthOut,
                  IMG_UINT32 *puiLog2DataPageSize,
                  IMG_UINT32 *puiLog2ImportAlignmentOut);

/*
 * Devmem_FindHeapByName()
 *
 * returns the heap handle for the named _automagic_ heap in this
 * context.  "automagic" heaps are those that are born with the
 * context from a blueprint
 */
PVRSRV_ERROR
DevmemFindHeapByName(const DEVMEM_CONTEXT *psCtx,
                     const IMG_CHAR *pszHeapName,
                     DEVMEM_HEAP **ppsHeapRet);

/*
 * DevmemGetHeapBaseDevVAddr()
 *
 * returns the device virtual address of the base of the heap.
 */

PVRSRV_ERROR
DevmemGetHeapBaseDevVAddr(DEVMEM_HEAP *psHeap,
			  IMG_DEV_VIRTADDR *pDevVAddr);

PVRSRV_ERROR
DevmemLocalGetImportHandle(DEVMEM_MEMDESC *psMemDesc,
			   IMG_HANDLE *phImport);

PVRSRV_ERROR
DevmemGetImportUID(DEVMEM_MEMDESC *psMemDesc,
		   IMG_UINT64 *pui64UID);

PVRSRV_ERROR
DevmemGetReservation(DEVMEM_MEMDESC *psMemDesc,
		     IMG_HANDLE *hReservation);

IMG_INTERNAL PVRSRV_ERROR
DevmemGetPMRData(DEVMEM_MEMDESC *psMemDesc,
		IMG_HANDLE *hPMR,
		IMG_DEVMEM_OFFSET_T *puiPMROffset);

IMG_INTERNAL void
DevmemGetFlags(DEVMEM_MEMDESC *psMemDesc,
				PVRSRV_MEMALLOCFLAGS_T *puiFlags);

IMG_INTERNAL SHARED_DEV_CONNECTION
DevmemGetConnection(DEVMEM_MEMDESC *psMemDesc);

PVRSRV_ERROR
DevmemLocalImport(SHARED_DEV_CONNECTION hDevConnection,
				  IMG_HANDLE hExtHandle,
				  PVRSRV_MEMALLOCFLAGS_T uiFlags,
				  DEVMEM_MEMDESC **ppsMemDescPtr,
				  IMG_DEVMEM_SIZE_T *puiSizePtr,
				  const IMG_CHAR *pszAnnotation);

IMG_INTERNAL PVRSRV_ERROR
DevmemIsDevVirtAddrValid(DEVMEM_CONTEXT *psContext,
                         IMG_DEV_VIRTADDR sDevVAddr);

IMG_INTERNAL PVRSRV_ERROR
DevmemGetFaultAddress(DEVMEM_CONTEXT *psContext,
                      IMG_DEV_VIRTADDR *psFaultAddress);

IMG_INTERNAL PVRSRV_ERROR
DevmemFlushDeviceSLCRange(DEVMEM_MEMDESC *psMemDesc,
                          IMG_DEV_VIRTADDR sDevVAddr,
                          IMG_DEVMEM_SIZE_T uiSize,
                          IMG_BOOL bInvalidate);

IMG_INTERNAL PVRSRV_ERROR
DevmemInvalidateFBSCTable(DEVMEM_CONTEXT *psContext,
                          IMG_UINT64 ui64FBSCEntries);

/* DevmemGetHeapLog2PageSize()
 *
 * Get the page size used for a certain heap.
 */
IMG_UINT32
DevmemGetHeapLog2PageSize(DEVMEM_HEAP *psHeap);

/* DevmemGetHeapReservedSize()
 *
 * Get the reserved size used for a certain heap.
 */
IMG_DEVMEM_SIZE_T
DevmemGetHeapReservedSize(DEVMEM_HEAP *psHeap);

/*************************************************************************/ /*!
@Function       RegisterDevMemPFNotify
@Description    Registers that the application wants to be signaled when a page
                fault occurs.

@Input          psContext      Memory context the process that would like to
                               be notified about.
@Input          ui32PID        The PID  of the calling process.
@Input          bRegister      If true, register. If false, de-register.
@Return         PVRSRV_ERROR:  PVRSRV_OK on success. Otherwise, a PVRSRV_
                               error code
*/ /**************************************************************************/
IMG_INTERNAL PVRSRV_ERROR
RegisterDevmemPFNotify(DEVMEM_CONTEXT *psContext,
                       IMG_UINT32     ui32PID,
                       IMG_BOOL       bRegister);

/*************************************************************************/ /*!
@Function       GetMaxDevMemSize
@Description    Get the amount of device memory on current platform
                (memory size in Bytes)
@Output         puiLMASize            LMA memory size
@Output         puiUMASize            UMA memory size
@Return         Error code
*/ /**************************************************************************/
IMG_INTERNAL PVRSRV_ERROR
GetMaxDevMemSize(SHARED_DEV_CONNECTION hDevConnection,
		 IMG_DEVMEM_SIZE_T *puiLMASize,
		 IMG_DEVMEM_SIZE_T *puiUMASize);

/*************************************************************************/ /*!
@Function       DevmemHeapSetPremapStatus
@Description    In some special cases like virtualisation, a device memory heap
			    must be entirely backed by physical memory and mapped into the
				device's virtual address space. This is done at context creation.
			    When objects are allocated from such a heap, the mapping part
			    must be skipped. The 'bPremapped' flag dictates if allocations
			    are to be mapped or not.

@Input          psHeap            Device memory heap to be updated
@Input          IsPremapped       The premapping status to be set
*/ /**************************************************************************/
IMG_INTERNAL void
DevmemHeapSetPremapStatus(DEVMEM_HEAP *psHeap, IMG_BOOL IsPremapped);

#endif /* #ifndef SRVCLIENT_DEVICEMEM_H */
