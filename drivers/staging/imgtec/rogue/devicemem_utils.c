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

#include "allocmem.h"
#include "img_types.h"
#include "pvrsrv_error.h"
#include "ra.h"
#include "devicemem_utils.h"
#include "client_mm_bridge.h"

/*
	SVM heap management support functions for CPU (un)mapping
*/
#define DEVMEM_MAP_SVM_USER_MANAGED_RETRY				2

static inline PVRSRV_ERROR 
_DevmemCPUMapSVMKernelManaged(DEVMEM_HEAP *psHeap,
							  DEVMEM_IMPORT *psImport,
							  IMG_UINT64 *ui64MapAddress)
{
	PVRSRV_ERROR eError;
	IMG_UINT64 ui64SvmMapAddr;
	IMG_UINT64 ui64SvmMapAddrEnd;
	IMG_UINT64 ui64SvmHeapAddrEnd;

	/* SVM heap management is always XXX_KERNEL_MANAGED unless we
	   have triggered the fall back code-path in which case we
	   should not be calling into this code-path */
	PVR_ASSERT(psHeap->eHeapType == DEVMEM_HEAP_TYPE_KERNEL_MANAGED);

	/* By acquiring the CPU virtual address here, it essentially
	   means we lock-down the virtual address for the duration
	   of the life-cycle of the allocation until a de-allocation
	   request comes in. Thus the allocation is guaranteed not to
	   change its virtual address on the CPU during its life-time. 
	   NOTE: Import might have already been CPU Mapped before now,
	   normally this is not a problem, see fall back */
	eError = _DevmemImportStructCPUMap(psImport);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Unable to CPU map (lock-down) device memory for SVM use",
				__func__));
		eError = PVRSRV_ERROR_DEVICEMEM_MAP_FAILED;
		goto failSVM;
	}

	/* Supplied kernel mmap virtual address is also device virtual address;
	   calculate the heap & kernel supplied mmap virtual address limits */
	ui64SvmMapAddr = (IMG_UINT64)(uintptr_t)psImport->sCPUImport.pvCPUVAddr;
	ui64SvmHeapAddrEnd = psHeap->sBaseAddress.uiAddr + psHeap->uiSize;
	ui64SvmMapAddrEnd = ui64SvmMapAddr + psImport->uiSize;
	PVR_ASSERT(ui64SvmMapAddr != (IMG_UINT64)0);

	/* SVM limit test may fail if processor has more virtual address bits than device */
	if (ui64SvmMapAddr >= ui64SvmHeapAddrEnd || ui64SvmMapAddrEnd > ui64SvmHeapAddrEnd)
	{
		/* Unmap incompatible SVM virtual address, this
		   may not release address if it was elsewhere
		   CPU Mapped before call into this function */
		_DevmemImportStructCPUUnmap(psImport);

		/* Flag incompatible SVM mapping */
		eError = PVRSRV_ERROR_BAD_MAPPING;
		goto failSVM;
	}

	*ui64MapAddress = ui64SvmMapAddr;
failSVM:
	/* either OK, MAP_FAILED or BAD_MAPPING */
	return eError;
}

static inline void 
_DevmemCPUUnmapSVMKernelManaged(DEVMEM_HEAP *psHeap, DEVMEM_IMPORT *psImport)
{
	PVR_UNREFERENCED_PARAMETER(psHeap);
	_DevmemImportStructCPUUnmap(psImport);
}

