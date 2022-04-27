/*************************************************************************/ /*!
@File
@Title          Device Memory Management internal utility functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Utility functions used internally by device memory management
                code.
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

#ifndef DEVICEMEM_UTILS_H
#define DEVICEMEM_UTILS_H

#include "devicemem.h"
#include "img_types.h"
#include "pvrsrv_error.h"
#include "pvr_debug.h"
#include "allocmem.h"
#include "ra.h"
#include "osfunc.h"
#include "lock.h"
#include "osmmap.h"

#define DEVMEM_HEAPNAME_MAXLENGTH 160

/*
 * Reserved VA space of a heap must always be multiple of DEVMEM_HEAP_RESERVED_SIZE_GRANULARITY,
 * this check is validated in the DDK. Note this is only reserving "Virtual Address" space and
 * physical allocations (and mappings thereon) should only be done as much as required (to avoid
 * wastage).
 * Granularity has been chosen to support the max possible practically used OS page size.
 */
#define DEVMEM_HEAP_RESERVED_SIZE_GRANULARITY        0x10000 /* 64KB is MAX anticipated OS page size */

/*
 * VA heap size should be at least OS page size. This check is validated in the DDK.
 */
#define DEVMEM_HEAP_MINIMUM_SIZE                     0x10000 /* 64KB is MAX anticipated OS page size */

#if defined(DEVMEM_DEBUG) && defined(REFCOUNT_DEBUG)
#define DEVMEM_REFCOUNT_PRINT(fmt, ...) PVRSRVDebugPrintf(PVR_DBG_ERROR, __FILE__, __LINE__, fmt, __VA_ARGS__)
#else
#define DEVMEM_REFCOUNT_PRINT(fmt, ...)
#endif

/* If we need a "hMapping" but we don't have a server-side mapping, we poison
 * the entry with this value so that it's easily recognised in the debugger.
 * Note that this is potentially a valid handle, but then so is NULL, which is
 * no better, indeed worse, as it's not obvious in the debugger. The value
 * doesn't matter. We _never_ use it (and because it's valid, we never assert
 * it isn't this) but it's nice to have a value in the source code that we can
 * grep for if things go wrong.
 */
#define LACK_OF_MAPPING_POISON ((IMG_HANDLE)0x6116dead)
#define LACK_OF_RESERVATION_POISON ((IMG_HANDLE)0x7117dead)

#define DEVICEMEM_HISTORY_ALLOC_INDEX_NONE 0xFFFFFFFF

struct DEVMEM_CONTEXT_TAG
{

	SHARED_DEV_CONNECTION hDevConnection;

	/* Number of heaps that have been created in this context
	 * (regardless of whether they have allocations)
	 */
	IMG_UINT32 uiNumHeaps;

	/* Each "DEVMEM_CONTEXT" has a counterpart in the server, which
	 * is responsible for handling the mapping into device MMU.
	 * We have a handle to that here.
	 */
	IMG_HANDLE hDevMemServerContext;

	/* Number of automagically created heaps in this context,
	 *  i.e. those that are born at context creation time from the
	 * chosen "heap config" or "blueprint"
	 */
	IMG_UINT32 uiAutoHeapCount;

	/* Pointer to array of such heaps */
	struct DEVMEM_HEAP_TAG **ppsAutoHeapArray;

	/* The cache line size for use when allocating memory,
	 * as it is not queryable on the client side
	 */
	IMG_UINT32 ui32CPUCacheLineSize;

	/* Private data handle for device specific data */
	IMG_HANDLE hPrivData;
};

/* Flags that record how a heaps virtual address space is managed. */
#define DEVMEM_HEAP_MANAGER_UNKNOWN      0
/* Heap VAs assigned by the client of Services APIs, heap's RA not used at all. */
#define DEVMEM_HEAP_MANAGER_USER         (1U << 0)
/* Heap VAs managed by the OSs kernel, VA from CPU mapping call used */
#define DEVMEM_HEAP_MANAGER_KERNEL       (1U << 1)
/* Heap VAs managed by the heap's own RA  */
#define DEVMEM_HEAP_MANAGER_RA           (1U << 2)
/* Heap VAs managed jointly by Services and the client of Services.
 * The reserved region of the heap is managed explicitly by the client of Services
 * The non-reserved region of the heap is managed by the heap's own RA */
#define DEVMEM_HEAP_MANAGER_DUAL_USER_RA (DEVMEM_HEAP_MANAGER_USER | DEVMEM_HEAP_MANAGER_RA)

