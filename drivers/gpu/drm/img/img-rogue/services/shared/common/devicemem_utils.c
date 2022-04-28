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
#include "img_defs.h"
#include "pvrsrv_error.h"
#include "ra.h"
#include "devicemem_utils.h"
#include "client_mm_bridge.h"
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)
#include "client_ri_bridge.h"
#if defined(__KERNEL__)
#include "pvrsrv.h"
#else
#include "pvr_bridge_client.h"
#endif
#endif

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#include "proc_stats.h"
#endif

/*
	SVM heap management support functions for CPU (un)mapping
 */
#define DEVMEM_MAP_SVM_USER_MANAGED_RETRY				2

static inline PVRSRV_ERROR
DevmemCPUMapSVMKernelManaged(DEVMEM_HEAP *psHeap,
		DEVMEM_IMPORT *psImport,
		IMG_UINT64 *ui64MapAddress)
{
	PVRSRV_ERROR eError;
	IMG_UINT64 ui64SvmMapAddr;
	IMG_UINT64 ui64SvmMapAddrEnd;
	IMG_UINT64 ui64SvmHeapAddrEnd;

	/* SVM heap management always has XXX_MANAGER_KERNEL unless we
	   have triggered the fall back code-path in which case we
	   should not be calling into this code-path */
	PVR_ASSERT(psHeap->ui32HeapManagerFlags == DEVMEM_HEAP_MANAGER_KERNEL);

	/* By acquiring the CPU virtual address here, it essentially
	   means we lock-down the virtual address for the duration
	   of the life-cycle of the allocation until a de-allocation
	   request comes in. Thus the allocation is guaranteed not to
	   change its virtual address on the CPU during its life-time.
	   NOTE: Import might have already been CPU Mapped before now,
	   normally this is not a problem, see fall back */
	eError = DevmemImportStructCPUMap(psImport);
	if (eError != PVRSRV_OK)
	{
		PVR_LOG_ERROR(eError, "DevmemImportStructCPUMap");
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
	if ((ui64SvmMapAddr >= ui64SvmHeapAddrEnd || ui64SvmMapAddrEnd > ui64SvmHeapAddrEnd) ||
		(ui64SvmMapAddr & ~(ui64SvmHeapAddrEnd - 1)))
	{
		/* Unmap incompatible SVM virtual address, this
		   may not release address if it was elsewhere
		   CPU Mapped before call into this function */
		DevmemImportStructCPUUnmap(psImport);

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
DevmemCPUUnmapSVMKernelManaged(DEVMEM_HEAP *psHeap, DEVMEM_IMPORT *psImport)
{
	PVR_UNREFERENCED_PARAMETER(psHeap);
	DevmemImportStructCPUUnmap(psImport);
}

static inline PVRSRV_ERROR
DevmemCPUMapSVMUserManaged(DEVMEM_HEAP *psHeap,
		DEVMEM_IMPORT *psImport,
		IMG_UINT uiAlign,
		IMG_UINT64 *ui64MapAddress)
{
	RA_LENGTH_T uiAllocatedSize;
	RA_BASE_T uiAllocatedAddr;
	IMG_UINT64 ui64SvmMapAddr;
	IMG_UINT uiRetry = 0;
	PVRSRV_ERROR eError;

	/* If SVM heap management has transitioned to XXX_MANAGER_USER,
	   this is essentially a fall back approach that ensures we
	   continue to satisfy SVM alloc. This approach is not without
	   hazards in that we may specify a virtual address that is
	   already in use by the user process */
	PVR_ASSERT(psHeap->ui32HeapManagerFlags == DEVMEM_HEAP_MANAGER_USER);

	/* Normally, for SVM heap allocations, CPUMap _must_ be done
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
		   obtained virtual address is responsible for the above
		   XXX_MANAGER_KERNEL failure. As we are not responsible for
		   this, we cannot progress any further so need to fail */
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Previously obtained CPU map address not SVM compatible"
				, __func__));

		/* Revert SVM heap to DEVMEM_HEAP_MANAGER_KERNEL */
		psHeap->ui32HeapManagerFlags = DEVMEM_HEAP_MANAGER_KERNEL;
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
			PVR_LOG_ERROR(eError, "RA_Alloc");
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
			if (eError == PVRSRV_ERROR_RA_REQUEST_ALLOC_FAIL)
			{
				PVRSRV_ERROR eErr;
				eErr = BridgePVRSRVUpdateOOMStats(GetBridgeHandle(psHeap->psCtx->hDevConnection),
								  PVRSRV_PROCESS_STAT_TYPE_OOM_VIRTMEM_COUNT,
								  OSGetCurrentProcessID());
				PVR_LOG_IF_ERROR(eErr, "BridgePVRSRVUpdateOOMStats");
			}
#endif
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
		eError = DevmemImportStructCPUMap(psImport);
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
DevmemCPUUnmapSVMUserManaged(DEVMEM_HEAP *psHeap, DEVMEM_IMPORT *psImport)
{
	RA_BASE_T uiAllocatedAddr;

	/* We only free SVM compatible addresses, all addresses in
	   the blacklist are essentially excluded from future RA_Alloc */
	uiAllocatedAddr = psImport->sDeviceImport.sDevVAddr.uiAddr;
	RA_Free(psHeap->psQuantizedVMRA, uiAllocatedAddr);

	DevmemImportStructCPUUnmap(psImport);
}

static inline PVRSRV_ERROR
DevmemImportStructDevMapSVM(DEVMEM_HEAP *psHeap,
		DEVMEM_IMPORT *psImport,
		IMG_UINT uiAlign,
		IMG_UINT64 *ui64MapAddress)
{
	PVRSRV_ERROR eError;

	switch (psHeap->ui32HeapManagerFlags)
	{
	case DEVMEM_HEAP_MANAGER_KERNEL:
		eError = DevmemCPUMapSVMKernelManaged(psHeap,
				psImport,
				ui64MapAddress);
		if (eError == PVRSRV_ERROR_BAD_MAPPING)
		{
			/* If the SVM map address is outside of SVM heap limits,
				   change heap type to DEVMEM_HEAP_MANAGER_USER */
			psHeap->ui32HeapManagerFlags = DEVMEM_HEAP_MANAGER_USER;

			PVR_DPF((PVR_DBG_WARNING,
					"%s: Kernel managed SVM heap is now user managed",
					__func__));

			/* Retry using user managed fall-back approach */
			eError = DevmemCPUMapSVMUserManaged(psHeap,
					psImport,
					uiAlign,
					ui64MapAddress);
		}
		break;

	case DEVMEM_HEAP_MANAGER_USER:
		eError = DevmemCPUMapSVMUserManaged(psHeap,
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
DevmemImportStructDevUnmapSVM(DEVMEM_HEAP *psHeap, DEVMEM_IMPORT *psImport)
{
	switch (psHeap->ui32HeapManagerFlags)
	{
	case DEVMEM_HEAP_MANAGER_KERNEL:
		DevmemCPUUnmapSVMKernelManaged(psHeap, psImport);
		break;

	case DEVMEM_HEAP_MANAGER_USER:
		DevmemCPUUnmapSVMUserManaged(psHeap, psImport);
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
void DevmemImportStructAcquire(DEVMEM_IMPORT *psImport)
{
	IMG_INT iRefCount = OSAtomicIncrement(&psImport->hRefCount);
	PVR_UNREFERENCED_PARAMETER(iRefCount);
	PVR_ASSERT(iRefCount != 1);

	DEVMEM_REFCOUNT_PRINT("%s (%p) %d->%d",
			__func__,
			psImport,
			iRefCount-1,
			iRefCount);
}

IMG_INTERNAL
IMG_BOOL DevmemImportStructRelease(DEVMEM_IMPORT *psImport)
{
	IMG_INT iRefCount = OSAtomicDecrement(&psImport->hRefCount);
	PVR_ASSERT(iRefCount >= 0);

	DEVMEM_REFCOUNT_PRINT("%s (%p) %d->%d",
			__func__,
			psImport,
			iRefCount+1,
			iRefCount);

	if (iRefCount == 0)
	{
		BridgePMRUnrefPMR(GetBridgeHandle(psImport->hDevConnection),
				psImport->hPMR);
		OSLockDestroy(psImport->sCPUImport.hLock);
		OSLockDestroy(psImport->sDeviceImport.hLock);
		OSLockDestroy(psImport->hLock);
		OSFreeMem(psImport);

		return IMG_TRUE;
	}

	return IMG_FALSE;
}

IMG_INTERNAL
void DevmemImportDiscard(DEVMEM_IMPORT *psImport)
{
	PVR_ASSERT(OSAtomicRead(&psImport->hRefCount) == 0);
	OSLockDestroy(psImport->sCPUImport.hLock);
	OSLockDestroy(psImport->sDeviceImport.hLock);
	OSLockDestroy(psImport->hLock);
	OSFreeMem(psImport);
}

IMG_INTERNAL
PVRSRV_ERROR DevmemMemDescAlloc(DEVMEM_MEMDESC **ppsMemDesc)
{
	DEVMEM_MEMDESC *psMemDesc;
	PVRSRV_ERROR eError;

	/* Must be zeroed in case it needs to be freed before it is initialised */
	psMemDesc = OSAllocZMem(sizeof(DEVMEM_MEMDESC));
	PVR_GOTO_IF_NOMEM(psMemDesc, eError, failAlloc);

	eError = OSLockCreate(&psMemDesc->hLock);
	PVR_GOTO_IF_ERROR(eError, failMDLock);

	eError = OSLockCreate(&psMemDesc->sDeviceMemDesc.hLock);
	PVR_GOTO_IF_ERROR(eError, failDMDLock);

	eError = OSLockCreate(&psMemDesc->sCPUMemDesc.hLock);
	PVR_GOTO_IF_ERROR(eError, failCMDLock);

	OSAtomicWrite(&psMemDesc->hRefCount, 0);

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
void DevmemMemDescInit(DEVMEM_MEMDESC *psMemDesc,
		IMG_DEVMEM_OFFSET_T uiOffset,
		DEVMEM_IMPORT *psImport,
		IMG_DEVMEM_SIZE_T uiSize)
{
	DEVMEM_REFCOUNT_PRINT("%s (%p) %d->%d",
			__func__,
			psMemDesc,
			0,
			1);

	psMemDesc->psImport = psImport;
	psMemDesc->uiOffset = uiOffset;

	psMemDesc->sDeviceMemDesc.ui32RefCount = 0;
	psMemDesc->sCPUMemDesc.ui32RefCount = 0;
	psMemDesc->uiAllocSize = uiSize;
	psMemDesc->hPrivData = NULL;
	psMemDesc->ui32AllocationIndex = DEVICEMEM_HISTORY_ALLOC_INDEX_NONE;

	OSAtomicWrite(&psMemDesc->hRefCount, 1);
}

IMG_INTERNAL
void DevmemMemDescAcquire(DEVMEM_MEMDESC *psMemDesc)
{
	IMG_INT iRefCount = 0;

	iRefCount = OSAtomicIncrement(&psMemDesc->hRefCount);
	DEVMEM_REFCOUNT_PRINT("%s (%p) %d->%d",
			__func__,
			psMemDesc,
			iRefCount-1,
			iRefCount);

	PVR_UNREFERENCED_PARAMETER(iRefCount);
}

IMG_INTERNAL
IMG_BOOL DevmemMemDescRelease(DEVMEM_MEMDESC *psMemDesc)
{
	IMG_INT iRefCount;
	PVR_ASSERT(psMemDesc != NULL);

	iRefCount = OSAtomicDecrement(&psMemDesc->hRefCount);
	PVR_ASSERT(iRefCount >= 0);

	DEVMEM_REFCOUNT_PRINT("%s (%p) %d->%d",
			__func__,
			psMemDesc,
			iRefCount+1,
			iRefCount);

	if (iRefCount == 0)
	{
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)
		if (PVRSRVIsBridgeEnabled(GetBridgeHandle(psMemDesc->psImport->hDevConnection), PVRSRV_BRIDGE_RI) &&
		    (psMemDesc->hRIHandle))
		{
			PVRSRV_ERROR eError;

			eError = BridgeRIDeleteMEMDESCEntry(GetBridgeHandle(psMemDesc->psImport->hDevConnection),
			                                    psMemDesc->hRIHandle);
			PVR_LOG_IF_ERROR(eError, "BridgeRIDeleteMEMDESCEntry");
		}
#endif

		OSLockAcquire(psMemDesc->psImport->hLock);
		if (psMemDesc->psImport->uiProperties & DEVMEM_PROPERTIES_SUBALLOCATABLE)
		{
			/* As soon as the first sub-allocation on the psImport is freed
			 * we might get dirty memory when reusing it.
			 * We have to delete the ZEROED, CLEAN & POISONED flag */

			psMemDesc->psImport->uiProperties &=
					~(DEVMEM_PROPERTIES_IMPORT_IS_ZEROED |
							DEVMEM_PROPERTIES_IMPORT_IS_CLEAN |
							DEVMEM_PROPERTIES_IMPORT_IS_POISONED);

			OSLockRelease(psMemDesc->psImport->hLock);

			RA_Free(psMemDesc->psImport->sDeviceImport.psHeap->psSubAllocRA,
					psMemDesc->psImport->sDeviceImport.sDevVAddr.uiAddr +
					psMemDesc->uiOffset);
		}
		else
		{
			OSLockRelease(psMemDesc->psImport->hLock);
			DevmemImportStructRelease(psMemDesc->psImport);
		}

		OSLockDestroy(psMemDesc->sCPUMemDesc.hLock);
		OSLockDestroy(psMemDesc->sDeviceMemDesc.hLock);
		OSLockDestroy(psMemDesc->hLock);
		OSFreeMem(psMemDesc);

		return IMG_TRUE;
	}

	return IMG_FALSE;
}

IMG_INTERNAL
void DevmemMemDescDiscard(DEVMEM_MEMDESC *psMemDesc)
{
	PVR_ASSERT(OSAtomicRead(&psMemDesc->hRefCount) == 0);

	OSLockDestroy(psMemDesc->sCPUMemDesc.hLock);
	OSLockDestroy(psMemDesc->sDeviceMemDesc.hLock);
	OSLockDestroy(psMemDesc->hLock);
	OSFreeMem(psMemDesc);
}


IMG_INTERNAL
PVRSRV_ERROR DevmemValidateParams(IMG_DEVMEM_SIZE_T uiSize,
		IMG_DEVMEM_ALIGN_T uiAlign,
		PVRSRV_MEMALLOCFLAGS_T *puiFlags)
{
	if ((*puiFlags & PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC) &&
			(*puiFlags & PVRSRV_MEMALLOCFLAG_POISON_ON_ALLOC))
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Zero on Alloc and Poison on Alloc are mutually exclusive.",
				__func__));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (uiAlign & (uiAlign-1))
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: The requested alignment is not a power of two.",
				__func__));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (uiSize == 0)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Please request a non-zero size value.",
				__func__));
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
PVRSRV_ERROR DevmemImportStructAlloc(SHARED_DEV_CONNECTION hDevConnection,
		DEVMEM_IMPORT **ppsImport)
{
	DEVMEM_IMPORT *psImport;
	PVRSRV_ERROR eError;

	psImport = OSAllocMem(sizeof(*psImport));
	PVR_RETURN_IF_FALSE(psImport != NULL, PVRSRV_ERROR_OUT_OF_MEMORY);

	/* Setup some known bad values for things we don't have yet */
	psImport->sDeviceImport.hReservation = LACK_OF_RESERVATION_POISON;
	psImport->sDeviceImport.hMapping = LACK_OF_MAPPING_POISON;
	psImport->sDeviceImport.psHeap = NULL;
	psImport->sDeviceImport.bMapped = IMG_FALSE;

	eError = OSLockCreate(&psImport->sDeviceImport.hLock);
	PVR_GOTO_IF_ERROR(eError, failDIOSLockCreate);

	psImport->sCPUImport.hOSMMapData = NULL;
	psImport->sCPUImport.pvCPUVAddr = NULL;

	eError = OSLockCreate(&psImport->sCPUImport.hLock);
	PVR_GOTO_IF_ERROR(eError, failCIOSLockCreate);

	/* Set up common elements */
	psImport->hDevConnection = hDevConnection;

	/* Setup properties */
	psImport->uiProperties = 0;

	/* Setup refcounts */
	psImport->sDeviceImport.ui32RefCount = 0;
	psImport->sCPUImport.ui32RefCount = 0;
	OSAtomicWrite(&psImport->hRefCount, 0);

	/* Create the lock */
	eError = OSLockCreate(&psImport->hLock);
	PVR_GOTO_IF_ERROR(eError, failILockAlloc);

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
void DevmemImportStructInit(DEVMEM_IMPORT *psImport,
		IMG_DEVMEM_SIZE_T uiSize,
		IMG_DEVMEM_ALIGN_T uiAlign,
		PVRSRV_MEMALLOCFLAGS_T uiFlags,
		IMG_HANDLE hPMR,
		DEVMEM_PROPERTIES_T uiProperties)
{
	DEVMEM_REFCOUNT_PRINT("%s (%p) %d->%d",
			__func__,
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

/* Allocate the requested device virtual address region
 * from the heap */
static PVRSRV_ERROR DevmemReserveVARange(DEVMEM_HEAP *psHeap,
                                         DEVMEM_SIZE_T uiSize,
                                         IMG_UINT uiAlign,
                                         RA_LENGTH_T *puiAllocatedSize,
                                         IMG_UINT64 ui64OptionalMapAddress)
{
	PVRSRV_ERROR eError;

	/* Allocate space in the VM */
	eError = RA_Alloc_Range(psHeap->psQuantizedVMRA,
							uiSize,
							0,
							uiAlign,
							ui64OptionalMapAddress,
							puiAllocatedSize);

	if (PVRSRV_OK != eError)
	{
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
		if ((eError == PVRSRV_ERROR_RA_REQUEST_ALLOC_FAIL) ||
				(eError == PVRSRV_ERROR_RA_REQUEST_VIRT_ADDR_FAIL))
		{
			PVRSRV_ERROR eErr;
			eErr = BridgePVRSRVUpdateOOMStats(GetBridgeHandle(psHeap->psCtx->hDevConnection),
											PVRSRV_PROCESS_STAT_TYPE_INVALID_VIRTMEM,
											OSGetCurrentProcessID());
			PVR_LOG_IF_ERROR(eErr, "BridgePVRSRVUpdateOOMStats");
		}
#endif
		return eError;
	}

	/* No reason for the allocated virtual size to be different from
					   the PMR's size */
	PVR_ASSERT(*puiAllocatedSize == uiSize);

	return PVRSRV_OK;
}

/*
	Map an import to the device
 */
IMG_INTERNAL
PVRSRV_ERROR DevmemImportStructDevMap(DEVMEM_HEAP *psHeap,
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
	IMG_BOOL bDestroyed = IMG_FALSE;

	/* Round the provided import alignment to the configured heap alignment */
	uiAlign = 1ULL << psHeap->uiLog2ImportAlignment;
	uiAlign = (psImport->uiAlign + uiAlign - 1) & ~(uiAlign-1);

	psDeviceImport = &psImport->sDeviceImport;

	OSLockAcquire(psDeviceImport->hLock);
	DEVMEM_REFCOUNT_PRINT("%s (%p) %d->%d",
			__func__,
			psImport,
			psDeviceImport->ui32RefCount,
			psDeviceImport->ui32RefCount+1);

	if (psDeviceImport->ui32RefCount++ == 0)
	{
		DevmemImportStructAcquire(psImport);

		OSAtomicIncrement(&psHeap->hImportCount);

		if (PVRSRV_CHECK_SVM_ALLOC(psImport->uiFlags))
		{
			/*  SVM (shared virtual memory) imports or allocations always
				need to acquire CPU virtual address first as address is
				used to map the allocation into the device virtual address
				space; i.e. the virtual address of the allocation for both
				the CPU/GPU must be identical. */
			eError = DevmemImportStructDevMapSVM(psHeap,
					psImport,
					uiAlign,
					&ui64OptionalMapAddress);
			PVR_GOTO_IF_ERROR(eError, failVMRAAlloc);
		}

		if (ui64OptionalMapAddress == 0)
		{
			/* If heap is _completely_ managed by USER or KERNEL, we shouldn't
			 * be here, as this is RA manager code-path */
			if (psHeap->ui32HeapManagerFlags == DEVMEM_HEAP_MANAGER_USER ||
				psHeap->ui32HeapManagerFlags == DEVMEM_HEAP_MANAGER_KERNEL)
			{
				PVR_DPF((PVR_DBG_ERROR,
						psHeap->ui32HeapManagerFlags == DEVMEM_HEAP_MANAGER_USER ?
						"%s: Heap is user managed, please use PVRSRVMapToDeviceAddress().":
						"%s: Heap is kernel managed, use right allocation flags (e.g. SVM).",
						__func__));
				PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_INVALID_PARAMS, failVMRAAlloc);
			}

			if (psHeap->ui32HeapManagerFlags == DEVMEM_HEAP_MANAGER_UNKNOWN)
			{
				/* Only set the heap manager (to RA) at first map when heap manager
				 * is unknown. It might be a dual heap (both, user and RA managed),
				 * in which case heap manager is set at creation time */
				psHeap->ui32HeapManagerFlags = DEVMEM_HEAP_MANAGER_RA;
			}

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
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
				if (eError == PVRSRV_ERROR_RA_REQUEST_ALLOC_FAIL)
				{
					PVRSRV_ERROR eErr;
					eErr = BridgePVRSRVUpdateOOMStats(GetBridgeHandle(psHeap->psCtx->hDevConnection),
									  PVRSRV_PROCESS_STAT_TYPE_OOM_VIRTMEM_COUNT,
									  OSGetCurrentProcessID());
					PVR_LOG_IF_ERROR(eErr, "BridgePVRSRVUpdateOOMStats");
				}
#endif
				PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_DEVICEMEM_OUT_OF_DEVICE_VM, failVMRAAlloc);
			}

			/* No reason for the allocated virtual size to be different from
			   the PMR's size */
			PVR_ASSERT(uiAllocatedSize == psImport->uiSize);

			sBase.uiAddr = uiAllocatedAddr;

		}
		else
		{
			IMG_UINT64 ui64ValidEndAddr;

			/* Ensure supplied ui64OptionalMapAddress is within heap range */
			ui64ValidEndAddr = psHeap->sBaseAddress.uiAddr + psHeap->uiSize;
			if ((ui64OptionalMapAddress + psImport->uiSize > ui64ValidEndAddr) ||
					(ui64OptionalMapAddress < psHeap->sBaseAddress.uiAddr))
			{
				PVR_DPF((PVR_DBG_ERROR,
						"%s: ui64OptionalMapAddress %p is outside of heap limits <%p:%p>."
						, __func__
						, (void*)(uintptr_t)ui64OptionalMapAddress
						, (void*)(uintptr_t)psHeap->sBaseAddress.uiAddr
						, (void*)(uintptr_t)ui64ValidEndAddr));
				PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_INVALID_PARAMS, failVMRAAlloc);
			}

			switch (psHeap->ui32HeapManagerFlags)
			{
				case DEVMEM_HEAP_MANAGER_UNKNOWN:
					/* DEVMEM_HEAP_MANAGER_USER can apply to _any_ heap and can only
					 * be determined here. This heap type transitions from
					 * DEVMEM_HEAP_MANAGER_UNKNOWN to DEVMEM_HEAP_MANAGER_USER on
					 * 1st alloc. */
					psHeap->ui32HeapManagerFlags = DEVMEM_HEAP_MANAGER_USER;
					break;

				case DEVMEM_HEAP_MANAGER_USER:
				case DEVMEM_HEAP_MANAGER_KERNEL:
					if (! psHeap->uiSize)
					{
						PVR_DPF((PVR_DBG_ERROR,
								psHeap->ui32HeapManagerFlags == DEVMEM_HEAP_MANAGER_USER ?
										"%s: Heap DEVMEM_HEAP_MANAGER_USER is disabled.":
										"%s: Heap DEVMEM_HEAP_MANAGER_KERNEL is disabled."
										, __func__));
						PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_INVALID_HEAP, failVMRAAlloc);
					}
					break;

				case DEVMEM_HEAP_MANAGER_DUAL_USER_RA:
					/* When the heap is dual managed, ensure supplied ui64OptionalMapAddress
					 * and import size are within heap address space range */
					if (ui64OptionalMapAddress + psImport->uiSize <=
							psHeap->sBaseAddress.uiAddr + psHeap->uiReservedRegionSize)
					{
						break;
					}
					else
					{
						/* Allocate requested VM range */
						eError = DevmemReserveVARange(psHeap,
													psImport->uiSize,
													uiAlign,
													&uiAllocatedSize,
													ui64OptionalMapAddress);
						if (eError != PVRSRV_OK)
						{
							PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_DEVICEMEM_VA_ALLOC_FAILED, failVMRAAlloc);
						}

					}
					break;
				case DEVMEM_HEAP_MANAGER_RA:
					/* Allocate requested VM range */
					eError = DevmemReserveVARange(psHeap,
												psImport->uiSize,
												uiAlign,
												&uiAllocatedSize,
												ui64OptionalMapAddress);
					if (eError != PVRSRV_OK)
					{
						PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_DEVICEMEM_VA_ALLOC_FAILED, failVMRAAlloc);
					}
					break;

				default:
					break;
			}

			if (ui64OptionalMapAddress & ((1 << psHeap->uiLog2Quantum) - 1))
			{
				PVR_DPF((PVR_DBG_ERROR,
						"%s: Invalid address to map to. Please provide an "
						"address aligned to a page multiple of the heap."
						, __func__));
				PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_INVALID_PARAMS, failVMRAAlloc);
			}

			if (psImport->uiSize & ((1 << psHeap->uiLog2Quantum) - 1))
			{
				PVR_DPF((PVR_DBG_ERROR,
						"%s: Invalid heap to map to. "
						"Please choose a heap that can handle smaller page sizes."
						, __func__));
				PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_INVALID_PARAMS, failVMRAAlloc);
			}

			uiAllocatedAddr = ui64OptionalMapAddress;
			uiAllocatedSize = psImport->uiSize;
			sBase.uiAddr = uiAllocatedAddr;
		}

		if (psHeap->bPremapped)
		{
			/* no virtual address reservation and mapping are required for memory that's already mapped */
			psDeviceImport->hReservation = LACK_OF_RESERVATION_POISON;
			psDeviceImport->hMapping = LACK_OF_MAPPING_POISON;
		}
		else
		{
			/* Setup page tables for the allocated VM space */
			eError = BridgeDevmemIntReserveRange(GetBridgeHandle(psHeap->psCtx->hDevConnection),
					psHeap->hDevMemServerHeap,
					sBase,
					uiAllocatedSize,
					&hReservation);
			PVR_GOTO_IF_ERROR(eError, failReserve);

			if (bMap)
			{
				PVRSRV_MEMALLOCFLAGS_T uiMapFlags;

				uiMapFlags = psImport->uiFlags & PVRSRV_MEMALLOCFLAGS_PERMAPPINGFLAGSMASK;

				/* Actually map the PMR to allocated VM space */
				eError = BridgeDevmemIntMapPMR(GetBridgeHandle(psHeap->psCtx->hDevConnection),
						psHeap->hDevMemServerHeap,
						hReservation,
						psImport->hPMR,
						uiMapFlags,
						&psDeviceImport->hMapping);
				PVR_GOTO_IF_ERROR(eError, failMap);

				psDeviceImport->bMapped = IMG_TRUE;
			}

			psDeviceImport->hReservation = hReservation;
		}

		/* Setup device mapping specific parts of the mapping info */
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
			PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_INVALID_HEAP, failParams);
		}
	}
	OSLockRelease(psDeviceImport->hLock);

	return PVRSRV_OK;

