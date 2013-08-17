/*************************************************************************/ /*!
@Title          Buffer Management.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Manages buffers mapped into two virtual memory spaces, host and
                device and referenced by handles.
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

#ifndef _BUFFER_MANAGER_H_
#define _BUFFER_MANAGER_H_

#include "img_types.h"
#include "ra.h"
#include "perproc.h"

#if defined(__cplusplus)
extern "C"{
#endif

/* forward reference */
typedef struct _BM_HEAP_ BM_HEAP;

/*
 * The mapping structure is used to record relations between CPU virtual,
 * CPU physical and device virtual addresses for large chunks of memory
 * from which we have resource-allocator draw our buffers.
 *
 * There is one per contiguous pool and one per import from the host OS.
 */
struct _BM_MAPPING_
{
	enum
	{
		hm_wrapped = 1,		/*!< wrapped user supplied contiguous*/
		hm_wrapped_scatter,	/*!< wrapped user supplied scattered */
		hm_wrapped_virtaddr, /*!< wrapped user supplied contiguous with virtual address*/
		hm_wrapped_scatter_virtaddr, /*!< wrapped user supplied scattered with virtual address*/
		hm_env,				/*!< obtained from environment */
		hm_contiguous		/*!< contigous arena */
	} eCpuMemoryOrigin;

	BM_HEAP				*pBMHeap;	/* which BM heap */
	RA_ARENA			*pArena;	/* whence the memory comes */

	IMG_CPU_VIRTADDR	CpuVAddr;
	IMG_CPU_PHYADDR		CpuPAddr;
	IMG_DEV_VIRTADDR	DevVAddr;
	IMG_SYS_PHYADDR		*psSysAddr;
	IMG_SIZE_T			uSize;
	IMG_SIZE_T			uSizeVM;
    IMG_HANDLE          hOSMemHandle;
	IMG_UINT32			ui32Flags;

	/* Sparse mapping data */
	IMG_UINT32			ui32ChunkSize;
	IMG_UINT32			ui32NumVirtChunks;
	IMG_UINT32			ui32NumPhysChunks;
	IMG_BOOL			*pabMapChunk;
};

/*
 * The buffer structure handles individual allocations from the user; thus
 * there is one allocated per call to BM_Alloc and one per call to BM_Wrap.
 * We record a mapping reference so we know where to return allocated
 * resources at BM_Free time.
 */
typedef struct _BM_BUF_
{
	IMG_CPU_VIRTADDR	*CpuVAddr;
    IMG_VOID            *hOSMemHandle;
	IMG_CPU_PHYADDR		CpuPAddr;
	IMG_DEV_VIRTADDR	DevVAddr;

	BM_MAPPING			*pMapping;
	IMG_UINT32			ui32RefCount;
	IMG_UINT32			ui32ExportCount;
} BM_BUF;

struct _BM_HEAP_
{
	IMG_UINT32				ui32Attribs;
	BM_CONTEXT				*pBMContext;
	RA_ARENA				*pImportArena;
	RA_ARENA				*pLocalDevMemArena;
	RA_ARENA				*pVMArena;
	DEV_ARENA_DESCRIPTOR	sDevArena;
	MMU_HEAP				*pMMUHeap;
	PDUMP_MMU_ATTRIB 		*psMMUAttrib;
	
	struct _BM_HEAP_ 		*psNext;
	struct _BM_HEAP_ 		**ppsThis;
	/* BIF tile stride for this heap */
	IMG_UINT32 				ui32XTileStride;
};

/*
 * The bm-context structure
 */
struct _BM_CONTEXT_
{
	MMU_CONTEXT	*psMMUContext;

	/*
	 * Resource allocation arena of dual mapped pages. For devices
	 * where the hardware imposes different constraints on the valid
	 * device virtual address range depending on the use of the buffer
	 * we maintain two allocation arenas, one low address range, the
	 * other high. For devices without such a constrain we do not
	 * create the high arena, instead all allocations come from the
	 * low arena.
	 */
	 BM_HEAP *psBMHeap;

	/*
	 * The Shared Heaps
	 */
	 BM_HEAP *psBMSharedHeap;

	PVRSRV_DEVICE_NODE *psDeviceNode;

	/*
	 * Hash table management.
	 */
	HASH_TABLE *pBufferHash;

	/*
	 *	Resman item handle
	 */
	IMG_HANDLE hResItem;

	IMG_UINT32 ui32RefCount;

	/*
		linked list next pointer
	*/
	struct _BM_CONTEXT_ *psNext;
	struct _BM_CONTEXT_ **ppsThis;
};