struct DEVMEM_HEAP_TAG
{
	/* Name of heap - for debug and lookup purposes. */
	IMG_CHAR *pszName;

	/* Number of live imports in the heap */
	ATOMIC_T hImportCount;

	/* Base address and size of heap, required by clients due to some
	 * requesters not being full range
	 */
	IMG_DEV_VIRTADDR sBaseAddress;
	DEVMEM_SIZE_T uiSize;

	DEVMEM_SIZE_T uiReservedRegionSize; /* uiReservedRegionLength in DEVMEM_HEAP_BLUEPRINT */

	/* The heap manager, describing if the space is managed by the user, an RA,
	 * kernel or combination */
	IMG_UINT32 ui32HeapManagerFlags;

	/* This RA is for managing sub-allocations within the imports (PMRs)
	 * within the heap's virtual space. RA only used in DevmemSubAllocate()
	 * to track sub-allocated buffers.
	 *
	 * Resource Span - a PMR import added when the RA calls the
	 *                 imp_alloc CB (SubAllocImportAlloc) which returns the
	 *                 PMR import and size (span length).
	 * Resource - an allocation/buffer i.e. a MemDesc. Resource size represents
	 *            the size of the sub-allocation.
	 */
	RA_ARENA *psSubAllocRA;
	IMG_CHAR *pszSubAllocRAName;

	/* The psQuantizedVMRA is for the coarse allocation (PMRs) of virtual
	 * space from the heap.
	 *
	 * Resource Span - the heap's VM space from base to base+length,
	 *                 only one is added at heap creation.
	 * Resource - a PMR import associated with the heap. Dynamic number
	 *            as memory is allocated/freed from or mapped/unmapped to
	 *            the heap. Resource size follows PMR logical size.
	 */
	RA_ARENA *psQuantizedVMRA;
	IMG_CHAR *pszQuantizedVMRAName;

	/* We also need to store a copy of the quantum size in order to feed
	 * this down to the server.
	 */
	IMG_UINT32 uiLog2Quantum;

	/* Store a copy of the minimum import alignment */
	IMG_UINT32 uiLog2ImportAlignment;

	/* The parent memory context for this heap */
	struct DEVMEM_CONTEXT_TAG *psCtx;

	/* Lock to protect this structure */
	POS_LOCK hLock;

	/* Each "DEVMEM_HEAP" has a counterpart in the server, which is
	 * responsible for handling the mapping into device MMU.
	 * We have a handle to that here.
	 */
	IMG_HANDLE hDevMemServerHeap;

	/* This heap is fully allocated and premapped into the device address space.
	 * Used in virtualisation for firmware heaps of Guest and optionally Host drivers. */
	IMG_BOOL bPremapped;
};

typedef IMG_UINT32 DEVMEM_PROPERTIES_T;                  /*!< Typedef for Devicemem properties */
#define DEVMEM_PROPERTIES_EXPORTABLE         (1UL<<0)    /*!< Is it exportable? */
#define DEVMEM_PROPERTIES_IMPORTED           (1UL<<1)    /*!< Is it imported from another process? */
#define DEVMEM_PROPERTIES_SUBALLOCATABLE     (1UL<<2)    /*!< Is it suballocatable? */
#define DEVMEM_PROPERTIES_UNPINNED           (1UL<<3)    /*!< Is it currently pinned? */
#define DEVMEM_PROPERTIES_IMPORT_IS_ZEROED   (1UL<<4)    /*!< Is the memory fully zeroed? */
#define DEVMEM_PROPERTIES_IMPORT_IS_CLEAN    (1UL<<5)    /*!< Is the memory clean, i.e. not been used before? */
#define DEVMEM_PROPERTIES_SECURE             (1UL<<6)    /*!< Is it a special secure buffer? No CPU maps allowed! */
#define DEVMEM_PROPERTIES_IMPORT_IS_POISONED (1UL<<7)    /*!< Is the memory fully poisoned? */
#define DEVMEM_PROPERTIES_NO_CPU_MAPPING     (1UL<<8)    /* No CPU Mapping is allowed, RW attributes
                                                            are further derived from allocation memory flags */
#define DEVMEM_PROPERTIES_NO_LAYOUT_CHANGE	 (1UL<<9)    /* No sparse resizing allowed, once a memory
                                                            layout is chosen, no change allowed later,
                                                            This includes pinning and unpinning */


