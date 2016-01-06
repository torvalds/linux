/*************************************************************************/ /*!
@File
@Title          Physmem (PMR) abstraction
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Part of the memory management.  This module is responsible for
                the "PMR" abstraction.  A PMR (Physical Memory Resource)
                represents some unit of physical memory which is
                allocated/freed/mapped/unmapped as an indivisible unit
                (higher software levels provide an abstraction above that
                to deal with dividing this down into smaller manageable units).
                Importantly, this module knows nothing of virtual memory, or
                of MMUs etc., with one excuseable exception.  We have the
                concept of a "page size", which really means nothing in
                physical memory, but represents a "contiguity quantum" such
                that the higher level modules which map this memory are able
                to verify that it matches the needs of the page size for the
                virtual realm into which it is being mapped.
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

#include "img_types.h"
#include "pvr_debug.h"
#include "pvrsrv_error.h"

#include "pdump.h"

#include "osfunc.h"
#include "pdump_km.h"
#include "pdump_physmem.h"
#include "pmr_impl.h"
#include "pvrsrv.h"

#include "allocmem.h"
#if defined(PVRSRV_SPLIT_LARGE_OSMEM_ALLOC) && defined(LINUX)
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <asm/pgtable.h>
#endif
#include "lock.h"

#if defined(SUPPORT_SECURE_EXPORT)
#include "secure_export.h"
#include "ossecure_export.h"
#endif

#if defined(PVR_RI_DEBUG)
#include "ri_server.h"
#endif 

/* ourselves */
#include "pmr.h"

/* A "context" for the physical memory block resource allocator.

   Context is probably the wrong word.

   There is almost certainly only one of these, ever, in the system.
   But, let's keep the notion of a context anyway, "just-in-case".
*/
struct _PMR_CTX_
{
    /* For debugging, and PDump, etc., let's issue a forever
       incrementing serial number to each allocation. */
    IMG_UINT64 uiNextSerialNum;

    /* For security, we only allow a PMR to be mapped if the caller
       knows its key.  We can pseudo-randomly generate keys */
    IMG_UINT64 uiNextKey;

    /* For debugging only, I guess:  Number of live PMRs */
    IMG_UINT32 uiNumLivePMRs;

	/* Lock for this structure */
	POS_LOCK hLock;

    /* In order to seed the uiNextKey, we enforce initialisation at
       driver load time.  Also, we can debug check at driver unload
       that the PMR count is zero. */
  IMG_BOOL bModuleInitialised;
} _gsSingletonPMRContext = { 1, 0, 0, IMG_NULL, IMG_FALSE };


typedef struct _PMR_MAPPING_TABLE_
{
	PMR_SIZE_T	uiChunkSize;			/*!< Size of a "chunk" */
	IMG_UINT32 	ui32NumPhysChunks;		/*!< Number of physical chunks that are valid */
	IMG_UINT32 	ui32NumVirtChunks;		/*!< Number of virtual chunks in the mapping */
	/* Must be last */
#if defined(PVRSRV_SPLIT_LARGE_OSMEM_ALLOC)
	IMG_UINT32 	*aui32Translation;      /*!< Translation mapping for "logical" to physical */
#else
	IMG_UINT32 	aui32Translation[1];    /*!< Translation mapping for "logical" to physical */
#endif
} PMR_MAPPING_TABLE;

#define TRANSLATION_INVALID 0xFFFFFFFFL

/* A PMR. One per physical allocation.  May be "shared".

   "shared" is ambiguous.  We need to be careful with terminology.
   There are two ways in which a PMR may be "shared" and we need to be
   sure that we are clear which we mean.

   i)   multiple small allocations living together inside one PMR;

   ii)  one single allocation filling a PMR but mapped into multiple
        memory contexts.

   This is more important further up the stack - at this level, all we
   care is that the PMR is being referenced multiple times.
*/
struct _PMR_
{
    /* This object is strictly refcounted.  References include:
       - mapping
       - live handles (to this object)
       - live export handles
       (thus it is normal for allocated and exported memory to have a refcount of 3)
       The object is destroyed when and only when the refcount reaches 0
    */
    /*
       Physical address translation (device <> cpu) is done on a per device
       basis which means we need the physcial heap info
    */
    PHYS_HEAP *psPhysHeap;

    IMG_UINT32 uiRefCount;

    /* lock count - this is the number of times
       PMRLockSysPhysAddresses() has been called, less the number of
       PMRUnlockSysPhysAddresses() calls.  This is arguably here for
       debug reasons only, as the refcount is already incremented as a
       matter of course.  Really, this just allows us to trap protocol
       errors: i.e. calling PMRSysPhysAddr(),
       without a lock, or calling PMRUnlockSysPhysAddresses() too many
       or too few times. */
    IMG_UINT32 uiLockCount;

	/* Lock for this structure */
	POS_LOCK hLock;

    /* Incrementing serial number to each allocation. */
    IMG_UINT64 uiSerialNum;

    /* For security, we only allow a PMR to be mapped if the caller
       knows its key.  We can pseudo-randomly generate keys */
    PMR_PASSWORD_T uiKey;

    /* Callbacks for per-flavour functions */
    const PMR_IMPL_FUNCTAB *psFuncTab;

    /* Data associated with the "subtype" */
    PMR_IMPL_PRIVDATA pvFlavourData;

    /* And for pdump */
    const IMG_CHAR *pszPDumpDefaultMemspaceName;
    const IMG_CHAR *pszPDumpFlavour;

    /* Logical size of allocation.  "logical", because a PMR can
       represent memory that will never physically exist.  This is the
       amount of virtual space that the PMR would consume when it's
       mapped into a virtual allocation. */
    PMR_SIZE_T uiLogicalSize;

	/* Mapping table for the allocation.
	   PMR's can be sparse in which case not all the "logic" addresses
	   in it are valid. We need to know which addresses are and aren't
	   valid when mapping or reading the PMR.
	   The mapping table translates "logical" offsets into physical
	   offsets which is what we always pass to the PMR factory
	   (so it doesn't have to be concerned about sparseness issues) */
    PMR_MAPPING_TABLE *psMappingTable;

    /* Minimum Physical Contiguity Guarantee.  Might be called "page
       size", but that would be incorrect, as page size is something
       meaningful only in virtual realm.  This contiguity guarantee
       provides an inequality that can be verified/asserted/whatever
       to ensure that this PMR conforms to the page size requirement
       of the place the PMR gets mapped.  (May be used to select an
       appropriate heap in variable page size systems)

       The absolutely necessary condition is this:

       device MMU page size <= actual physical contiguity.

       We go one step further in order to be able to provide an early warning / early compatibility check and say this:

       device MMU page size <= 2**(uiLog2ContiguityGuarantee) <= actual physical contiguity.

       In this way, it is possible to make the page table reservation
       in the device MMU without even knowing the granularity of the
       physical memory (i.e. useful for being able to allocate virtual
       before physical)
    */
    PMR_LOG2ALIGN_T uiLog2ContiguityGuarantee;

    /* Flags.  We store a copy of the "PMR flags" (usually a subset of
       the flags given at allocation time) and return them to any
       caller of PMR_Flags().  The intention of these flags is that
       the ones stored here are used to represent permissions, such
       that noone is able to map a PMR in a mode in which they are not
       allowed, e.g. writeable for a read-only PMR, etc. */
    PMR_FLAGS_T uiFlags;

    /* Do we really need this? For now we'll keep it, until we know we don't. */
    /* NB: this is not the "memory context" in client terms - this is
       _purely_ the "PMR" context, of which there is almost certainly only
       ever one per system as a whole, but we'll keep the concept
       anyway, just-in-case. */
    struct _PMR_CTX_ *psContext;

#if defined(PVR_RI_DEBUG)
    /*
	 * Stored handle to PMR RI entry
	 */
	IMG_PVOID	hRIHandle;
#endif

	/* Whether PDumping of this PMR must be persistent
	 * (i.e. it must be present in every future PDump stream as well)
	 */
	IMG_BOOL	bForcePersistent;
};

/* do we need a struct for the export handle?  I'll use one for now, but if nothing goes in it, we'll lose it */
struct _PMR_EXPORT_
{
    struct _PMR_ *psPMR;
};

struct _PMR_PAGELIST_
{
	struct _PMR_ *psReferencePMR;
};

/*
 * This Lock is used to protect the sequence of operation used in MMapPMR and in
 * the memory management bridge. This should make possible avoid the use of the bridge
 * lock in mmap.c avoiding regressions.
 */

/* this structure tracks the current owner of the PMR lock, avoiding use of
 * the Linux (struct mutex).owner field which is not guaranteed to be up to date.
 * there is Linux-specific code to provide an opimised approach for Linux,
 * using the kernel (struct task_struct *) instead of a PID/TID combination.
 */
typedef struct _PMR_LOCK_OWNER_
{
#if defined(LINUX)
	struct task_struct *task;
#else
	POS_LOCK hPIDTIDLock;
	IMG_PID uiPID;
	IMG_UINTPTR_T uiTID;
#endif
} PMR_LOCK_OWNER;

POS_LOCK gGlobalLookupPMRLock;
static PMR_LOCK_OWNER gsPMRLockOwner;

static IMG_VOID _SetPMRLockOwner(IMG_VOID)
{
#if defined(LINUX)
	gsPMRLockOwner.task = current;
#else
	OSLockAcquire(gsPMRLockOwner.hPIDTIDLock);
	gsPMRLockOwner.uiPID = OSGetCurrentProcessID();
	gsPMRLockOwner.uiTID = OSGetCurrentThreadID();
	OSLockRelease(gsPMRLockOwner.hPIDTIDLock);
#endif
}

/* Must only be called by the thread which owns the PMR lock */
static IMG_VOID _ClearPMRLockOwner(IMG_VOID)
{
#if defined(LINUX)
	gsPMRLockOwner.task = IMG_NULL;
#else
	OSLockAcquire(gsPMRLockOwner.hPIDTIDLock);
	gsPMRLockOwner.uiPID = 0;
	gsPMRLockOwner.uiTID = 0;
	OSLockRelease(gsPMRLockOwner.hPIDTIDLock);
#endif
}

static IMG_BOOL _ComparePMRLockOwner(IMG_VOID)
{
#if defined(LINUX)
	return gsPMRLockOwner.task == current;
#else
	IMG_BOOL bRet;

	OSLockAcquire(gsPMRLockOwner.hPIDTIDLock);
	bRet = (gsPMRLockOwner.uiPID == OSGetCurrentProcessID()) &&
			(gsPMRLockOwner.uiTID == OSGetCurrentThreadID());
	OSLockRelease(gsPMRLockOwner.hPIDTIDLock);
	return bRet;
#endif
}

IMG_VOID PMRLock()
{
	OSLockAcquire(gGlobalLookupPMRLock);
	_SetPMRLockOwner();
}

IMG_VOID PMRUnlock()
{
	_ClearPMRLockOwner();
	OSLockRelease(gGlobalLookupPMRLock);
}

IMG_BOOL PMRIsLocked(void)
{
	return OSLockIsLocked(gGlobalLookupPMRLock);
}


IMG_BOOL PMRIsLockedByMe(void)
{
	return PMRIsLocked() && _ComparePMRLockOwner();
}

#define MIN3(a,b,c)	(((a) < (b)) ? (((a) < (c)) ? (a):(c)) : (((b) < (c)) ? (b):(c)))

#if defined(PVRSRV_SPLIT_LARGE_OSMEM_ALLOC)
#if defined(LINUX)
static INLINE IMG_BOOL _IsVmallocAddr(const IMG_VOID *pvAddr)
{
	unsigned long lAddr = (unsigned long) pvAddr;
	return (lAddr >= VMALLOC_START) && (lAddr < VMALLOC_END);
}
#endif

static INLINE IMG_VOID *_AllocMem(const IMG_SIZE_T size)
{
#if defined(LINUX)
	if (size > OSGetPageSize())
		return vmalloc(size);
	else
		return OSAllocMem(size);
#else
	return OSAllocMem(size);
#endif
}

static INLINE IMG_VOID _FreeMem(IMG_VOID *pvAddr)
{
#if defined(LINUX)
	if (_IsVmallocAddr(pvAddr))
		vfree(pvAddr);
	else
		OSFreeMem(pvAddr);
#else
	OSFreeMem(pvAddr);
#endif
}
#endif