failMap:
	if (!psHeap->bPremapped)
	{
		BridgeDevmemIntUnreserveRange(GetBridgeHandle(psHeap->psCtx->hDevConnection),
				hReservation);
	}
failReserve:
	if (ui64OptionalMapAddress == 0)
	{
		RA_Free(psHeap->psQuantizedVMRA,
				uiAllocatedAddr);
	}
failVMRAAlloc:
	if ((ui64OptionalMapAddress) && PVRSRV_CHECK_SVM_ALLOC(psImport->uiFlags))
	{
		DevmemImportStructDevUnmapSVM(psHeap, psImport);
	}
	bDestroyed = DevmemImportStructRelease(psImport);
	OSAtomicDecrement(&psHeap->hImportCount);
failParams:
	if (!bDestroyed)
	{
		psDeviceImport->ui32RefCount--;
		OSLockRelease(psDeviceImport->hLock);
	}
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

/*
	Unmap an import from the Device
 */
IMG_INTERNAL
IMG_BOOL DevmemImportStructDevUnmap(DEVMEM_IMPORT *psImport)
{
	PVRSRV_ERROR eError;
	DEVMEM_DEVICE_IMPORT *psDeviceImport;

	psDeviceImport = &psImport->sDeviceImport;

	OSLockAcquire(psDeviceImport->hLock);
	DEVMEM_REFCOUNT_PRINT("%s (%p) %d->%d",
			__func__,
			psImport,
			psDeviceImport->ui32RefCount,
			psDeviceImport->ui32RefCount-1);

	if (--psDeviceImport->ui32RefCount == 0)
	{
		DEVMEM_HEAP *psHeap = psDeviceImport->psHeap;

		if (!psHeap->bPremapped)
		{
			if (psDeviceImport->bMapped)
			{
				eError = BridgeDevmemIntUnmapPMR(GetBridgeHandle(psImport->hDevConnection),
						psDeviceImport->hMapping);
				PVR_ASSERT(eError == PVRSRV_OK);
			}

			eError = BridgeDevmemIntUnreserveRange(GetBridgeHandle(psImport->hDevConnection),
					psDeviceImport->hReservation);
			PVR_ASSERT(eError == PVRSRV_OK);
		}

		psDeviceImport->bMapped = IMG_FALSE;
		psDeviceImport->hMapping = LACK_OF_MAPPING_POISON;
		psDeviceImport->hReservation = LACK_OF_RESERVATION_POISON;

		/* DEVMEM_HEAP_MANAGER_RA can also come from a dual managed heap in which case,
		   we need to check if the allocated VA falls within RA managed range */
		if ((psHeap->ui32HeapManagerFlags & DEVMEM_HEAP_MANAGER_RA) &&
		    psDeviceImport->sDevVAddr.uiAddr >= (psHeap->sBaseAddress.uiAddr + psHeap->uiReservedRegionSize) &&
		    psDeviceImport->sDevVAddr.uiAddr < (psHeap->sBaseAddress.uiAddr + psHeap->uiSize))
		{
			RA_Free(psHeap->psQuantizedVMRA, psDeviceImport->sDevVAddr.uiAddr);
		}

		if (PVRSRV_CHECK_SVM_ALLOC(psImport->uiFlags))
		{
			DevmemImportStructDevUnmapSVM(psHeap, psImport);
		}

		OSLockRelease(psDeviceImport->hLock);

		DevmemImportStructRelease(psImport);

		OSAtomicDecrement(&psHeap->hImportCount);

		return IMG_TRUE;
	}
	else
	{
		OSLockRelease(psDeviceImport->hLock);
		return IMG_FALSE;
	}
}