typedef struct DEVMEM_DEVICE_IMPORT_TAG
{
	DEVMEM_HEAP *psHeap;            /*!< Heap this import is bound to */
	IMG_DEV_VIRTADDR sDevVAddr;     /*!< Device virtual address of the import */
	IMG_UINT32 ui32RefCount;        /*!< Refcount of the device virtual address */
	IMG_HANDLE hReservation;        /*!< Device memory reservation handle */
	IMG_HANDLE hMapping;            /*!< Device mapping handle */
	IMG_BOOL bMapped;               /*!< This is import mapped? */
	POS_LOCK hLock;                 /*!< Lock to protect the device import */
} DEVMEM_DEVICE_IMPORT;

typedef struct DEVMEM_CPU_IMPORT_TAG
{
	void *pvCPUVAddr;               /*!< CPU virtual address of the import */
	IMG_UINT32 ui32RefCount;        /*!< Refcount of the CPU virtual address */
	IMG_HANDLE hOSMMapData;         /*!< CPU mapping handle */
	POS_LOCK hLock;                 /*!< Lock to protect the CPU import */
} DEVMEM_CPU_IMPORT;

typedef struct DEVMEM_IMPORT_TAG
{
	SHARED_DEV_CONNECTION hDevConnection;
	IMG_DEVMEM_ALIGN_T uiAlign;         /*!< Alignment of the PMR */
	DEVMEM_SIZE_T uiSize;               /*!< Size of import */
	ATOMIC_T hRefCount;                 /*!< Refcount for this import */
	DEVMEM_PROPERTIES_T uiProperties;   /*!< Stores properties of an import like if
	                                         it is exportable, pinned or suballocatable */
	IMG_HANDLE hPMR;                    /*!< Handle to the PMR */
	PVRSRV_MEMALLOCFLAGS_T uiFlags;     /*!< Flags for this import */
	POS_LOCK hLock;                     /*!< Lock to protect the import */

	DEVMEM_DEVICE_IMPORT sDeviceImport; /*!< Device specifics of the import */
	DEVMEM_CPU_IMPORT sCPUImport;       /*!< CPU specifics of the import */
} DEVMEM_IMPORT;

typedef struct DEVMEM_DEVICE_MEMDESC_TAG
{
	IMG_DEV_VIRTADDR sDevVAddr;     /*!< Device virtual address of the allocation */
	IMG_UINT32 ui32RefCount;        /*!< Refcount of the device virtual address */
	POS_LOCK hLock;                 /*!< Lock to protect device memdesc */
} DEVMEM_DEVICE_MEMDESC;

typedef struct DEVMEM_CPU_MEMDESC_TAG
{
	void *pvCPUVAddr;           /*!< CPU virtual address of the import */
	IMG_UINT32 ui32RefCount;    /*!< Refcount of the device CPU address */
	POS_LOCK hLock;             /*!< Lock to protect CPU memdesc */
} DEVMEM_CPU_MEMDESC;

struct DEVMEM_MEMDESC_TAG
{
	DEVMEM_IMPORT *psImport;                /*!< Import this memdesc is on */
	IMG_DEVMEM_OFFSET_T uiOffset;           /*!< Offset into import where our allocation starts */
	IMG_DEVMEM_SIZE_T uiAllocSize;          /*!< Size of the allocation */
	ATOMIC_T hRefCount;                     /*!< Refcount of the memdesc */
	POS_LOCK hLock;                         /*!< Lock to protect memdesc */
	IMG_HANDLE hPrivData;

	DEVMEM_DEVICE_MEMDESC sDeviceMemDesc;   /*!< Device specifics of the memdesc */
	DEVMEM_CPU_MEMDESC sCPUMemDesc;         /*!< CPU specifics of the memdesc */

	IMG_CHAR szText[DEVMEM_ANNOTATION_MAX_LEN]; /*!< Annotation for this memdesc */

	IMG_UINT32 ui32AllocationIndex;

#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)
	IMG_HANDLE hRIHandle;                   /*!< Handle to RI information */
#endif
};

/* The physical descriptor used to store handles and information of device
 * physical allocations.
 */
struct DEVMEMX_PHYS_MEMDESC_TAG
{
	IMG_UINT32 uiNumPages;                  /*!< Number of pages that the import has*/
	IMG_UINT32 uiLog2PageSize;              /*!< Page size */
	ATOMIC_T hRefCount;                     /*!< Refcount of the memdesc */
	PVRSRV_MEMALLOCFLAGS_T uiFlags;         /*!< Flags for this import */
	IMG_HANDLE hPMR;                        /*!< Handle to the PMR */
	DEVMEM_CPU_IMPORT sCPUImport;           /*!< CPU specifics of the memdesc */
	DEVMEM_BRIDGE_HANDLE hBridge;           /*!< Bridge connection for the server */
	void *pvUserData;						/*!< User data */
};