/* refcount.c needs to know the internals of this structure */
typedef struct _XPROC_DATA_{
	IMG_UINT32 ui32RefCount;
	IMG_UINT32 ui32AllocFlags;
	IMG_UINT32 ui32Size;
	IMG_UINT32 ui32PageSize;
    RA_ARENA *psArena;
    IMG_SYS_PHYADDR sSysPAddr;
	IMG_VOID *pvCpuVAddr;
	IMG_HANDLE hOSMemHandle;
} XPROC_DATA;

extern XPROC_DATA gXProcWorkaroundShareData[];
/*
	Buffer handle.
*/
typedef IMG_VOID *BM_HANDLE;

/** Buffer manager allocation flags.
 *
 *  Flags passed to BM_Alloc to specify buffer capabilities.
 *
 * @defgroup BP Buffer Manager Allocation Flags
 * @{
 */

/** Pool number mask. */
#define BP_POOL_MASK         0x7

/* Request physically contiguous pages of memory */
#define BP_CONTIGUOUS			(1 << 3)
#define BP_PARAMBUFFER			(1 << 4)

#define BM_MAX_DEVMEM_ARENAS  2

/** @} */

/**
 *  @Function BM_CreateContext
 *
 *  @Description
 *
 *  @Input

 *  @Return
 */

IMG_HANDLE
BM_CreateContext(PVRSRV_DEVICE_NODE			*psDeviceNode,
				 IMG_DEV_PHYADDR			*psPDDevPAddr,
				 PVRSRV_PER_PROCESS_DATA	*psPerProc,
				 IMG_BOOL					*pbCreated);


/**
 *  @Function   BM_DestroyContext
 *
 *  @Description
 *
 *  @Input
 *
 *  @Return PVRSRV_ERROR
 */
PVRSRV_ERROR
BM_DestroyContext (IMG_HANDLE hBMContext,
					IMG_BOOL *pbCreated);


/**
 *  @Function   BM_CreateHeap
 *
 *  @Description
 *
 *  @Input
 *
 *  @Return
 */
IMG_HANDLE
BM_CreateHeap (IMG_HANDLE hBMContext,
				DEVICE_MEMORY_HEAP_INFO *psDevMemHeapInfo);

/**
 *  @Function   BM_DestroyHeap
 *
 *  @Description
 *
 *  @Input
 *
 *  @Return
 */
IMG_VOID
BM_DestroyHeap (IMG_HANDLE hDevMemHeap);


/**
 *  @Function   BM_Reinitialise
 *
 *  @Description
 *
 *  Reinitialises the buffer manager after a power event. Calling this
 *  function will reprogram MMU registers and renable the MMU.
 *
 *  @Input None
 *  @Return None
 */

IMG_BOOL
BM_Reinitialise (PVRSRV_DEVICE_NODE *psDeviceNode);

/**
 *  @Function   BM_Alloc
 *
 *  @Description
 *
 *  Allocate a buffer mapped into both host and device virtual memory
 *  maps.
 *
 *  @Input uSize - require size in bytes of the buffer.
 *  @Input/Output pui32Flags - bit mask of buffer property flags + recieves heap flags.
 *  @Input uDevVAddrAlignment - required alignment in bytes, or 0.
 *  @Input pvPrivData - private data passed to OS allocator
 *  @Input ui32PrivDataLength - length of private data
 *  @Input ui32ChunkSize - Chunk size
 *  @Input ui32NumVirtChunks - Number of virtual chunks
 *  @Input ui32NumPhysChunks - Number of physical chunks
 *  @Input pabMapChunk - Chunk mapping array
 *  @Output phBuf - receives the buffer handle.
 *  @Return IMG_TRUE - Success, IMG_FALSE - Failed.
 */
IMG_BOOL
BM_Alloc (IMG_HANDLE			hDevMemHeap,
			IMG_DEV_VIRTADDR	*psDevVAddr,
			IMG_SIZE_T			uSize,
			IMG_UINT32			*pui32Flags,
			IMG_UINT32			uDevVAddrAlignment,
			IMG_PVOID			pvPrivData,
			IMG_UINT32			ui32PrivDataLength,
			IMG_UINT32			ui32ChunkSize,
			IMG_UINT32			ui32NumVirtChunks,
			IMG_UINT32			ui32NumPhysChunks,
			IMG_BOOL			*pabMapChunk,
			BM_HANDLE			*phBuf);

/**
 *  @Function   BM_Wrap
 *
 *  @Description
 *
 *  Create a buffer which wraps user provided host physical memory.
 *  The wrapped memory must be page aligned. BM_Wrap will roundup the
 *  size to a multiple of host pages.
 *
 *  @Input ui32Size - size of memory to wrap.
 *  @Input ui32Offset - Offset into page of memory to wrap.
 *  @Input bPhysContig - Is the wrap physically contiguous.
 *  @Input psSysAddr - list of system physical page addresses of memory to wrap.
 *	@Input pvCPUVAddr - optional CPU kernel virtual address (Page aligned) of memory to wrap.
 *  @Input uFlags - bit mask of buffer property flags.
 *  @Input phBuf - receives the buffer handle.
 *  @Return IMG_TRUE - Success, IMG_FALSE - Failed
 */