static inline PVRSRV_ERROR 
_DevmemCPUMapSVMUserManaged(DEVMEM_HEAP *psHeap,
							DEVMEM_IMPORT *psImport,
							IMG_UINT uiAlign,
							IMG_UINT64 *ui64MapAddress)
{
	RA_LENGTH_T uiAllocatedSize;
	RA_BASE_T uiAllocatedAddr;
	IMG_UINT64 ui64SvmMapAddr;
	IMG_UINT uiRetry = 0;
	PVRSRV_ERROR eError;

	/* If SVM heap management has transitioned to XXX_USER_MANAGED,
	   this is essentially a fall back approach that ensures we
	   continue to satisfy SVM alloc. This approach is not without
	   hazards in that we may specify a virtual address that is
	   already in use by the user process */
	PVR_ASSERT(psHeap->eHeapType == DEVMEM_HEAP_TYPE_USER_MANAGED);

	/* Normally, for SVM heap allocations, CPUMap _must_  be done
	   before DevMap; ideally the initial CPUMap should be done by
	   SVM functions though this is not a hard requirement as long
	   as the prior elsewhere obtained CPUMap virtual address meets
	   SVM address requirements. This is a fall-back code-pathway
	   so we have to test that this assumption holds before we 
	   progress any further */
	OSLockAcquire(psImport->sCPUImport.hLock);

	if (psImport->sCPUImport.ui32RefCount)
	{
		/* Already CPU Mapped SVM heap allocation, this prior elsewhere
		   obtained virtual address is  responsible for the above 
		   XXX_KERNEL_MANAGED failure. As we are not responsible for 
		   this, we cannot progress any further so need to fail */
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Previously obtained CPU map address not SVM compatible"
				, __func__));

		/* Revert SVM heap to DEVMEM_HEAP_TYPE_KERNEL_MANAGED */
		psHeap->eHeapType = DEVMEM_HEAP_TYPE_KERNEL_MANAGED;
		PVR_DPF((PVR_DBG_MESSAGE,
				"%s: Reverting SVM heap back to kernel managed",
				__func__));

		OSLockRelease(psImport->sCPUImport.hLock);

		/* Do we need a more specific error code here */
		eError = PVRSRV_ERROR_DEVICEMEM_ALREADY_MAPPED;
		goto failSVM;
	}

	OSLockRelease(psImport->sCPUImport.hLock);

	do
	{
		/* Next we proceed to instruct the kernel to use the RA_Alloc supplied
		   virtual address to map-in this SVM import suballocation; there is no
		   guarantee that this RA_Alloc virtual address may not collide with an
		   already in-use VMA range in the process */
		eError = RA_Alloc(psHeap->psQuantizedVMRA,
						psImport->uiSize,
						RA_NO_IMPORT_MULTIPLIER,
						0, /* flags: this RA doesn't use flags*/
						uiAlign,
						"SVM_Virtual_Alloc",
						&uiAllocatedAddr,
						&uiAllocatedSize,
						NULL /* don't care about per-import priv data */);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s: Cannot RA allocate SVM compatible address",
					__func__));
			goto failSVM;
		}

		/* No reason for allocated virtual size to be different from
		   the PMR's size */
		psImport->sCPUImport.pvCPUVAddr = (void*)(uintptr_t)uiAllocatedAddr;
		PVR_ASSERT(uiAllocatedSize == psImport->uiSize);
			
		/* Map the import or allocation using the RA_Alloc virtual address;
		   the kernel may fail the request if the supplied virtual address
		   is already in-use in which case we re-try using another virtual
		   address obtained from the RA_Alloc */
		eError = _DevmemImportStructCPUMap(psImport);
		if (eError != PVRSRV_OK)
		{
			/* For now we simply discard failed RA_Alloc() obtained virtual 
			   address (i.e. plenty of virtual space), this prevents us from
			   re-using these and furthermore essentially blacklists these
			   addresses from future SVM consideration; We exit fall-back
			   attempt if retry exceeds the fall-back retry limit */
			if (uiRetry++ > DEVMEM_MAP_SVM_USER_MANAGED_RETRY)
			{
				PVR_DPF((PVR_DBG_ERROR,
						"%s: Cannot find SVM compatible address, bad mapping",
						__func__));
				eError = PVRSRV_ERROR_BAD_MAPPING;
				goto failSVM;
			}
		}
		else
		{
			/* Found compatible SVM virtual address, set as device virtual address */
			ui64SvmMapAddr = (IMG_UINT64)(uintptr_t)psImport->sCPUImport.pvCPUVAddr;
		}
	} while (eError != PVRSRV_OK);

	*ui64MapAddress = ui64SvmMapAddr;
failSVM:	
	return eError;
}

static inline void 
_DevmemCPUUnmapSVMUserManaged(DEVMEM_HEAP *psHeap, DEVMEM_IMPORT *psImport)
{
	RA_BASE_T uiAllocatedAddr;

	/* We only free SVM compatible addresses, all addresses in
	   the blacklist are essentially excluded from future RA_Alloc */
	uiAllocatedAddr = psImport->sDeviceImport.sDevVAddr.uiAddr;
	RA_Free(psHeap->psQuantizedVMRA, uiAllocatedAddr);

	_DevmemImportStructCPUUnmap(psImport);
}