static PVRSRV_ERROR
_PMRCreate(PMR_SIZE_T uiLogicalSize,
           PMR_SIZE_T uiChunkSize,
           IMG_UINT32 ui32NumPhysChunks,
           IMG_UINT32 ui32NumVirtChunks,
           IMG_BOOL *pabMappingTable,
           PMR_LOG2ALIGN_T uiLog2ContiguityGuarantee,
           PMR_FLAGS_T uiFlags,
           PMR **ppsPMR)
{
    IMG_VOID *pvPMRLinAddr;
#if defined(PVRSRV_SPLIT_LARGE_OSMEM_ALLOC)
    IMG_VOID *pvMapLinAddr;
#endif
    PMR *psPMR;
    PMR_MAPPING_TABLE *psMappingTable;
    struct _PMR_CTX_ *psContext;
    IMG_UINT32 i;
    IMG_UINT32 ui32ValidCount = 0;
    IMG_UINT32 ui32Remainder;
    PVRSRV_ERROR eError;
    IMG_UINT32 ui32PhysIndex = 0;

    psContext = &_gsSingletonPMRContext;


	/* Extra checks required for sparse PMRs */
	if (uiLogicalSize != uiChunkSize)
	{
		/* Check the logical size and chunk information agree with each other */
		if (uiLogicalSize != (uiChunkSize * ui32NumVirtChunks))
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Bad mapping size (uiLogicalSize = 0x%llx, uiChunkSize = 0x%llx, ui32NumVirtChunks = %d)",
					__FUNCTION__, (unsigned long long)uiLogicalSize, (unsigned long long)uiChunkSize, ui32NumVirtChunks));
			return PVRSRV_ERROR_PMR_BAD_MAPPINGTABLE_SIZE;
		}

		/* Check that the chunk size is a multiple of the contiguity */
		OSDivide64(uiChunkSize, (1<< uiLog2ContiguityGuarantee), &ui32Remainder);
		if (ui32Remainder)
		{
			return PVRSRV_ERROR_PMR_BAD_CHUNK_SIZE;
		}

		/* Check the mapping table */
		for (i = 0; i<ui32NumVirtChunks;i++)
		{
			if (pabMappingTable[i])
			{
				ui32ValidCount++;
			}
		}
	
		if (ui32ValidCount != ui32NumPhysChunks)
		{
			PVR_DPF((PVR_DBG_ERROR,
					 "%s: Mismatch in mapping table, expecting %d valid entries but found %d",
					 __FUNCTION__,
					 ui32NumPhysChunks,
					 ui32ValidCount));
			return PVRSRV_ERROR_PMR_MAPPINGTABLE_MISMATCH;
		}
	}

#if defined(PVRSRV_SPLIT_LARGE_OSMEM_ALLOC)
	pvPMRLinAddr = OSAllocMem(sizeof(*psPMR) + sizeof(*psMappingTable));
#else
	pvPMRLinAddr = OSAllocMem(sizeof(*psPMR) + sizeof(*psMappingTable) + sizeof(IMG_UINT32) * ui32NumVirtChunks);