IMG_BOOL
BM_Wrap (	IMG_HANDLE hDevMemHeap,
		    IMG_SIZE_T uSize,
			IMG_SIZE_T uOffset,
			IMG_BOOL bPhysContig,
			IMG_SYS_PHYADDR *psSysAddr,
			IMG_VOID *pvCPUVAddr,
			IMG_UINT32 *pui32Flags,
			BM_HANDLE *phBuf);

/**
 *  @Function   BM_Free
 *
 *  @Description
 *
 *  Free a buffer previously allocated via BM_Alloc.
 *
 *  @Input  hBuf - buffer handle.
 *  @Return None.
 */
IMG_VOID
BM_Free (BM_HANDLE hBuf,
		IMG_UINT32 ui32Flags);


/**
 *  @Function   BM_HandleToCpuVaddr
 *
 *  @Description
 *
 *  Retrieve the host virtual address associated with a buffer.
 *
 *  @Input  hBuf - buffer handle.
 *
 *  @Return buffers host virtual address.
 */
IMG_CPU_VIRTADDR
BM_HandleToCpuVaddr (BM_HANDLE hBuf);

/**
 *  @Function   BM_HandleToDevVaddr
 *
 *  @Description
 *
 *  Retrieve the device virtual address associated with a buffer.
 *
 *  @Input hBuf - buffer handle.
 *  @Return buffers device virtual address.
 */
IMG_DEV_VIRTADDR
BM_HandleToDevVaddr (BM_HANDLE hBuf);

/**
 *  @Function   BM_HandleToSysPaddr
 *
 *  @Description
 *
 *  Retrieve the system physical address associated with a buffer.
 *
 *  @Input hBuf - buffer handle.
 *  @Return buffers device virtual address.
 */
IMG_SYS_PHYADDR
BM_HandleToSysPaddr (BM_HANDLE hBuf);

/**
 *  @Function   BM_HandleToMemOSHandle
 *
 *  @Description
 *
 *  Retrieve the underlying memory handle associated with a buffer.
 *
 *  @Input hBuf - buffer handle.
 *  @Return An OS Specific memory handle
 */
IMG_HANDLE
BM_HandleToOSMemHandle (BM_HANDLE hBuf);

/**
 *  @Function   BM_GetPhysPageAddr
 *
 *  @Description
 *
 *  Retreive physical address backing dev V address
 *
 *  @Input psMemInfo
 *  @Input sDevVPageAddr
 *  @Output psDevPAddr
 *  @Return PVRSRV_ERROR
 */
IMG_VOID BM_GetPhysPageAddr(PVRSRV_KERNEL_MEM_INFO *psMemInfo,
								IMG_DEV_VIRTADDR sDevVPageAddr,
								IMG_DEV_PHYADDR *psDevPAddr);

/*!
******************************************************************************
 @Function	 	BM_GetMMUContext

 @Description
 				utility function to return the MMU context

 @inputs        hDevMemHeap - the Dev mem heap handle

 @Return   		MMU context, else NULL
**************************************************************************/
MMU_CONTEXT* BM_GetMMUContext(IMG_HANDLE hDevMemHeap);

/*!
******************************************************************************
 @Function	 	BM_GetMMUContextFromMemContext

 @Description
 				utility function to return the MMU context

 @inputs        hDevMemHeap - the Dev mem heap handle

 @Return   		MMU context, else NULL
**************************************************************************/
MMU_CONTEXT* BM_GetMMUContextFromMemContext(IMG_HANDLE hDevMemContext);

/*!
******************************************************************************
 @Function	 	BM_GetMMUHeap

 @Description
 				utility function to return the MMU heap handle

 @inputs        hDevMemHeap - the Dev mem heap handle

 @Return   		MMU heap handle, else NULL
**************************************************************************/
IMG_HANDLE BM_GetMMUHeap(IMG_HANDLE hDevMemHeap);

/*!
******************************************************************************
 @Function	 	BM_GetDeviceNode

 @Description	utility function to return the devicenode from the BM Context

 @inputs        hDevMemContext - the Dev Mem Context

 @Return   		MMU heap handle, else NULL
**************************************************************************/
PVRSRV_DEVICE_NODE* BM_GetDeviceNode(IMG_HANDLE hDevMemContext);


/*!
******************************************************************************
 @Function	 	BM_GetMappingHandle

 @Description	utility function to return the mapping handle from a meminfo

 @inputs        psMemInfo - kernel meminfo

 @Return   		mapping handle, else NULL
**************************************************************************/
IMG_HANDLE BM_GetMappingHandle(PVRSRV_KERNEL_MEM_INFO *psMemInfo);

