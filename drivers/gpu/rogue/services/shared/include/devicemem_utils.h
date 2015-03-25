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

#ifndef _DEVICEMEM_UTILS_H_
#define _DEVICEMEM_UTILS_H_

#include "devicemem.h"
#include "img_types.h"
#include "pvrsrv_error.h"
#include "pvr_debug.h"
#include "allocmem.h"
#include "ra.h"
#include "osfunc.h"
#include "lock.h"
#include "devicemem_mmap.h"
#include "devicemem_utils.h"

#define DEVMEM_HEAPNAME_MAXLENGTH 160


#if defined(DEVMEM_DEBUG) && defined(REFCOUNT_DEBUG)
#define DEVMEM_REFCOUNT_PRINT(fmt, ...) PVRSRVDebugPrintf(PVR_DBG_ERROR, __FILE__, __LINE__, fmt, __VA_ARGS__)
#else
#define DEVMEM_REFCOUNT_PRINT(fmt, ...)
#endif


/* If we need a "hMapping" but we don't have a server-side mapping, we
   poison the entry with this value so that it's easily recognised in
   the debugger.  Note that this is potentially a valid handle, but
   then so is IMG_NULL, which is no better, indeed worse, as it's not
   obvious in the debugger.  The value doesn't matter.  We _never_ use
   it (and because it's valid, we never assert it isn't this) but it's
   nice to have a value in the source code that we can grep for when
   things go wrong. */
#define LACK_OF_MAPPING_POISON ((IMG_HANDLE)0x6116dead)
#define LACK_OF_RESERVATION_POISON ((IMG_HANDLE)0x7117dead)

struct _DEVMEM_CONTEXT_ {
    /* Cookie of the device on which this memory context resides */
    IMG_HANDLE hDeviceNode;

    /* Number of heaps that have been created in this context
       (regardless of whether they have allocations) */
    IMG_UINT32 uiNumHeaps;

    /* Sometimes we need to talk to Kernel Services.  In order to do
       so, we need the connection handle */
    DEVMEM_BRIDGE_HANDLE hBridge;

    /*
      Each "DEVMEM_CONTEXT" has a counterpart in the server,
      which is responsible for handling the mapping into device MMU.
      We have a handle to that here.
    */
    IMG_HANDLE hDevMemServerContext;

    /* Number of automagically created heaps in this context,
       i.e. those that are born at context creation time from the
       chosen "heap config" or "blueprint" */
    IMG_UINT32 uiAutoHeapCount;

    /* pointer to array of such heaps */
    struct _DEVMEM_HEAP_ **ppsAutoHeapArray;

	/* Private data handle for device specific data */
	IMG_HANDLE hPrivData;
};

struct _DEVMEM_HEAP_ {
    /* Name of heap - for debug and lookup purposes. */
    IMG_CHAR *pszName;

    /* Number of live imports in the heap */
    IMG_UINT32 uiImportCount;

    /*
     * Base address of heap, required by clients due to some requesters
     * not being full range 
     */
    IMG_DEV_VIRTADDR sBaseAddress;

    /* This RA is for managing sub-allocations in virtual space.  Two
       more RA's will be used under the Hood for managing the coarser
       allocation of virtual space from the heap, and also for
       managing the physical backing storage. */
    RA_ARENA *psSubAllocRA;
    IMG_CHAR *pszSubAllocRAName;
    /*
      This RA is for the coarse allocation of virtual space from the heap
    */
    RA_ARENA *psQuantizedVMRA;
    IMG_CHAR *pszQuantizedVMRAName;

    /* We also need to store a copy of the quantum size in order to
       feed this down to the server */
    IMG_UINT32 uiLog2Quantum;

    /* The parent memory context for this heap */
    struct _DEVMEM_CONTEXT_ *psCtx;

	POS_LOCK hLock;							/*!< Lock to protect this structure */

    /*
      Each "DEVMEM_HEAP" has a counterpart in the server,
      which is responsible for handling the mapping into device MMU.
      We have a handle to that here.
    */
    IMG_HANDLE hDevMemServerHeap;
};


typedef struct _DEVMEM_DEVICE_IMPORT_ {
	DEVMEM_HEAP *psHeap;			/*!< Heap this import is bound to */
	IMG_DEV_VIRTADDR sDevVAddr;		/*!< Device virtual address of the import */
	IMG_UINT32 ui32RefCount;		/*!< Refcount of the device virtual address */
	IMG_HANDLE hReservation;		/*!< Device memory reservation handle */
	IMG_HANDLE hMapping;			/*!< Device mapping handle */
	IMG_BOOL bMapped;				/*!< This is import mapped? */
	POS_LOCK hLock;					/*!< Lock to protect the device import */
} DEVMEM_DEVICE_IMPORT;