static inline PVRSRV_ERROR 
_DevmemImportStructDevMapSVM(DEVMEM_HEAP *psHeap,
							 DEVMEM_IMPORT *psImport,
							 IMG_UINT uiAlign,
							 IMG_UINT64 *ui64MapAddress)
{
	PVRSRV_ERROR eError;

	switch(psHeap->eHeapType)
	{
		case DEVMEM_HEAP_TYPE_KERNEL_MANAGED:
			eError = _DevmemCPUMapSVMKernelManaged(psHeap,
												   psImport,
												   ui64MapAddress);
			if (eError == PVRSRV_ERROR_BAD_MAPPING)
			{
				/* If the SVM map address is outside of SVM heap limits,
				   change heap type to DEVMEM_HEAP_TYPE_USER_MANAGED */
				psHeap->eHeapType = DEVMEM_HEAP_TYPE_USER_MANAGED;
				PVR_DPF((PVR_DBG_MESSAGE,
					"%s: Kernel managed SVM heap is now user managed",
					__func__));

				/* Retry using user managed fall-back approach */
				eError = _DevmemCPUMapSVMUserManaged(psHeap,
													 psImport,
													 uiAlign,
													 ui64MapAddress);
			}
			break;

		case DEVMEM_HEAP_TYPE_USER_MANAGED:
			eError = _DevmemCPUMapSVMUserManaged(psHeap,
												 psImport,
												 uiAlign,
												 ui64MapAddress);
			break;

		default:
			eError = PVRSRV_ERROR_INVALID_PARAMS;
			break;
	}

	return eError;
}

static inline void 
_DevmemImportStructDevUnmapSVM(DEVMEM_HEAP *psHeap, DEVMEM_IMPORT *psImport)
{
	switch(psHeap->eHeapType)
	{
		case DEVMEM_HEAP_TYPE_KERNEL_MANAGED:
			_DevmemCPUUnmapSVMKernelManaged(psHeap, psImport);
			break;

		case DEVMEM_HEAP_TYPE_USER_MANAGED:
			_DevmemCPUUnmapSVMUserManaged(psHeap, psImport);
			break;

		default:
			break;
	}
}

/*
	The Devmem import structure is the structure we use
	to manage memory that is "imported" (which is page
	granular) from the server into our process, this
	includes allocations.

	This allows memory to be imported without requiring
	any CPU or device mapping. Memory can then be mapped
	into the device or CPU on demand, but neither is
	required.
*/

IMG_INTERNAL
void _DevmemImportStructAcquire(DEVMEM_IMPORT *psImport)
{
	IMG_INT iRefCount = OSAtomicIncrement(&psImport->hRefCount);
	PVR_UNREFERENCED_PARAMETER(iRefCount);
	PVR_ASSERT(iRefCount != 1);

	DEVMEM_REFCOUNT_PRINT("%s (%p) %d->%d",
					__FUNCTION__,
					psImport,
					iRefCount-1,
					iRefCount);
}

IMG_INTERNAL
void _DevmemImportStructRelease(DEVMEM_IMPORT *psImport)
{
	IMG_INT iRefCount = OSAtomicDecrement(&psImport->hRefCount);
	PVR_ASSERT(iRefCount >= 0);

	DEVMEM_REFCOUNT_PRINT("%s (%p) %d->%d",
					__FUNCTION__,
					psImport,
					iRefCount+1,
					iRefCount);

	if (iRefCount == 0)
	{
		BridgePMRUnrefPMR(psImport->hDevConnection,
						  psImport->hPMR);
		OSLockDestroy(psImport->sCPUImport.hLock);
		OSLockDestroy(psImport->sDeviceImport.hLock);
		OSLockDestroy(psImport->hLock);
#if defined(PDUMP)
		OSFreeMem(psImport->pszAnnotation);
#endif
		OSFreeMem(psImport);
	}
}

IMG_INTERNAL
void _DevmemImportDiscard(DEVMEM_IMPORT *psImport)
{
	PVR_ASSERT(OSAtomicRead(&psImport->hRefCount) == 0);
	OSLockDestroy(psImport->sCPUImport.hLock);
	OSLockDestroy(psImport->sDeviceImport.hLock);
	OSLockDestroy(psImport->hLock);
	OSFreeMem(psImport);
}

IMG_INTERNAL
PVRSRV_ERROR _DevmemMemDescAlloc(DEVMEM_MEMDESC **ppsMemDesc)
{
	DEVMEM_MEMDESC *psMemDesc;
	PVRSRV_ERROR eError;

	psMemDesc = OSAllocMem(sizeof(DEVMEM_MEMDESC));

	if (psMemDesc == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto failAlloc;
	}
	
	/* Structure must be zero'd incase it needs to be freed before it is initialised! */
	OSCachedMemSet(psMemDesc, 0, sizeof(DEVMEM_MEMDESC));

	eError = OSLockCreate(&psMemDesc->hLock, LOCK_TYPE_PASSIVE);
	if (eError != PVRSRV_OK)
	{
		goto failMDLock;
	}

	eError = OSLockCreate(&psMemDesc->sDeviceMemDesc.hLock, LOCK_TYPE_PASSIVE);
	if (eError != PVRSRV_OK)
	{
		goto failDMDLock;
	}

	eError = OSLockCreate(&psMemDesc->sCPUMemDesc.hLock, LOCK_TYPE_PASSIVE);
	if (eError != PVRSRV_OK)
	{
		goto failCMDLock;
	}

	*ppsMemDesc = psMemDesc;

	return PVRSRV_OK;

failCMDLock:
	OSLockDestroy(psMemDesc->sDeviceMemDesc.hLock);
failDMDLock:
	OSLockDestroy(psMemDesc->hLock);
failMDLock:
	OSFreeMem(psMemDesc);
failAlloc:
	PVR_ASSERT(eError != PVRSRV_OK);

	return eError;
}