/* The virtual descriptor used to store handles and information of a device
 * virtual range and the mappings to it.
 */
struct DEVMEMX_VIRT_MEMDESC_TAG
{
	IMG_UINT32 uiNumPages;                  /*!< Number of pages that the import has*/
	PVRSRV_MEMALLOCFLAGS_T uiFlags;         /*!< Flags for this import */
	DEVMEMX_PHYSDESC **apsPhysDescTable;    /*!< Table to store links to physical descs */
	DEVMEM_DEVICE_IMPORT sDeviceImport;     /*!< Device specifics of the memdesc */

	IMG_CHAR szText[DEVMEM_ANNOTATION_MAX_LEN]; /*!< Annotation for this virt memdesc */
	IMG_UINT32 ui32AllocationIndex;         /*!< To track mappings in this range */

#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)
	IMG_HANDLE hRIHandle;                   /*!< Handle to RI information */
#endif
};

#define DEVICEMEM_UTILS_NO_ADDRESS 0

/******************************************************************************
@Function       DevmemValidateParams
@Description    Check if flags are conflicting and if align is a size multiple.

@Input          uiSize      Size of the import.
@Input          uiAlign     Alignment of the import.
@Input          puiFlags    Pointer to the flags for the import.
@return         PVRSRV_ERROR
******************************************************************************/
PVRSRV_ERROR DevmemValidateParams(IMG_DEVMEM_SIZE_T uiSize,
                                   IMG_DEVMEM_ALIGN_T uiAlign,
                                   PVRSRV_MEMALLOCFLAGS_T *puiFlags);

/******************************************************************************
@Function       DevmemImportStructAlloc
@Description    Allocates memory for an import struct. Does not allocate a PMR!
                Create locks for CPU and Devmem mappings.

@Input          hDevConnection  Connection to use for calls from the import.
@Input          ppsImport       The import to allocate.
@return         PVRSRV_ERROR
******************************************************************************/
PVRSRV_ERROR DevmemImportStructAlloc(SHARED_DEV_CONNECTION hDevConnection,
                                      DEVMEM_IMPORT **ppsImport);

/******************************************************************************
@Function       DevmemImportStructInit
@Description    Initialises the import struct with the given parameters.
                Set it's refcount to 1!

@Input          psImport     The import to initialise.
@Input          uiSize       Size of the import.
@Input          uiAlign      Alignment of allocations in the import.
@Input          uiMapFlags
@Input          hPMR         Reference to the PMR of this import struct.
@Input          uiProperties Properties of the import. Is it exportable,
                              imported, suballocatable, unpinned?
******************************************************************************/
void DevmemImportStructInit(DEVMEM_IMPORT *psImport,
                             IMG_DEVMEM_SIZE_T uiSize,
                             IMG_DEVMEM_ALIGN_T uiAlign,
                             PVRSRV_MEMALLOCFLAGS_T uiMapFlags,
                             IMG_HANDLE hPMR,
                             DEVMEM_PROPERTIES_T uiProperties);

/******************************************************************************
@Function       DevmemImportStructDevMap
@Description    NEVER call after the last DevmemMemDescRelease()
                Maps the PMR referenced by the import struct to the device's
                virtual address space.
                Does nothing but increase the cpu mapping refcount if the
                import struct was already mapped.

@Input          psHeap    The heap to map to.
@Input          bMap      Caller can choose if the import should be really
                          mapped in the page tables or if just a virtual range
                          should be reserved and the refcounts increased.
@Input          psImport  The import we want to map.
@Input          uiOptionalMapAddress  An optional address to map to.
                                      Pass DEVICEMEM_UTILS_NOADDRESS if not used.
@return         PVRSRV_ERROR
******************************************************************************/
PVRSRV_ERROR DevmemImportStructDevMap(DEVMEM_HEAP *psHeap,
                                       IMG_BOOL bMap,
                                       DEVMEM_IMPORT *psImport,
                                       IMG_UINT64 uiOptionalMapAddress);

/******************************************************************************
@Function       DevmemImportStructDevUnmap
@Description    Unmaps the PMR referenced by the import struct from the
                device's virtual address space.
                If this was not the last remaining CPU mapping on the import
                struct only the cpu mapping refcount is decreased.
@return         A boolean to signify if the import was unmapped.
******************************************************************************/
IMG_BOOL DevmemImportStructDevUnmap(DEVMEM_IMPORT *psImport);