typedef struct _DEVMEM_CPU_IMPORT_ {
	IMG_PVOID pvCPUVAddr;			/*!< CPU virtual address of the import */
	IMG_UINT32 ui32RefCount;		/*!< Refcount of the CPU virtual address */
	IMG_HANDLE hOSMMapData;			/*!< CPU mapping handle */
	POS_LOCK hLock;					/*!< Lock to protect the CPU import */
#if !defined(__KERNEL__) && defined(SUPPORT_ION)
	int iDmaBufFd;					/*!< >=0 if this was an imported ion allocation */
#endif
} DEVMEM_CPU_IMPORT;

typedef struct _DEVMEM_IMPORT_ {
    DEVMEM_BRIDGE_HANDLE hBridge;		/*!< Bridge connection for the server */
    IMG_DEVMEM_ALIGN_T uiAlign;			/*!< Alignment requirement */
	DEVMEM_SIZE_T uiSize;				/*!< Size of import */
    IMG_UINT32 ui32RefCount;			/*!< Refcount for this import */
    IMG_BOOL bExportable;				/*!< Is this import exportable? */
    IMG_HANDLE hPMR;					/*!< Handle to the PMR */
    DEVMEM_FLAGS_T uiFlags;				/*!< Flags for this import */
    POS_LOCK hLock;						/*!< Lock to protect the import */

	DEVMEM_DEVICE_IMPORT sDeviceImport;	/*!< Device specifics of the import */
	DEVMEM_CPU_IMPORT sCPUImport;		/*!< CPU specifics of the import */
} DEVMEM_IMPORT;

typedef struct _DEVMEM_DEVICE_MEMDESC_ {
	IMG_DEV_VIRTADDR sDevVAddr;		/*!< Device virtual address of the allocation */
	IMG_UINT32 ui32RefCount;		/*!< Refcount of the device virtual address */
	POS_LOCK hLock;					/*!< Lock to protect device memdesc */
} DEVMEM_DEVICE_MEMDESC;

typedef struct _DEVMEM_CPU_MEMDESC_ {
	IMG_PVOID pvCPUVAddr;			/*!< CPU virtual address of the import */
	IMG_UINT32 ui32RefCount;		/*!< Refcount of the device CPU address */
	POS_LOCK hLock;					/*!< Lock to protect CPU memdesc */
} DEVMEM_CPU_MEMDESC;

struct _DEVMEM_MEMDESC_ {
    DEVMEM_IMPORT *psImport;				/*!< Import this memdesc is on */
    IMG_DEVMEM_OFFSET_T uiOffset;			/*!< Offset into import where our allocation starts */
    IMG_UINT32 ui32RefCount;				/*!< Refcount of the memdesc */
    POS_LOCK hLock;							/*!< Lock to protect memdesc */

	DEVMEM_DEVICE_MEMDESC sDeviceMemDesc;	/*!< Device specifics of the memdesc */
	DEVMEM_CPU_MEMDESC sCPUMemDesc;		/*!< CPU specifics of the memdesc */

#if defined(PVR_RI_DEBUG)
    IMG_HANDLE hRIHandle;					/*!< Handle to RI information */
#endif
};

/******************************************************************************
@Function       _DevmemValidateParams
@Description    Check if flags are conflicting and if align is a size multiple.

@Input          uiSize      Size of the import.
@Input          uiAlign     Alignment of the import.
@Input          uiFlags     Flags for the import.
@return         PVRSRV_ERROR
******************************************************************************/
PVRSRV_ERROR _DevmemValidateParams(IMG_DEVMEM_SIZE_T uiSize,
								   IMG_DEVMEM_ALIGN_T uiAlign,
								   DEVMEM_FLAGS_T uiFlags);

/******************************************************************************
@Function       _DevmemImportStructAlloc
@Description    Allocates memory for an import struct. Does not allocate a PMR!
                Create locks for CPU and Devmem mappings.

@Input          hBridge       Bridge to use for calls from the import.
@Input          bExportable   Is this import is exportable?
@Input          ppsImport     The import to allocate.
@return         PVRSRV_ERROR
******************************************************************************/
PVRSRV_ERROR _DevmemImportStructAlloc(IMG_HANDLE hBridge,
									  IMG_BOOL bExportable,
									  DEVMEM_IMPORT **ppsImport);

/******************************************************************************
@Function       _DevmemImportStructInit
@Description    Initialises the import struct with the given parameters.
                Set it's refcount to 1!

@Input          psImport    The import to initialise.
@Input          uiSize      Size of the import.
@Input          uiAlign     Alignment of allocations in the import.
@Input          uiMapFlags
@Input          hPMR        Reference to the PMR of this import struct.
******************************************************************************/
IMG_VOID _DevmemImportStructInit(DEVMEM_IMPORT *psImport,
								 IMG_DEVMEM_SIZE_T uiSize,
								 IMG_DEVMEM_ALIGN_T uiAlign,
								 PVRSRV_MEMALLOCFLAGS_T uiMapFlags,
								 IMG_HANDLE hPMR);