/*
	Init the MemDesc structure
*/
IMG_INTERNAL
void _DevmemMemDescInit(DEVMEM_MEMDESC *psMemDesc,
										  IMG_DEVMEM_OFFSET_T uiOffset,
										  DEVMEM_IMPORT *psImport,
										  IMG_DEVMEM_SIZE_T uiSize)
{
	DEVMEM_REFCOUNT_PRINT("%s (%p) %d->%d",
					__FUNCTION__,
					psMemDesc,
					0,
					1);

	psMemDesc->psImport = psImport;
	psMemDesc->uiOffset = uiOffset;

	psMemDesc->sDeviceMemDesc.ui32RefCount = 0;
	psMemDesc->sCPUMemDesc.ui32RefCount = 0;
	psMemDesc->uiAllocSize = uiSize;
	psMemDesc->hPrivData = NULL;

#if defined(SUPPORT_PAGE_FAULT_DEBUG)
	psMemDesc->sTraceData.ui32AllocationIndex = DEVICEMEM_HISTORY_ALLOC_INDEX_NONE;
#endif

	OSAtomicWrite(&psMemDesc->hRefCount, 1);
}

IMG_INTERNAL
void _DevmemMemDescAcquire(DEVMEM_MEMDESC *psMemDesc)
{
	IMG_INT iRefCount = 0;

	iRefCount = OSAtomicIncrement(&psMemDesc->hRefCount);
	DEVMEM_REFCOUNT_PRINT("%s (%p) %d->%d",
					__FUNCTION__,
					psMemDesc,
					iRefCount-1,
					iRefCount);
}

IMG_INTERNAL
void _DevmemMemDescRelease(DEVMEM_MEMDESC *psMemDesc)
{
	IMG_INT iRefCount;
	PVR_ASSERT(psMemDesc != NULL);
	
	iRefCount = OSAtomicDecrement(&psMemDesc->hRefCount);
	PVR_ASSERT(iRefCount >= 0);

	DEVMEM_REFCOUNT_PRINT("%s (%p) %d->%d",
					__FUNCTION__,
					psMemDesc,
					iRefCount+1,
					iRefCount);

	if (iRefCount == 0)
	{
		if (psMemDesc->psImport->uiProperties & DEVMEM_PROPERTIES_SUBALLOCATABLE)
		{
			/* As soon as the first sub-allocation on the psImport is freed
			 * we might get dirty memory when reusing it.
			 * We have to delete the ZEROED & CLEAN flag */

			psMemDesc->psImport->uiProperties &= ~DEVMEM_PROPERTIES_IMPORT_IS_ZEROED;
			psMemDesc->psImport->uiProperties &= ~DEVMEM_PROPERTIES_IMPORT_IS_CLEAN;

			RA_Free(psMemDesc->psImport->sDeviceImport.psHeap->psSubAllocRA,
					psMemDesc->psImport->sDeviceImport.sDevVAddr.uiAddr +
					psMemDesc->uiOffset);
		}
		else
		{
			_DevmemImportStructRelease(psMemDesc->psImport);
		}

		OSLockDestroy(psMemDesc->sCPUMemDesc.hLock);
		OSLockDestroy(psMemDesc->sDeviceMemDesc.hLock);
		OSLockDestroy(psMemDesc->hLock);
		OSFreeMem(psMemDesc);
	}
}

IMG_INTERNAL
void _DevmemMemDescDiscard(DEVMEM_MEMDESC *psMemDesc)
{
	PVR_ASSERT(OSAtomicRead(&psMemDesc->hRefCount) == 0);

	OSLockDestroy(psMemDesc->sCPUMemDesc.hLock);
	OSLockDestroy(psMemDesc->sDeviceMemDesc.hLock);
	OSLockDestroy(psMemDesc->hLock);
	OSFreeMem(psMemDesc);
}