/*!
******************************************************************************
 @Function	 	BM_Export

 @Description	Export a buffer previously allocated via BM_Alloc.

 @inputs        hBuf - buffer handle.

 @Return   		None.
**************************************************************************/
IMG_VOID BM_Export(BM_HANDLE hBuf);

/*!
******************************************************************************
 @Function	 	BM_FreeExport

 @Description	Free a buffer previously exported via BM_Export.

 @inputs        hBuf - buffer handle.
                ui32Flags - flags

 @Return   		None.
**************************************************************************/
IMG_VOID BM_FreeExport(BM_HANDLE hBuf, IMG_UINT32 ui32Flags);

/*!
******************************************************************************
 @Function	BM_MappingHandleFromBuffer

 @Description	utility function to get the BM mapping handle from a BM buffer

 @Input     hBuffer - Handle to BM buffer

 @Return	BM mapping handle
**************************************************************************/
IMG_HANDLE BM_MappingHandleFromBuffer(IMG_HANDLE hBuffer);

/*!
******************************************************************************
 @Function	BM_GetVirtualSize

 @Description	utility function to get the VM size of a BM mapping

 @Input     hBMHandle - Handle to BM mapping

 @Return	VM size of mapping
**************************************************************************/
IMG_UINT32 BM_GetVirtualSize(IMG_HANDLE hBMHandle);

/*!
******************************************************************************
 @Function	BM_MapPageAtOffset

 @Description	utility function check if the specificed offset in a BM mapping
                is a page that needs tp be mapped

 @Input     hBMHandle - Handle to BM mapping

 @Input     ui32Offset - Offset into import

 @Return	IMG_TRUE if the page should be mapped
**************************************************************************/
IMG_BOOL BM_MapPageAtOffset(IMG_HANDLE hBMHandle, IMG_UINT32 ui32Offset);

/*!
******************************************************************************
 @Function	BM_VirtOffsetToPhyscial

 @Description	utility function find of physical offset of a sparse allocation
                from it's virtual offset.

 @Input     hBMHandle - Handle to BM mapping

 @Input     ui32VirtOffset - Virtual offset into allocation
 
 @Output    pui32PhysOffset - Physical offset

 @Return	IMG_TRUE if the virtual offset is physically backed
**************************************************************************/
IMG_BOOL BM_VirtOffsetToPhysical(IMG_HANDLE hBMHandle,
								   IMG_UINT32 ui32VirtOffset,
								   IMG_UINT32 *pui32PhysOffset);

/* The following are present for the "share mem" workaround for
   cross-process mapping.  This is only valid for a specific
   use-case, and only tested on Linux (Android) and only
   superficially at that.  Do not rely on this API! */
/* The two "Set" functions set a piece of "global" state in the buffer
   manager, and "Unset" removes this global state.  Therefore, there
   is no thread-safety here and it's the caller's responsibility to
   ensure that a mutex is acquired before using these functions or any
   device memory allocation functions, including, especially,
   callbacks from RA. */
/* Once a "Share Index" is set by this means, any requests from the RA
   to import a block of physical memory shall cause the physical
   memory allocation to be refcounted, and shared iff the IDs chosen
   match */
/* This API is difficult to use, but saves a lot of plumbing in other
   APIs.  The next generation of this library should have this functionality 
   plumbed in properly */
PVRSRV_ERROR BM_XProcWorkaroundSetShareIndex(IMG_UINT32 ui32Index);
PVRSRV_ERROR BM_XProcWorkaroundUnsetShareIndex(IMG_UINT32 ui32Index);
PVRSRV_ERROR BM_XProcWorkaroundFindNewBufferAndSetShareIndex(IMG_UINT32 *pui32Index);

#if defined(PVRSRV_REFCOUNT_DEBUG)
IMG_VOID _BM_XProcIndexAcquireDebug(const IMG_CHAR *pszFile, IMG_INT iLine, IMG_UINT32 ui32Index);
IMG_VOID _BM_XProcIndexReleaseDebug(const IMG_CHAR *pszFile, IMG_INT iLine, IMG_UINT32 ui32Index);

#define BM_XProcIndexAcquire(x...) \
	_BM_XProcIndexAcquireDebug(__FILE__, __LINE__, x)
#define BM_XProcIndexRelease(x...) \
	_BM_XProcIndexReleaseDebug(__FILE__, __LINE__, x)

#else
IMG_VOID _BM_XProcIndexAcquire(IMG_UINT32 ui32Index);
IMG_VOID _BM_XProcIndexRelease(IMG_UINT32 ui32Index);


#define BM_XProcIndexAcquire(x) \
	_BM_XProcIndexAcquire( x)
#define BM_XProcIndexRelease(x) \
	_BM_XProcIndexRelease( x)
#endif


#if defined(__cplusplus)
}
#endif

#endif