#endif
	if (pvPMRLinAddr == IMG_NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

#if defined(PVRSRV_SPLIT_LARGE_OSMEM_ALLOC)
	pvMapLinAddr = _AllocMem(sizeof(IMG_UINT32) * ui32NumVirtChunks);
	if (pvMapLinAddr == IMG_NULL)
	{
		OSFreeMem(pvPMRLinAddr);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psPMR = (PMR *) pvPMRLinAddr;
	psMappingTable = (PMR_MAPPING_TABLE *) (((IMG_CHAR *) pvPMRLinAddr) + sizeof(*psPMR));
	psMappingTable->aui32Translation = (IMG_UINT32 *) pvMapLinAddr;
#else
	psPMR = (PMR *) pvPMRLinAddr;
	psMappingTable = (PMR_MAPPING_TABLE *) (((IMG_CHAR *) pvPMRLinAddr) + sizeof(*psPMR));
#endif

	eError = OSLockCreate(&psPMR->hLock, LOCK_TYPE_PASSIVE);
	if (eError != PVRSRV_OK)
	{
#if defined(PVRSRV_SPLIT_LARGE_OSMEM_ALLOC)
		_FreeMem(psMappingTable->aui32Translation);
#endif
		OSFreeMem(psPMR);
		return eError;
	}

	/* Setup the mapping table */
	psMappingTable->uiChunkSize = uiChunkSize;
	psMappingTable->ui32NumVirtChunks = ui32NumVirtChunks;
	psMappingTable->ui32NumPhysChunks = ui32NumPhysChunks;
	for (i=0;i<ui32NumVirtChunks;i++)
	{
		if (pabMappingTable[i])
		{
			psMappingTable->aui32Translation[i] = ui32PhysIndex++;
		}
		else
		{
			psMappingTable->aui32Translation[i] = TRANSLATION_INVALID;
		}
	}

	/* Setup the PMR */
	psPMR->uiRefCount = 0;
	psPMR->uiLockCount = 0;
	psPMR->psContext = psContext;
	psPMR->uiLogicalSize = uiLogicalSize;
	psPMR->uiLog2ContiguityGuarantee = uiLog2ContiguityGuarantee;
	psPMR->uiFlags = uiFlags;
	psPMR->psMappingTable = psMappingTable;
	psPMR->uiKey = psContext->uiNextKey;
	psPMR->uiSerialNum = psContext->uiNextSerialNum;

#if defined(PVR_RI_DEBUG)
	psPMR->hRIHandle = IMG_NULL;
#endif

	OSLockAcquire(psContext->hLock);
	psContext->uiNextKey = (0x80200003 * psContext->uiNextKey)
		^ (0xf00f0081 * (IMG_UINTPTR_T)pvPMRLinAddr);
	psContext->uiNextSerialNum ++;
	*ppsPMR = psPMR;
	PVR_DPF((PVR_DBG_MESSAGE, "pmr.c: created PMR @0x%p", psPMR));
	/* Increment live PMR count */
	psContext->uiNumLivePMRs ++;
	OSLockRelease(psContext->hLock);

	return PVRSRV_OK;
}

static IMG_UINT32
_RefNoLock(PMR *psPMR)
{
	psPMR->uiRefCount++;
	return psPMR->uiRefCount;
}

static IMG_UINT32
_UnrefNoLock(PMR *psPMR)
{
    PVR_ASSERT(psPMR->uiRefCount > 0);
	psPMR->uiRefCount--;
	return psPMR->uiRefCount;
}

static IMG_VOID
_Ref(PMR *psPMR)
{
	OSLockAcquire(psPMR->hLock);
	_RefNoLock(psPMR);
	OSLockRelease(psPMR->hLock);
}

static IMG_VOID
_UnrefAndMaybeDestroy(PMR *psPMR)
{
    PVRSRV_ERROR eError2;
    struct _PMR_CTX_ *psCtx;
    IMG_UINT32 uiRefCount;

    PVR_ASSERT(psPMR != IMG_NULL);
    PVR_ASSERT(psPMR->uiRefCount > 0);

    OSLockAcquire(psPMR->hLock);
	uiRefCount = _UnrefNoLock(psPMR);
    OSLockRelease(psPMR->hLock);

    if (uiRefCount == 0)
    {
        if (psPMR->psFuncTab->pfnFinalize != IMG_NULL)
        {
            eError2 = psPMR->psFuncTab->pfnFinalize(psPMR->pvFlavourData);
            PVR_ASSERT (eError2 == PVRSRV_OK); /* can we do better? */
        }

#ifdef PVRSRV_NEED_PVR_ASSERT
        OSLockAcquire(psPMR->hLock);
        PVR_ASSERT(psPMR->uiLockCount == 0);
        OSLockRelease(psPMR->hLock);
#endif

#if defined(PVR_RI_DEBUG)
		{
            PVRSRV_ERROR eError;

			/* Delete RI entry */
            if (psPMR->hRIHandle)
            {
            	eError = RIDeletePMREntryKM (psPMR->hRIHandle);
            }
		}
#endif /* if defined(PVR_RI_DEBUG) */
		psCtx = psPMR->psContext;

		OSLockDestroy(psPMR->hLock);
#if defined(PVRSRV_SPLIT_LARGE_OSMEM_ALLOC)
		_FreeMem(psPMR->psMappingTable->aui32Translation);
#endif
        OSFreeMem(psPMR);

        /* Decrement live PMR count.  Probably only of interest for debugging */
        PVR_ASSERT(psCtx->uiNumLivePMRs > 0);

        OSLockAcquire(psCtx->hLock);
        psCtx->uiNumLivePMRs --;
        OSLockRelease(psCtx->hLock);
    }
}

static IMG_BOOL _PMRIsSparse(const PMR *psPMR)
{
	if (psPMR->psMappingTable->ui32NumVirtChunks == psPMR->psMappingTable->ui32NumPhysChunks)
	{
		return IMG_FALSE;
	}

	return IMG_TRUE;
}

PVRSRV_ERROR
PMRCreatePMR(PHYS_HEAP *psPhysHeap,
             PMR_SIZE_T uiLogicalSize,
             PMR_SIZE_T uiChunkSize,
             IMG_UINT32 ui32NumPhysChunks,
             IMG_UINT32 ui32NumVirtChunks,
             IMG_BOOL *pabMappingTable,
             PMR_LOG2ALIGN_T uiLog2ContiguityGuarantee,
             PMR_FLAGS_T uiFlags,
             const IMG_CHAR *pszPDumpFlavour,
             const PMR_IMPL_FUNCTAB *psFuncTab,
             PMR_IMPL_PRIVDATA pvPrivData,
             PMR **ppsPMRPtr,
             IMG_HANDLE *phPDumpAllocInfo,
             IMG_BOOL bForcePersistent)
{
    PMR *psPMR = IMG_NULL;
    PVRSRV_ERROR eError;

    eError = _PMRCreate(uiLogicalSize,
						uiChunkSize,
						ui32NumPhysChunks,
						ui32NumVirtChunks,
						pabMappingTable,
						uiLog2ContiguityGuarantee,
						uiFlags,
                        &psPMR);
    if (eError != PVRSRV_OK)
    {
        goto e0;
    }

    psPMR->psPhysHeap = psPhysHeap;
    psPMR->psFuncTab = psFuncTab;
    psPMR->pszPDumpDefaultMemspaceName = PhysHeapPDumpMemspaceName(psPhysHeap);
    psPMR->pszPDumpFlavour = pszPDumpFlavour;
    psPMR->pvFlavourData = pvPrivData;
    psPMR->uiRefCount = 1;
    psPMR->bForcePersistent = bForcePersistent;

    *ppsPMRPtr = psPMR;


	if (phPDumpAllocInfo)
	{
		PDumpPMRMallocPMR(psPMR,
						  (uiChunkSize * ui32NumPhysChunks),
						  1ULL<<uiLog2ContiguityGuarantee,
						  bForcePersistent,
						  phPDumpAllocInfo);
	}

    return PVRSRV_OK;

    /*
      error exit paths follow
    */
 e0:
    PVR_ASSERT(eError != PVRSRV_OK);
    return eError;
}

PVRSRV_ERROR PMRLockSysPhysAddressesNested(PMR *psPMR,
                        IMG_UINT32 uiLog2RequiredContiguity,
                        IMG_UINT32 ui32NestingLevel)
{
    PVRSRV_ERROR eError;

    PVR_ASSERT(psPMR != IMG_NULL);

    if (uiLog2RequiredContiguity > psPMR->uiLog2ContiguityGuarantee)
    {
        eError = PVRSRV_ERROR_PMR_INCOMPATIBLE_CONTIGUITY;
        goto e0;
    }

	OSLockAcquireNested(psPMR->hLock, ui32NestingLevel);
    /* We also count the locks as references, so that the PMR is not
       freed while someone is using a physical address. */
    /* "lock" here simply means incrementing the refcount.  It means
       the refcount is multipurpose, but that's okay.  We only have to
       promise that physical addresses are valid after this point, and
       remain valid until the corresponding
       PMRUnlockSysPhysAddressesOSMem() */
    _RefNoLock(psPMR);

    /* Also count locks separately from other types of references, to
       allow for debug assertions */
    psPMR->uiLockCount++;

    /* Only call callback if lockcount transitions from 0 to 1 */
    if (psPMR->uiLockCount == 1)
    {
        if (psPMR->psFuncTab->pfnLockPhysAddresses != IMG_NULL)
        {
            /* must always have lock and unlock in pairs! */
            PVR_ASSERT(psPMR->psFuncTab->pfnUnlockPhysAddresses != IMG_NULL);

            eError = psPMR->psFuncTab->pfnLockPhysAddresses(psPMR->pvFlavourData,
                                                            uiLog2RequiredContiguity);

            if (eError != PVRSRV_OK)
            {
                goto e1;
            }
        }
    }
	OSLockRelease(psPMR->hLock);

    return PVRSRV_OK;

 e1:
    psPMR->uiLockCount--;
    _UnrefNoLock(psPMR);
    PVR_ASSERT(psPMR->uiRefCount != 0);
    OSLockRelease(psPMR->hLock);
 e0:
    PVR_ASSERT(eError != PVRSRV_OK);
    return eError;
}

PVRSRV_ERROR
PMRLockSysPhysAddresses(PMR *psPMR,
                        IMG_UINT32 uiLog2RequiredContiguity)
{
	return PMRLockSysPhysAddressesNested(psPMR, uiLog2RequiredContiguity, 0);
}

PVRSRV_ERROR
PMRUnlockSysPhysAddresses(PMR *psPMR)
{
    PVRSRV_ERROR eError;

    PVR_ASSERT(psPMR != IMG_NULL);

	OSLockAcquire(psPMR->hLock);
	PVR_ASSERT(psPMR->uiLockCount > 0);
	psPMR->uiLockCount--;

    if (psPMR->uiLockCount == 0)
    {
        if (psPMR->psFuncTab->pfnUnlockPhysAddresses != IMG_NULL)
        {
            PVR_ASSERT(psPMR->psFuncTab->pfnLockPhysAddresses != IMG_NULL);

            eError = psPMR->psFuncTab->pfnUnlockPhysAddresses(psPMR->pvFlavourData);
            /* must never fail */
            PVR_ASSERT(eError == PVRSRV_OK);
        }
    }

    OSLockRelease(psPMR->hLock);

    /* We also count the locks as references, so that the PMR is not
       freed while someone is using a physical address. */
    _UnrefAndMaybeDestroy(psPMR);

    return PVRSRV_OK;
}

PVRSRV_ERROR
PMRExportPMR(PMR *psPMR,
             PMR_EXPORT **ppsPMRExportPtr,
             PMR_SIZE_T *puiSize,
             PMR_LOG2ALIGN_T *puiLog2Contig,
             PMR_PASSWORD_T *puiPassword)
{
    IMG_UINT64 uiPassword;
    PMR_EXPORT *psPMRExport;

    uiPassword = psPMR->uiKey;

    psPMRExport = OSAllocMem(sizeof(*psPMRExport));
    if (psPMRExport == IMG_NULL)
    {
        return PVRSRV_ERROR_OUT_OF_MEMORY;
    }

    psPMRExport->psPMR = psPMR;
    _Ref(psPMR);

    *ppsPMRExportPtr = psPMRExport;
    *puiSize = psPMR->uiLogicalSize;
    *puiLog2Contig = psPMR->uiLog2ContiguityGuarantee;
    *puiPassword = uiPassword;

    return PVRSRV_OK;
}

PVRSRV_ERROR
PMRMakeServerExportClientExport(DEVMEM_EXPORTCOOKIE *psPMRExportIn,
								PMR_EXPORT **ppsPMRExportPtr,
								PMR_SIZE_T *puiSize,
								PMR_LOG2ALIGN_T *puiLog2Contig,
								PMR_PASSWORD_T *puiPassword)
{
	*ppsPMRExportPtr = (PMR_EXPORT *) psPMRExportIn->hPMRExportHandle;
	*puiSize = psPMRExportIn->uiSize;
	*puiLog2Contig = psPMRExportIn->uiLog2ContiguityGuarantee;
	*puiPassword = psPMRExportIn->uiPMRExportPassword;

	return PVRSRV_OK;
}

PVRSRV_ERROR
PMRUnmakeServerExportClientExport(PMR_EXPORT *psPMRExport)
{
	PVR_UNREFERENCED_PARAMETER(psPMRExport);

	/*
	 * There is nothing to do here, the server will call unexport
	 * regardless of the type of shutdown. In order to play ball
	 * with the handle manager (where it's used) we need to pair
	 * functions and this is PMRMakeServerExportClientExport
	 * counterpart.
	 */
	return PVRSRV_OK;
}

PVRSRV_ERROR
PMRUnexportPMR(PMR_EXPORT *psPMRExport)
{
    /* FIXME: probably shouldn't be assertions? */
    PVR_ASSERT(psPMRExport != IMG_NULL);
    PVR_ASSERT(psPMRExport->psPMR != IMG_NULL);
    PVR_ASSERT(psPMRExport->psPMR->uiRefCount > 0);

    _UnrefAndMaybeDestroy(psPMRExport->psPMR);

    OSFreeMem(psPMRExport);

    return PVRSRV_OK;
}


PVRSRV_ERROR
PMRImportPMR(PMR_EXPORT *psPMRExport,
             PMR_PASSWORD_T uiPassword,
             PMR_SIZE_T uiSize,
             PMR_LOG2ALIGN_T uiLog2Contig,
             PMR **ppsPMR)
{
    PMR *psPMR;

    /* FIXME: probably shouldn't be assertions? */
    PVR_ASSERT(psPMRExport != IMG_NULL);
    PVR_ASSERT(psPMRExport->psPMR != IMG_NULL);
    PVR_ASSERT(psPMRExport->psPMR->uiRefCount > 0);

    psPMR = psPMRExport->psPMR;

    if (psPMR->uiKey != uiPassword)
    {
        PVR_DPF((PVR_DBG_ERROR,
                 "PMRImport: password given = %016llx, expected = %016llx\n",
                 uiPassword,
                 psPMR->uiKey));
        return PVRSRV_ERROR_PMR_WRONG_PASSWORD_OR_STALE_PMR;
    }

    if (psPMR->uiLogicalSize != uiSize || psPMR->uiLog2ContiguityGuarantee != uiLog2Contig)
    {
        return PVRSRV_ERROR_PMR_MISMATCHED_ATTRIBUTES;
    }

    _Ref(psPMR);

    *ppsPMR = psPMR;

    return PVRSRV_OK;
}

PVRSRV_ERROR
PMRUnimportPMR(PMR *psPMR)
{
    _UnrefAndMaybeDestroy(psPMR);

    return PVRSRV_OK;
}

/*
	Note:
	We pass back the PMR as it was passed in as a different handle type
	(DEVMEM_MEM_IMPORT) and it allows us to change the import structure
	type if we should need to embed any meta data in it.
*/
PVRSRV_ERROR
PMRLocalImportPMR(PMR *psPMR,
				  PMR **ppsPMR,
				  IMG_DEVMEM_SIZE_T *puiSize,
				  IMG_DEVMEM_ALIGN_T *puiAlign)
{
	 _Ref(psPMR);

	/* Return the PMR */
	*ppsPMR = psPMR;
	*puiSize = psPMR->uiLogicalSize;
	*puiAlign = 1ULL << psPMR->uiLog2ContiguityGuarantee;
	return PVRSRV_OK;
}

PVRSRV_ERROR
PMRGetUID(PMR *psPMR,
		  IMG_UINT64 *pui64UID)
{
	PVR_ASSERT(psPMR != IMG_NULL);

	*pui64UID = psPMR->uiSerialNum;

	return PVRSRV_OK;
}

#if defined(SUPPORT_SECURE_EXPORT)
PVRSRV_ERROR PMRSecureExportPMR(CONNECTION_DATA *psConnection,
								PMR *psPMR,
								IMG_SECURE_TYPE *phSecure,
								PMR **ppsPMR,
								CONNECTION_DATA **ppsSecureConnection)
{
	PVRSRV_ERROR eError;

	/* We are acquiring reference to PMR here because OSSecureExport
	 * releases bridge lock and PMR lock for a moment and we don't want PMR
	 * to be removed by other thread in the meantime. */
	_Ref(psPMR);

	eError = OSSecureExport(psConnection,
							(IMG_PVOID) psPMR,
							phSecure,
							ppsSecureConnection);

	if (eError != PVRSRV_OK)
	{
		goto e0;
	}

	*ppsPMR = psPMR;

	return PVRSRV_OK;
e0:
	PVR_ASSERT(eError != PVRSRV_OK);
	_UnrefAndMaybeDestroy(psPMR);
	return eError;
}

PVRSRV_ERROR PMRSecureUnexportPMR(PMR *psPMR)
{
	_UnrefAndMaybeDestroy(psPMR);
	return PVRSRV_OK;
}

PVRSRV_ERROR PMRSecureImportPMR(IMG_SECURE_TYPE hSecure,
								PMR **ppsPMR,
								IMG_DEVMEM_SIZE_T *puiSize,
								IMG_DEVMEM_ALIGN_T *puiAlign)
{
	PVRSRV_ERROR eError;
	PMR *psPMR;

	eError = OSSecureImport(hSecure, (IMG_PVOID *) &psPMR);
	if (eError != PVRSRV_OK)
	{
		goto e0;
	}

	_Ref(psPMR);

	/* Return the PMR */
	*ppsPMR = psPMR;
	*puiSize = psPMR->uiLogicalSize;
	*puiAlign = 1 << psPMR->uiLog2ContiguityGuarantee;
	return PVRSRV_OK;
e0:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

PVRSRV_ERROR PMRSecureUnimportPMR(PMR *psPMR)
{
	_UnrefAndMaybeDestroy(psPMR);
	return PVRSRV_OK;
}
#endif

#if defined(PVR_RI_DEBUG)
PVRSRV_ERROR
PMRStoreRIHandle(PMR *psPMR,
				 IMG_PVOID hRIHandle)
{
    PVR_ASSERT(psPMR != IMG_NULL);

    psPMR->hRIHandle = hRIHandle;
    return PVRSRV_OK;
}
#endif

static PVRSRV_ERROR
_PMRAcquireKernelMappingData(PMR *psPMR,
                            IMG_SIZE_T uiLogicalOffset,
                            IMG_SIZE_T uiSize,
                            IMG_VOID **ppvKernelAddressOut,
                            IMG_SIZE_T *puiLengthOut,
                            IMG_HANDLE *phPrivOut,
                            IMG_BOOL bMapSparse)
{
    PVRSRV_ERROR eError;
    IMG_VOID *pvKernelAddress;
    IMG_HANDLE hPriv;
    PMR_FLAGS_T ulFlags;

    PVR_ASSERT(psPMR != IMG_NULL);

    if (_PMRIsSparse(psPMR) && !bMapSparse)
    {
        /* Generally we don't support mapping of sparse allocations but if there
           is a justified need we can do that by passing IMG_TRUE in bMapSparse.
           Although the callback is supported by the PMR it will always map
           the physical 1:1 as sparseness issues are handled here in the core */
        return PVRSRV_ERROR_PMR_NOT_PERMITTED;
    }

    /* Acquire/Release functions must be overridden in pairs */
    if (psPMR->psFuncTab->pfnAcquireKernelMappingData == IMG_NULL)
    {
        PVR_ASSERT (psPMR->psFuncTab->pfnReleaseKernelMappingData == IMG_NULL);

        /* If PMR implementation does not supply this pair of
           functions, it means they do not permit the PMR to be mapped
           into kernel memory at all */
        eError = PVRSRV_ERROR_PMR_NOT_PERMITTED;
        goto e0;
    }
    PVR_ASSERT (psPMR->psFuncTab->pfnReleaseKernelMappingData != IMG_NULL);

    PMR_Flags(psPMR, &ulFlags);

    eError = psPMR->psFuncTab->pfnAcquireKernelMappingData(psPMR->pvFlavourData,
                                                           uiLogicalOffset,
                                                           uiSize,
                                                           &pvKernelAddress,
                                                           &hPriv,
                                                           ulFlags);
    if (eError != PVRSRV_OK)
    {
        goto e0;
    }

    *ppvKernelAddressOut = pvKernelAddress;
    if (uiSize == 0)
    {
        /* Zero size means map the whole PMR in ...*/
        *puiLengthOut = (IMG_SIZE_T)psPMR->uiLogicalSize;
    }
    else if (uiSize > (1 << psPMR->uiLog2ContiguityGuarantee))
    {
    	/* ... map in the requested pages ...*/
		*puiLengthOut = uiSize;
    }
    else
    {
        /* ... otherwise we just map in one page */
        *puiLengthOut = 1 << psPMR->uiLog2ContiguityGuarantee;
    }
    *phPrivOut = hPriv;

    return PVRSRV_OK;

 e0:
    PVR_ASSERT(eError != PVRSRV_OK);
    return eError;
}

PVRSRV_ERROR
PMRAcquireKernelMappingData(PMR *psPMR,
                            IMG_SIZE_T uiLogicalOffset,
                            IMG_SIZE_T uiSize,
                            IMG_VOID **ppvKernelAddressOut,
                            IMG_SIZE_T *puiLengthOut,
                            IMG_HANDLE *phPrivOut)
{
    return _PMRAcquireKernelMappingData(psPMR,
                                        uiLogicalOffset,
                                        uiSize,
                                        ppvKernelAddressOut,
                                        puiLengthOut,
                                        phPrivOut,
                                        IMG_FALSE);
}

PVRSRV_ERROR
PMRAcquireSparseKernelMappingData(PMR *psPMR,
                                  IMG_SIZE_T uiLogicalOffset,
                                  IMG_SIZE_T uiSize,
                                  IMG_VOID **ppvKernelAddressOut,
                                  IMG_SIZE_T *puiLengthOut,
                                  IMG_HANDLE *phPrivOut)
{
    return _PMRAcquireKernelMappingData(psPMR,
                                        uiLogicalOffset,
                                        uiSize,
                                        ppvKernelAddressOut,
                                        puiLengthOut,
                                        phPrivOut,
                                        IMG_TRUE);
}

PVRSRV_ERROR
PMRReleaseKernelMappingData(PMR *psPMR,
                            IMG_HANDLE hPriv)
{
    PVR_ASSERT (psPMR->psFuncTab->pfnAcquireKernelMappingData != IMG_NULL);
    PVR_ASSERT (psPMR->psFuncTab->pfnReleaseKernelMappingData != IMG_NULL);

    psPMR->psFuncTab->pfnReleaseKernelMappingData(psPMR->pvFlavourData,
                                                  hPriv);

    return PVRSRV_OK;
}

/*
	_PMRLogicalOffsetToPhysicalOffset

	Translate between the "logical" offset which the upper levels
	provide and the physical offset which is what the PMR
	factories works on.

	As well as returning the physical offset we return the number of
	bytes remaining till the next chunk and if this chunk is valid.

	For multi-page operations, upper layers communicate their 
	Log2PageSize else argument is redundant (set to zero). 
*/

static IMG_VOID
_PMRLogicalOffsetToPhysicalOffset(const PMR *psPMR,
								  IMG_UINT32 ui32Log2PageSize,
								  IMG_UINT32 ui32NumOfPages,
								  IMG_DEVMEM_OFFSET_T uiLogicalOffset,
								  IMG_DEVMEM_OFFSET_T *puiPhysicalOffset,
								  IMG_UINT32 *pui32BytesRemain,
								  IMG_BOOL *bValid)
{
	PMR_MAPPING_TABLE *psMappingTable = psPMR->psMappingTable;
	IMG_DEVMEM_OFFSET_T uiPageSize = 1ULL << ui32Log2PageSize;
	IMG_DEVMEM_OFFSET_T uiOffset = uiLogicalOffset;
	IMG_UINT64 ui64ChunkIndex;
	IMG_UINT32 ui32Remain;
	IMG_UINT32 idx;

	/* Must be translating at least a page */
	PVR_ASSERT(ui32NumOfPages);

	if (psMappingTable->ui32NumPhysChunks == psMappingTable->ui32NumVirtChunks)
	{
		/* Fast path the common case, as logical and physical offsets are
			equal we _assume_ the ui32NumOfPages span is also valid */
		*pui32BytesRemain = TRUNCATE_64BITS_TO_32BITS(psPMR->uiLogicalSize - uiOffset);
		puiPhysicalOffset[0] = uiOffset;
		bValid[0] = IMG_TRUE;
		
		if (ui32NumOfPages > 1)
		{
			/* initial offset may not be page aligned, round down */
			uiOffset &= ~(uiPageSize-1);
			for (idx=1; idx < ui32NumOfPages; idx++)
			{
				uiOffset += uiPageSize;
				puiPhysicalOffset[idx] = uiOffset;
				bValid[idx] = IMG_TRUE;
			}
		}
	}
	else
	{
		for (idx=0; idx < ui32NumOfPages; idx++)
		{
			ui64ChunkIndex = OSDivide64r64(
					uiOffset,
					TRUNCATE_64BITS_TO_32BITS(psMappingTable->uiChunkSize),
					&ui32Remain);

			if (psMappingTable->aui32Translation[ui64ChunkIndex] == TRANSLATION_INVALID)
			{
				bValid[idx] = IMG_FALSE;
			}
			else
			{
				bValid[idx] = IMG_TRUE;
			}

			if (idx == 0)
			{
				if (ui32Remain == 0)
				{
					/* Start of chunk so return the chunk size */
					*pui32BytesRemain = TRUNCATE_64BITS_TO_32BITS(psMappingTable->uiChunkSize);
				}
				else
				{
					*pui32BytesRemain = TRUNCATE_64BITS_TO_32BITS(psMappingTable->uiChunkSize - ui32Remain);
				}

				puiPhysicalOffset[idx] = (psMappingTable->aui32Translation[ui64ChunkIndex] * psMappingTable->uiChunkSize) +	 ui32Remain;
				
				/* initial offset may not be page aligned, round down */
				uiOffset &= ~(uiPageSize-1);
			}
			else
			{
				puiPhysicalOffset[idx] = psMappingTable->aui32Translation[ui64ChunkIndex] * psMappingTable->uiChunkSize;
			}
			uiOffset += uiPageSize;
		}
	}
}

static PVRSRV_ERROR
_PMR_ReadBytesPhysical(PMR *psPMR,
                       IMG_DEVMEM_OFFSET_T uiPhysicalOffset,
                       IMG_UINT8 *pcBuffer,
                       IMG_SIZE_T uiBufSz,
                       IMG_SIZE_T *puiNumBytes)
{
	PVRSRV_ERROR eError;

    if (psPMR->psFuncTab->pfnReadBytes != IMG_NULL)
    {
        /* defer to callback if present */

        eError = PMRLockSysPhysAddresses(psPMR,
                                         psPMR->uiLog2ContiguityGuarantee);
        if (eError != PVRSRV_OK)
        {
            goto e0;
        }

        eError = psPMR->psFuncTab->pfnReadBytes(psPMR->pvFlavourData,
                                                uiPhysicalOffset,
                                                pcBuffer,
                                                uiBufSz,
                                                puiNumBytes);
        PMRUnlockSysPhysAddresses(psPMR);
        if (eError != PVRSRV_OK)
        {
            goto e0;
        }
    }
    else if (psPMR->psFuncTab->pfnAcquireKernelMappingData)
    {
        /* "default" handler for reading bytes */

        IMG_HANDLE hKernelMappingHandle;
        IMG_UINT8 *pcKernelAddress;
        PMR_FLAGS_T ulFlags;

        PMR_Flags(psPMR, &ulFlags);

        eError = psPMR->psFuncTab->pfnAcquireKernelMappingData(psPMR->pvFlavourData,
                                                               (IMG_SIZE_T) uiPhysicalOffset,
                                                               uiBufSz,
                                                               (IMG_VOID **)&pcKernelAddress,
                                                               &hKernelMappingHandle,
                                                               ulFlags);
        if (eError != PVRSRV_OK)
        {
            goto e0;
        }

        OSMemCopy(&pcBuffer[0], pcKernelAddress, uiBufSz);
        *puiNumBytes = uiBufSz;

        psPMR->psFuncTab->pfnReleaseKernelMappingData(psPMR->pvFlavourData,
                                                      hKernelMappingHandle);
    }
    else
    {
        PVR_DPF((PVR_DBG_ERROR, "PMR_ReadBytes: can't read from this PMR"));
        eError = PVRSRV_ERROR_INVALID_PARAMS;
        OSPanic();
        goto e0;
    }

    return PVRSRV_OK;

    /*
      error exit paths follow
    */

 e0:
    PVR_ASSERT(eError != PVRSRV_OK);
    return eError;
}

PVRSRV_ERROR
PMR_ReadBytes(PMR *psPMR,
              IMG_DEVMEM_OFFSET_T uiLogicalOffset,
              IMG_UINT8 *pcBuffer,
              IMG_SIZE_T uiBufSz,
              IMG_SIZE_T *puiNumBytes)
{
    PVRSRV_ERROR eError = PVRSRV_OK;
    IMG_DEVMEM_OFFSET_T uiPhysicalOffset;
    IMG_SIZE_T uiBytesCopied = 0;

    if (uiLogicalOffset + uiBufSz > psPMR->uiLogicalSize)
    {
		uiBufSz = TRUNCATE_64BITS_TO_32BITS(psPMR->uiLogicalSize - uiLogicalOffset);
    }
    PVR_ASSERT(uiBufSz > 0);
    PVR_ASSERT(uiBufSz <= psPMR->uiLogicalSize);

    /*
      PMR implementations can override this.  If they don't, a
      "default" handler uses kernel virtual mappings.  If the kernel
      can't provide a kernel virtual mapping, this function fails
    */
    PVR_ASSERT(psPMR->psFuncTab->pfnAcquireKernelMappingData != IMG_NULL ||
               psPMR->psFuncTab->pfnReadBytes != IMG_NULL);

	while (uiBytesCopied != uiBufSz)
	{
		IMG_UINT32 ui32Remain;
		IMG_SIZE_T uiBytesToCopy;
		IMG_SIZE_T uiRead;
		IMG_BOOL bValid;

		_PMRLogicalOffsetToPhysicalOffset(psPMR,
										  0,
										  1,
										  uiLogicalOffset,
										  &uiPhysicalOffset,
										  &ui32Remain,
										  &bValid);
		/* 
			Copy till either then end of the
			chunk or end end of the buffer
		*/
		uiBytesToCopy = MIN(uiBufSz - uiBytesCopied, ui32Remain);

		if (bValid)
		{
			/* Read the data from the PMR */
			eError = _PMR_ReadBytesPhysical(psPMR,
											uiPhysicalOffset,
											&pcBuffer[uiBytesCopied],
											uiBytesToCopy,
											&uiRead);
			if ((eError != PVRSRV_OK) || (uiRead != uiBytesToCopy))
			{
				PVR_DPF((PVR_DBG_ERROR,
						 "%s: Failed to read chunk (eError = %s, uiRead = "IMG_SIZE_FMTSPEC" uiBytesToCopy = "IMG_SIZE_FMTSPEC")",
						 __FUNCTION__,
						 PVRSRVGetErrorStringKM(eError),
						 uiRead,
						 uiBytesToCopy));
				/* Bail out as soon as we hit an error */
				break;
			}
		}
		else
		{
			/* Fill invalid chunks with 0 */
			OSMemSet(&pcBuffer[uiBytesCopied], 0, uiBytesToCopy);
			uiRead = uiBytesToCopy;
		}
		uiLogicalOffset += uiRead;
		uiBytesCopied += uiRead;
	}

	*puiNumBytes = uiBytesCopied;
    return eError;
}

static PVRSRV_ERROR
_PMR_WriteBytesPhysical(PMR *psPMR,
						IMG_DEVMEM_OFFSET_T uiPhysicalOffset,
						IMG_UINT8 *pcBuffer,
						IMG_SIZE_T uiBufSz,
						IMG_SIZE_T *puiNumBytes)
{
	PVRSRV_ERROR eError;

    if (psPMR->psFuncTab->pfnWriteBytes != IMG_NULL)
    {
        /* defer to callback if present */

        eError = PMRLockSysPhysAddresses(psPMR,
                                         psPMR->uiLog2ContiguityGuarantee);
        if (eError != PVRSRV_OK)
        {
            goto e0;
        }

        eError = psPMR->psFuncTab->pfnWriteBytes(psPMR->pvFlavourData,
												 uiPhysicalOffset,
                                                 pcBuffer,
                                                 uiBufSz,
                                                 puiNumBytes);
        PMRUnlockSysPhysAddresses(psPMR);
        if (eError != PVRSRV_OK)
        {
            goto e0;
        }
    }
    else if (psPMR->psFuncTab->pfnAcquireKernelMappingData)
    {
        /* "default" handler for reading bytes */

        IMG_HANDLE hKernelMappingHandle;
        IMG_UINT8 *pcKernelAddress;
        PMR_FLAGS_T ulFlags;

        PMR_Flags(psPMR, &ulFlags);

        eError = psPMR->psFuncTab->pfnAcquireKernelMappingData(psPMR->pvFlavourData,
                                                               (IMG_SIZE_T) uiPhysicalOffset,
                                                               uiBufSz,
                                                               (IMG_VOID **)&pcKernelAddress,
                                                               &hKernelMappingHandle,
                                                               ulFlags);
        if (eError != PVRSRV_OK)
        {
            goto e0;
        }

		OSMemCopy(pcKernelAddress, &pcBuffer[0], uiBufSz);
        *puiNumBytes = uiBufSz;

        psPMR->psFuncTab->pfnReleaseKernelMappingData(psPMR->pvFlavourData,
                                                      hKernelMappingHandle);
    }
    else
    {
		/*
			The write callback is optional as it's only required by the debug
			tools
		*/
        PVR_DPF((PVR_DBG_ERROR, "_PMR_WriteBytesPhysical: can't write to this PMR"));
        eError = PVRSRV_ERROR_PMR_NOT_PERMITTED;
        OSPanic();
        goto e0;
    }

    return PVRSRV_OK;

    /*
      error exit paths follow
    */

 e0:
    PVR_ASSERT(eError != PVRSRV_OK);
    return eError;
}

PVRSRV_ERROR
PMR_WriteBytes(PMR *psPMR,
			   IMG_DEVMEM_OFFSET_T uiLogicalOffset,
               IMG_UINT8 *pcBuffer,
               IMG_SIZE_T uiBufSz,
               IMG_SIZE_T *puiNumBytes)
{
    PVRSRV_ERROR eError = PVRSRV_OK;
	#if 0
    PMR_FLAGS_T uiFlags;
	#endif
    IMG_DEVMEM_OFFSET_T uiPhysicalOffset;
    IMG_SIZE_T uiBytesCopied = 0;

	/* FIXME: When we honour CPU mapping flags remove the #if 0*/
	#if 0
	/* Check that writes are allowed */
	PMR_Flags(psPMR, &uiFlags);
	if (!(uiFlags & PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE))
	{
		return PVRSRV_ERROR_PMR_NOT_PERMITTED;
	}
	#endif

    if (uiLogicalOffset + uiBufSz > psPMR->uiLogicalSize)
    {
        uiBufSz = TRUNCATE_64BITS_TO_32BITS(psPMR->uiLogicalSize - uiLogicalOffset);
    }
    PVR_ASSERT(uiBufSz > 0);
    PVR_ASSERT(uiBufSz <= psPMR->uiLogicalSize);

    /*
      PMR implementations can override this.  If they don't, a
      "default" handler uses kernel virtual mappings.  If the kernel
      can't provide a kernel virtual mapping, this function fails
    */
    PVR_ASSERT(psPMR->psFuncTab->pfnAcquireKernelMappingData != IMG_NULL ||
               psPMR->psFuncTab->pfnWriteBytes != IMG_NULL);

	while (uiBytesCopied != uiBufSz)
	{
		IMG_UINT32 ui32Remain;
		IMG_SIZE_T uiBytesToCopy;
		IMG_SIZE_T uiWrite;
		IMG_BOOL bValid;

		_PMRLogicalOffsetToPhysicalOffset(psPMR,
										  0,
										  1,
										  uiLogicalOffset,
										  &uiPhysicalOffset,
										  &ui32Remain,
										  &bValid);

		/* 
			Copy till either then end of the
			chunk or end end of the buffer
		*/
		uiBytesToCopy = MIN(uiBufSz - uiBytesCopied, ui32Remain);

		if (bValid)
		{
			/* Write the data to the PMR */
			eError = _PMR_WriteBytesPhysical(psPMR,
											 uiPhysicalOffset,
											 &pcBuffer[uiBytesCopied],
											 uiBytesToCopy,
											 &uiWrite);
			if ((eError != PVRSRV_OK) || (uiWrite != uiBytesToCopy))
			{
				PVR_DPF((PVR_DBG_ERROR,
						 "%s: Failed to read chunk (eError = %s, uiWrite = "IMG_SIZE_FMTSPEC" uiBytesToCopy = "IMG_SIZE_FMTSPEC")",
						 __FUNCTION__,
						 PVRSRVGetErrorStringKM(eError),
						 uiWrite,
						 uiBytesToCopy));
				/* Bail out as soon as we hit an error */
				break;
			}
		}
		else
		{
			/* Ignore writes to invalid pages */
			uiWrite = uiBytesToCopy;
		}
		uiLogicalOffset += uiWrite;
		uiBytesCopied += uiWrite;
	}

	*puiNumBytes = uiBytesCopied;
    return eError;
}

IMG_VOID
PMRRefPMR(PMR *psPMR)
{
	PVR_ASSERT(psPMR != IMG_NULL);
	_Ref(psPMR);
}

PVRSRV_ERROR
PMRUnrefPMR(PMR *psPMR)
{
    _UnrefAndMaybeDestroy(psPMR);
    return PVRSRV_OK;
}

PVRSRV_ERROR
PMR_Flags(const PMR *psPMR,
          PMR_FLAGS_T *puiPMRFlags)
{
    PVR_ASSERT(psPMR != IMG_NULL);

    *puiPMRFlags = psPMR->uiFlags;
    return PVRSRV_OK;
}

PVRSRV_ERROR
PMR_LogicalSize(const PMR *psPMR,
				IMG_DEVMEM_SIZE_T *puiLogicalSize)
{
	PVR_ASSERT(psPMR != IMG_NULL);

    *puiLogicalSize = psPMR->uiLogicalSize;
    return PVRSRV_OK;
}

/* must have called PMRLockSysPhysAddresses() before calling this! */
PVRSRV_ERROR
PMR_DevPhysAddr(const PMR *psPMR,
				IMG_UINT32 ui32Log2PageSize,
				IMG_UINT32 ui32NumOfPages,
                IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                IMG_DEV_PHYADDR *psDevAddrPtr,
                IMG_BOOL *pbValid)
{
	IMG_UINT32 ui32Remain;	
    PVRSRV_ERROR eError = PVRSRV_OK;
    IMG_DEVMEM_OFFSET_T auiPhysicalOffset[PMR_MAX_TRANSLATION_STACK_ALLOC];
    IMG_DEVMEM_OFFSET_T *puiPhysicalOffset = auiPhysicalOffset;

    PVR_ASSERT(psPMR != IMG_NULL);
    PVR_ASSERT(ui32NumOfPages > 0);
    PVR_ASSERT(psPMR->psFuncTab->pfnDevPhysAddr != IMG_NULL);

#ifdef PVRSRV_NEED_PVR_ASSERT
    OSLockAcquire(psPMR->hLock);
    PVR_ASSERT(psPMR->uiLockCount > 0);
    OSLockRelease(psPMR->hLock);
#endif

    if (ui32NumOfPages > PMR_MAX_TRANSLATION_STACK_ALLOC)
    {
    	puiPhysicalOffset = OSAllocMem(ui32NumOfPages * sizeof(IMG_DEVMEM_OFFSET_T));
    	if (puiPhysicalOffset == IMG_NULL)
    	{
    		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
    		goto e0;
    	}
    }

	_PMRLogicalOffsetToPhysicalOffset(psPMR,
									 ui32Log2PageSize,
									 ui32NumOfPages,
									 uiLogicalOffset,
									 puiPhysicalOffset,
									 &ui32Remain,
									 pbValid);
	if (*pbValid || _PMRIsSparse(psPMR))
	{
		/* Sparse PMR may not always have the first page valid */
		eError = psPMR->psFuncTab->pfnDevPhysAddr(psPMR->pvFlavourData,
												  ui32NumOfPages,
												  puiPhysicalOffset,
												  pbValid,
												  psDevAddrPtr);
	}

	if (puiPhysicalOffset != auiPhysicalOffset)
	{
		OSFreeMem(puiPhysicalOffset);
	}

    if (eError != PVRSRV_OK)
    {
        goto e0;
    }

    return PVRSRV_OK;

 e0:
    PVR_ASSERT(eError != PVRSRV_OK);
    return eError;
}

PVRSRV_ERROR
PMR_CpuPhysAddr(const PMR *psPMR,
                IMG_UINT32 ui32Log2PageSize,
                IMG_UINT32 ui32NumOfPages,
                IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                IMG_CPU_PHYADDR *psCpuAddrPtr,
                IMG_BOOL *pbValid)
{
    PVRSRV_ERROR eError;
	IMG_DEV_PHYADDR asDevPAddr[PMR_MAX_TRANSLATION_STACK_ALLOC];
	IMG_DEV_PHYADDR *psDevPAddr = asDevPAddr;

    if (ui32NumOfPages > PMR_MAX_TRANSLATION_STACK_ALLOC)
    {
    	psDevPAddr = OSAllocMem(ui32NumOfPages * sizeof(IMG_DEV_PHYADDR));
    	if (psDevPAddr == IMG_NULL)
    	{
    		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
    		goto e0;
    	}
    }

    eError = PMR_DevPhysAddr(psPMR, ui32Log2PageSize, ui32NumOfPages, 
							 uiLogicalOffset, psDevPAddr, pbValid);
    if (eError != PVRSRV_OK)
    {
        goto e1;
    }
	PhysHeapDevPAddrToCpuPAddr(psPMR->psPhysHeap, ui32NumOfPages, psCpuAddrPtr, psDevPAddr);
	
	if (ui32NumOfPages > PMR_MAX_TRANSLATION_STACK_ALLOC)
	{
		OSFreeMem(psDevPAddr);
	}

    return PVRSRV_OK;
e1:
	if (psDevPAddr != asDevPAddr)
	{
		OSFreeMem(psDevPAddr);
	}
e0:
    PVR_ASSERT(eError != PVRSRV_OK);
    return eError;
}

#if defined(PDUMP)

static PVRSRV_ERROR
_PMR_PDumpSymbolicAddrPhysical(const PMR *psPMR,
                               IMG_DEVMEM_OFFSET_T uiPhysicalOffset,
                               IMG_UINT32 ui32MemspaceNameLen,
                               IMG_CHAR *pszMemspaceName,
                               IMG_UINT32 ui32SymbolicAddrLen,
                               IMG_CHAR *pszSymbolicAddr,
                               IMG_DEVMEM_OFFSET_T *puiNewOffset,
                               IMG_DEVMEM_OFFSET_T *puiNextSymName)
{
	const IMG_CHAR *pszPrefix;

    if (psPMR->psFuncTab->pfnPDumpSymbolicAddr != IMG_NULL)
    {
        /* defer to callback if present */
        return psPMR->psFuncTab->pfnPDumpSymbolicAddr(psPMR->pvFlavourData,
                                                      uiPhysicalOffset,
                                                      pszMemspaceName,
                                                      ui32MemspaceNameLen,
                                                      pszSymbolicAddr,
                                                      ui32SymbolicAddrLen,
                                                      puiNewOffset,
                                                      puiNextSymName);
    }
    else
    {
        OSSNPrintf(pszMemspaceName, ui32MemspaceNameLen, "%s",
                   psPMR->pszPDumpDefaultMemspaceName);

        if (psPMR->pszPDumpFlavour != IMG_NULL)
        {
            pszPrefix = psPMR->pszPDumpFlavour;
        }
        else
        {
            pszPrefix = PMR_DEFAULT_PREFIX;
        }
        OSSNPrintf(pszSymbolicAddr, ui32SymbolicAddrLen, PMR_SYMBOLICADDR_FMTSPEC,
                   pszPrefix, psPMR->uiSerialNum);
        *puiNewOffset = uiPhysicalOffset;
        *puiNextSymName = (IMG_DEVMEM_OFFSET_T) psPMR->uiLogicalSize;

        return PVRSRV_OK;
    }
}


PVRSRV_ERROR
PMR_PDumpSymbolicAddr(const PMR *psPMR,
                      IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                      IMG_UINT32 ui32MemspaceNameLen,
                      IMG_CHAR *pszMemspaceName,
                      IMG_UINT32 ui32SymbolicAddrLen,
                      IMG_CHAR *pszSymbolicAddr,
                      IMG_DEVMEM_OFFSET_T *puiNewOffset,
                      IMG_DEVMEM_OFFSET_T *puiNextSymName
                      )
{
    IMG_DEVMEM_OFFSET_T uiPhysicalOffset;
    IMG_UINT32 ui32Remain;
    IMG_BOOL bValid;

    PVR_ASSERT(uiLogicalOffset < psPMR->uiLogicalSize);

    _PMRLogicalOffsetToPhysicalOffset(psPMR,
								      0,
								      1,
								      uiLogicalOffset,
								      &uiPhysicalOffset,
								      &ui32Remain,
								      &bValid);

	if (!bValid)
	{
		/* We should never be asked a symbolic address of an invalid chunk */
		return PVRSRV_ERROR_PMR_INVALID_CHUNK;
	}

	return _PMR_PDumpSymbolicAddrPhysical(psPMR,
										  uiPhysicalOffset,
										  ui32MemspaceNameLen,
										  pszMemspaceName,
										  ui32SymbolicAddrLen,
										  pszSymbolicAddr,
										  puiNewOffset,
										  puiNextSymName);
}

/*!
 * @brief Writes a WRW command to the script2 buffer, representing a
 * 		  dword write to a physical allocation. Size is always
 * 		  sizeof(IMG_UINT32).
 * @param psPMR - PMR object representing allocation
 * @param uiLogicalOffset - offset
 * @param ui32Value - value to write
 * @param uiPDumpFlags - pdump flags
 * @return PVRSRV_ERROR
 */
PVRSRV_ERROR
PMRPDumpLoadMemValue32(PMR *psPMR,
			         IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                     IMG_UINT32 ui32Value,
                     PDUMP_FLAGS_T uiPDumpFlags)
{
    PVRSRV_ERROR eError;
    IMG_CHAR aszMemspaceName[PMR_MAX_MEMSPACE_NAME_LENGTH_DEFAULT];
    IMG_CHAR aszSymbolicName[PMR_MAX_SYMBOLIC_ADDRESS_LENGTH_DEFAULT];
    IMG_DEVMEM_OFFSET_T uiPDumpSymbolicOffset;
    IMG_DEVMEM_OFFSET_T uiNextSymName;

    PVR_ASSERT(uiLogicalOffset + sizeof(IMG_UINT32) <= psPMR->uiLogicalSize);

    eError = PMRLockSysPhysAddresses(psPMR,
                                     psPMR->uiLog2ContiguityGuarantee);
    PVR_ASSERT(eError == PVRSRV_OK);

	/* Get the symbolic address of the PMR */
	eError = PMR_PDumpSymbolicAddr(psPMR,
								   uiLogicalOffset,
								   sizeof(aszMemspaceName),
								   &aszMemspaceName[0],
								   sizeof(aszSymbolicName),
								   &aszSymbolicName[0],
								   &uiPDumpSymbolicOffset,
				                   &uiNextSymName);
	PVR_ASSERT(eError == PVRSRV_OK);

	/* Write the WRW script command */
	eError = PDumpPMRWRW32(aszMemspaceName,
						 aszSymbolicName,
						 uiPDumpSymbolicOffset,
						 ui32Value,
						 uiPDumpFlags);
	PVR_ASSERT(eError == PVRSRV_OK);

    eError = PMRUnlockSysPhysAddresses(psPMR);
    PVR_ASSERT(eError == PVRSRV_OK);

    return PVRSRV_OK;
}

/*!
 * @brief Writes a WRW64 command to the script2 buffer, representing a
 * 		  dword write to a physical allocation. Size is always
 * 		  sizeof(IMG_UINT64).
 * @param psPMR - PMR object representing allocation
 * @param uiLogicalOffset - offset
 * @param ui64Value - value to write
 * @param uiPDumpFlags - pdump flags
 * @return PVRSRV_ERROR
 */
PVRSRV_ERROR
PMRPDumpLoadMemValue64(PMR *psPMR,
			         IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                     IMG_UINT64 ui64Value,
                     PDUMP_FLAGS_T uiPDumpFlags)
{
    PVRSRV_ERROR eError;
    IMG_CHAR aszMemspaceName[PMR_MAX_MEMSPACE_NAME_LENGTH_DEFAULT];
    IMG_CHAR aszSymbolicName[PMR_MAX_SYMBOLIC_ADDRESS_LENGTH_DEFAULT];
    IMG_DEVMEM_OFFSET_T uiPDumpSymbolicOffset;
    IMG_DEVMEM_OFFSET_T uiNextSymName;

    PVR_ASSERT(uiLogicalOffset + sizeof(IMG_UINT64) <= psPMR->uiLogicalSize);

    eError = PMRLockSysPhysAddresses(psPMR,
                                     psPMR->uiLog2ContiguityGuarantee);
    PVR_ASSERT(eError == PVRSRV_OK);

	/* Get the symbolic address of the PMR */
	eError = PMR_PDumpSymbolicAddr(psPMR,
								   uiLogicalOffset,
								   sizeof(aszMemspaceName),
								   &aszMemspaceName[0],
								   sizeof(aszSymbolicName),
								   &aszSymbolicName[0],
								   &uiPDumpSymbolicOffset,
				                   &uiNextSymName);
	PVR_ASSERT(eError == PVRSRV_OK);

	/* Write the WRW script command */
	eError = PDumpPMRWRW64(aszMemspaceName,
						 aszSymbolicName,
						 uiPDumpSymbolicOffset,
						 ui64Value,
						 uiPDumpFlags);
	PVR_ASSERT(eError == PVRSRV_OK);

    eError = PMRUnlockSysPhysAddresses(psPMR);
    PVR_ASSERT(eError == PVRSRV_OK);

    return PVRSRV_OK;
}

/*!
 * @brief PDumps the contents of the given allocation.
 * If bZero is IMG_TRUE then the zero page in the parameter stream is used
 * as the source of data, rather than the allocation's actual backing.
 * @param psPMR - PMR object representing allocation
 * @param uiLogicalOffset - Offset to write at
 * @param uiSize - Number of bytes to write
 * @param uiPDumpFlags - PDump flags
 * @param bZero - Use the PDump zero page as the source
 * @return PVRSRV_ERROR
 */
PVRSRV_ERROR
PMRPDumpLoadMem(PMR *psPMR,
					IMG_DEVMEM_OFFSET_T uiLogicalOffset,
					IMG_DEVMEM_SIZE_T uiSize,
					PDUMP_FLAGS_T uiPDumpFlags,
					IMG_BOOL bZero)
{
	/* common variables */
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_CHAR aszMemspaceName[PMR_MAX_MEMSPACE_NAME_LENGTH_DEFAULT];
	IMG_CHAR aszSymbolicName[PMR_MAX_SYMBOLIC_ADDRESS_LENGTH_DEFAULT];
	IMG_DEVMEM_OFFSET_T uiPDumpSymbolicOffset;
	PDUMP_FILEOFFSET_T uiParamStreamFileOffset;
	IMG_SIZE_T uiBufSz;
	IMG_SIZE_T uiNumBytes;
	IMG_DEVMEM_OFFSET_T uiNextSymName;
	const IMG_CHAR *pszParamStreamFileName;

	/* required when !bZero */
	#define PMR_MAX_PDUMP_BUFSZ 16384
	IMG_CHAR aszParamStreamFilename[PMR_MAX_PARAMSTREAM_FILENAME_LENGTH_DEFAULT];
	IMG_UINT8 *pcBuffer = IMG_NULL;

	PVR_ASSERT(uiLogicalOffset + uiSize <= psPMR->uiLogicalSize);

	if(bZero)
	{
		/* Check if this PMR needs to be persistent:
		 * If the allocation is persistent then it will be present in every
		 * pdump stream after its allocation. We must ensure the zeroing is also
		 * persistent so that every PDump MALLOC is accompanied by the initialisation
		 * to zero.
		 */
		if(psPMR->bForcePersistent)
		{
			uiPDumpFlags = PDUMP_FLAGS_PERSISTENT;
		}

		PDumpCommentWithFlags(uiPDumpFlags, "Zeroing allocation (%llu bytes)",
										(unsigned long long) uiSize);

		/* get the zero page information. it is constant for this function */
		PDumpGetParameterZeroPageInfo(&uiParamStreamFileOffset, &uiBufSz, &pszParamStreamFileName);
	}
	else
	{
		uiBufSz = PMR_MAX_PDUMP_BUFSZ;
		if (uiBufSz > uiSize)
		{
			uiBufSz = TRUNCATE_64BITS_TO_SIZE_T(uiSize);
		}

		pcBuffer = OSAllocMem(uiBufSz);
		PVR_ASSERT(pcBuffer != IMG_NULL);

		eError = PMRLockSysPhysAddresses(psPMR,
								psPMR->uiLog2ContiguityGuarantee);
		PVR_ASSERT(eError == PVRSRV_OK);

		pszParamStreamFileName = aszParamStreamFilename;
	}

	while (uiSize > 0)
	{
		IMG_DEVMEM_OFFSET_T uiPhysicalOffset;
		IMG_UINT32 ui32Remain;
		IMG_BOOL bValid;

		_PMRLogicalOffsetToPhysicalOffset(psPMR,
 										  0,
 										  1,
										  uiLogicalOffset,
										  &uiPhysicalOffset,
										  &ui32Remain,
										  &bValid);

		if (bValid)
		{
			eError = _PMR_PDumpSymbolicAddrPhysical(psPMR,
													uiPhysicalOffset,
													sizeof(aszMemspaceName),
													&aszMemspaceName[0],
													sizeof(aszSymbolicName),
													&aszSymbolicName[0],
													&uiPDumpSymbolicOffset,
													&uiNextSymName);
			if(eError != PVRSRV_OK)
			{
				goto err_unlock_phys;
			}

			if(bZero)
			{
				uiNumBytes = TRUNCATE_64BITS_TO_SIZE_T(MIN(uiSize, uiBufSz));
			}
			else
			{

				/* Reads enough to fill buffer, or until next chunk,
				or until end of PMR, whichever comes first */
				eError = _PMR_ReadBytesPhysical(psPMR,
												uiPhysicalOffset,
												pcBuffer,
												TRUNCATE_64BITS_TO_SIZE_T(MIN3(uiBufSz, uiSize, ui32Remain)),
												&uiNumBytes);
				if(eError != PVRSRV_OK)
				{
				    goto err_unlock_phys;
				}
				PVR_ASSERT(uiNumBytes > 0);

				eError = PDumpWriteBuffer(pcBuffer,
							  uiNumBytes,
							  uiPDumpFlags,
							  &aszParamStreamFilename[0],
							  sizeof(aszParamStreamFilename),
							  &uiParamStreamFileOffset);
				if(eError != PVRSRV_OK)
				{
				    goto err_unlock_phys;
				}
			}

			eError = PDumpPMRLDB(aszMemspaceName,
									aszSymbolicName,
									uiPDumpSymbolicOffset,
									uiNumBytes,
									pszParamStreamFileName,
									uiParamStreamFileOffset,
									uiPDumpFlags);

			if(eError != PVRSRV_OK)
			{
				goto err_unlock_phys;
			}
		}
		else
		{
			/* Skip over invalid chunks */
			uiNumBytes = TRUNCATE_64BITS_TO_SIZE_T(MIN(ui32Remain, uiSize));
		}

		 uiLogicalOffset += uiNumBytes;
		 PVR_ASSERT(uiNumBytes <= uiSize);
		 uiSize -= uiNumBytes;
	}

err_unlock_phys:

	if(!bZero)
	{
	    eError = PMRUnlockSysPhysAddresses(psPMR);
	    PVR_ASSERT(eError == PVRSRV_OK);

	    OSFreeMem(pcBuffer);
	}
    return eError;
}



PVRSRV_ERROR
PMRPDumpSaveToFile(const PMR *psPMR,
                   IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                   IMG_DEVMEM_SIZE_T uiSize,
                   IMG_UINT32 uiArraySize,
                   const IMG_CHAR *pszFilename)
{
    PVRSRV_ERROR eError;
    IMG_CHAR aszMemspaceName[PMR_MAX_MEMSPACE_NAME_LENGTH_DEFAULT];
    IMG_CHAR aszSymbolicName[PMR_MAX_SYMBOLIC_ADDRESS_LENGTH_DEFAULT];
    IMG_DEVMEM_OFFSET_T uiOutOffset;
    IMG_DEVMEM_OFFSET_T uiNextSymName;

    PVR_UNREFERENCED_PARAMETER(uiArraySize);

    PVR_ASSERT(uiLogicalOffset + uiSize <= psPMR->uiLogicalSize);

    eError = PMR_PDumpSymbolicAddr(psPMR,
                                   uiLogicalOffset,
                                   sizeof(aszMemspaceName),
                                   &aszMemspaceName[0],
                                   sizeof(aszSymbolicName),
                                   &aszSymbolicName[0],
                                   &uiOutOffset,
				   &uiNextSymName);
    PVR_ASSERT(eError == PVRSRV_OK);
    PVR_ASSERT(uiLogicalOffset + uiSize <= uiNextSymName);

    eError = PDumpPMRSAB(aszMemspaceName,
                         aszSymbolicName,
                         uiOutOffset,
                         uiSize,
                         pszFilename,
                         0);
    PVR_ASSERT(eError == PVRSRV_OK);

    return PVRSRV_OK;
}
#endif	/* PDUMP */

/*
   FIXME: Find a better way to do this
 */

IMG_VOID *PMRGetPrivateDataHack(const PMR *psPMR,
                                const PMR_IMPL_FUNCTAB *psFuncTab)
{
    PVR_ASSERT(psFuncTab == psPMR->psFuncTab);

    return psPMR->pvFlavourData;
}

PVRSRV_ERROR
PMRWritePMPageList(/* Target PMR, offset, and length */
                   PMR *psPageListPMR,
                   IMG_DEVMEM_OFFSET_T uiTableOffset,
                   IMG_DEVMEM_SIZE_T  uiTableLength,
                   /* Referenced PMR, and "page" granularity */
                   PMR *psReferencePMR,
                   IMG_DEVMEM_LOG2ALIGN_T uiLog2PageSize,
                   PMR_PAGELIST **ppsPageList,
                   IMG_UINT64 *pui64CheckSum)
{
    PVRSRV_ERROR eError;
    IMG_DEVMEM_SIZE_T uiWordSize;
    IMG_UINT32 uiNumPages;
    IMG_UINT32 uiPageIndex;
    IMG_UINT32 ui32CheckSumXor = 0;
    IMG_UINT32 ui32CheckSumAdd = 0;
    PMR_FLAGS_T uiFlags;
    PMR_PAGELIST *psPageList;
#if defined(PDUMP)
    IMG_CHAR aszTableEntryMemspaceName[100];
    IMG_CHAR aszTableEntrySymbolicName[100];
    IMG_DEVMEM_OFFSET_T uiTableEntryPDumpOffset;
    IMG_CHAR aszPageMemspaceName[100];
    IMG_CHAR aszPageSymbolicName[100];
    IMG_DEVMEM_OFFSET_T uiPagePDumpOffset;
    IMG_DEVMEM_OFFSET_T uiNextSymName;
#endif
#if !defined(NO_HARDWARE)
    IMG_UINT32 uiPageListPageSize = 1 << psPageListPMR->uiLog2ContiguityGuarantee;
    IMG_BOOL bPageIsMapped = IMG_FALSE;
    IMG_UINT64 uiPageListPMRPage = 0;
    IMG_UINT64 uiPrevPageListPMRPage = 0;
    IMG_HANDLE hPrivData = IMG_NULL;
    IMG_VOID *pvKernAddr = IMG_NULL;
	IMG_DEV_PHYADDR asDevPAddr[PMR_MAX_TRANSLATION_STACK_ALLOC];
	IMG_BOOL abValid[PMR_MAX_TRANSLATION_STACK_ALLOC];
    IMG_DEV_PHYADDR *pasDevAddrPtr;
    IMG_UINT32 *pui32DataPtr;
    IMG_BOOL *pbPageIsValid;
#endif
    /* FIXME: should this be configurable? */
    uiWordSize = 4;

    /* check we're being asked to write the same number of 4-byte units as there are pages */
    uiNumPages = (IMG_UINT32)(psReferencePMR->uiLogicalSize >> uiLog2PageSize);

    if ((PMR_SIZE_T)uiNumPages << uiLog2PageSize != psReferencePMR->uiLogicalSize)
    {
		/* Strictly speaking, it's possible to provoke this error in two ways:
			(i) if it's not a whole multiple of the page size; or
			(ii) if there are more than 4 billion pages.
           The latter is unlikely. :)  but the check is required in order to justify the cast.
		*/
        eError = PVRSRV_ERROR_PMR_NOT_PAGE_MULTIPLE;
        goto e0;
    }
    uiWordSize = (IMG_UINT32)uiTableLength / uiNumPages;
    if (uiNumPages * uiWordSize != uiTableLength)
    {
        eError = PVRSRV_ERROR_PMR_NOT_PAGE_MULTIPLE;
        goto e0;
    }

    /* Check we're not being asked to write off the end of the PMR */
    if (uiTableOffset + uiTableLength > psPageListPMR->uiLogicalSize)
    {
        /* table memory insufficient to store all the entries */
        /* table insufficient to store addresses of whole block */
        eError = PVRSRV_ERROR_INVALID_PARAMS;
        goto e0;
    }

    /* the PMR into which we are writing must not be user CPU mappable: */
    eError = PMR_Flags(psPageListPMR, &uiFlags);
    if ((eError != PVRSRV_OK) ||
		((uiFlags & (PVRSRV_MEMALLOCFLAG_CPU_READABLE | PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE)) != 0))
    {
		PVR_DPF((PVR_DBG_ERROR, "eError = %d", eError));
		PVR_DPF((PVR_DBG_ERROR, "masked flags = 0x%08x", (uiFlags & (PVRSRV_MEMALLOCFLAG_CPU_READABLE | PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE))));
		PVR_DPF((PVR_DBG_ERROR, "Page list PMR allows CPU mapping (0x%08x)", uiFlags));
		eError = PVRSRV_ERROR_DEVICEMEM_INVALID_PMR_FLAGS;
        goto e0;
    }

	if (_PMRIsSparse(psPageListPMR))
	{
		PVR_DPF((PVR_DBG_ERROR, "PageList PMR is sparse"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto e0;
	}

	if (_PMRIsSparse(psReferencePMR))
	{
		PVR_DPF((PVR_DBG_ERROR, "Reference PMR is sparse"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto e0;
	}

	psPageList = OSAllocMem(sizeof(PMR_PAGELIST));
	if (psPageList == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to allocate PMR page list"));
		goto e0;
	}
	psPageList->psReferencePMR = psReferencePMR;

    /* Need to lock down the physical addresses of the reference PMR */
    /* N.B.  This also checks that the requested "contiguity" is achievable */
    eError = PMRLockSysPhysAddresses(psReferencePMR,
                                     uiLog2PageSize);
    if(eError != PVRSRV_OK)
    {
        goto e1;
    }

#if !defined(NO_HARDWARE)
    if (uiNumPages > PMR_MAX_TRANSLATION_STACK_ALLOC)
	{
	    pasDevAddrPtr = OSAllocMem(uiNumPages * sizeof(IMG_DEV_PHYADDR));
		if (pasDevAddrPtr == IMG_NULL)
		{
			 PVR_DPF((PVR_DBG_ERROR, "Failed to allocate PMR page list"));
			 eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			 goto e2;
		}

		pbPageIsValid = OSAllocMem(uiNumPages * sizeof(IMG_BOOL));
		if (pbPageIsValid == IMG_NULL)
		{
			/* Clean-up before exit */
			 OSFreeMem(pasDevAddrPtr);

			 PVR_DPF((PVR_DBG_ERROR, "Failed to allocate PMR page state"));
			 eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			 goto e2;
		}
	}
	else
	{
		pasDevAddrPtr = asDevPAddr;
		pbPageIsValid = abValid;
	}
	
	
	eError = PMR_DevPhysAddr(psReferencePMR, uiLog2PageSize, uiNumPages, 0,
							 pasDevAddrPtr, pbPageIsValid);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to map PMR pages into device physical addresses"));
		goto e3;
	}	
#endif

    for (uiPageIndex = 0; uiPageIndex < uiNumPages; uiPageIndex++)
    {
#if !defined(NO_HARDWARE)
        IMG_DEV_PHYADDR sOldDevAddrPtr = {1}; //Set to non-aligned non-valid page
#endif
        IMG_DEVMEM_OFFSET_T uiPMROffset = uiTableOffset + (uiWordSize * uiPageIndex);
#if defined(PDUMP)
        eError = PMR_PDumpSymbolicAddr(psPageListPMR,
                                       uiPMROffset,
                                       sizeof(aszTableEntryMemspaceName),
                                       &aszTableEntryMemspaceName[0],
                                       sizeof(aszTableEntrySymbolicName),
                                       &aszTableEntrySymbolicName[0],
                                       &uiTableEntryPDumpOffset,
                                       &uiNextSymName);
        PVR_ASSERT(eError == PVRSRV_OK);

        eError = PMR_PDumpSymbolicAddr(psReferencePMR,
                                       (IMG_DEVMEM_OFFSET_T)uiPageIndex << uiLog2PageSize,
                                       sizeof(aszPageMemspaceName),
                                       &aszPageMemspaceName[0],
                                       sizeof(aszPageSymbolicName),
                                       &aszPageSymbolicName[0],
                                       &uiPagePDumpOffset,
                                       &uiNextSymName);
        PVR_ASSERT(eError == PVRSRV_OK);

        eError = PDumpWriteShiftedMaskedValue(/* destination */
                                              aszTableEntryMemspaceName,
                                              aszTableEntrySymbolicName,
                                              uiTableEntryPDumpOffset,
                                              /* source */
                                              aszPageMemspaceName,
                                              aszPageSymbolicName,
                                              uiPagePDumpOffset,
                                              /* shift right */
                                              uiLog2PageSize,
                                              /* shift left */
                                              0,
                                              /* mask */
                                              0xffffffff,
                                              /* word size */
                                              uiWordSize,
                                              /* flags */
                                              PDUMP_FLAGS_CONTINUOUS);
        PVR_ASSERT(eError == PVRSRV_OK);
#else
		PVR_UNREFERENCED_PARAMETER(uiPMROffset);
#endif
#if !defined(NO_HARDWARE)

		/*
			We check for sparse PMR's at function entry, but as we can,
			check that every page is valid
		*/
		PVR_ASSERT(pbPageIsValid[uiPageIndex]);

        uiPageListPMRPage = uiPMROffset >> psReferencePMR->uiLog2ContiguityGuarantee;

        if ((bPageIsMapped == IMG_FALSE) || (uiPageListPMRPage != uiPrevPageListPMRPage))
        {
            IMG_SIZE_T uiMappingOffset = uiPMROffset & (~(uiPageListPageSize - 1));
            IMG_SIZE_T uiMappedSize;

            if (bPageIsMapped == IMG_TRUE)
            {
                PMRReleaseKernelMappingData(psPageListPMR, hPrivData);
            }

            eError = PMRAcquireKernelMappingData(psPageListPMR,
                                                 uiMappingOffset,
                                                 uiPageListPageSize,
                                                 &pvKernAddr,
                                                 &uiMappedSize,
                                                 &hPrivData);
            if (eError != PVRSRV_OK)
            {
                PVR_DPF((PVR_DBG_ERROR, "Error mapping page list PMR page (%llu) into kernel (%d)",
                         uiPageListPMRPage, eError));
                goto e3;
            }

            bPageIsMapped = IMG_TRUE;
            uiPrevPageListPMRPage = uiPageListPMRPage;
            PVR_ASSERT(uiMappedSize >= uiPageListPageSize);
            PVR_ASSERT(pvKernAddr != IMG_NULL);
        }

        PVR_ASSERT(((pasDevAddrPtr[uiPageIndex].uiAddr >> uiLog2PageSize) & 0xFFFFFFFF00000000ll) == 0);

        /* Write the physcial page index into the page list PMR */
        pui32DataPtr = (IMG_UINT32 *) (((IMG_CHAR *) pvKernAddr) + (uiPMROffset & (uiPageListPageSize - 1)));
        *pui32DataPtr = TRUNCATE_64BITS_TO_32BITS(pasDevAddrPtr[uiPageIndex].uiAddr >> uiLog2PageSize);
        ui32CheckSumXor ^= TRUNCATE_64BITS_TO_32BITS(pasDevAddrPtr[uiPageIndex].uiAddr >> uiLog2PageSize);
        ui32CheckSumAdd += TRUNCATE_64BITS_TO_32BITS(pasDevAddrPtr[uiPageIndex].uiAddr >> uiLog2PageSize);
        PVR_ASSERT(pasDevAddrPtr[uiPageIndex].uiAddr != 0);
        PVR_ASSERT(pasDevAddrPtr[uiPageIndex].uiAddr != sOldDevAddrPtr.uiAddr);
        sOldDevAddrPtr.uiAddr = pasDevAddrPtr[uiPageIndex].uiAddr;
        /* Last page so unmap */
        if (uiPageIndex == (uiNumPages - 1))
        {
            PMRReleaseKernelMappingData(psPageListPMR, hPrivData);
        }
#endif
    }

#if !defined(NO_HARDWARE)
    if (pasDevAddrPtr != asDevPAddr)
	{
		OSFreeMem(pbPageIsValid);
		OSFreeMem(pasDevAddrPtr);
	}
#endif
    *pui64CheckSum = ((IMG_UINT64)ui32CheckSumXor << 32) | ui32CheckSumAdd;
    *ppsPageList = psPageList;
    return PVRSRV_OK;

    /*
      error exit paths follow
    */
#if !defined(NO_HARDWARE)
e3: 
    if (pasDevAddrPtr != asDevPAddr)
	{
		OSFreeMem(pbPageIsValid);  
		OSFreeMem(pasDevAddrPtr);
	}
 e2:
   PMRUnlockSysPhysAddresses(psReferencePMR);
#endif
 e1:
	OSFreeMem(psPageList);
 e0:
    PVR_ASSERT(eError != PVRSRV_OK);
    return eError;
}


PVRSRV_ERROR /* FIXME: should be IMG_VOID */
PMRUnwritePMPageList(PMR_PAGELIST *psPageList)
{
    PVRSRV_ERROR eError2;

    eError2 = PMRUnlockSysPhysAddresses(psPageList->psReferencePMR);
    PVR_ASSERT(eError2 == PVRSRV_OK);
	OSFreeMem(psPageList);

    return PVRSRV_OK;
}

PVRSRV_ERROR
PMRZeroingPMR(PMR *psPMR,
				IMG_DEVMEM_LOG2ALIGN_T uiLog2PageSize)
{
    IMG_UINT32 uiNumPages;
    IMG_UINT32 uiPageIndex;
    IMG_UINT32 ui32PageSize = 1 << uiLog2PageSize;
    IMG_HANDLE hPrivData = IMG_NULL;
    IMG_VOID *pvKernAddr = IMG_NULL;
    PVRSRV_ERROR eError = PVRSRV_OK;
    IMG_SIZE_T uiMapedSize;

    PVR_ASSERT(psPMR);

    /* Calculate number of pages in this PMR */
	uiNumPages = (IMG_UINT32)(psPMR->uiLogicalSize >> uiLog2PageSize);

	/* Verify the logical Size is a multiple or the physical page size */
    if ((PMR_SIZE_T)uiNumPages << uiLog2PageSize != psPMR->uiLogicalSize)
    {
		PVR_DPF((PVR_DBG_ERROR, "PMRZeroingPMR: PMR is not a multiple of %u",ui32PageSize));
        eError = PVRSRV_ERROR_PMR_NOT_PAGE_MULTIPLE;
        goto MultiPage_Error;
    }

	if (_PMRIsSparse(psPMR))
	{
		PVR_DPF((PVR_DBG_ERROR, "PMRZeroingPMR: PMR is sparse"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto Sparse_Error;
	}

	/* Scan through all pages of the PMR */
    for (uiPageIndex = 0; uiPageIndex < uiNumPages; uiPageIndex++)
    {
        /* map the physical page (for a given PMR offset) into kernel space */
        eError = PMRAcquireKernelMappingData(psPMR,
                                             (IMG_SIZE_T)uiPageIndex << uiLog2PageSize,
                                             ui32PageSize,
                                             &pvKernAddr,
                                             &uiMapedSize,
                                             &hPrivData);
        if (eError != PVRSRV_OK)
        {
    		PVR_DPF((PVR_DBG_ERROR, "PMRZeroingPMR: AcquireKernelMapping failed with error %u", eError));
        	goto AcquireKernelMapping_Error;
        }

        /* ensure the mapped page size is the same as the physical page size */
        if (uiMapedSize != ui32PageSize)
        {
    		PVR_DPF((PVR_DBG_ERROR, "PMRZeroingPMR: Physical Page size = 0x%08x, Size of Mapping = 0x%016llx",
    								ui32PageSize,
    								(IMG_UINT64)uiMapedSize));
    		eError = PVRSRV_ERROR_INVALID_PARAMS;
        	goto MappingSize_Error;
        }

        /* zeroing page content */
        OSMemSet(pvKernAddr, 0, ui32PageSize);

        /* release mapping */
        PMRReleaseKernelMappingData(psPMR, hPrivData);

    }

    PVR_DPF((PVR_DBG_WARNING,"PMRZeroingPMR: Zeroing PMR %p done (num pages %u, page size %u)",
    						psPMR,
    						uiNumPages,
    						ui32PageSize));

    return PVRSRV_OK;


    /* Error handling */

MappingSize_Error:
	PMRReleaseKernelMappingData(psPMR, hPrivData);

AcquireKernelMapping_Error:
Sparse_Error:
MultiPage_Error:

    PVR_ASSERT(eError != PVRSRV_OK);
    return eError;
}

PVRSRV_ERROR
PMRDumpPageList(PMR *psPMR,
					IMG_DEVMEM_LOG2ALIGN_T uiLog2PageSize)
{
    IMG_DEV_PHYADDR sDevAddrPtr;
    IMG_UINT32 uiNumPages;
    IMG_UINT32 uiPageIndex;
    IMG_BOOL bPageIsValid;
    IMG_UINT32 ui32Col = 16;
    IMG_UINT32 ui32SizePerCol = 11;
    IMG_UINT32 ui32ByteCount = 0;
    IMG_CHAR pszBuffer[16 /* ui32Col */ * 11 /* ui32SizePerCol */ + 1];
    PVRSRV_ERROR eError = PVRSRV_OK;

    /* Get number of pages */
	uiNumPages = (IMG_UINT32)(psPMR->uiLogicalSize >> uiLog2PageSize);

	/* Verify the logical Size is a multiple or the physical page size */
    if ((PMR_SIZE_T)uiNumPages << uiLog2PageSize != psPMR->uiLogicalSize)
    {
		PVR_DPF((PVR_DBG_ERROR, "PMRPrintPageList: PMR is not a multiple of %u", 1 << uiLog2PageSize));
        eError = PVRSRV_ERROR_PMR_NOT_PAGE_MULTIPLE;
        goto MultiPage_Error;
    }

	if (_PMRIsSparse(psPMR))
	{
		PVR_DPF((PVR_DBG_ERROR, "PMRPrintPageList: PMR is sparse"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto Sparse_Error;
	}

	PVR_LOG(("    PMR %p, Number of pages %u, Log2PageSize %d", psPMR, uiNumPages, uiLog2PageSize));

	/* Print the address of the physical pages */
    for (uiPageIndex = 0; uiPageIndex < uiNumPages; uiPageIndex++)
    {
    	/* Get Device physical Address */
        eError = PMR_DevPhysAddr(psPMR,
                        uiLog2PageSize,
                        1,
                        (IMG_DEVMEM_OFFSET_T)uiPageIndex << uiLog2PageSize,
                        &sDevAddrPtr,
                        &bPageIsValid);
        if (eError != PVRSRV_OK)
        {
    		PVR_DPF((PVR_DBG_ERROR, "PMRPrintPageList: PMR %p failed to get DevPhysAddr with error %u",
    								psPMR,
    								eError));
        	goto DevPhysAddr_Error;
        }

        ui32ByteCount += OSSNPrintf(pszBuffer + ui32ByteCount, ui32SizePerCol + 1, "%08x ", (IMG_UINT32)(sDevAddrPtr.uiAddr >> uiLog2PageSize));
        PVR_ASSERT(ui32ByteCount < ui32Col * ui32SizePerCol);

		if (uiPageIndex % ui32Col == ui32Col -1)
		{
			PVR_LOG(("      Phys Page: %s", pszBuffer));
			ui32ByteCount = 0;
		}
    }
    if (ui32ByteCount > 0)
    {
		PVR_LOG(("      Phys Page: %s", pszBuffer));
    }

    return PVRSRV_OK;

    /* Error handling */
DevPhysAddr_Error:
Sparse_Error:
MultiPage_Error:
    PVR_ASSERT(eError != PVRSRV_OK);
    return eError;
}

#if defined(PDUMP)
extern PVRSRV_ERROR
PMRPDumpPol32(const PMR *psPMR,
              IMG_DEVMEM_OFFSET_T uiLogicalOffset,
              IMG_UINT32 ui32Value,
              IMG_UINT32 ui32Mask,
              PDUMP_POLL_OPERATOR eOperator,
              PDUMP_FLAGS_T uiPDumpFlags)
{
    PVRSRV_ERROR eError;
    IMG_CHAR aszMemspaceName[100];
    IMG_CHAR aszSymbolicName[100];
    IMG_DEVMEM_OFFSET_T uiPDumpOffset;
    IMG_DEVMEM_OFFSET_T uiNextSymName;

    eError = PMR_PDumpSymbolicAddr(psPMR,
                                   uiLogicalOffset,
                                   sizeof(aszMemspaceName),
                                   &aszMemspaceName[0],
                                   sizeof(aszSymbolicName),
                                   &aszSymbolicName[0],
                                   &uiPDumpOffset,
				   &uiNextSymName);
    if (eError != PVRSRV_OK)
    {
        goto e0;
    }

#define _MEMPOLL_DELAY		(1000)
#define _MEMPOLL_COUNT		(2000000000 / _MEMPOLL_DELAY)

    eError = PDumpPMRPOL(aszMemspaceName,
                         aszSymbolicName,
                         uiPDumpOffset,
                         ui32Value,
                         ui32Mask,
                         eOperator,
                         _MEMPOLL_COUNT,
                         _MEMPOLL_DELAY,
                         uiPDumpFlags);
    if (eError != PVRSRV_OK)
    {
        goto e0;
    }

    return PVRSRV_OK;

    /*
      error exit paths follow
    */

 e0:
    PVR_ASSERT(eError != PVRSRV_OK);
    return eError;
}

PVRSRV_ERROR
PMRPDumpCBP(const PMR *psPMR,
            IMG_DEVMEM_OFFSET_T uiReadOffset,
            IMG_DEVMEM_OFFSET_T uiWriteOffset,
            IMG_DEVMEM_SIZE_T uiPacketSize,
            IMG_DEVMEM_SIZE_T uiBufferSize)
{
    PVRSRV_ERROR eError;
    IMG_CHAR aszMemspaceName[100];
    IMG_CHAR aszSymbolicName[100];
    IMG_DEVMEM_OFFSET_T uiPDumpOffset;
    IMG_DEVMEM_OFFSET_T uiNextSymName;

    eError = PMR_PDumpSymbolicAddr(psPMR,
                                   uiReadOffset,
                                   sizeof(aszMemspaceName),
                                   &aszMemspaceName[0],
                                   sizeof(aszSymbolicName),
                                   &aszSymbolicName[0],
                                   &uiPDumpOffset,
                                   &uiNextSymName);
    if (eError != PVRSRV_OK)
    {
        goto e0;
    }

    eError = PDumpPMRCBP(aszMemspaceName,
                         aszSymbolicName,
                         uiPDumpOffset,
                         uiWriteOffset,
                         uiPacketSize,
                         uiBufferSize);
    if (eError != PVRSRV_OK)
    {
        goto e0;
    }

    return PVRSRV_OK;

    /*
      error exit paths follow
    */

 e0:
    PVR_ASSERT(eError != PVRSRV_OK);
    return eError;
}
#endif

PVRSRV_ERROR
PMRInit()
{
	PVRSRV_ERROR eError;

    if (_gsSingletonPMRContext.bModuleInitialised)
    {
        PVR_DPF((PVR_DBG_ERROR, "pmr.c:  oops, already initialized"));
        return PVRSRV_ERROR_PMR_UNRECOVERABLE_ERROR;
    }

	eError = OSLockCreate(&_gsSingletonPMRContext.hLock, LOCK_TYPE_PASSIVE);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	eError = OSLockCreate(&gGlobalLookupPMRLock, LOCK_TYPE_PASSIVE);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

    _gsSingletonPMRContext.uiNextSerialNum = 1;

    _gsSingletonPMRContext.uiNextKey = 0x8300f001 * (IMG_UINTPTR_T)&_gsSingletonPMRContext;

    _gsSingletonPMRContext.bModuleInitialised = IMG_TRUE;

    _gsSingletonPMRContext.uiNumLivePMRs = 0;

    return PVRSRV_OK;
}

PVRSRV_ERROR
PMRDeInit()
{
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();

	if (psPVRSRVData->eServicesState != PVRSRV_SERVICES_STATE_OK)
	{
		return PVRSRV_OK;
	}

    PVR_ASSERT(_gsSingletonPMRContext.bModuleInitialised);
    if (!_gsSingletonPMRContext.bModuleInitialised)
    {
        PVR_DPF((PVR_DBG_ERROR, "pmr.c:  oops, not initialized"));
        return PVRSRV_ERROR_PMR_UNRECOVERABLE_ERROR;
    }

    PVR_ASSERT(_gsSingletonPMRContext.uiNumLivePMRs == 0);
    if (_gsSingletonPMRContext.uiNumLivePMRs != 0)
    {
        PVR_DPF((PVR_DBG_ERROR, "pmr.c:  %d live PMR(s) remain(s)", _gsSingletonPMRContext.uiNumLivePMRs));
        PVR_DPF((PVR_DBG_ERROR, "pmr.c:  This is an unrecoverable error; a subsequent crash is inevitable"));
        return PVRSRV_ERROR_PMR_UNRECOVERABLE_ERROR;
    }

	OSLockDestroy(_gsSingletonPMRContext.hLock);
	OSLockDestroy(gGlobalLookupPMRLock);

    _gsSingletonPMRContext.bModuleInitialised = IMG_FALSE;

    /*
      FIXME:

      should deinitialise the mutex here
    */

    return PVRSRV_OK;
}