IMG_INTERNAL
PVRSRV_ERROR _DevmemValidateParams(IMG_DEVMEM_SIZE_T uiSize,
                                   IMG_DEVMEM_ALIGN_T uiAlign,
                                   DEVMEM_FLAGS_T *puiFlags)
{
	if ((*puiFlags & PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC) &&
	    (*puiFlags & PVRSRV_MEMALLOCFLAG_POISON_ON_ALLOC))
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Zero on Alloc and Poison on Alloc are mutually exclusive.",
		         __FUNCTION__));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (uiAlign & (uiAlign-1))
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: The requested alignment is not a power of two.",
		         __FUNCTION__));
		return PVRSRV_ERROR_INVALID_PARAMS;
 	}

	if (uiSize == 0)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Please request a non-zero size value.",
		         __FUNCTION__));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* If zero flag is set we have to have write access to the page. */
	if (PVRSRV_CHECK_ZERO_ON_ALLOC(*puiFlags) || PVRSRV_CHECK_CPU_WRITEABLE(*puiFlags))
	{
		(*puiFlags) |= PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
		             PVRSRV_MEMALLOCFLAG_CPU_READABLE;
	}

	return PVRSRV_OK;
}

/*
	Allocate and init an import structure
*/
IMG_INTERNAL
PVRSRV_ERROR _DevmemImportStructAlloc(SHARED_DEV_CONNECTION hDevConnection,
									  DEVMEM_IMPORT **ppsImport)
{
	DEVMEM_IMPORT *psImport;
	PVRSRV_ERROR eError;

    psImport = OSAllocMem(sizeof *psImport);
    if (psImport == NULL)
    {
        return PVRSRV_ERROR_OUT_OF_MEMORY;
    }

#if defined (PDUMP)
	/* Make sure this points nowhere as long as we don't need it */
	psImport->pszAnnotation = NULL;
#endif

	/* Setup some known bad values for things we don't have yet */
	psImport->sDeviceImport.hReservation = LACK_OF_RESERVATION_POISON;
    psImport->sDeviceImport.hMapping = LACK_OF_MAPPING_POISON;
    psImport->sDeviceImport.psHeap = NULL;
    psImport->sDeviceImport.bMapped = IMG_FALSE;

	eError = OSLockCreate(&psImport->sDeviceImport.hLock, LOCK_TYPE_PASSIVE);
	if (eError != PVRSRV_OK)
	{
		goto failDIOSLockCreate;
	}

	psImport->sCPUImport.hOSMMapData = NULL;
	psImport->sCPUImport.pvCPUVAddr = NULL;

	eError = OSLockCreate(&psImport->sCPUImport.hLock, LOCK_TYPE_PASSIVE);
	if (eError != PVRSRV_OK)
	{
		goto failCIOSLockCreate;
	}

	/* Set up common elements */
    psImport->hDevConnection = hDevConnection;

    /* Setup properties */
    psImport->uiProperties = 0;

	/* Setup refcounts */
    psImport->sDeviceImport.ui32RefCount = 0;
    psImport->sCPUImport.ui32RefCount = 0;
    OSAtomicWrite(&psImport->hRefCount, 0);

	/* Create the lock */
	eError = OSLockCreate(&psImport->hLock, LOCK_TYPE_PASSIVE);
	if (eError != PVRSRV_OK)
	{
		goto failILockAlloc;
	}

    *ppsImport = psImport;
    
    return PVRSRV_OK;

failILockAlloc:
	OSLockDestroy(psImport->sCPUImport.hLock);
failCIOSLockCreate:
	OSLockDestroy(psImport->sDeviceImport.hLock);
failDIOSLockCreate:
	OSFreeMem(psImport);
	PVR_ASSERT(eError != PVRSRV_OK);

	return eError;
}

/*
	Initialise the import structure
*/
IMG_INTERNAL
void _DevmemImportStructInit(DEVMEM_IMPORT *psImport,
								 IMG_DEVMEM_SIZE_T uiSize,
								 IMG_DEVMEM_ALIGN_T uiAlign,
								 DEVMEM_FLAGS_T uiFlags,
								 IMG_HANDLE hPMR,
								 DEVMEM_PROPERTIES_T uiProperties)
{
	DEVMEM_REFCOUNT_PRINT("%s (%p) %d->%d",
					__FUNCTION__,
					psImport,
					0,
					1);

	psImport->uiSize = uiSize;
	psImport->uiAlign = uiAlign;
	psImport->uiFlags = uiFlags;
	psImport->hPMR = hPMR;
	psImport->uiProperties = uiProperties;
	OSAtomicWrite(&psImport->hRefCount, 1);
}