/*
	Map an import into the CPU
 */
IMG_INTERNAL
PVRSRV_ERROR DevmemImportStructCPUMap(DEVMEM_IMPORT *psImport)
{
	PVRSRV_ERROR eError;
	DEVMEM_CPU_IMPORT *psCPUImport;
	size_t uiMappingLength;

	psCPUImport = &psImport->sCPUImport;

	OSLockAcquire(psCPUImport->hLock);
	DEVMEM_REFCOUNT_PRINT("%s (%p) %d->%d",
			__func__,
			psImport,
			psCPUImport->ui32RefCount,
			psCPUImport->ui32RefCount+1);

	if (psCPUImport->ui32RefCount++ == 0)
	{
		DevmemImportStructAcquire(psImport);

		eError = OSMMapPMR(GetBridgeHandle(psImport->hDevConnection),
				psImport->hPMR,
				psImport->uiSize,
				psImport->uiFlags,
				&psCPUImport->hOSMMapData,
				&psCPUImport->pvCPUVAddr,
				&uiMappingLength);
		PVR_GOTO_IF_ERROR(eError, failMap);

		/* MappingLength might be rounded up to page size */
		PVR_ASSERT(uiMappingLength >= psImport->uiSize);
	}
	OSLockRelease(psCPUImport->hLock);

	return PVRSRV_OK;

failMap:
	psCPUImport->ui32RefCount--;
	if (!DevmemImportStructRelease(psImport))
	{
		OSLockRelease(psCPUImport->hLock);
	}
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

/*
	Unmap an import from the CPU
 */
IMG_INTERNAL
void DevmemImportStructCPUUnmap(DEVMEM_IMPORT *psImport)
{
	DEVMEM_CPU_IMPORT *psCPUImport;

	psCPUImport = &psImport->sCPUImport;

	OSLockAcquire(psCPUImport->hLock);
	DEVMEM_REFCOUNT_PRINT("%s (%p) %d->%d",
			__func__,
			psImport,
			psCPUImport->ui32RefCount,
			psCPUImport->ui32RefCount-1);

	if (--psCPUImport->ui32RefCount == 0)
	{
		/* psImport->uiSize is a 64-bit quantity whereas the 5th
		 * argument to OSUnmapPMR is a 32-bit quantity on 32-bit systems
		 * hence a compiler warning of implicit cast and loss of data.
		 * Added explicit cast and assert to remove warning.
		 */
#if defined(__linux__) && defined(__i386__)
		PVR_ASSERT(psImport->uiSize<IMG_UINT32_MAX);
#endif
		OSMUnmapPMR(GetBridgeHandle(psImport->hDevConnection),
				psImport->hPMR,
				psCPUImport->hOSMMapData,
				psCPUImport->pvCPUVAddr,
				(size_t)psImport->uiSize);

		psCPUImport->hOSMMapData = NULL;
		psCPUImport->pvCPUVAddr = NULL;

		OSLockRelease(psCPUImport->hLock);

		DevmemImportStructRelease(psImport);
	}
	else
	{
		OSLockRelease(psCPUImport->hLock);
	}
}