/******************************************************************************
@Function       _DevmemImportStructDevMap
@Description    NEVER call after the last _DevmemMemDescRelease()
                Maps the PMR referenced by the import struct to the device's
                virtual address space.
                Does nothing but increase the cpu mapping refcount if the
                import struct was already mapped.

@Input          psHeap    The heap to map to.
@Input          bMap      Caller can choose if the import should be really
                          mapped in the page tables or if just a virtual range
                          should be reserved and the refcounts increased.
@Input          psImport  The import we want to map.
@return         PVRSRV_ERROR
******************************************************************************/
PVRSRV_ERROR _DevmemImportStructDevMap(DEVMEM_HEAP *psHeap,
									   IMG_BOOL bMap,
									   DEVMEM_IMPORT *psImport);

/******************************************************************************
@Function       _DevmemImportStructDevUnmap
@Description    Unmaps the PMR referenced by the import struct from the
                device's virtual address space.
                If this was not the last remaining CPU mapping on the import
                struct only the cpu mapping refcount is decreased.
******************************************************************************/
IMG_VOID _DevmemImportStructDevUnmap(DEVMEM_IMPORT *psImport);

/******************************************************************************
@Function       _DevmemImportStructCPUMap
@Description    NEVER call after the last _DevmemMemDescRelease()
                Maps the PMR referenced by the import struct to the CPU's
                virtual address space.
                Does nothing but increase the cpu mapping refcount if the
                import struct was already mapped.
@return         PVRSRV_ERROR
******************************************************************************/
PVRSRV_ERROR _DevmemImportStructCPUMap(DEVMEM_IMPORT *psImport);

/******************************************************************************
@Function       _DevmemImportStructCPUUnmap
@Description    Unmaps the PMR referenced by the import struct from the CPU's
                virtual address space.
                If this was not the last remaining CPU mapping on the import
                struct only the cpu mapping refcount is decreased.
******************************************************************************/
IMG_VOID _DevmemImportStructCPUUnmap(DEVMEM_IMPORT *psImport);


/******************************************************************************
@Function       _DevmemImportStructAcquire
@Description    Acquire an import struct by increasing it's refcount.
******************************************************************************/
IMG_VOID _DevmemImportStructAcquire(DEVMEM_IMPORT *psImport);

/******************************************************************************
@Function       _DevmemImportStructRelease
@Description    Reduces the refcount of the import struct.
                Destroys the import in the case it was the last reference.
                Destroys underlying PMR if this import was the last reference
                to it.
@return         A boolean to signal if the import was destroyed. True = yes.
******************************************************************************/
IMG_VOID _DevmemImportStructRelease(DEVMEM_IMPORT *psImport);

/******************************************************************************
@Function       _DevmemImportDiscard
@Description    Discard a created, but unitilised import structure.
                This must only be called before _DevmemImportStructInit
                after which _DevmemImportStructRelease must be used to
                "free" the import structure.
******************************************************************************/
IMG_VOID _DevmemImportDiscard(DEVMEM_IMPORT *psImport);

/******************************************************************************
@Function       _DevmemMemDescAlloc
@Description    Allocates a MemDesc and create it's various locks.
                Zero the allocated memory.
@return         PVRSRV_ERROR
******************************************************************************/
PVRSRV_ERROR _DevmemMemDescAlloc(DEVMEM_MEMDESC **ppsMemDesc);

/******************************************************************************
@Function       _DevmemMemDescInit
@Description    Sets the given offset and import struct fields in the MemDesc.
                Initialises refcount to 1 and other values to 0.

@Input          psMemDesc    MemDesc to initialise.
@Input          uiOffset     Offset in the import structure.
@Input          psImport     Import the MemDesc is on.
******************************************************************************/
IMG_VOID _DevmemMemDescInit(DEVMEM_MEMDESC *psMemDesc,
						  	IMG_DEVMEM_OFFSET_T uiOffset,
						  	DEVMEM_IMPORT *psImport);

/******************************************************************************
@Function       _DevmemMemDescAcquire
@Description    Acquires the MemDesc by increasing it's refcount.
******************************************************************************/
IMG_VOID _DevmemMemDescAcquire(DEVMEM_MEMDESC *psMemDesc);

/******************************************************************************
@Function       _DevmemMemDescRelease
@Description    Releases the MemDesc by reducing it's refcount.
                Destroy the MemDesc if it's recount is 0.
                Destroy the import struct the MemDesc is on if that was the
                last MemDesc on the import, probably following the destruction
                of the underlying PMR.
******************************************************************************/
IMG_VOID _DevmemMemDescRelease(DEVMEM_MEMDESC *psMemDesc);

/******************************************************************************
@Function       _DevmemMemDescDiscard
@Description    Discard a created, but unitilised MemDesc structure.
                This must only be called before _DevmemMemDescInit
                after which _DevmemMemDescRelease must be used to
                "free" the MemDesc structure.
******************************************************************************/
IMG_VOID _DevmemMemDescDiscard(DEVMEM_MEMDESC *psMemDesc);

#endif /* _DEVICEMEM_UTILS_H_ */