/*
	Map an import to the device
*/
IMG_INTERNAL
PVRSRV_ERROR _DevmemImportStructDevMap(DEVMEM_HEAP *psHeap,
                                       IMG_BOOL bMap,
                                       DEVMEM_IMPORT *psImport,
                                       IMG_UINT64 ui64OptionalMapAddress)
{
	DEVMEM_DEVICE_IMPORT *psDeviceImport;
	RA_BASE_T uiAllocatedAddr;
	RA_LENGTH_T uiAllocatedSize;
	IMG_DEV_VIRTADDR sBase;
	IMG_HANDLE hReservation;
	PVRSRV_ERROR eError;
	IMG_UINT uiAlign;

	/* Round the provided import alignment to the configured heap alignment */
	uiAlign = 1ULL << psHeap->uiLog2ImportAlignment;
	uiAlign = (psImport->uiAlign + uiAlign - 1) & ~(uiAlign-1);

	psDeviceImport = &psImport->sDeviceImport;

	OSLockAcquire(psDeviceImport->hLock);
	DEVMEM_REFCOUNT_PRINT("%s (%p) %d->%d",
					__FUNCTION__,
					psImport,
					psDeviceImport->ui32RefCount,
					psDeviceImport->ui32RefCount+1);

	if (psDeviceImport->ui32RefCount++ == 0)
	{
		_DevmemImportStructAcquire(psImport);

		OSAtomicIncrement(&psHeap->hImportCount);

		if (PVRSRV_CHECK_SVM_ALLOC(psImport->uiFlags))
		{
			/*  SVM (shared virtual memory) imports or allocations always
				need to acquire CPU virtual address first as address is
				used to map the allocation into the device virtual address
				space; i.e. the virtual address of the allocation for both
				the CPU/GPU must be identical. */
			eError = _DevmemImportStructDevMapSVM(psHeap,
												  psImport,
												  uiAlign,
												  &ui64OptionalMapAddress);
			if (eError != PVRSRV_OK)
			{
				goto failVMRAAlloc;
			}
		}

		if (ui64OptionalMapAddress == 0)
		{
			if (psHeap->eHeapType == DEVMEM_HEAP_TYPE_USER_MANAGED ||
				psHeap->eHeapType == DEVMEM_HEAP_TYPE_KERNEL_MANAGED)
			{
				PVR_DPF((PVR_DBG_ERROR,
						psHeap->eHeapType == DEVMEM_HEAP_TYPE_USER_MANAGED ?
						"%s: Heap is user managed, please use PVRSRVMapToDeviceAddress().":
						"%s: Heap is kernel managed, use right allocation flags (e.g. SVM).",
						__func__));
				eError = PVRSRV_ERROR_INVALID_PARAMS;
				goto failVMRAAlloc;
			}
			psHeap->eHeapType = DEVMEM_HEAP_TYPE_RA_MANAGED;

			/* Allocate space in the VM */
			eError = RA_Alloc(psHeap->psQuantizedVMRA,
			                  psImport->uiSize,
			                  RA_NO_IMPORT_MULTIPLIER,
			                  0, /* flags: this RA doesn't use flags*/
			                  uiAlign,
			                  "Virtual_Alloc",
			                  &uiAllocatedAddr,
			                  &uiAllocatedSize,
			                  NULL /* don't care about per-import priv data */
			                  );
			if (PVRSRV_OK != eError)
			{
				eError = PVRSRV_ERROR_DEVICEMEM_OUT_OF_DEVICE_VM;
				goto failVMRAAlloc;
			}

			/* No reason for the allocated virtual size to be different from
			   the PMR's size */
			PVR_ASSERT(uiAllocatedSize == psImport->uiSize);

			sBase.uiAddr = uiAllocatedAddr;

		}
		else
		{
			IMG_UINT64 uiHeapAddrEnd;

			switch (psHeap->eHeapType)
			{
				case DEVMEM_HEAP_TYPE_UNKNOWN:
					/* DEVMEM_HEAP_TYPE_USER_MANAGED can apply to _any_
					   heap and can only be determined here. This heap
					   type transitions from DEVMEM_HEAP_TYPE_UNKNOWN
					   to DEVMEM_HEAP_TYPE_USER_MANAGED on 1st alloc */
					psHeap->eHeapType = DEVMEM_HEAP_TYPE_USER_MANAGED;
					break;

				case DEVMEM_HEAP_TYPE_USER_MANAGED:
				case DEVMEM_HEAP_TYPE_KERNEL_MANAGED:
					if (! psHeap->uiSize)
					{
						PVR_DPF((PVR_DBG_ERROR,
							psHeap->eHeapType == DEVMEM_HEAP_TYPE_USER_MANAGED ?
							"%s: Heap DEVMEM_HEAP_TYPE_USER_MANAGED is disabled.":
							"%s: Heap DEVMEM_HEAP_TYPE_KERNEL_MANAGED is disabled."
							, __func__));
						eError = PVRSRV_ERROR_INVALID_HEAP;
						goto failVMRAAlloc;
					}
					break;

				case DEVMEM_HEAP_TYPE_RA_MANAGED:
					PVR_DPF((PVR_DBG_ERROR,
						"%s: This heap is managed by an RA, please use PVRSRVMapToDevice()"
						" and don't use allocation flags that assume differently (e.g. SVM)."
						, __func__));
					eError = PVRSRV_ERROR_INVALID_PARAMS;
					goto failVMRAAlloc;

				default:
					break;
			}

			/* Ensure supplied ui64OptionalMapAddress is within heap range */
			uiHeapAddrEnd = psHeap->sBaseAddress.uiAddr + psHeap->uiSize;
			if (ui64OptionalMapAddress >= uiHeapAddrEnd ||
				ui64OptionalMapAddress + psImport->uiSize > uiHeapAddrEnd)
			{
				PVR_DPF((PVR_DBG_ERROR,
						"%s: ui64OptionalMapAddress %p is outside of heap limits <%p:%p>."
						, __func__
						, (void*)(uintptr_t)ui64OptionalMapAddress
						, (void*)(uintptr_t)psHeap->sBaseAddress.uiAddr
						, (void*)(uintptr_t)uiHeapAddrEnd));
				eError = PVRSRV_ERROR_INVALID_PARAMS;
				goto failVMRAAlloc;
			}

			if (ui64OptionalMapAddress & ((1 << psHeap->uiLog2Quantum) - 1))
			{
				PVR_DPF((PVR_DBG_ERROR,
						"%s: Invalid address to map to. Please prove an address aligned to"
						"a page multiple of the heap."
						, __func__));
				eError = PVRSRV_ERROR_INVALID_PARAMS;
				goto failVMRAAlloc;
			}

			uiAllocatedAddr = ui64OptionalMapAddress;

			if (psImport->uiSize & ((1 << psHeap->uiLog2Quantum) - 1))
			{
				PVR_DPF((PVR_DBG_ERROR,
						"%s: Invalid heap to map to. "
						"Please choose a heap that can handle smaller page sizes."
						, __func__));
				eError = PVRSRV_ERROR_INVALID_PARAMS;
				goto failVMRAAlloc;
			}
			uiAllocatedSize = psImport->uiSize;
			sBase.uiAddr = uiAllocatedAddr;
		}
	
		/* Setup page tables for the allocated VM space */
		eError = BridgeDevmemIntReserveRange(psHeap->psCtx->hDevConnection,
											 psHeap->hDevMemServerHeap,
											 sBase,
											 uiAllocatedSize,
											 &hReservation);
		if (eError != PVRSRV_OK)
		{
			goto failReserve;
		}

		if (bMap)
		{
			DEVMEM_FLAGS_T uiMapFlags;
			
			uiMapFlags = psImport->uiFlags & PVRSRV_MEMALLOCFLAGS_PERMAPPINGFLAGSMASK;

			/* Actually map the PMR to allocated VM space */
			eError = BridgeDevmemIntMapPMR(psHeap->psCtx->hDevConnection,
										   psHeap->hDevMemServerHeap,
										   hReservation,
										   psImport->hPMR,
										   uiMapFlags,
										   &psDeviceImport->hMapping);
			if (eError != PVRSRV_OK)
			{
				goto failMap;
			}
			psDeviceImport->bMapped = IMG_TRUE;
		}

		/* Setup device mapping specific parts of the mapping info */
	    psDeviceImport->hReservation = hReservation;
		psDeviceImport->sDevVAddr.uiAddr = uiAllocatedAddr;
		psDeviceImport->psHeap = psHeap;
	}
	else
	{
		/*
			Check that we've been asked to map it into the
			same heap 2nd time around
		*/
		if (psHeap != psDeviceImport->psHeap)
		{
			eError = PVRSRV_ERROR_INVALID_HEAP;
			goto failParams;
		}
	}
	OSLockRelease(psDeviceImport->hLock);

	return PVRSRV_OK;

failMap:
	BridgeDevmemIntUnreserveRange(psHeap->psCtx->hDevConnection,
								  hReservation);
failReserve:
	if (ui64OptionalMapAddress == 0)
	{
		RA_Free(psHeap->psQuantizedVMRA,
				uiAllocatedAddr);
	}
failVMRAAlloc:
	_DevmemImportStructRelease(psImport);
	OSAtomicDecrement(&psHeap->hImportCount);
failParams:
	psDeviceImport->ui32RefCount--;
	OSLockRelease(psDeviceImport->hLock);
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

/*
	Unmap an import from the Device
*/
IMG_INTERNAL
void _DevmemImportStructDevUnmap(DEVMEM_IMPORT *psImport)
{
	PVRSRV_ERROR eError;
	DEVMEM_DEVICE_IMPORT *psDeviceImport;

	psDeviceImport = &psImport->sDeviceImport;

	OSLockAcquire(psDeviceImport->hLock);
	DEVMEM_REFCOUNT_PRINT("%s (%p) %d->%d",
					__FUNCTION__,
					psImport,
					psDeviceImport->ui32RefCount,
					psDeviceImport->ui32RefCount-1);

	if (--psDeviceImport->ui32RefCount == 0)
	{
		DEVMEM_HEAP *psHeap = psDeviceImport->psHeap;

		if (psDeviceImport->bMapped)
		{
			eError = BridgeDevmemIntUnmapPMR(psImport->hDevConnection,
											psDeviceImport->hMapping);
			PVR_ASSERT(eError == PVRSRV_OK);
		}
	
	    eError = BridgeDevmemIntUnreserveRange(psImport->hDevConnection,
	                                        psDeviceImport->hReservation);
	    PVR_ASSERT(eError == PVRSRV_OK);

	    psDeviceImport->bMapped = IMG_FALSE;
	    psDeviceImport->hMapping = LACK_OF_MAPPING_POISON;
	    psDeviceImport->hReservation = LACK_OF_RESERVATION_POISON;

		if (psHeap->eHeapType == DEVMEM_HEAP_TYPE_RA_MANAGED)
		{
			RA_Free(psHeap->psQuantizedVMRA,
					psDeviceImport->sDevVAddr.uiAddr);
		}

		if (PVRSRV_CHECK_SVM_ALLOC(psImport->uiFlags))
		{
			_DevmemImportStructDevUnmapSVM(psHeap, psImport);
		}

	    OSLockRelease(psDeviceImport->hLock);

		_DevmemImportStructRelease(psImport);

		OSAtomicDecrement(&psHeap->hImportCount);
	}
	else
	{
		OSLockRelease(psDeviceImport->hLock);
	}
}

/*
	Map an import into the CPU
*/
IMG_INTERNAL
PVRSRV_ERROR _DevmemImportStructCPUMap(DEVMEM_IMPORT *psImport)
{
	PVRSRV_ERROR eError;
	DEVMEM_CPU_IMPORT *psCPUImport;
	size_t uiMappingLength;

	psCPUImport = &psImport->sCPUImport;

	OSLockAcquire(psCPUImport->hLock);
	DEVMEM_REFCOUNT_PRINT("%s (%p) %d->%d",
					__FUNCTION__,
					psImport,
					psCPUImport->ui32RefCount,
					psCPUImport->ui32RefCount+1);

	if (psCPUImport->ui32RefCount++ == 0)
	{
		_DevmemImportStructAcquire(psImport);

		eError = OSMMapPMR(psImport->hDevConnection,
		                   psImport->hPMR,
		                   psImport->uiSize,
		                   psImport->uiFlags,
		                   &psCPUImport->hOSMMapData,
		                   &psCPUImport->pvCPUVAddr,
		                   &uiMappingLength);
		if (eError != PVRSRV_OK)
		{
			goto failMap;
		}

		/* There is no reason the mapping length is different to the size */
		PVR_ASSERT(uiMappingLength == psImport->uiSize);
	}
	OSLockRelease(psCPUImport->hLock);

	return PVRSRV_OK;

failMap:
	psCPUImport->ui32RefCount--;
	_DevmemImportStructRelease(psImport);
	OSLockRelease(psCPUImport->hLock);
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

/*
	Unmap an import from the CPU
*/
IMG_INTERNAL
void _DevmemImportStructCPUUnmap(DEVMEM_IMPORT *psImport)
{
	DEVMEM_CPU_IMPORT *psCPUImport;

	psCPUImport = &psImport->sCPUImport;

	OSLockAcquire(psCPUImport->hLock);
	DEVMEM_REFCOUNT_PRINT("%s (%p) %d->%d",
					__FUNCTION__,
					psImport,
					psCPUImport->ui32RefCount,
					psCPUImport->ui32RefCount-1);

	if (--psCPUImport->ui32RefCount == 0)
	{
		/* FIXME: psImport->uiSize is a 64-bit quantity where as the 5th
		 * argument to OSUnmapPMR is a 32-bit quantity on 32-bit systems
		 * hence a compiler warning of implicit cast and loss of data.
		 * Added explicit cast and assert to remove warning.
		 */
#if (defined(_WIN32) && !defined(_WIN64)) || (defined(LINUX) && defined(__i386__))
		PVR_ASSERT(psImport->uiSize<IMG_UINT32_MAX);
#endif
		OSMUnmapPMR(psImport->hDevConnection,
					psImport->hPMR,
					psCPUImport->hOSMMapData,
					psCPUImport->pvCPUVAddr,
					psImport->uiSize);

		OSLockRelease(psCPUImport->hLock);

		_DevmemImportStructRelease(psImport);
	}
	else
	{
		OSLockRelease(psCPUImport->hLock);
	}
}