/******************************************************************************
@Function       DevmemImportStructCPUMap
@Description    NEVER call after the last DevmemMemDescRelease()
                Maps the PMR referenced by the import struct to the CPU's
                virtual address space.
                Does nothing but increase the cpu mapping refcount if the
                import struct was already mapped.
@return         PVRSRV_ERROR
******************************************************************************/
PVRSRV_ERROR DevmemImportStructCPUMap(DEVMEM_IMPORT *psImport);

/******************************************************************************
@Function       DevmemImportStructCPUUnmap
@Description    Unmaps the PMR referenced by the import struct from the CPU's
                virtual address space.
                If this was not the last remaining CPU mapping on the import
                struct only the cpu mapping refcount is decreased.
******************************************************************************/
void DevmemImportStructCPUUnmap(DEVMEM_IMPORT *psImport);


/******************************************************************************
@Function       DevmemImportStructAcquire
@Description    Acquire an import struct by increasing it's refcount.
******************************************************************************/
void DevmemImportStructAcquire(DEVMEM_IMPORT *psImport);

/******************************************************************************
@Function       DevmemImportStructRelease
@Description    Reduces the refcount of the import struct.
                Destroys the import in the case it was the last reference.
                Destroys underlying PMR if this import was the last reference
                to it.
@return         A boolean to signal if the import was destroyed. True = yes.
******************************************************************************/
IMG_BOOL DevmemImportStructRelease(DEVMEM_IMPORT *psImport);

/******************************************************************************
@Function       DevmemImportDiscard
@Description    Discard a created, but unitilised import structure.
                This must only be called before DevmemImportStructInit
                after which DevmemImportStructRelease must be used to
                "free" the import structure.
******************************************************************************/
void DevmemImportDiscard(DEVMEM_IMPORT *psImport);

/******************************************************************************
@Function       DevmemMemDescAlloc
@Description    Allocates a MemDesc and create it's various locks.
                Zero the allocated memory.
@return         PVRSRV_ERROR
******************************************************************************/
PVRSRV_ERROR DevmemMemDescAlloc(DEVMEM_MEMDESC **ppsMemDesc);

/******************************************************************************
@Function       DevmemMemDescInit
@Description    Sets the given offset and import struct fields in the MemDesc.
                Initialises refcount to 1 and other values to 0.

@Input          psMemDesc    MemDesc to initialise.
@Input          uiOffset     Offset in the import structure.
@Input          psImport     Import the MemDesc is on.
@Input          uiAllocSize  Size of the allocation
******************************************************************************/
void DevmemMemDescInit(DEVMEM_MEMDESC *psMemDesc,
						IMG_DEVMEM_OFFSET_T uiOffset,
						DEVMEM_IMPORT *psImport,
						IMG_DEVMEM_SIZE_T uiAllocSize);

/******************************************************************************
@Function       DevmemMemDescAcquire
@Description    Acquires the MemDesc by increasing it's refcount.
******************************************************************************/
void DevmemMemDescAcquire(DEVMEM_MEMDESC *psMemDesc);

/******************************************************************************
@Function       DevmemMemDescRelease
@Description    Releases the MemDesc by reducing it's refcount.
                Destroy the MemDesc if it's recount is 0.
                Destroy the import struct the MemDesc is on if that was the
                last MemDesc on the import, probably following the destruction
                of the underlying PMR.
@return         A boolean to signal if the MemDesc was destroyed. True = yes.
******************************************************************************/
IMG_BOOL DevmemMemDescRelease(DEVMEM_MEMDESC *psMemDesc);

/******************************************************************************
@Function       DevmemMemDescDiscard
@Description    Discard a created, but uninitialised MemDesc structure.
                This must only be called before DevmemMemDescInit after
                which DevmemMemDescRelease must be used to "free" the
                MemDesc structure.
******************************************************************************/
void DevmemMemDescDiscard(DEVMEM_MEMDESC *psMemDesc);


/******************************************************************************
@Function       GetImportProperties
@Description    Atomically read psImport->uiProperties
                It's possible that another thread modifies uiProperties
                immediately after this function returns, making its result
                stale. So, it's recommended to use this function only to
                check if certain non-volatile flags were set.
******************************************************************************/
static INLINE DEVMEM_PROPERTIES_T GetImportProperties(DEVMEM_IMPORT *psImport)
{
	DEVMEM_PROPERTIES_T uiProperties;

	OSLockAcquire(psImport->hLock);
	uiProperties = psImport->uiProperties;
	OSLockRelease(psImport->hLock);
	return uiProperties;
}

#endif /* DEVICEMEM_UTILS_H */
