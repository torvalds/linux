/*************************************************************************/ /*!
@Title          MMU Management
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements basic low level control of MMU.
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

#include "sgxdefs.h"
#include "sgxmmu.h"
#include "services_headers.h"
#include "buffer_manager.h"
#include "hash.h"
#include "ra.h"
#include "pdump_km.h"
#include "sgxapi_km.h"
#include "sgxinfo.h"
#include "sgxinfokm.h"
#include "mmu.h"
#include "sgxconfig.h"
#include "sgx_bridge_km.h"
#include "pdump_osfunc.h"

#define UINT32_MAX_VALUE	0xFFFFFFFFUL

/*
	MMU performs device virtual to physical translation.
	terminology:
	page directory (PD)
	pagetable (PT)
	data page (DP)

	Incoming 32bit Device Virtual Addresses are deconstructed into 3 fields:
	---------------------------------------------------------
	|	PD Index/tag:	|	PT Index:	|	DP offset:		|
	|	bits 31:22		|	bits 21:n	|	bits (n-1):0	|
	---------------------------------------------------------
		where typically n=12 for a standard 4k DP
		but n=16 for a 64k DP

	MMU page directory (PD), pagetable (PT) and data page (DP) config:
	PD:
	- always one page per address space
	- up to 4k in size to span 4Gb (32bit)
	- contains up to 1024 32bit entries
	- entries are indexed by the top 12 bits of an incoming 32bit device virtual address
	- the PD entry selected contains the physical address of the PT to
	  perform the next stage of the V to P translation

	PT:
	- size depends on the DP size, e.g. 4k DPs have 4k PTs but 16k DPs have 1k PTs
	- each PT always spans 4Mb of device virtual address space irrespective of DP size
	- number of entries in a PT depend on DP size and ranges from 1024 to 4 entries
	- entries are indexed by the PT Index field of the device virtual address (21:n)
	- the PT entry selected contains the physical address of the DP to access

	DP:
	- size varies from 4k to 4M in multiple of 4 steppings
	- DP offset field of the device virtual address ((n-1):0) is used as a byte offset
	  to address into the DP itself
*/

#define SGX_MAX_PD_ENTRIES	(1<<(SGX_FEATURE_ADDRESS_SPACE_SIZE - SGX_MMU_PT_SHIFT - SGX_MMU_PAGE_SHIFT))

#if defined(FIX_HW_BRN_31620)
/* Sim doesn't use the address mask */
#define SGX_MMU_PDE_DUMMY_PAGE		(0)//(0x00000020U)
#define SGX_MMU_PTE_DUMMY_PAGE		(0)//(0x00000020U)

/* 4MB adress range per page table */
#define BRN31620_PT_ADDRESS_RANGE_SHIFT		22
#define BRN31620_PT_ADDRESS_RANGE_SIZE		(1 << BRN31620_PT_ADDRESS_RANGE_SHIFT)

/* 64MB address range per PDE cache line */
#define BRN31620_PDE_CACHE_FILL_SHIFT		26
#define BRN31620_PDE_CACHE_FILL_SIZE		(1 << BRN31620_PDE_CACHE_FILL_SHIFT)
#define BRN31620_PDE_CACHE_FILL_MASK		(BRN31620_PDE_CACHE_FILL_SIZE - 1)

/* Page Directory Enteries per cache line */
#define BRN31620_PDES_PER_CACHE_LINE_SHIFT	(BRN31620_PDE_CACHE_FILL_SHIFT - BRN31620_PT_ADDRESS_RANGE_SHIFT)
#define BRN31620_PDES_PER_CACHE_LINE_SIZE	(1 << BRN31620_PDES_PER_CACHE_LINE_SHIFT)
#define BRN31620_PDES_PER_CACHE_LINE_MASK	(BRN31620_PDES_PER_CACHE_LINE_SIZE - 1)

/* Macros for working out offset for dummy pages */
#define BRN31620_DUMMY_PAGE_OFFSET	(1 * SGX_MMU_PAGE_SIZE)
#define BRN31620_DUMMY_PDE_INDEX	(BRN31620_DUMMY_PAGE_OFFSET / BRN31620_PT_ADDRESS_RANGE_SIZE)
#define BRN31620_DUMMY_PTE_INDEX	((BRN31620_DUMMY_PAGE_OFFSET - (BRN31620_DUMMY_PDE_INDEX * BRN31620_PT_ADDRESS_RANGE_SIZE))/SGX_MMU_PAGE_SIZE)

/* Cache number of cache lines */
#define BRN31620_CACHE_FLUSH_SHIFT		(32 - BRN31620_PDE_CACHE_FILL_SHIFT)
#define BRN31620_CACHE_FLUSH_SIZE		(1 << BRN31620_CACHE_FLUSH_SHIFT)

/* Cache line bits in a UINT32 */
#define BRN31620_CACHE_FLUSH_BITS_SHIFT		5
#define BRN31620_CACHE_FLUSH_BITS_SIZE		(1 << BRN31620_CACHE_FLUSH_BITS_SHIFT)
#define BRN31620_CACHE_FLUSH_BITS_MASK		(BRN31620_CACHE_FLUSH_BITS_SIZE - 1)

/* Cache line index in array */
#define BRN31620_CACHE_FLUSH_INDEX_BITS		(BRN31620_CACHE_FLUSH_SHIFT - BRN31620_CACHE_FLUSH_BITS_SHIFT)
#define BRN31620_CACHE_FLUSH_INDEX_SIZE		(1 << BRN31620_CACHE_FLUSH_INDEX_BITS)

#define BRN31620_DUMMY_PAGE_SIGNATURE	0xFEEBEE01
#endif

typedef struct _MMU_PT_INFO_
{
	/* note: may need a union here to accommodate a PT page address for local memory */
	IMG_VOID *hPTPageOSMemHandle;
	IMG_CPU_VIRTADDR PTPageCpuVAddr;
	/* Map of reserved PTEs.
	 * Reserved PTEs are like "valid" PTEs in that they (and the DevVAddrs they represent)
	 * cannot be assigned to another allocation but their "reserved" status persists through
	 * any amount of mapping and unmapping, until the allocation is finally destroyed.
	 *
	 * Reserved and Valid are independent.
	 * When a PTE is first reserved, it will have Reserved=1 and Valid=0.
	 * When the PTE is actually mapped, it will have Reserved=1 and Valid=1.
	 * When the PTE is unmapped, it will have Reserved=1 and Valid=0.
	 * At this point, the PT will can not be destroyed because although there is
	 * not an active mapping on the PT, it is known a PTE is reserved for use.
	 *
	 * The above sequence of mapping and unmapping may repeat any number of times
	 * until the allocation is unmapped and destroyed which causes the PTE to have
	 * Valid=0 and Reserved=0.
	 */
	/* Number of PTEs set up.
	 * i.e. have a valid SGX Phys Addr and the "VALID" PTE bit == 1
	 */
	IMG_UINT32 ui32ValidPTECount;
} MMU_PT_INFO;

#define MMU_CONTEXT_NAME_SIZE	50
struct _MMU_CONTEXT_
{
	/* the device node */
	PVRSRV_DEVICE_NODE *psDeviceNode;

	/* Page Directory CPUVirt and DevPhys Addresses */
	IMG_CPU_VIRTADDR pvPDCpuVAddr;
	IMG_DEV_PHYADDR sPDDevPAddr;

	IMG_VOID *hPDOSMemHandle;

	/* information about dynamically allocated pagetables */
	MMU_PT_INFO *apsPTInfoList[SGX_MAX_PD_ENTRIES];

	PVRSRV_SGXDEV_INFO *psDevInfo;

#if defined(PDUMP)
	IMG_UINT32 ui32PDumpMMUContextID;
#if defined(SUPPORT_PDUMP_MULTI_PROCESS)
	IMG_BOOL bPDumpActive;
#endif
#endif

	IMG_UINT32 ui32PID;
	IMG_CHAR szName[MMU_CONTEXT_NAME_SIZE];

#if defined (FIX_HW_BRN_31620)
	IMG_UINT32 ui32PDChangeMask[BRN31620_CACHE_FLUSH_INDEX_SIZE];
	IMG_UINT32 ui32PDCacheRangeRefCount[BRN31620_CACHE_FLUSH_SIZE];
	MMU_PT_INFO *apsPTInfoListSave[SGX_MAX_PD_ENTRIES];
#endif
	struct _MMU_CONTEXT_ *psNext;
};

struct _MMU_HEAP_
{
	/* MMU context */
	MMU_CONTEXT			*psMMUContext;

	/*
		heap specific details:
	*/
	/* the Base PD index for the heap */
	IMG_UINT32			ui32PDBaseIndex;
	/* number of pagetables in this heap */
	IMG_UINT32			ui32PageTableCount;
	/* total number of pagetable entries in this heap which may be mapped to data pages */
	IMG_UINT32			ui32PTETotalUsable;
	/* PD entry DP size control field */
	IMG_UINT32			ui32PDEPageSizeCtrl;

	/*
		Data Page (DP) Details:
	*/
	/* size in bytes of a data page */
	IMG_UINT32			ui32DataPageSize;
	/* bit width of the data page offset addressing field */
	IMG_UINT32			ui32DataPageBitWidth;
	/* bit mask of the data page offset addressing field */
	IMG_UINT32			ui32DataPageMask;

	/*
		PageTable (PT) Details:
	*/
	/* bit shift to base of PT addressing field */
	IMG_UINT32			ui32PTShift;
	/* bit width of the PT addressing field */
	IMG_UINT32			ui32PTBitWidth;
	/* bit mask of the PT addressing field */
	IMG_UINT32			ui32PTMask;
	/* size in bytes of a pagetable */
	IMG_UINT32			ui32PTSize;
	/* Allocated PT Entries per PT */
	IMG_UINT32			ui32PTNumEntriesAllocated;
	/* Usable PT Entries per PT (may be different to num allocated for 4MB data page) */
	IMG_UINT32			ui32PTNumEntriesUsable;

	/*
		PageDirectory Details:
	*/
	/* bit shift to base of PD addressing field */
	IMG_UINT32			ui32PDShift;
	/* bit width of the PD addressing field */
	IMG_UINT32			ui32PDBitWidth;
	/* bit mask of the PT addressing field */
	IMG_UINT32			ui32PDMask;

	/*
		Arena Info:
	*/
	RA_ARENA *psVMArena;
	DEV_ARENA_DESCRIPTOR *psDevArena;

	/* If we have sparse mappings then we can't do PT level sanity checks */
	IMG_BOOL bHasSparseMappings;
#if defined(PDUMP)
	PDUMP_MMU_ATTRIB sMMUAttrib;
#endif
};



#if defined (SUPPORT_SGX_MMU_DUMMY_PAGE)
#define DUMMY_DATA_PAGE_SIGNATURE	0xDEADBEEF
#endif

/* local prototypes: */
static IMG_VOID
_DeferredFreePageTable (MMU_HEAP *pMMUHeap, IMG_UINT32 ui32PTIndex, IMG_BOOL bOSFreePT);

#if defined(PDUMP)
static IMG_VOID
MMU_PDumpPageTables	(MMU_HEAP *pMMUHeap,
					 IMG_DEV_VIRTADDR DevVAddr,
					 IMG_SIZE_T uSize,
					 IMG_BOOL bForUnmap,
					 IMG_HANDLE hUniqueTag);
#endif /* #if defined(PDUMP) */

/* This option tests page table memory, for use during device bring-up. */
#define PAGE_TEST					0
#if PAGE_TEST
static IMG_VOID PageTest(IMG_VOID* pMem, IMG_DEV_PHYADDR sDevPAddr);
#endif

/* This option dumps out the PT if an assert fails */
#define PT_DUMP 1

/* This option sanity checks page table PTE valid count matches active PTEs */
#define PT_DEBUG 0
#if (PT_DEBUG || PT_DUMP) && defined(PVRSRV_NEED_PVR_DPF)
static IMG_VOID DumpPT(MMU_PT_INFO *psPTInfoList)
{
	IMG_UINT32 *p = (IMG_UINT32*)psPTInfoList->PTPageCpuVAddr;
	IMG_UINT32 i;

	/* 1024 entries in a 4K page table */
	for(i = 0; i < 1024; i += 8)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "%08X %08X %08X %08X %08X %08X %08X %08X\n",
				 p[i + 0], p[i + 1], p[i + 2], p[i + 3],
				 p[i + 4], p[i + 5], p[i + 6], p[i + 7]));
	}
}
#else /* (PT_DEBUG || PT_DUMP) && defined(PVRSRV_NEED_PVR_DPF) */
static INLINE IMG_VOID DumpPT(MMU_PT_INFO *psPTInfoList)
{
	PVR_UNREFERENCED_PARAMETER(psPTInfoList);
}
#endif /* (PT_DEBUG || PT_DUMP) && defined(PVRSRV_NEED_PVR_DPF) */

#if PT_DEBUG
static IMG_VOID CheckPT(MMU_PT_INFO *psPTInfoList)
{
	IMG_UINT32 *p = (IMG_UINT32*) psPTInfoList->PTPageCpuVAddr;
	IMG_UINT32 i, ui32Count = 0;

	/* 1024 entries in a 4K page table */
	for(i = 0; i < 1024; i++)
		if(p[i] & SGX_MMU_PTE_VALID)
			ui32Count++;

	if(psPTInfoList->ui32ValidPTECount != ui32Count)
	{
		PVR_DPF((PVR_DBG_ERROR, "ui32ValidPTECount: %u ui32Count: %u\n",
				 psPTInfoList->ui32ValidPTECount, ui32Count));
		DumpPT(psPTInfoList);
		BUG();
	}
}
#else /* PT_DEBUG */
static INLINE IMG_VOID CheckPT(MMU_PT_INFO *psPTInfoList)
{
	PVR_UNREFERENCED_PARAMETER(psPTInfoList);
}
#endif /* PT_DEBUG */

/*
	Debug functionality that allows us to make the CPU
	mapping of pagetable memory readonly and only make
	it read/write when we alter it. This allows us
	to check that our memory isn't being overwritten
*/
#if defined(PVRSRV_MMU_MAKE_READWRITE_ON_DEMAND)

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38))
#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif
#else
#include <generated/autoconf.h>
#endif

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/highmem.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>

static IMG_VOID MakeKernelPageReadWrite(IMG_PVOID ulCPUVAddr)
{
    pgd_t *psPGD;
    pud_t *psPUD;
    pmd_t *psPMD;
    pte_t *psPTE;
    pte_t ptent;
    IMG_UINT32 ui32CPUVAddr = (IMG_UINT32) ulCPUVAddr;

    psPGD = pgd_offset_k(ui32CPUVAddr);
    if (pgd_none(*psPGD) || pgd_bad(*psPGD))
    {
        PVR_ASSERT(0);
    }

    psPUD = pud_offset(psPGD, ui32CPUVAddr);
    if (pud_none(*psPUD) || pud_bad(*psPUD))
    {
        PVR_ASSERT(0);
    }

    psPMD = pmd_offset(psPUD, ui32CPUVAddr);
    if (pmd_none(*psPMD) || pmd_bad(*psPMD))
    {
        PVR_ASSERT(0);
    }
	psPTE = (pte_t *)pte_offset_kernel(psPMD, ui32CPUVAddr);

	ptent = ptep_modify_prot_start(&init_mm, ui32CPUVAddr, psPTE);
	ptent = pte_mkwrite(ptent);
	ptep_modify_prot_commit(&init_mm, ui32CPUVAddr, psPTE, ptent);

	flush_tlb_all();
}

static IMG_VOID MakeKernelPageReadOnly(IMG_PVOID ulCPUVAddr)
{
    pgd_t *psPGD;
    pud_t *psPUD;
    pmd_t *psPMD;
    pte_t *psPTE;
    pte_t ptent;
    IMG_UINT32 ui32CPUVAddr = (IMG_UINT32) ulCPUVAddr;

	OSWriteMemoryBarrier();

    psPGD = pgd_offset_k(ui32CPUVAddr);
    if (pgd_none(*psPGD) || pgd_bad(*psPGD))
    {
        PVR_ASSERT(0);
    }

    psPUD = pud_offset(psPGD, ui32CPUVAddr);
    if (pud_none(*psPUD) || pud_bad(*psPUD))
    {
        PVR_ASSERT(0);
    }

    psPMD = pmd_offset(psPUD, ui32CPUVAddr);
    if (pmd_none(*psPMD) || pmd_bad(*psPMD))
    {
        PVR_ASSERT(0);
    }

	psPTE = (pte_t *)pte_offset_kernel(psPMD, ui32CPUVAddr);

	ptent = ptep_modify_prot_start(&init_mm, ui32CPUVAddr, psPTE);
	ptent = pte_wrprotect(ptent);
	ptep_modify_prot_commit(&init_mm, ui32CPUVAddr, psPTE, ptent);

	flush_tlb_all();

}

#else /* defined(PVRSRV_MMU_MAKE_READWRITE_ON_DEMAND) */

static INLINE IMG_VOID MakeKernelPageReadWrite(IMG_PVOID ulCPUVAddr)
{
	PVR_UNREFERENCED_PARAMETER(ulCPUVAddr);
}

static INLINE IMG_VOID MakeKernelPageReadOnly(IMG_PVOID ulCPUVAddr)
{
	PVR_UNREFERENCED_PARAMETER(ulCPUVAddr);
}

#endif /* defined(PVRSRV_MMU_MAKE_READWRITE_ON_DEMAND) */

/*___________________________________________________________________________

	Information for SUPPORT_PDUMP_MULTI_PROCESS feature.
	
	The client marked for pdumping will set the bPDumpActive flag in
	the MMU Context (see MMU_Initialise).
	
	Shared heap allocations should be persistent so all apps which
	are pdumped will see the allocation. Persistent flag over-rides
	the bPDumpActive flag (see pdump_common.c/DbgWrite function).
	
	The idea is to dump PT,DP for shared heap allocations, but only
	dump the PDE if the allocation is mapped into the kernel or active
	client context. This ensures if a background app allocates on a
	shared heap then all clients can access it in the pdump toolchain.
	
	
	
	PD		PT		DP
	+-+
	| |--->	+-+
	+-+		| |--->	+-+
			+-+		+ +
					+-+
					
	PD allocation/free: pdump flags are 0 (only need PD for active apps)
	PT allocation/free: pdump flags are 0
						unless PT is for a shared heap, in which case persistent is set
	PD entries (MMU init/insert shared heap):
						only pdump if PDE is on the active MMU context, flags are 0
	PD entries (PT alloc):
						pdump flags are 0 if kernel heap
						pdump flags are 0 if shared heap and PDE is on active MMU context
						otherwise ignore.
	PT entries			pdump flags are 0
						unless PTE is for a shared heap, in which case persistent is set
						
	NOTE: PDump common code:-
	PDumpMallocPages and PDumpMemKM also set the persistent flag for
	shared heap allocations.
	
  ___________________________________________________________________________
*/


/*!
******************************************************************************
	FUNCTION:   MMU_IsHeapShared

	PURPOSE:    Is this heap shared?
	PARAMETERS: In: pMMU_Heap
	RETURNS:    true if heap is shared
******************************************************************************/
IMG_BOOL MMU_IsHeapShared(MMU_HEAP* pMMUHeap)
{
	switch(pMMUHeap->psDevArena->DevMemHeapType)
	{
		case DEVICE_MEMORY_HEAP_SHARED :
		case DEVICE_MEMORY_HEAP_SHARED_EXPORTED :
			return IMG_TRUE;
		case DEVICE_MEMORY_HEAP_PERCONTEXT :
		case DEVICE_MEMORY_HEAP_KERNEL :
			return IMG_FALSE;
		default:
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_IsHeapShared: ERROR invalid heap type"));
			return IMG_FALSE;
		}
	}
}

#ifdef SUPPORT_SGX_MMU_BYPASS
/*!
******************************************************************************
	FUNCTION:   EnableHostAccess

	PURPOSE:    Enables Host accesses to device memory, by passing the device
				MMU address translation

	PARAMETERS: In: psMMUContext
	RETURNS:    None
******************************************************************************/
IMG_VOID
EnableHostAccess (MMU_CONTEXT *psMMUContext)
{
	IMG_UINT32 ui32RegVal;
	IMG_VOID *pvRegsBaseKM = psMMUContext->psDevInfo->pvRegsBaseKM;

	/*
		bypass the MMU for the host port requestor,
		conserving bypass state of other requestors
	*/
	ui32RegVal = OSReadHWReg(pvRegsBaseKM, EUR_CR_BIF_CTRL);

	OSWriteHWReg(pvRegsBaseKM,
				EUR_CR_BIF_CTRL,
				ui32RegVal | EUR_CR_BIF_CTRL_MMU_BYPASS_HOST_MASK);
	/* assume we're not wiping-out any other bits */
	PDUMPREG(SGX_PDUMPREG_NAME, EUR_CR_BIF_CTRL, EUR_CR_BIF_CTRL_MMU_BYPASS_HOST_MASK);
}

/*!
******************************************************************************
	FUNCTION:   DisableHostAccess

	PURPOSE:    Disables Host accesses to device memory, by passing the device
				MMU address translation

	PARAMETERS: In: psMMUContext
	RETURNS:    None
******************************************************************************/
IMG_VOID
DisableHostAccess (MMU_CONTEXT *psMMUContext)
{
	IMG_UINT32 ui32RegVal;
	IMG_VOID *pvRegsBaseKM = psMMUContext->psDevInfo->pvRegsBaseKM;

	/*
		disable MMU-bypass for the host port requestor,
		conserving bypass state of other requestors
		and flushing all caches/tlbs
	*/
	OSWriteHWReg(pvRegsBaseKM,
				EUR_CR_BIF_CTRL,
				ui32RegVal & ~EUR_CR_BIF_CTRL_MMU_BYPASS_HOST_MASK);
	/* assume we're not wiping-out any other bits */
	PDUMPREG(SGX_PDUMPREG_NAME, EUR_CR_BIF_CTRL, 0);
}
#endif


#if defined(SGX_FEATURE_SYSTEM_CACHE)
/*!
******************************************************************************
	FUNCTION:   MMU_InvalidateSystemLevelCache

	PURPOSE:    Invalidates the System Level Cache to purge stale PDEs and PTEs

	PARAMETERS: In: psDevInfo
	RETURNS:    None

******************************************************************************/
static IMG_VOID MMU_InvalidateSystemLevelCache(PVRSRV_SGXDEV_INFO *psDevInfo)
{
	#if defined(SGX_FEATURE_MP)
	psDevInfo->ui32CacheControl |= SGXMKIF_CC_INVAL_BIF_SL;
	#else
	/* The MMU always bypasses the SLC */
	PVR_UNREFERENCED_PARAMETER(psDevInfo);
	#endif /* SGX_FEATURE_MP */
}
#endif /* SGX_FEATURE_SYSTEM_CACHE */

/*!
******************************************************************************
	FUNCTION:   MMU_InvalidateDirectoryCache

	PURPOSE:    Invalidates the page directory cache + page table cache + requestor TLBs

	PARAMETERS: In: psDevInfo
	RETURNS:    None

******************************************************************************/
IMG_VOID MMU_InvalidateDirectoryCache(PVRSRV_SGXDEV_INFO *psDevInfo)
{
	psDevInfo->ui32CacheControl |= SGXMKIF_CC_INVAL_BIF_PD;
	#if defined(SGX_FEATURE_SYSTEM_CACHE)
	MMU_InvalidateSystemLevelCache(psDevInfo);
	#endif /* SGX_FEATURE_SYSTEM_CACHE */
}


/*!
******************************************************************************
	FUNCTION:   MMU_InvalidatePageTableCache

	PURPOSE:    Invalidates the page table cache + requestor TLBs

	PARAMETERS: In: psDevInfo
	RETURNS:    None

******************************************************************************/
static IMG_VOID MMU_InvalidatePageTableCache(PVRSRV_SGXDEV_INFO *psDevInfo)
{
	psDevInfo->ui32CacheControl |= SGXMKIF_CC_INVAL_BIF_PT;
	#if defined(SGX_FEATURE_SYSTEM_CACHE)
	MMU_InvalidateSystemLevelCache(psDevInfo);
	#endif /* SGX_FEATURE_SYSTEM_CACHE */
}

#if defined(FIX_HW_BRN_31620)
/*!
******************************************************************************
	FUNCTION:   BRN31620InvalidatePageTableEntry

	PURPOSE:    Frees page tables in PDE cache line chunks re-wiring the
	            dummy page when required

	PARAMETERS: In: psMMUContext, ui32PDIndex, ui32PTIndex
	RETURNS:    None

******************************************************************************/
static IMG_VOID BRN31620InvalidatePageTableEntry(MMU_CONTEXT *psMMUContext, IMG_UINT32 ui32PDIndex, IMG_UINT32 ui32PTIndex, IMG_UINT32 *pui32PTE)
{
	PVRSRV_SGXDEV_INFO *psDevInfo = psMMUContext->psDevInfo;

	/*
	 * Note: We can't tell at this stage if this PT will be freed before
	 * the end of the function so we always wire up the dummy page to
	 * to the PT.
	 */
	if (((ui32PDIndex % (BRN31620_PDE_CACHE_FILL_SIZE/BRN31620_PT_ADDRESS_RANGE_SIZE)) == BRN31620_DUMMY_PDE_INDEX)
		&& (ui32PTIndex == BRN31620_DUMMY_PTE_INDEX))
	{
		*pui32PTE = (psDevInfo->sBRN31620DummyPageDevPAddr.uiAddr>>SGX_MMU_PTE_ADDR_ALIGNSHIFT)
								| SGX_MMU_PTE_DUMMY_PAGE
								| SGX_MMU_PTE_READONLY
								| SGX_MMU_PTE_VALID;
	}
	else
	{
		*pui32PTE = 0;
	}
}

/*!
******************************************************************************
	FUNCTION:   BRN31620FreePageTable

	PURPOSE:    Frees page tables in PDE cache line chunks re-wiring the
	            dummy page when required

	PARAMETERS: In: psMMUContext, ui32PDIndex
	RETURNS:    IMG_TRUE if we freed any PT's

******************************************************************************/
static IMG_BOOL BRN31620FreePageTable(MMU_HEAP *psMMUHeap, IMG_UINT32 ui32PDIndex)
{
	MMU_CONTEXT *psMMUContext = psMMUHeap->psMMUContext;
	PVRSRV_SGXDEV_INFO *psDevInfo = psMMUContext->psDevInfo;
	IMG_UINT32 ui32PDCacheLine = ui32PDIndex >> BRN31620_PDES_PER_CACHE_LINE_SHIFT;
	IMG_UINT32 bFreePTs = IMG_FALSE;
	IMG_UINT32 *pui32Tmp;

	PVR_ASSERT(psMMUHeap != IMG_NULL);

	/* 
	 * Clear the PT info for this PD index so even if we don't
	 * free the memory here apsPTInfoList[PDIndex] will trigger
	 * an "allocation" in _DeferredAllocPagetables which
	 * bumps up the refcount.
	 */
	PVR_ASSERT(psMMUContext->apsPTInfoListSave[ui32PDIndex] == IMG_NULL);

	psMMUContext->apsPTInfoListSave[ui32PDIndex] = psMMUContext->apsPTInfoList[ui32PDIndex];
	psMMUContext->apsPTInfoList[ui32PDIndex] = IMG_NULL;

	/* Check if this was the last PT in the cache line */
	if (--psMMUContext->ui32PDCacheRangeRefCount[ui32PDCacheLine] == 0)
	{
		IMG_UINT32 i;
		IMG_UINT32 ui32PDIndexStart = ui32PDCacheLine * BRN31620_PDES_PER_CACHE_LINE_SIZE;
		IMG_UINT32 ui32PDIndexEnd = ui32PDIndexStart + BRN31620_PDES_PER_CACHE_LINE_SIZE;
		IMG_UINT32 ui32PDBitMaskIndex, ui32PDBitMaskShift;

		/* Free all PT's in cache line */
		for (i=ui32PDIndexStart;i<ui32PDIndexEnd;i++)
		{
			/* This PT is _really_ being freed now */
			psMMUContext->apsPTInfoList[i] = psMMUContext->apsPTInfoListSave[i];
			psMMUContext->apsPTInfoListSave[i] = IMG_NULL;
			_DeferredFreePageTable(psMMUHeap, i - psMMUHeap->ui32PDBaseIndex, IMG_TRUE);
		}

		ui32PDBitMaskIndex = ui32PDCacheLine >> BRN31620_CACHE_FLUSH_BITS_SHIFT;
		ui32PDBitMaskShift = ui32PDCacheLine & BRN31620_CACHE_FLUSH_BITS_MASK;

		/* Check if this is a shared heap */
		if (MMU_IsHeapShared(psMMUHeap))
		{
			/* Mark the remove of the Page Table from all memory contexts */
			MMU_CONTEXT *psMMUContextWalker = (MMU_CONTEXT*) psMMUHeap->psMMUContext->psDevInfo->pvMMUContextList;

			while(psMMUContextWalker)
			{
				psMMUContextWalker->ui32PDChangeMask[ui32PDBitMaskIndex] |= 1 << ui32PDBitMaskShift;

				/*
				 * We've just cleared a cache line's worth of PDE's so we need
				 * to wire up the dummy PT
				 */
				MakeKernelPageReadWrite(psMMUContextWalker->pvPDCpuVAddr);
				pui32Tmp = (IMG_UINT32 *) psMMUContextWalker->pvPDCpuVAddr;
				pui32Tmp[ui32PDIndexStart + BRN31620_DUMMY_PDE_INDEX] = (psDevInfo->sBRN31620DummyPTDevPAddr.uiAddr>>SGX_MMU_PDE_ADDR_ALIGNSHIFT)
												| SGX_MMU_PDE_PAGE_SIZE_4K
												| SGX_MMU_PDE_DUMMY_PAGE
												| SGX_MMU_PDE_VALID;
				MakeKernelPageReadOnly(psMMUContextWalker->pvPDCpuVAddr);

				PDUMPCOMMENT("BRN31620 Re-wire dummy PT due to releasing PT allocation block");
				PDUMPPDENTRIES(&psMMUHeap->sMMUAttrib, psMMUContextWalker->hPDOSMemHandle, (IMG_VOID*)&pui32Tmp[ui32PDIndexStart + BRN31620_DUMMY_PDE_INDEX], sizeof(IMG_UINT32), 0, IMG_FALSE, PDUMP_PT_UNIQUETAG, PDUMP_PT_UNIQUETAG);
				psMMUContextWalker = psMMUContextWalker->psNext;
			}
		}
		else
		{
			psMMUContext->ui32PDChangeMask[ui32PDBitMaskIndex] |= 1 << ui32PDBitMaskShift;

			/*
			 * We've just cleared a cache line's worth of PDE's so we need
			 * to wire up the dummy PT
			 */
			MakeKernelPageReadWrite(psMMUContext->pvPDCpuVAddr);
			pui32Tmp = (IMG_UINT32 *) psMMUContext->pvPDCpuVAddr;
			pui32Tmp[ui32PDIndexStart + BRN31620_DUMMY_PDE_INDEX] = (psDevInfo->sBRN31620DummyPTDevPAddr.uiAddr>>SGX_MMU_PDE_ADDR_ALIGNSHIFT)
											| SGX_MMU_PDE_PAGE_SIZE_4K
											| SGX_MMU_PDE_DUMMY_PAGE
											| SGX_MMU_PDE_VALID;
			MakeKernelPageReadOnly(psMMUContext->pvPDCpuVAddr);

			PDUMPCOMMENT("BRN31620 Re-wire dummy PT due to releasing PT allocation block");
			PDUMPPDENTRIES(&psMMUHeap->sMMUAttrib, psMMUContext->hPDOSMemHandle, (IMG_VOID*)&pui32Tmp[ui32PDIndexStart + BRN31620_DUMMY_PDE_INDEX], sizeof(IMG_UINT32), 0, IMG_FALSE, PDUMP_PT_UNIQUETAG, PDUMP_PT_UNIQUETAG);
		}
		/* We've freed a cachline's worth of PDE's so trigger a PD cache flush */
		bFreePTs = IMG_TRUE;
	}

	return bFreePTs;
}
#endif

/*!
******************************************************************************
	FUNCTION:   _AllocPageTableMemory

	PURPOSE:    Allocate physical memory for a page table

	PARAMETERS: In: pMMUHeap - the mmu
				In: psPTInfoList - PT info
				Out: psDevPAddr - device physical address for new PT
	RETURNS:    IMG_TRUE - Success
	            IMG_FALSE - Failed
******************************************************************************/
static IMG_BOOL
_AllocPageTableMemory (MMU_HEAP *pMMUHeap,
						MMU_PT_INFO *psPTInfoList,
						IMG_DEV_PHYADDR	*psDevPAddr)
{
	IMG_DEV_PHYADDR	sDevPAddr;
	IMG_CPU_PHYADDR sCpuPAddr;

	/*
		depending on the specific system, pagetables are allocated from system memory
		or device local memory.  For now, just look for at least a valid local heap/arena
	*/
	if(pMMUHeap->psDevArena->psDeviceMemoryHeapInfo->psLocalDevMemArena == IMG_NULL)
	{
		//FIXME: replace with an RA, this allocator only handles 4k allocs
		if (OSAllocPages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
						 pMMUHeap->ui32PTSize,
						 SGX_MMU_PAGE_SIZE,//FIXME: assume 4K page size for now (wastes memory for smaller pagetables
						 IMG_NULL,
						 0,
						 IMG_NULL,
						 (IMG_VOID **)&psPTInfoList->PTPageCpuVAddr,
						 &psPTInfoList->hPTPageOSMemHandle) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "_AllocPageTableMemory: ERROR call to OSAllocPages failed"));
			return IMG_FALSE;
		}

		/*
			Force the page to read only, we will make it read/write as
			and when we need to
		*/
		MakeKernelPageReadOnly(psPTInfoList->PTPageCpuVAddr);

		/* translate address to device physical */
		if(psPTInfoList->PTPageCpuVAddr)
		{
			sCpuPAddr = OSMapLinToCPUPhys(psPTInfoList->hPTPageOSMemHandle,
										  psPTInfoList->PTPageCpuVAddr);
		}
		else
		{
			/* This isn't used in all cases since not all ports currently support
			 * OSMemHandleToCpuPAddr() */
			sCpuPAddr = OSMemHandleToCpuPAddr(psPTInfoList->hPTPageOSMemHandle, 0);
		}

		sDevPAddr = SysCpuPAddrToDevPAddr (PVRSRV_DEVICE_TYPE_SGX, sCpuPAddr);
	}
	else
	{
		/* 
		   We cannot use IMG_SYS_PHYADDR here, as that is 64-bit for 32-bit PAE builds.
		   The physical address in this call to RA_Alloc is specifically the SysPAddr 
		   of local (card) space, and it is highly unlikely we would ever need to 
		   support > 4GB of local (card) memory (this does assume that such local
		   memory will be mapped into System physical memory space at a low address so
		   that any and all local memory exists within the 4GB SYSPAddr range).
		 */
		IMG_UINTPTR_T uiLocalPAddr;
		IMG_SYS_PHYADDR sSysPAddr;

		/*
			just allocate from the first local memory arena
			(unlikely to be more than one local mem area(?))
		*/
		//FIXME: just allocate a 4K page for each PT for now
		if(RA_Alloc(pMMUHeap->psDevArena->psDeviceMemoryHeapInfo->psLocalDevMemArena,
					SGX_MMU_PAGE_SIZE,//pMMUHeap->ui32PTSize,
					IMG_NULL,
					IMG_NULL,
					0,
					SGX_MMU_PAGE_SIZE,//pMMUHeap->ui32PTSize,
					0,
					IMG_NULL,
					0,
					&uiLocalPAddr)!= IMG_TRUE)
		{
			PVR_DPF((PVR_DBG_ERROR, "_AllocPageTableMemory: ERROR call to RA_Alloc failed"));
			return IMG_FALSE;
		}

		/* Munge the local PAddr back into the SysPAddr */
		sSysPAddr.uiAddr = uiLocalPAddr;

		/* derive the CPU virtual address */
		sCpuPAddr = SysSysPAddrToCpuPAddr(sSysPAddr);
		/* note: actual ammount is pMMUHeap->ui32PTSize but must be a multiple of 4k pages */
		psPTInfoList->PTPageCpuVAddr = OSMapPhysToLin(sCpuPAddr,
													SGX_MMU_PAGE_SIZE,
													PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
													&psPTInfoList->hPTPageOSMemHandle);
		if(!psPTInfoList->PTPageCpuVAddr)
		{
			PVR_DPF((PVR_DBG_ERROR, "_AllocPageTableMemory: ERROR failed to map page tables"));
			return IMG_FALSE;
		}

		/* translate address to device physical */
		sDevPAddr = SysCpuPAddrToDevPAddr (PVRSRV_DEVICE_TYPE_SGX, sCpuPAddr);

		#if PAGE_TEST
		PageTest(psPTInfoList->PTPageCpuVAddr, sDevPAddr);
		#endif
	}

	MakeKernelPageReadWrite(psPTInfoList->PTPageCpuVAddr);
#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
	{
		IMG_UINT32 *pui32Tmp;
		IMG_UINT32 i;

		pui32Tmp = (IMG_UINT32*)psPTInfoList->PTPageCpuVAddr;
		/* point the new PT entries to the dummy data page */
		for(i=0; i<pMMUHeap->ui32PTNumEntriesUsable; i++)
		{
			pui32Tmp[i] = (pMMUHeap->psMMUContext->psDevInfo->sDummyDataDevPAddr.uiAddr>>SGX_MMU_PTE_ADDR_ALIGNSHIFT)
						| SGX_MMU_PTE_VALID;
		}
		/* zero the remaining allocated entries, if any */
		for(; i<pMMUHeap->ui32PTNumEntriesAllocated; i++)
		{
			pui32Tmp[i] = 0;
		}
	}
#else
	/* Zero the page table. */
	OSMemSet(psPTInfoList->PTPageCpuVAddr, 0, pMMUHeap->ui32PTSize);
#endif
	MakeKernelPageReadOnly(psPTInfoList->PTPageCpuVAddr);

#if defined(PDUMP)
	{
		IMG_UINT32 ui32Flags = 0;
#if defined(SUPPORT_PDUMP_MULTI_PROCESS)
		/* make sure shared heap PT allocs are always pdumped */
		ui32Flags |= ( MMU_IsHeapShared(pMMUHeap) ) ? PDUMP_FLAGS_PERSISTENT : 0;
#endif
		/* pdump the PT malloc */
		PDUMPMALLOCPAGETABLE(&pMMUHeap->psMMUContext->psDeviceNode->sDevId, psPTInfoList->hPTPageOSMemHandle, 0, psPTInfoList->PTPageCpuVAddr, pMMUHeap->ui32PTSize, ui32Flags, PDUMP_PT_UNIQUETAG);
		/* pdump the PT Pages */
		PDUMPMEMPTENTRIES(&pMMUHeap->sMMUAttrib, psPTInfoList->hPTPageOSMemHandle, psPTInfoList->PTPageCpuVAddr, pMMUHeap->ui32PTSize, ui32Flags, IMG_TRUE, PDUMP_PT_UNIQUETAG, PDUMP_PT_UNIQUETAG);
	}
#endif

	/* return the DevPAddr */
	*psDevPAddr = sDevPAddr;

	return IMG_TRUE;
}


/*!
******************************************************************************
	FUNCTION:   _FreePageTableMemory

	PURPOSE:    Free physical memory for a page table

	PARAMETERS: In: pMMUHeap - the mmu
				In: psPTInfoList - PT info to free
	RETURNS:    NONE
******************************************************************************/
static IMG_VOID
_FreePageTableMemory (MMU_HEAP *pMMUHeap, MMU_PT_INFO *psPTInfoList)
{
	/*
		free the PT page:
		depending on the specific system, pagetables are allocated from system memory
		or device local memory.  For now, just look for at least a valid local heap/arena
	*/
	if(pMMUHeap->psDevArena->psDeviceMemoryHeapInfo->psLocalDevMemArena == IMG_NULL)
	{
		/* Force the page to read write before we free it*/
		MakeKernelPageReadWrite(psPTInfoList->PTPageCpuVAddr);

		//FIXME: replace with an RA, this allocator only handles 4k allocs
		OSFreePages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
					  pMMUHeap->ui32PTSize,
					  psPTInfoList->PTPageCpuVAddr,
					  psPTInfoList->hPTPageOSMemHandle);
	}
	else
	{
		IMG_SYS_PHYADDR sSysPAddr;
		IMG_CPU_PHYADDR sCpuPAddr;

		/*  derive the system physical address */
		sCpuPAddr = OSMapLinToCPUPhys(psPTInfoList->hPTPageOSMemHandle, 
									  psPTInfoList->PTPageCpuVAddr);
		sSysPAddr = SysCpuPAddrToSysPAddr (sCpuPAddr);

		/* unmap the CPU mapping */
		/* note: actual ammount is pMMUHeap->ui32PTSize but must be a multiple of 4k pages */
		OSUnMapPhysToLin(psPTInfoList->PTPageCpuVAddr,
                         SGX_MMU_PAGE_SIZE,
                         PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
                         psPTInfoList->hPTPageOSMemHandle);

		/*
			just free from the first local memory arena
			(unlikely to be more than one local mem area(?))
			Note that the cast to IMG_UINTPTR_T is ok as we're local mem.
		*/
		RA_Free (pMMUHeap->psDevArena->psDeviceMemoryHeapInfo->psLocalDevMemArena, (IMG_UINTPTR_T)sSysPAddr.uiAddr, IMG_FALSE);
	}
}



/*!
******************************************************************************
	FUNCTION:   _DeferredFreePageTable

	PURPOSE:    Free one page table associated with an MMU.

	PARAMETERS: In:  pMMUHeap - the mmu heap
				In:  ui32PTIndex - index of the page table to free relative
								   to the base of heap.
	RETURNS:    None
******************************************************************************/
static IMG_VOID
_DeferredFreePageTable (MMU_HEAP *pMMUHeap, IMG_UINT32 ui32PTIndex, IMG_BOOL bOSFreePT)
{
	IMG_UINT32 *pui32PDEntry;
	IMG_UINT32 i;
	IMG_UINT32 ui32PDIndex;
	SYS_DATA *psSysData;
	MMU_PT_INFO **ppsPTInfoList;

	SysAcquireData(&psSysData);

	/* find the index/offset in PD entries  */
	ui32PDIndex = pMMUHeap->psDevArena->BaseDevVAddr.uiAddr >> pMMUHeap->ui32PDShift;

	/* set the base PT info */
	ppsPTInfoList = &pMMUHeap->psMMUContext->apsPTInfoList[ui32PDIndex];

	{
#if PT_DEBUG
		if(ppsPTInfoList[ui32PTIndex] && ppsPTInfoList[ui32PTIndex]->ui32ValidPTECount > 0)
		{
			DumpPT(ppsPTInfoList[ui32PTIndex]);
			/* Fall-through, will fail assert */
		}
#endif

		/* Assert that all mappings have gone */
		PVR_ASSERT(ppsPTInfoList[ui32PTIndex] == IMG_NULL || ppsPTInfoList[ui32PTIndex]->ui32ValidPTECount == 0);
	}

#if defined(PDUMP)
	{
		IMG_UINT32 ui32Flags = 0;
#if defined(SUPPORT_PDUMP_MULTI_PROCESS)
		ui32Flags |= ( MMU_IsHeapShared(pMMUHeap) ) ? PDUMP_FLAGS_PERSISTENT : 0;
#endif
		/* pdump the PT free */
		PDUMPCOMMENT("Free page table (page count == %08X)", pMMUHeap->ui32PageTableCount);
		if(ppsPTInfoList[ui32PTIndex] && ppsPTInfoList[ui32PTIndex]->PTPageCpuVAddr)
		{
			PDUMPFREEPAGETABLE(&pMMUHeap->psMMUContext->psDeviceNode->sDevId, ppsPTInfoList[ui32PTIndex]->hPTPageOSMemHandle, ppsPTInfoList[ui32PTIndex]->PTPageCpuVAddr, pMMUHeap->ui32PTSize, ui32Flags, PDUMP_PT_UNIQUETAG);
		}
	}
#endif

	switch(pMMUHeap->psDevArena->DevMemHeapType)
	{
		case DEVICE_MEMORY_HEAP_SHARED :
		case DEVICE_MEMORY_HEAP_SHARED_EXPORTED :
		{
			/* Remove Page Table from all memory contexts */
			MMU_CONTEXT *psMMUContext = (MMU_CONTEXT*)pMMUHeap->psMMUContext->psDevInfo->pvMMUContextList;

			while(psMMUContext)
			{
				/* get the PD CPUVAddr base and advance to the first entry */
				MakeKernelPageReadWrite(psMMUContext->pvPDCpuVAddr);
				pui32PDEntry = (IMG_UINT32*)psMMUContext->pvPDCpuVAddr;
				pui32PDEntry += ui32PDIndex;

#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
				/* point the PD entry to the dummy PT */
				pui32PDEntry[ui32PTIndex] = (psMMUContext->psDevInfo->sDummyPTDevPAddr.uiAddr
											>>SGX_MMU_PDE_ADDR_ALIGNSHIFT)
											| SGX_MMU_PDE_PAGE_SIZE_4K
											| SGX_MMU_PDE_VALID;
#else
				/* free the entry */
				if(bOSFreePT)
				{
					pui32PDEntry[ui32PTIndex] = 0;
				}
#endif
				MakeKernelPageReadOnly(psMMUContext->pvPDCpuVAddr);
			#if defined(PDUMP)
				/* pdump the PD Page modifications */
			#if defined(SUPPORT_PDUMP_MULTI_PROCESS)
				if(psMMUContext->bPDumpActive)
			#endif
				{
					PDUMPPDENTRIES(&pMMUHeap->sMMUAttrib, psMMUContext->hPDOSMemHandle, (IMG_VOID*)&pui32PDEntry[ui32PTIndex], sizeof(IMG_UINT32), 0, IMG_FALSE, PDUMP_PT_UNIQUETAG, PDUMP_PT_UNIQUETAG);
				}
			#endif
				/* advance to next context */
				psMMUContext = psMMUContext->psNext;
			}
			break;
		}
		case DEVICE_MEMORY_HEAP_PERCONTEXT :
		case DEVICE_MEMORY_HEAP_KERNEL :
		{
			MakeKernelPageReadWrite(pMMUHeap->psMMUContext->pvPDCpuVAddr);
			/* Remove Page Table from this memory context only */
			pui32PDEntry = (IMG_UINT32*)pMMUHeap->psMMUContext->pvPDCpuVAddr;
			pui32PDEntry += ui32PDIndex;

#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
			/* point the PD entry to the dummy PT */
			pui32PDEntry[ui32PTIndex] = (pMMUHeap->psMMUContext->psDevInfo->sDummyPTDevPAddr.uiAddr
										>>SGX_MMU_PDE_ADDR_ALIGNSHIFT)
										| SGX_MMU_PDE_PAGE_SIZE_4K
										| SGX_MMU_PDE_VALID;
#else
			/* free the entry */
			if(bOSFreePT)
			{
				pui32PDEntry[ui32PTIndex] = 0;
			}
#endif
			MakeKernelPageReadOnly(pMMUHeap->psMMUContext->pvPDCpuVAddr);

			/* pdump the PD Page modifications */
			PDUMPPDENTRIES(&pMMUHeap->sMMUAttrib, pMMUHeap->psMMUContext->hPDOSMemHandle, (IMG_VOID*)&pui32PDEntry[ui32PTIndex], sizeof(IMG_UINT32), 0, IMG_FALSE, PDUMP_PD_UNIQUETAG, PDUMP_PT_UNIQUETAG);
			break;
		}
		default:
		{
			PVR_DPF((PVR_DBG_ERROR, "_DeferredFreePagetable: ERROR invalid heap type"));
			return;
		}
	}

	/* clear the PT entries in each PT page */
	if(ppsPTInfoList[ui32PTIndex] != IMG_NULL)
	{
		if(ppsPTInfoList[ui32PTIndex]->PTPageCpuVAddr != IMG_NULL)
		{
			IMG_PUINT32 pui32Tmp;

			MakeKernelPageReadWrite(ppsPTInfoList[ui32PTIndex]->PTPageCpuVAddr);
			pui32Tmp = (IMG_UINT32*)ppsPTInfoList[ui32PTIndex]->PTPageCpuVAddr;

			/* clear the entries */
			for(i=0;
				(i<pMMUHeap->ui32PTETotalUsable) && (i<pMMUHeap->ui32PTNumEntriesUsable);
				 i++)
			{
				/* over-allocated PT entries for 4MB data page case should never be non-zero */
				pui32Tmp[i] = 0;
			}
			MakeKernelPageReadOnly(ppsPTInfoList[ui32PTIndex]->PTPageCpuVAddr);

			/*
				free the pagetable memory
			*/
			if(bOSFreePT)
			{
				_FreePageTableMemory(pMMUHeap, ppsPTInfoList[ui32PTIndex]);
			}

			/*
				decrement the PT Entry Count by the number
				of entries we've cleared in this pass
			*/
			pMMUHeap->ui32PTETotalUsable -= i;
		}
		else
		{
			/* decrement the PT Entry Count by a page's worth of entries  */
			pMMUHeap->ui32PTETotalUsable -= pMMUHeap->ui32PTNumEntriesUsable;
		}

		if(bOSFreePT)
		{
			/* free the pt info */
			OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
						sizeof(MMU_PT_INFO),
						ppsPTInfoList[ui32PTIndex],
						IMG_NULL);
			ppsPTInfoList[ui32PTIndex] = IMG_NULL;
		}
	}
	else
	{
		/* decrement the PT Entry Count by a page's worth of usable entries */
		pMMUHeap->ui32PTETotalUsable -= pMMUHeap->ui32PTNumEntriesUsable;
	}

	PDUMPCOMMENT("Finished free page table (page count == %08X)", pMMUHeap->ui32PageTableCount);
}

/*!
******************************************************************************
	FUNCTION:   _DeferredFreePageTables

	PURPOSE:    Free the page tables associated with an MMU.

	PARAMETERS: In:  pMMUHeap - the mmu
	RETURNS:    None
******************************************************************************/
static IMG_VOID
_DeferredFreePageTables (MMU_HEAP *pMMUHeap)
{
	IMG_UINT32 i;
#if defined(FIX_HW_BRN_31620)
	MMU_CONTEXT *psMMUContext = pMMUHeap->psMMUContext;
	IMG_BOOL bInvalidateDirectoryCache = IMG_FALSE;
	IMG_UINT32 ui32PDIndex;
	IMG_UINT32 *pui32Tmp;
	IMG_UINT32 j;
#endif
#if defined(PDUMP)
	PDUMPCOMMENT("Free PTs (MMU Context ID == %u, PDBaseIndex == %u, PT count == 0x%x)",
			pMMUHeap->psMMUContext->ui32PDumpMMUContextID,
			pMMUHeap->ui32PDBaseIndex,
			pMMUHeap->ui32PageTableCount);
#endif
#if defined(FIX_HW_BRN_31620)
	for(i=0; i<pMMUHeap->ui32PageTableCount; i++)
	{
		ui32PDIndex = (pMMUHeap->ui32PDBaseIndex + i);

		if (psMMUContext->apsPTInfoList[ui32PDIndex])
		{
			if (psMMUContext->apsPTInfoList[ui32PDIndex]->PTPageCpuVAddr)
			{
				/*
				 * We have to do this to setup the dummy page as
				 * not all heaps are PD cache size or aligned
				 */
				for (j=0;j<SGX_MMU_PT_SIZE;j++)
				{
					pui32Tmp = (IMG_UINT32 *) psMMUContext->apsPTInfoList[ui32PDIndex]->PTPageCpuVAddr;
					BRN31620InvalidatePageTableEntry(psMMUContext, ui32PDIndex, j, &pui32Tmp[j]);
				}
			}
			/* Free the PT and NULL's out the PTInfo */
			if (BRN31620FreePageTable(pMMUHeap, ui32PDIndex) == IMG_TRUE)
			{
				bInvalidateDirectoryCache = IMG_TRUE;
			}
		}
	}

	/*
	 * Due to freeing PT's in chunks we might need to flush the PT cache
	 * rather then the directory cache
	 */
	if (bInvalidateDirectoryCache)
	{
		MMU_InvalidateDirectoryCache(pMMUHeap->psMMUContext->psDevInfo);
	}
	else
	{
		MMU_InvalidatePageTableCache(pMMUHeap->psMMUContext->psDevInfo);
	}
#else
	for(i=0; i<pMMUHeap->ui32PageTableCount; i++)
	{
		_DeferredFreePageTable(pMMUHeap, i, IMG_TRUE);
	}
	MMU_InvalidateDirectoryCache(pMMUHeap->psMMUContext->psDevInfo);
#endif
}


/*!
******************************************************************************
	FUNCTION:   _DeferredAllocPagetables

	PURPOSE:    allocates page tables at time of allocation

	PARAMETERS: In:  pMMUHeap - the mmu heap
					 DevVAddr - devVAddr of allocation
					 ui32Size - size of allocation
	RETURNS:    IMG_TRUE - Success
	            IMG_FALSE - Failed
******************************************************************************/
static IMG_BOOL
_DeferredAllocPagetables(MMU_HEAP *pMMUHeap, IMG_DEV_VIRTADDR DevVAddr, IMG_UINT32 ui32Size)
{
	IMG_UINT32 ui32PageTableCount;
	IMG_UINT32 ui32PDIndex;
	IMG_UINT32 i;
	IMG_UINT32 *pui32PDEntry;
	MMU_PT_INFO **ppsPTInfoList;
	SYS_DATA *psSysData;
	IMG_DEV_VIRTADDR sHighDevVAddr;
#if defined(FIX_HW_BRN_31620)
	IMG_BOOL bFlushSystemCache = IMG_FALSE;
	IMG_BOOL bSharedPT = IMG_FALSE;
	IMG_DEV_VIRTADDR sDevVAddrRequestStart;
	IMG_DEV_VIRTADDR sDevVAddrRequestEnd;
	IMG_UINT32 ui32PDRequestStart;
	IMG_UINT32 ui32PDRequestEnd;
	IMG_UINT32 ui32ModifiedCachelines[BRN31620_CACHE_FLUSH_INDEX_SIZE];
#endif

	/* Check device linear address */
#if SGX_FEATURE_ADDRESS_SPACE_SIZE < 32
	PVR_ASSERT(DevVAddr.uiAddr < (1<<SGX_FEATURE_ADDRESS_SPACE_SIZE));
#endif

	/* get the sysdata */
	SysAcquireData(&psSysData);

	/* find the index/offset in PD entries  */
	ui32PDIndex = DevVAddr.uiAddr >> pMMUHeap->ui32PDShift;

	/* how many PDs does the allocation occupy? */
	/* first check for overflows */
	if((UINT32_MAX_VALUE - DevVAddr.uiAddr)
		< (ui32Size + pMMUHeap->ui32DataPageMask + pMMUHeap->ui32PTMask))
	{
		/* detected overflow, clamp to highest address, reserve all PDs */
		sHighDevVAddr.uiAddr = UINT32_MAX_VALUE;
		ui32PageTableCount = 1024;
	}
	else
	{
		sHighDevVAddr.uiAddr = DevVAddr.uiAddr
								+ ui32Size
								+ pMMUHeap->ui32DataPageMask
								+ pMMUHeap->ui32PTMask;

		ui32PageTableCount = sHighDevVAddr.uiAddr >> pMMUHeap->ui32PDShift;
	}


	/* Fix allocation of last 4MB */
	if (ui32PageTableCount == 0)
		ui32PageTableCount = 1024;

#if defined(FIX_HW_BRN_31620)
	for (i=0;i<BRN31620_CACHE_FLUSH_INDEX_SIZE;i++)
	{
		ui32ModifiedCachelines[i] = 0;
	}

	/*****************************************************************/
	/* Save off requested data and round allocation to PD cache line */
	/*****************************************************************/
	sDevVAddrRequestStart = DevVAddr;
	ui32PDRequestStart = ui32PDIndex;
	sDevVAddrRequestEnd = sHighDevVAddr;
	ui32PDRequestEnd = ui32PageTableCount - 1;

	/* Round allocations down to the PD cacheline */
	DevVAddr.uiAddr = DevVAddr.uiAddr & (~BRN31620_PDE_CACHE_FILL_MASK);

	/* Round the end address of the PD allocation to cacheline */
	if (UINT32_MAX_VALUE - sHighDevVAddr.uiAddr < (BRN31620_PDE_CACHE_FILL_SIZE - 1))
	{
		sHighDevVAddr.uiAddr = UINT32_MAX_VALUE;
		ui32PageTableCount = 1024;
	}
	else
	{
		sHighDevVAddr.uiAddr = ((sHighDevVAddr.uiAddr + (BRN31620_PDE_CACHE_FILL_SIZE - 1)) & (~BRN31620_PDE_CACHE_FILL_MASK));
		ui32PageTableCount = sHighDevVAddr.uiAddr >> pMMUHeap->ui32PDShift;
	}

	ui32PDIndex = DevVAddr.uiAddr >> pMMUHeap->ui32PDShift;

	/* Fix allocation of last 4MB */
	if (ui32PageTableCount == 0)
		ui32PageTableCount = 1024;
#endif

	ui32PageTableCount -= ui32PDIndex;

	/* get the PD CPUVAddr base and advance to the first entry */
	pui32PDEntry = (IMG_UINT32*)pMMUHeap->psMMUContext->pvPDCpuVAddr;
	pui32PDEntry += ui32PDIndex;

	/* and advance to the first PT info list */
	ppsPTInfoList = &pMMUHeap->psMMUContext->apsPTInfoList[ui32PDIndex];

#if defined(PDUMP)
	{
		IMG_UINT32 ui32Flags = 0;
		
		/* pdump the PD Page modifications */
		if( MMU_IsHeapShared(pMMUHeap) )
		{
			ui32Flags |= PDUMP_FLAGS_CONTINUOUS;
		}
		PDUMPCOMMENTWITHFLAGS(ui32Flags, "Alloc PTs (MMU Context ID == %u, PDBaseIndex == %u, Size == 0x%x, Shared = %s)",
				pMMUHeap->psMMUContext->ui32PDumpMMUContextID,
				pMMUHeap->ui32PDBaseIndex,
				ui32Size,
				MMU_IsHeapShared(pMMUHeap)?"True":"False");
		PDUMPCOMMENTWITHFLAGS(ui32Flags, "Alloc page table (page count == %08X)", ui32PageTableCount);
		PDUMPCOMMENTWITHFLAGS(ui32Flags, "Page directory mods (page count == %08X)", ui32PageTableCount);
	}
#endif
	/* walk the psPTInfoList to see what needs allocating: */
	for(i=0; i<ui32PageTableCount; i++)
	{
		if(ppsPTInfoList[i] == IMG_NULL)
		{
#if defined(FIX_HW_BRN_31620)
			/* Check if we have a saved PT (i.e. this PDE cache line is still live) */
			if (pMMUHeap->psMMUContext->apsPTInfoListSave[ui32PDIndex + i])
			{
				/* Only make this PTInfo "live" if it's requested */
				if (((ui32PDIndex + i) >= ui32PDRequestStart) && ((ui32PDIndex + i) <= ui32PDRequestEnd))
				{
					IMG_UINT32 ui32PDCacheLine = (ui32PDIndex + i) >> BRN31620_PDES_PER_CACHE_LINE_SHIFT;

					ppsPTInfoList[i] = pMMUHeap->psMMUContext->apsPTInfoListSave[ui32PDIndex + i];
					pMMUHeap->psMMUContext->apsPTInfoListSave[ui32PDIndex + i] = IMG_NULL;

					pMMUHeap->psMMUContext->ui32PDCacheRangeRefCount[ui32PDCacheLine]++;
				}
			}
			else
			{
#endif
			OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
						 sizeof (MMU_PT_INFO),
						 (IMG_VOID **)&ppsPTInfoList[i], IMG_NULL,
						 "MMU Page Table Info");
			if (ppsPTInfoList[i] == IMG_NULL)
			{
				PVR_DPF((PVR_DBG_ERROR, "_DeferredAllocPagetables: ERROR call to OSAllocMem failed"));
				return IMG_FALSE;
			}
			OSMemSet (ppsPTInfoList[i], 0, sizeof(MMU_PT_INFO));
#if defined(FIX_HW_BRN_31620)
			}
#endif
		}
#if defined(FIX_HW_BRN_31620)
		/* Only try to allocate if ppsPTInfoList[i] is valid */
		if (ppsPTInfoList[i])
		{
#endif
		if(ppsPTInfoList[i]->hPTPageOSMemHandle == IMG_NULL
		&& ppsPTInfoList[i]->PTPageCpuVAddr == IMG_NULL)
		{
			IMG_DEV_PHYADDR	sDevPAddr = { 0 };
#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
			IMG_UINT32 *pui32Tmp;
			IMG_UINT32 j;
#else
#if !defined(FIX_HW_BRN_31620)
			/* no page table has been allocated so allocate one */
			PVR_ASSERT(pui32PDEntry[i] == 0);
#endif
#endif
			if(_AllocPageTableMemory (pMMUHeap, ppsPTInfoList[i], &sDevPAddr) != IMG_TRUE)
			{
				PVR_DPF((PVR_DBG_ERROR, "_DeferredAllocPagetables: ERROR call to _AllocPageTableMemory failed"));
				return IMG_FALSE;
			}
#if defined(FIX_HW_BRN_31620)
			bFlushSystemCache = IMG_TRUE;
			/* Bump up the page table count if required */
			{
				IMG_UINT32 ui32PD;
				IMG_UINT32 ui32PDCacheLine;
				IMG_UINT32 ui32PDBitMaskIndex;
				IMG_UINT32 ui32PDBitMaskShift;

				ui32PD = ui32PDIndex + i;
				ui32PDCacheLine = ui32PD >> BRN31620_PDES_PER_CACHE_LINE_SHIFT;
				ui32PDBitMaskIndex = ui32PDCacheLine >> BRN31620_CACHE_FLUSH_BITS_SHIFT;
				ui32PDBitMaskShift = ui32PDCacheLine & BRN31620_CACHE_FLUSH_BITS_MASK;
				ui32ModifiedCachelines[ui32PDBitMaskIndex] |= 1 << ui32PDBitMaskShift;

				/* Add 1 to ui32PD as we want the count, not a range */
				if ((pMMUHeap->ui32PDBaseIndex + pMMUHeap->ui32PageTableCount) < (ui32PD + 1))
				{
					pMMUHeap->ui32PageTableCount = (ui32PD + 1) - pMMUHeap->ui32PDBaseIndex;
				}

				if (((ui32PDIndex + i) >= ui32PDRequestStart) && ((ui32PDIndex + i) <= ui32PDRequestEnd))
				{
					pMMUHeap->psMMUContext->ui32PDCacheRangeRefCount[ui32PDCacheLine]++;
				}
			}
#endif
			switch(pMMUHeap->psDevArena->DevMemHeapType)
			{
				case DEVICE_MEMORY_HEAP_SHARED :
				case DEVICE_MEMORY_HEAP_SHARED_EXPORTED :
				{
					/* insert Page Table into all memory contexts */
					MMU_CONTEXT *psMMUContext = (MMU_CONTEXT*)pMMUHeap->psMMUContext->psDevInfo->pvMMUContextList;
#if defined(SUPPORT_PDUMP_MULTI_PROCESS)
					PVRSRV_SGXDEV_INFO *psDevInfo = psMMUContext->psDevInfo;
#endif
					while(psMMUContext)
					{
						MakeKernelPageReadWrite(psMMUContext->pvPDCpuVAddr);
						/* get the PD CPUVAddr base and advance to the first entry */
						pui32PDEntry = (IMG_UINT32*)psMMUContext->pvPDCpuVAddr;
						pui32PDEntry += ui32PDIndex;

						/* insert the page, specify the data page size and make the pde valid */
						pui32PDEntry[i] = (IMG_UINT32)(sDevPAddr.uiAddr>>SGX_MMU_PDE_ADDR_ALIGNSHIFT)
										| pMMUHeap->ui32PDEPageSizeCtrl
										| SGX_MMU_PDE_VALID;
						MakeKernelPageReadOnly(psMMUContext->pvPDCpuVAddr);
						#if defined(PDUMP)
						/* pdump the PD Page modifications */
						#if defined(SUPPORT_PDUMP_MULTI_PROCESS)
						if(psMMUContext->bPDumpActive)
						#endif
						{
#if defined(SUPPORT_PDUMP_MULTI_PROCESS)
							/*
								Any modification of the uKernel memory context
								needs to be PDumped when we're multi-process
							 */
							IMG_UINT32 ui32HeapFlags = ( psMMUContext->sPDDevPAddr.uiAddr == psDevInfo->sKernelPDDevPAddr.uiAddr ) ? PDUMP_FLAGS_PERSISTENT : 0;
#else
							IMG_UINT32 ui32HeapFlags = 0;
#endif
							PDUMPPDENTRIES(&pMMUHeap->sMMUAttrib, psMMUContext->hPDOSMemHandle, (IMG_VOID*)&pui32PDEntry[i], sizeof(IMG_UINT32), ui32HeapFlags, IMG_FALSE, PDUMP_PD_UNIQUETAG, PDUMP_PT_UNIQUETAG);
						}
						#endif /* PDUMP */
						/* advance to next context */
						psMMUContext = psMMUContext->psNext;
					}
#if defined(FIX_HW_BRN_31620)
					bSharedPT = IMG_TRUE;
#endif
					break;
				}
				case DEVICE_MEMORY_HEAP_PERCONTEXT :
				case DEVICE_MEMORY_HEAP_KERNEL :
				{
					MakeKernelPageReadWrite(pMMUHeap->psMMUContext->pvPDCpuVAddr);
					/* insert Page Table into only this memory context */
					pui32PDEntry[i] = (IMG_UINT32)(sDevPAddr.uiAddr>>SGX_MMU_PDE_ADDR_ALIGNSHIFT)
									| pMMUHeap->ui32PDEPageSizeCtrl
									| SGX_MMU_PDE_VALID;
					MakeKernelPageReadOnly(pMMUHeap->psMMUContext->pvPDCpuVAddr);
					/* pdump the PD Page modifications */
					PDUMPPDENTRIES(&pMMUHeap->sMMUAttrib, pMMUHeap->psMMUContext->hPDOSMemHandle, (IMG_VOID*)&pui32PDEntry[i], sizeof(IMG_UINT32), 0, IMG_FALSE, PDUMP_PD_UNIQUETAG, PDUMP_PT_UNIQUETAG);
					break;
				}
				default:
				{
					PVR_DPF((PVR_DBG_ERROR, "_DeferredAllocPagetables: ERROR invalid heap type"));
					return IMG_FALSE;
				}
			}

#if !defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS)
			/* This is actually not to do with multiple mem contexts, but to do with the directory cache.
			   In the 1 context implementation of the MMU, the directory "cache" is actually a copy of the
			   page directory memory, and requires updating whenever the page directory changes, even if there
			   was no previous value in a particular entry
			 */
			MMU_InvalidateDirectoryCache(pMMUHeap->psMMUContext->psDevInfo);
#endif
#if defined(FIX_HW_BRN_31620)
			/* If this PT is not in the requested range then save it and null out the main PTInfo */
			if (((ui32PDIndex + i) < ui32PDRequestStart) || ((ui32PDIndex + i) > ui32PDRequestEnd))
			{
					pMMUHeap->psMMUContext->apsPTInfoListSave[ui32PDIndex + i] = ppsPTInfoList[i];
					ppsPTInfoList[i] = IMG_NULL;
			}
#endif
		}
		else
		{
#if !defined(FIX_HW_BRN_31620)
			/* already have an allocated PT */
			PVR_ASSERT(pui32PDEntry[i] != 0);
#endif
		}
#if defined(FIX_HW_BRN_31620)
		}
#endif
	}

	#if defined(SGX_FEATURE_SYSTEM_CACHE)
	#if defined(FIX_HW_BRN_31620)
	/* This function might not allocate any new PT's so check before flushing */
	if (bFlushSystemCache)
	{
	#endif

	MMU_InvalidateSystemLevelCache(pMMUHeap->psMMUContext->psDevInfo);
	#endif /* SGX_FEATURE_SYSTEM_CACHE */
	#if defined(FIX_HW_BRN_31620)
	}

	/* Handle the last 4MB roll over */
	sHighDevVAddr.uiAddr = sHighDevVAddr.uiAddr - 1;

	/* Update our PD flush mask if required */
	if (bFlushSystemCache)
	{
		MMU_CONTEXT *psMMUContext;

		if (bSharedPT)
		{
			MMU_CONTEXT *psMMUContext = (MMU_CONTEXT*)pMMUHeap->psMMUContext->psDevInfo->pvMMUContextList;

			while(psMMUContext)
			{
				for (i=0;i<BRN31620_CACHE_FLUSH_INDEX_SIZE;i++)
				{
					psMMUContext->ui32PDChangeMask[i] |= ui32ModifiedCachelines[i];
				}

				/* advance to next context */
				psMMUContext = psMMUContext->psNext;
			}
		}
		else
		{
			for (i=0;i<BRN31620_CACHE_FLUSH_INDEX_SIZE;i++)
			{
				pMMUHeap->psMMUContext->ui32PDChangeMask[i] |= ui32ModifiedCachelines[i];
			}
		}

		/*
		 * Always hook up the dummy page when we allocate a new range of PTs.
		 * It might be this is overwritten before the SGX access the dummy page
		 * but we don't care, it's a lot simpler to add this logic here.
		 */
		psMMUContext = pMMUHeap->psMMUContext;
		for (i=0;i<BRN31620_CACHE_FLUSH_INDEX_SIZE;i++)
		{
			IMG_UINT32 j;

			for(j=0;j<BRN31620_CACHE_FLUSH_BITS_SIZE;j++)
			{
				if (ui32ModifiedCachelines[i] & (1 << j))
				{
					PVRSRV_SGXDEV_INFO *psDevInfo = psMMUContext->psDevInfo;
					MMU_PT_INFO *psTempPTInfo = IMG_NULL;
					IMG_UINT32 *pui32Tmp;

					ui32PDIndex = (((i * BRN31620_CACHE_FLUSH_BITS_SIZE) + j) * BRN31620_PDES_PER_CACHE_LINE_SIZE) + BRN31620_DUMMY_PDE_INDEX;

					/* The PT for the dummy page might not be "live". If not get it from the saved pointer */
					if (psMMUContext->apsPTInfoList[ui32PDIndex])
					{
						psTempPTInfo = psMMUContext->apsPTInfoList[ui32PDIndex];
					}
					else
					{
						psTempPTInfo = psMMUContext->apsPTInfoListSave[ui32PDIndex];
					}

					PVR_ASSERT(psTempPTInfo != IMG_NULL);

					MakeKernelPageReadWrite(psTempPTInfo->PTPageCpuVAddr);
					pui32Tmp = (IMG_UINT32 *) psTempPTInfo->PTPageCpuVAddr;
					PVR_ASSERT(pui32Tmp != IMG_NULL);
					pui32Tmp[BRN31620_DUMMY_PTE_INDEX] = (psDevInfo->sBRN31620DummyPageDevPAddr.uiAddr>>SGX_MMU_PTE_ADDR_ALIGNSHIFT)
															| SGX_MMU_PTE_DUMMY_PAGE
															| SGX_MMU_PTE_READONLY
															| SGX_MMU_PTE_VALID;
					MakeKernelPageReadOnly(psTempPTInfo->PTPageCpuVAddr);
					PDUMPCOMMENT("BRN31620 Dump PTE for dummy page after wireing up new PT");
					PDUMPMEMPTENTRIES(&pMMUHeap->sMMUAttrib, psTempPTInfo->hPTPageOSMemHandle, (IMG_VOID *) &pui32Tmp[BRN31620_DUMMY_PTE_INDEX], sizeof(IMG_UINT32), 0, IMG_FALSE, PDUMP_PT_UNIQUETAG, PDUMP_PT_UNIQUETAG);
				}
			}
		}
	}
	#endif

	return IMG_TRUE;
}


#if defined(PDUMP)
/*!
 *	FUNCTION:	MMU_GetPDumpContextID
 *
 *	RETURNS:	pdump MMU context ID
 */
IMG_UINT32 MMU_GetPDumpContextID(IMG_HANDLE hDevMemContext)
{
	BM_CONTEXT *pBMContext = hDevMemContext;
	PVR_ASSERT(pBMContext);
	/* PRQA S 0505 1 */ /* PVR_ASSERT should catch NULL ptr */
	return pBMContext->psMMUContext->ui32PDumpMMUContextID;
}

/*!
 *	FUNCTION:	MMU_SetPDumpAttribs
 *
 *	PURPOSE:	Called from MMU_Initialise and MMU_Create.
 *				Sets up device-specific attributes for pdumping.
 *				FIXME: breaks variable size PTs. Really need separate per context
 *				and per heap attribs.
 *
 *	INPUT:		psDeviceNode - used to access deviceID
 *	INPUT:		ui32DataPageMask - data page mask
 *	INPUT:		ui32PTSize - PT size
 *
 *	OUTPUT:		psMMUAttrib - pdump MMU attributes
 *
 *	RETURNS:	none
 */
#if defined(SGX_FEATURE_VARIABLE_MMU_PAGE_SIZE)
# error "FIXME: breaks variable size pagetables"
#endif
static IMG_VOID MMU_SetPDumpAttribs(PDUMP_MMU_ATTRIB *psMMUAttrib,
	PVRSRV_DEVICE_NODE *psDeviceNode,
	IMG_UINT32 ui32DataPageMask,
	IMG_UINT32 ui32PTSize)
{
	/* Sets up device ID, contains pdump memspace name */
	psMMUAttrib->sDevId = psDeviceNode->sDevId;
	
	psMMUAttrib->pszPDRegRegion = IMG_NULL;
	psMMUAttrib->ui32DataPageMask = ui32DataPageMask;
	
	psMMUAttrib->ui32PTEValid = SGX_MMU_PTE_VALID;
	psMMUAttrib->ui32PTSize = ui32PTSize;
	psMMUAttrib->ui32PTEAlignShift = SGX_MMU_PTE_ADDR_ALIGNSHIFT;
	
	psMMUAttrib->ui32PDEMask = SGX_MMU_PDE_ADDR_MASK;
	psMMUAttrib->ui32PDEAlignShift = SGX_MMU_PDE_ADDR_ALIGNSHIFT;
}
#endif /* PDUMP */

/*!
******************************************************************************
	FUNCTION:   MMU_Initialise

	PURPOSE:	Called from BM_CreateContext.
				Allocates the top level Page Directory 4k Page for the new context.

	PARAMETERS: None
	RETURNS:    PVRSRV_ERROR
******************************************************************************/
PVRSRV_ERROR
MMU_Initialise (PVRSRV_DEVICE_NODE *psDeviceNode, MMU_CONTEXT **ppsMMUContext, IMG_DEV_PHYADDR *psPDDevPAddr)
{
	IMG_UINT32 *pui32Tmp;
	IMG_UINT32 i;
	IMG_CPU_VIRTADDR pvPDCpuVAddr;
	IMG_DEV_PHYADDR sPDDevPAddr;
	IMG_CPU_PHYADDR sCpuPAddr;
	MMU_CONTEXT *psMMUContext;
	IMG_HANDLE hPDOSMemHandle = IMG_NULL;
	SYS_DATA *psSysData;
	PVRSRV_SGXDEV_INFO *psDevInfo;
#if defined(PDUMP)
	PDUMP_MMU_ATTRIB sMMUAttrib;
#endif
	PVR_DPF ((PVR_DBG_MESSAGE, "MMU_Initialise"));

	SysAcquireData(&psSysData);
#if defined(PDUMP)
	/* Note: these attribs are on the stack, used only to pdump the MMU context
	 * creation. */
	MMU_SetPDumpAttribs(&sMMUAttrib, psDeviceNode,
						SGX_MMU_PAGE_MASK,
						SGX_MMU_PT_SIZE * sizeof(IMG_UINT32));
#endif

	OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
				 sizeof (MMU_CONTEXT),
				 (IMG_VOID **)&psMMUContext, IMG_NULL,
				 "MMU Context");
	if (psMMUContext == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR call to OSAllocMem failed"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	OSMemSet (psMMUContext, 0, sizeof(MMU_CONTEXT));

	/* stick the devinfo in the context for subsequent use */
	psDevInfo = (PVRSRV_SGXDEV_INFO*)psDeviceNode->pvDevice;
	psMMUContext->psDevInfo = psDevInfo;

	/* record device node for subsequent use */
	psMMUContext->psDeviceNode = psDeviceNode;

	/* allocate 4k page directory page for the new context */
	if(psDeviceNode->psLocalDevMemArena == IMG_NULL)
	{
		if (OSAllocPages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
						 SGX_MMU_PAGE_SIZE,
						 SGX_MMU_PAGE_SIZE,
						 IMG_NULL,
						 0,
						 IMG_NULL,
						 &pvPDCpuVAddr,
						 &hPDOSMemHandle) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR call to OSAllocPages failed"));
			return PVRSRV_ERROR_FAILED_TO_ALLOC_PAGES;
		}

		if(pvPDCpuVAddr)
		{
			sCpuPAddr = OSMapLinToCPUPhys(hPDOSMemHandle,
										  pvPDCpuVAddr);
		}
		else
		{
			/* This is not used in all cases, since not all ports currently
			 * support OSMemHandleToCpuPAddr */
			sCpuPAddr = OSMemHandleToCpuPAddr(hPDOSMemHandle, 0);
		}
		sPDDevPAddr = SysCpuPAddrToDevPAddr (PVRSRV_DEVICE_TYPE_SGX, sCpuPAddr);

		#if PAGE_TEST
		PageTest(pvPDCpuVAddr, sPDDevPAddr);
		#endif

#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
		/* Allocate dummy PT and Data pages for the first context to be created */
		if(!psDevInfo->pvMMUContextList)
		{
			/* Dummy PT page */
			if (OSAllocPages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
							 SGX_MMU_PAGE_SIZE,
							 SGX_MMU_PAGE_SIZE,
							 IMG_NULL,
							 0,
							 IMG_NULL,
							 &psDevInfo->pvDummyPTPageCpuVAddr,
							 &psDevInfo->hDummyPTPageOSMemHandle) != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR call to OSAllocPages failed"));
				return PVRSRV_ERROR_FAILED_TO_ALLOC_PAGES;
			}

			if(psDevInfo->pvDummyPTPageCpuVAddr)
			{
				sCpuPAddr = OSMapLinToCPUPhys(psDevInfo->hDummyPTPageOSMemHandle,
											  psDevInfo->pvDummyPTPageCpuVAddr);
			}
			else
			{
				/* This is not used in all cases, since not all ports currently
				 * support OSMemHandleToCpuPAddr */
				sCpuPAddr = OSMemHandleToCpuPAddr(psDevInfo->hDummyPTPageOSMemHandle, 0);
			}
			psDevInfo->sDummyPTDevPAddr = SysCpuPAddrToDevPAddr (PVRSRV_DEVICE_TYPE_SGX, sCpuPAddr);

			/* Dummy Data page */
			if (OSAllocPages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
							 SGX_MMU_PAGE_SIZE,
							 SGX_MMU_PAGE_SIZE,
							 IMG_NULL,
							 0,
							 IMG_NULL,
							 &psDevInfo->pvDummyDataPageCpuVAddr,
							 &psDevInfo->hDummyDataPageOSMemHandle) != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR call to OSAllocPages failed"));
				return PVRSRV_ERROR_FAILED_TO_ALLOC_PAGES;
			}

			if(psDevInfo->pvDummyDataPageCpuVAddr)
			{
				sCpuPAddr = OSMapLinToCPUPhys(psDevInfo->hDummyPTPageOSMemHandle,
											  psDevInfo->pvDummyDataPageCpuVAddr);
			}
			else
			{
				sCpuPAddr = OSMemHandleToCpuPAddr(psDevInfo->hDummyDataPageOSMemHandle, 0);
			}
			psDevInfo->sDummyDataDevPAddr = SysCpuPAddrToDevPAddr (PVRSRV_DEVICE_TYPE_SGX, sCpuPAddr);
		}
#endif /* #if defined(SUPPORT_SGX_MMU_DUMMY_PAGE) */
#if defined(FIX_HW_BRN_31620)
		/* Allocate dummy Data pages for the first context to be created */
		if(!psDevInfo->pvMMUContextList)
		{
			IMG_UINT32 j;
			/* Allocate dummy page */
			if (OSAllocPages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
							 SGX_MMU_PAGE_SIZE,
							 SGX_MMU_PAGE_SIZE,
							 IMG_NULL,
							 0,
							 IMG_NULL,
							 &psDevInfo->pvBRN31620DummyPageCpuVAddr,
							 &psDevInfo->hBRN31620DummyPageOSMemHandle) != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR call to OSAllocPages failed"));
				return PVRSRV_ERROR_FAILED_TO_ALLOC_PAGES;
			}				

			/* Get a physical address */
			if(psDevInfo->pvBRN31620DummyPageCpuVAddr)
			{
				sCpuPAddr = OSMapLinToCPUPhys(psDevInfo->hBRN31620DummyPageOSMemHandle,
											  psDevInfo->pvBRN31620DummyPageCpuVAddr);
			}
			else
			{
				sCpuPAddr = OSMemHandleToCpuPAddr(psDevInfo->hBRN31620DummyPageOSMemHandle, 0);
			}

			pui32Tmp = (IMG_UINT32 *)psDevInfo->pvBRN31620DummyPageCpuVAddr;
			for(j=0; j<(SGX_MMU_PAGE_SIZE/4); j++)
			{
				pui32Tmp[j] = BRN31620_DUMMY_PAGE_SIGNATURE;
			}

			psDevInfo->sBRN31620DummyPageDevPAddr = SysCpuPAddrToDevPAddr(PVRSRV_DEVICE_TYPE_SGX, sCpuPAddr);
			PDUMPMALLOCPAGETABLE(&psDeviceNode->sDevId, psDevInfo->hBRN31620DummyPageOSMemHandle, 0, psDevInfo->pvBRN31620DummyPageCpuVAddr, SGX_MMU_PAGE_SIZE, 0, PDUMP_PT_UNIQUETAG);

			/* Allocate dummy PT */
			if (OSAllocPages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
							 SGX_MMU_PAGE_SIZE,
							 SGX_MMU_PAGE_SIZE,
							 IMG_NULL,
							 0,
							 IMG_NULL,
							 &psDevInfo->pvBRN31620DummyPTCpuVAddr,
							 &psDevInfo->hBRN31620DummyPTOSMemHandle) != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR call to OSAllocPages failed"));
				return PVRSRV_ERROR_FAILED_TO_ALLOC_PAGES;
			}				

			/* Get a physical address */
			if(psDevInfo->pvBRN31620DummyPTCpuVAddr)
			{
				sCpuPAddr = OSMapLinToCPUPhys(psDevInfo->hBRN31620DummyPTOSMemHandle,
											  psDevInfo->pvBRN31620DummyPTCpuVAddr);
			}
			else
			{
				sCpuPAddr = OSMemHandleToCpuPAddr(psDevInfo->hBRN31620DummyPTOSMemHandle, 0);
			}

			OSMemSet(psDevInfo->pvBRN31620DummyPTCpuVAddr,0,SGX_MMU_PAGE_SIZE);
			psDevInfo->sBRN31620DummyPTDevPAddr = SysCpuPAddrToDevPAddr(PVRSRV_DEVICE_TYPE_SGX, sCpuPAddr);
			PDUMPMALLOCPAGETABLE(&psDeviceNode->sDevId, psDevInfo->hBRN31620DummyPTOSMemHandle, 0, psDevInfo->pvBRN31620DummyPTCpuVAddr, SGX_MMU_PAGE_SIZE, 0, PDUMP_PT_UNIQUETAG);
		}
#endif
	}
	else
	{
		/* 
		   We cannot use IMG_SYS_PHYADDR here, as that is 64-bit for 32-bit PAE builds.
		   The physical address in this call to RA_Alloc is specifically the SysPAddr 
		   of local (card) space, and it is highly unlikely we would ever need to 
		   support > 4GB of local (card) memory (this does assume that such local
		   memory will be mapped into System physical memory space at a low address so
		   that any and all local memory exists within the 4GB SYSPAddr range).
		 */
		IMG_UINTPTR_T uiLocalPAddr;
		IMG_SYS_PHYADDR sSysPAddr;

		/* allocate from the device's local memory arena */
		if(RA_Alloc(psDeviceNode->psLocalDevMemArena,
					SGX_MMU_PAGE_SIZE,
					IMG_NULL,
					IMG_NULL,
					0,
					SGX_MMU_PAGE_SIZE,
					0,
					IMG_NULL,
					0,
					&uiLocalPAddr)!= IMG_TRUE)
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR call to RA_Alloc failed"));
			return PVRSRV_ERROR_FAILED_TO_ALLOC_VIRT_MEMORY;
		}

		/* Munge the local PAddr back into the SysPAddr */
		sSysPAddr.uiAddr = uiLocalPAddr;

		/* derive the CPU virtual address */
		sCpuPAddr = SysSysPAddrToCpuPAddr(sSysPAddr);
		sPDDevPAddr = SysSysPAddrToDevPAddr(PVRSRV_DEVICE_TYPE_SGX, sSysPAddr);
		pvPDCpuVAddr = OSMapPhysToLin(sCpuPAddr,
										SGX_MMU_PAGE_SIZE,
										PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
										&hPDOSMemHandle);
		if(!pvPDCpuVAddr)
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR failed to map page tables"));
			return PVRSRV_ERROR_FAILED_TO_MAP_PAGE_TABLE;
		}

		#if PAGE_TEST
		PageTest(pvPDCpuVAddr, sPDDevPAddr);
		#endif

#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
		/* Allocate dummy PT and Data pages for the first context to be created */
		if(!psDevInfo->pvMMUContextList)
		{
			/* Dummy PT page */
			if(RA_Alloc(psDeviceNode->psLocalDevMemArena,
						SGX_MMU_PAGE_SIZE,
						IMG_NULL,
						IMG_NULL,
						0,
						SGX_MMU_PAGE_SIZE,
						0,
						IMG_NULL,
						0,
						&uiLocalPAddr)!= IMG_TRUE)
			{
				PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR call to RA_Alloc failed"));
				return PVRSRV_ERROR_FAILED_TO_ALLOC_VIRT_MEMORY;
			}

			/* Munge the local PAddr back into the SysPAddr */
			sSysPAddr.uiAddr = uiLocalPAddr;

			/* derive the CPU virtual address */
			sCpuPAddr = SysSysPAddrToCpuPAddr(sSysPAddr);
			psDevInfo->sDummyPTDevPAddr = SysSysPAddrToDevPAddr(PVRSRV_DEVICE_TYPE_SGX, sSysPAddr);
			psDevInfo->pvDummyPTPageCpuVAddr = OSMapPhysToLin(sCpuPAddr,
																SGX_MMU_PAGE_SIZE,
																PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
																&psDevInfo->hDummyPTPageOSMemHandle);
			if(!psDevInfo->pvDummyPTPageCpuVAddr)
			{
				PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR failed to map page tables"));
				return PVRSRV_ERROR_FAILED_TO_MAP_PAGE_TABLE;
			}

			/* Dummy Data page */
			if(RA_Alloc(psDeviceNode->psLocalDevMemArena,
						SGX_MMU_PAGE_SIZE,
						IMG_NULL,
						IMG_NULL,
						0,
						SGX_MMU_PAGE_SIZE,
						0,
						IMG_NULL,
						0,
						&uiLocalPAddr)!= IMG_TRUE)
			{
				PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR call to RA_Alloc failed"));
				return PVRSRV_ERROR_FAILED_TO_ALLOC_VIRT_MEMORY;
			}

			/* Munge the local PAddr back into the SysPAddr */
			sSysPAddr.uiAddr = uiLocalPAddr;

			/* derive the CPU virtual address */
			sCpuPAddr = SysSysPAddrToCpuPAddr(sSysPAddr);
			psDevInfo->sDummyDataDevPAddr = SysSysPAddrToDevPAddr(PVRSRV_DEVICE_TYPE_SGX, sSysPAddr);
			psDevInfo->pvDummyDataPageCpuVAddr = OSMapPhysToLin(sCpuPAddr,
																SGX_MMU_PAGE_SIZE,
																PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
																&psDevInfo->hDummyDataPageOSMemHandle);
			if(!psDevInfo->pvDummyDataPageCpuVAddr)
			{
				PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR failed to map page tables"));
				return PVRSRV_ERROR_FAILED_TO_MAP_PAGE_TABLE;
			}
		}
#endif /* #if defined(SUPPORT_SGX_MMU_DUMMY_PAGE) */
#if defined(FIX_HW_BRN_31620)
		/* Allocate dummy PT and Data pages for the first context to be created */
		if(!psDevInfo->pvMMUContextList)
		{
			IMG_UINT32 j;
			/* Allocate dummy page */
			if(RA_Alloc(psDeviceNode->psLocalDevMemArena,
						SGX_MMU_PAGE_SIZE,
						IMG_NULL,
						IMG_NULL,
						0,
						SGX_MMU_PAGE_SIZE,
						0,
						IMG_NULL,
						0,
						&uiLocalPAddr)!= IMG_TRUE)
			{
				PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR call to RA_Alloc failed"));
				return PVRSRV_ERROR_FAILED_TO_ALLOC_VIRT_MEMORY;
			}

			/* Munge the local PAddr back into the SysPAddr */
			sSysPAddr.uiAddr = uiLocalPAddr;

			/* derive the CPU virtual address */
			sCpuPAddr = SysSysPAddrToCpuPAddr(sSysPAddr);
			psDevInfo->sBRN31620DummyPageDevPAddr = SysSysPAddrToDevPAddr(PVRSRV_DEVICE_TYPE_SGX, sSysPAddr);
			psDevInfo->pvBRN31620DummyPageCpuVAddr = OSMapPhysToLin(sCpuPAddr,
																SGX_MMU_PAGE_SIZE,
																PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
																&psDevInfo->hBRN31620DummyPageOSMemHandle);
			if(!psDevInfo->pvBRN31620DummyPageCpuVAddr)
			{
				PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR failed to map page tables"));
				return PVRSRV_ERROR_FAILED_TO_MAP_PAGE_TABLE;
			}

			MakeKernelPageReadWrite(psDevInfo->pvBRN31620DummyPageCpuVAddr);
			pui32Tmp = (IMG_UINT32 *)psDevInfo->pvBRN31620DummyPageCpuVAddr;
			for(j=0; j<(SGX_MMU_PAGE_SIZE/4); j++)
			{
				pui32Tmp[j] = BRN31620_DUMMY_PAGE_SIGNATURE;
			}
			MakeKernelPageReadOnly(psDevInfo->pvBRN31620DummyPageCpuVAddr);
			PDUMPMALLOCPAGETABLE(&psDeviceNode->sDevId, psDevInfo->hBRN31620DummyPageOSMemHandle, 0, psDevInfo->pvBRN31620DummyPageCpuVAddr, SGX_MMU_PAGE_SIZE, 0, PDUMP_PT_UNIQUETAG);

			/* Allocate dummy PT */
			if(RA_Alloc(psDeviceNode->psLocalDevMemArena,
						SGX_MMU_PAGE_SIZE,
						IMG_NULL,
						IMG_NULL,
						0,
						SGX_MMU_PAGE_SIZE,
						0,
						IMG_NULL,
						0,
						&uiLocalPAddr)!= IMG_TRUE)
			{
				PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR call to RA_Alloc failed"));
				return PVRSRV_ERROR_FAILED_TO_ALLOC_VIRT_MEMORY;
			}

			/* Munge the local PAddr back into the SysPAddr */
			sSysPAddr.uiAddr = uiLocalPAddr;

			/* derive the CPU virtual address */
			sCpuPAddr = SysSysPAddrToCpuPAddr(sSysPAddr);
			psDevInfo->sBRN31620DummyPTDevPAddr = SysSysPAddrToDevPAddr(PVRSRV_DEVICE_TYPE_SGX, sSysPAddr);
			psDevInfo->pvBRN31620DummyPTCpuVAddr = OSMapPhysToLin(sCpuPAddr,
																SGX_MMU_PAGE_SIZE,
																PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
																&psDevInfo->hBRN31620DummyPTOSMemHandle);

			if(!psDevInfo->pvBRN31620DummyPTCpuVAddr)
			{
				PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR failed to map page tables"));
				return PVRSRV_ERROR_FAILED_TO_MAP_PAGE_TABLE;
			}

			OSMemSet(psDevInfo->pvBRN31620DummyPTCpuVAddr,0,SGX_MMU_PAGE_SIZE);		
			PDUMPMALLOCPAGETABLE(&psDeviceNode->sDevId, psDevInfo->hBRN31620DummyPTOSMemHandle, 0, psDevInfo->pvBRN31620DummyPTCpuVAddr, SGX_MMU_PAGE_SIZE, 0, PDUMP_PT_UNIQUETAG);
		}
#endif /* #if defined(FIX_HW_BRN_31620) */
	}

#if defined(FIX_HW_BRN_31620)
	if (!psDevInfo->pvMMUContextList)
	{
		/* Save the kernel MMU context which is always the 1st to be created */
		psDevInfo->hKernelMMUContext = psMMUContext;
		PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: saving kernel mmu context: %p", psMMUContext));
	}
#endif

#if defined(PDUMP)
#if defined(SUPPORT_PDUMP_MULTI_PROCESS)
	/* Find out if this context is for the active pdump client.
	 * If it is, need to ensure PD entries are pdumped whenever another
	 * process allocates from a shared heap. */
	{
		PVRSRV_PER_PROCESS_DATA* psPerProc = PVRSRVFindPerProcessData();
		if(psPerProc == IMG_NULL)
		{
			/* changes to the kernel context PD/PTs should be pdumped */
			psMMUContext->bPDumpActive = IMG_TRUE;
		}
		else
		{
			psMMUContext->bPDumpActive = psPerProc->bPDumpActive;
		}
	}
#endif /* SUPPORT_PDUMP_MULTI_PROCESS */
	/* pdump the PD malloc */
	PDUMPCOMMENT("Alloc page directory for new MMU context (PDDevPAddr == 0x" DEVPADDR_FMT ")", sPDDevPAddr.uiAddr);
	PDUMPMALLOCPAGETABLE(&psDeviceNode->sDevId, hPDOSMemHandle, 0, pvPDCpuVAddr, SGX_MMU_PAGE_SIZE, 0, PDUMP_PD_UNIQUETAG);
#endif /* PDUMP */

#ifdef SUPPORT_SGX_MMU_BYPASS
	EnableHostAccess(psMMUContext);
#endif

	if (pvPDCpuVAddr)
	{
		pui32Tmp = (IMG_UINT32 *)pvPDCpuVAddr;
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: pvPDCpuVAddr invalid"));
		return PVRSRV_ERROR_INVALID_CPU_ADDR;
	}


#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
	MakeKernelPageReadWrite(pvPDCpuVAddr);
	/*  wire-up the new PD to the dummy PT */
	for(i=0; i<SGX_MMU_PD_SIZE; i++)
	{
		pui32Tmp[i] = (psDevInfo->sDummyPTDevPAddr.uiAddr>>SGX_MMU_PDE_ADDR_ALIGNSHIFT)
					| SGX_MMU_PDE_PAGE_SIZE_4K
					| SGX_MMU_PDE_VALID;
	}
	MakeKernelPageReadOnly(pvPDCpuVAddr);

	if(!psDevInfo->pvMMUContextList)
	{
		/*
			if we've just allocated the dummy pages
			wire up the dummy PT to the dummy data page
		*/
		MakeKernelPageReadWrite(psDevInfo->pvDummyPTPageCpuVAddr);
		pui32Tmp = (IMG_UINT32 *)psDevInfo->pvDummyPTPageCpuVAddr;
		for(i=0; i<SGX_MMU_PT_SIZE; i++)
		{
			pui32Tmp[i] = (psDevInfo->sDummyDataDevPAddr.uiAddr>>SGX_MMU_PTE_ADDR_ALIGNSHIFT)
						| SGX_MMU_PTE_VALID;
		}
		MakeKernelPageReadOnly(psDevInfo->pvDummyPTPageCpuVAddr);
		/* pdump the Dummy PT Page */
		PDUMPCOMMENT("Dummy Page table contents");
		PDUMPMEMPTENTRIES(&sMMUAttrib, psDevInfo->hDummyPTOSMemHandle, psDevInfo->pvDummyPTPageCpuVAddr, SGX_MMU_PAGE_SIZE, 0, IMG_TRUE, PDUMP_PD_UNIQUETAG, PDUMP_PT_UNIQUETAG);

		/*
			write a signature to the dummy data page
		*/
		MakeKernelPageReadWrite(psDevInfo->pvDummyDataPageCpuVAddr);
		pui32Tmp = (IMG_UINT32 *)psDevInfo->pvDummyDataPageCpuVAddr;
		for(i=0; i<(SGX_MMU_PAGE_SIZE/4); i++)
		{
			pui32Tmp[i] = DUMMY_DATA_PAGE_SIGNATURE;
		}
		MakeKernelPageReadOnly(psDevInfo->pvDummyDataPageCpuVAddr);
		/* pdump the Dummy Data Page */
		PDUMPCOMMENT("Dummy Data Page contents");
		PDUMPMEMPTENTRIES(PVRSRV_DEVICE_TYPE_SGX, psDevInfo->hDummyDataPageOSMemHandle, psDevInfo->pvDummyDataPageCpuVAddr, SGX_MMU_PAGE_SIZE, 0, IMG_TRUE, PDUMP_PD_UNIQUETAG, PDUMP_PT_UNIQUETAG);
	}
#else /* #if defined(SUPPORT_SGX_MMU_DUMMY_PAGE) */
	/* initialise the PD to invalid address state */
	MakeKernelPageReadWrite(pvPDCpuVAddr);
	for(i=0; i<SGX_MMU_PD_SIZE; i++)
	{
		/* invalid, no read, no write, no cache consistency */
		pui32Tmp[i] = 0;
	}
	MakeKernelPageReadOnly(pvPDCpuVAddr);
#endif /* #if defined(SUPPORT_SGX_MMU_DUMMY_PAGE) */

#if defined(PDUMP)
#if defined(SUPPORT_PDUMP_MULTI_PROCESS)
	if(psMMUContext->bPDumpActive)
#endif /* SUPPORT_PDUMP_MULTI_PROCESS */
	{
		/* pdump the PD Page */
		PDUMPCOMMENT("Page directory contents");
		PDUMPPDENTRIES(&sMMUAttrib, hPDOSMemHandle, pvPDCpuVAddr, SGX_MMU_PAGE_SIZE, 0, IMG_TRUE, PDUMP_PD_UNIQUETAG, PDUMP_PT_UNIQUETAG);
	}
#endif
#if defined(FIX_HW_BRN_31620)
	{
		IMG_UINT32 i;
		IMG_UINT32 ui32PDCount = 0;
		IMG_UINT32 *pui32PT;
		pui32Tmp = (IMG_UINT32 *)pvPDCpuVAddr;

		PDUMPCOMMENT("BRN31620 Set up dummy PT");

		MakeKernelPageReadWrite(psDevInfo->pvBRN31620DummyPTCpuVAddr);
		pui32PT = (IMG_UINT32 *) psDevInfo->pvBRN31620DummyPTCpuVAddr;
		pui32PT[BRN31620_DUMMY_PTE_INDEX] = (psDevInfo->sBRN31620DummyPageDevPAddr.uiAddr>>SGX_MMU_PTE_ADDR_ALIGNSHIFT)
								| SGX_MMU_PTE_DUMMY_PAGE
								| SGX_MMU_PTE_READONLY
								| SGX_MMU_PTE_VALID;
		MakeKernelPageReadOnly(psDevInfo->pvBRN31620DummyPTCpuVAddr);

#if defined(PDUMP)
		/* Dump initial contents */
		PDUMPCOMMENT("BRN31620 Dump dummy PT contents");
		PDUMPMEMPTENTRIES(&sMMUAttrib,  psDevInfo->hBRN31620DummyPTOSMemHandle, psDevInfo->pvBRN31620DummyPTCpuVAddr, SGX_MMU_PAGE_SIZE, 0, IMG_TRUE, PDUMP_PD_UNIQUETAG, PDUMP_PT_UNIQUETAG);
		PDUMPCOMMENT("BRN31620 Dump dummy page contents");
		PDUMPMEMPTENTRIES(&sMMUAttrib,  psDevInfo->hBRN31620DummyPageOSMemHandle, psDevInfo->pvBRN31620DummyPageCpuVAddr, SGX_MMU_PAGE_SIZE, 0, IMG_TRUE, PDUMP_PD_UNIQUETAG, PDUMP_PT_UNIQUETAG);

		/* Dump the wiring */		
		for(i=0;i<SGX_MMU_PT_SIZE;i++)
		{
			PDUMPMEMPTENTRIES(&sMMUAttrib, psDevInfo->hBRN31620DummyPTOSMemHandle, &pui32PT[i], sizeof(IMG_UINT32), 0, IMG_FALSE, PDUMP_PD_UNIQUETAG, PDUMP_PT_UNIQUETAG);
		}
#endif
		PDUMPCOMMENT("BRN31620 Dump PDE wire up");
		/* Walk the PD wireing up the PT's */
		for(i=0;i<SGX_MMU_PD_SIZE;i++)
		{
			pui32Tmp[i] = 0;

			if (ui32PDCount == BRN31620_DUMMY_PDE_INDEX)
			{
				MakeKernelPageReadWrite(pvPDCpuVAddr);
				pui32Tmp[i] = (psDevInfo->sBRN31620DummyPTDevPAddr.uiAddr>>SGX_MMU_PDE_ADDR_ALIGNSHIFT)
						| SGX_MMU_PDE_PAGE_SIZE_4K
						| SGX_MMU_PDE_DUMMY_PAGE
						| SGX_MMU_PDE_VALID;
				MakeKernelPageReadOnly(pvPDCpuVAddr);
			}
				PDUMPMEMPTENTRIES(&sMMUAttrib, hPDOSMemHandle, (IMG_VOID *) &pui32Tmp[i], sizeof(IMG_UINT32), 0, IMG_FALSE, PDUMP_PT_UNIQUETAG, PDUMP_PT_UNIQUETAG);
			ui32PDCount++;
			if (ui32PDCount == BRN31620_PDES_PER_CACHE_LINE_SIZE)
			{
				/* Reset PT count */
				ui32PDCount = 0;
			}
		}


		/* pdump the Dummy PT Page */
		PDUMPCOMMENT("BRN31620 dummy Page table contents");
		PDUMPMEMPTENTRIES(&sMMUAttrib, psDevInfo->hBRN31620DummyPageOSMemHandle, psDevInfo->pvBRN31620DummyPageCpuVAddr, SGX_MMU_PAGE_SIZE, 0, IMG_TRUE, PDUMP_PD_UNIQUETAG, PDUMP_PT_UNIQUETAG);
	}
#endif
#if defined(PDUMP)
	/* pdump set MMU context */
	{
		PVRSRV_ERROR eError;
		/* default MMU type is 1, 4k page */
		IMG_UINT32 ui32MMUType = 1;

		#if defined(SGX_FEATURE_36BIT_MMU)
			ui32MMUType = 3;
		#else
			#if defined(SGX_FEATURE_VARIABLE_MMU_PAGE_SIZE)
				ui32MMUType = 2;
			#endif
		#endif

		eError = PDumpSetMMUContext(PVRSRV_DEVICE_TYPE_SGX,
									psDeviceNode->sDevId.pszPDumpDevName,
									&psMMUContext->ui32PDumpMMUContextID,
									ui32MMUType,
									PDUMP_PT_UNIQUETAG,
									hPDOSMemHandle,
									pvPDCpuVAddr);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR call to PDumpSetMMUContext failed"));
			return eError;
		}
	}

	/* PDump the context ID */
	PDUMPCOMMENT("Set MMU context complete (MMU Context ID == %u)", psMMUContext->ui32PDumpMMUContextID);
#endif

#if defined(FIX_HW_BRN_31620)
	for(i=0;i<BRN31620_CACHE_FLUSH_INDEX_SIZE;i++)
	{
		psMMUContext->ui32PDChangeMask[i] = 0;
	}

	for(i=0;i<BRN31620_CACHE_FLUSH_SIZE;i++)
	{
		psMMUContext->ui32PDCacheRangeRefCount[i] = 0;
	}

	for(i=0;i<SGX_MAX_PD_ENTRIES;i++)
	{
		psMMUContext->apsPTInfoListSave[i] = IMG_NULL;
	}
#endif
	/* store PD info in the MMU context */
	psMMUContext->pvPDCpuVAddr = pvPDCpuVAddr;
	psMMUContext->sPDDevPAddr = sPDDevPAddr;
	psMMUContext->hPDOSMemHandle = hPDOSMemHandle;

	/* Get some process information to aid debug */
	psMMUContext->ui32PID = OSGetCurrentProcessIDKM();
	psMMUContext->szName[0] = '\0';
	OSGetCurrentProcessNameKM(psMMUContext->szName, MMU_CONTEXT_NAME_SIZE);

	/* return context */
	*ppsMMUContext = psMMUContext;

	/* return the PD DevVAddr */
	*psPDDevPAddr = sPDDevPAddr;


	/* add the new MMU context onto the list of MMU contexts */
	psMMUContext->psNext = (MMU_CONTEXT*)psDevInfo->pvMMUContextList;
	psDevInfo->pvMMUContextList = (IMG_VOID*)psMMUContext;

#ifdef SUPPORT_SGX_MMU_BYPASS
	DisableHostAccess(psMMUContext);
#endif

	return PVRSRV_OK;
}

/*!
******************************************************************************
	FUNCTION:   MMU_Finalise

	PURPOSE:    Finalise the mmu module, deallocate all resources.

	PARAMETERS: In: psMMUContext - MMU context to deallocate
	RETURNS:    None.
******************************************************************************/
IMG_VOID
MMU_Finalise (MMU_CONTEXT *psMMUContext)
{
	IMG_UINT32 *pui32Tmp, i;
	SYS_DATA *psSysData;
	MMU_CONTEXT **ppsMMUContext;
#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE) || defined(FIX_HW_BRN_31620)
	PVRSRV_SGXDEV_INFO *psDevInfo = (PVRSRV_SGXDEV_INFO*)psMMUContext->psDevInfo;
	MMU_CONTEXT *psMMUContextList = (MMU_CONTEXT*)psDevInfo->pvMMUContextList;
#endif

	SysAcquireData(&psSysData);

#if defined(PDUMP)
	/* pdump the MMU context clear */
	PDUMPCOMMENT("Clear MMU context (MMU Context ID == %u)", psMMUContext->ui32PDumpMMUContextID);
	PDUMPCLEARMMUCONTEXT(PVRSRV_DEVICE_TYPE_SGX, psMMUContext->psDeviceNode->sDevId.pszPDumpDevName, psMMUContext->ui32PDumpMMUContextID, 2);

	/* pdump the PD free */
	PDUMPCOMMENT("Free page directory (PDDevPAddr == 0x" DEVPADDR_FMT ")",
			psMMUContext->sPDDevPAddr.uiAddr);
#endif /* PDUMP */

	PDUMPFREEPAGETABLE(&psMMUContext->psDeviceNode->sDevId, psMMUContext->hPDOSMemHandle, psMMUContext->pvPDCpuVAddr, SGX_MMU_PAGE_SIZE, 0, PDUMP_PT_UNIQUETAG);
#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
	PDUMPFREEPAGETABLE(&psMMUContext->psDeviceNode->sDevId, psDevInfo->hDummyPTPageOSMemHandle, psDevInfo->pvDummyPTPageCpuVAddr, SGX_MMU_PAGE_SIZE, 0, PDUMP_PT_UNIQUETAG);
	PDUMPFREEPAGETABLE(&psMMUContext->psDeviceNode->sDevId, psDevInfo->hDummyDataPageOSMemHandle, psDevInfo->pvDummyDataPageCpuVAddr, SGX_MMU_PAGE_SIZE, 0, PDUMP_PT_UNIQUETAG);
#endif

	pui32Tmp = (IMG_UINT32 *)psMMUContext->pvPDCpuVAddr;

	MakeKernelPageReadWrite(psMMUContext->pvPDCpuVAddr);
	/* initialise the PD to invalid address state */
	for(i=0; i<SGX_MMU_PD_SIZE; i++)
	{
		/* invalid, no read, no write, no cache consistency */
		pui32Tmp[i] = 0;
	}
	MakeKernelPageReadOnly(psMMUContext->pvPDCpuVAddr);

	/*
		free the PD:
		depending on the specific system, the PD is allocated from system memory
		or device local memory.  For now, just look for at least a valid local heap/arena
	*/
	if(psMMUContext->psDeviceNode->psLocalDevMemArena == IMG_NULL)
	{
#if defined(FIX_HW_BRN_31620)
		PVRSRV_SGXDEV_INFO *psDevInfo = (PVRSRV_SGXDEV_INFO*)psMMUContext->psDevInfo;
#endif
		MakeKernelPageReadWrite(psMMUContext->pvPDCpuVAddr);
		OSFreePages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
						SGX_MMU_PAGE_SIZE,
						psMMUContext->pvPDCpuVAddr,
						psMMUContext->hPDOSMemHandle);

#if defined(FIX_HW_BRN_31620)
		/* If this is the _last_ MMU context it must be the uKernel */
		if (!psMMUContextList->psNext)
		{
			PDUMPFREEPAGETABLE(&psMMUContext->psDeviceNode->sDevId, psDevInfo->hBRN31620DummyPageOSMemHandle, psDevInfo->pvBRN31620DummyPageCpuVAddr, SGX_MMU_PAGE_SIZE, 0, PDUMP_PT_UNIQUETAG);
			OSFreePages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
							SGX_MMU_PAGE_SIZE,
							psDevInfo->pvBRN31620DummyPageCpuVAddr,
							psDevInfo->hBRN31620DummyPageOSMemHandle);

			PDUMPFREEPAGETABLE(&psMMUContext->psDeviceNode->sDevId, psDevInfo->hBRN31620DummyPTOSMemHandle, psDevInfo->pvBRN31620DummyPTCpuVAddr, SGX_MMU_PAGE_SIZE, 0, PDUMP_PT_UNIQUETAG);
			OSFreePages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
							SGX_MMU_PAGE_SIZE,
							psDevInfo->pvBRN31620DummyPTCpuVAddr,
							psDevInfo->hBRN31620DummyPTOSMemHandle);
	
		}
#endif
#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
		/* if this is the last context free the dummy pages too */
		if(!psMMUContextList->psNext)
		{
			OSFreePages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
							SGX_MMU_PAGE_SIZE,
							psDevInfo->pvDummyPTPageCpuVAddr,
							psDevInfo->hDummyPTPageOSMemHandle);
			OSFreePages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
							SGX_MMU_PAGE_SIZE,
							psDevInfo->pvDummyDataPageCpuVAddr,
							psDevInfo->hDummyDataPageOSMemHandle);
		}
#endif
	}
	else
	{
		IMG_SYS_PHYADDR sSysPAddr;
		IMG_CPU_PHYADDR sCpuPAddr;

		/*  derive the system physical address */
		sCpuPAddr = OSMapLinToCPUPhys(psMMUContext->hPDOSMemHandle,
									  psMMUContext->pvPDCpuVAddr);
		sSysPAddr = SysCpuPAddrToSysPAddr(sCpuPAddr);

		/* unmap the CPU mapping */
		OSUnMapPhysToLin(psMMUContext->pvPDCpuVAddr,
							SGX_MMU_PAGE_SIZE,
                            PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
							psMMUContext->hPDOSMemHandle);
		/* and free the memory, Note that the cast to IMG_UINTPTR_T is ok as we're local mem. */
		RA_Free (psMMUContext->psDeviceNode->psLocalDevMemArena, (IMG_UINTPTR_T)sSysPAddr.uiAddr, IMG_FALSE);

#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
		/* if this is the last context free the dummy pages too */
		if(!psMMUContextList->psNext)
		{
			/* free the Dummy PT Page */
			sCpuPAddr = OSMapLinToCPUPhys(psDevInfo->hDummyPTPageOSMemHandle,
										  psDevInfo->pvDummyPTPageCpuVAddr);
			sSysPAddr = SysCpuPAddrToSysPAddr(sCpuPAddr);

			/* unmap the CPU mapping */
			OSUnMapPhysToLin(psDevInfo->pvDummyPTPageCpuVAddr,
								SGX_MMU_PAGE_SIZE,
                                PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
								psDevInfo->hDummyPTPageOSMemHandle);
			/* and free the memory */
			RA_Free (psMMUContext->psDeviceNode->psLocalDevMemArena, sSysPAddr.uiAddr, IMG_FALSE);

			/* free the Dummy Data Page */
			sCpuPAddr = OSMapLinToCPUPhys(psDevInfo->hDummyDataPageOSMemHandle,
										  psDevInfo->pvDummyDataPageCpuVAddr);
			sSysPAddr = SysCpuPAddrToSysPAddr(sCpuPAddr);

			/* unmap the CPU mapping */
			OSUnMapPhysToLin(psDevInfo->pvDummyDataPageCpuVAddr,
								SGX_MMU_PAGE_SIZE,
                                PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
								psDevInfo->hDummyDataPageOSMemHandle);
			/* and free the memory */
			RA_Free (psMMUContext->psDeviceNode->psLocalDevMemArena, sSysPAddr.uiAddr, IMG_FALSE);
		}
#endif
#if defined(FIX_HW_BRN_31620)
		/* if this is the last context free the dummy pages too */
		if(!psMMUContextList->psNext)
		{
			/* free the Page */
			PDUMPFREEPAGETABLE(&psMMUContext->psDeviceNode->sDevId, psDevInfo->hBRN31620DummyPageOSMemHandle, psDevInfo->pvBRN31620DummyPageCpuVAddr, SGX_MMU_PAGE_SIZE, 0, PDUMP_PT_UNIQUETAG);

			sCpuPAddr = OSMapLinToCPUPhys(psDevInfo->hBRN31620DummyPageOSMemHandle,
										  psDevInfo->pvBRN31620DummyPageCpuVAddr);
			sSysPAddr = SysCpuPAddrToSysPAddr(sCpuPAddr);

			/* unmap the CPU mapping */
			OSUnMapPhysToLin(psDevInfo->pvBRN31620DummyPageCpuVAddr,
								SGX_MMU_PAGE_SIZE,
                                PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
								psDevInfo->hBRN31620DummyPageOSMemHandle);
			/* and free the memory */
			RA_Free (psMMUContext->psDeviceNode->psLocalDevMemArena, sSysPAddr.uiAddr, IMG_FALSE);

			/* free the Dummy PT */
			PDUMPFREEPAGETABLE(&psMMUContext->psDeviceNode->sDevId, psDevInfo->hBRN31620DummyPTOSMemHandle, psDevInfo->pvBRN31620DummyPTCpuVAddr, SGX_MMU_PAGE_SIZE, 0, PDUMP_PT_UNIQUETAG);

			sCpuPAddr = OSMapLinToCPUPhys(psDevInfo->hBRN31620DummyPTOSMemHandle,
										  psDevInfo->pvBRN31620DummyPTCpuVAddr);
			sSysPAddr = SysCpuPAddrToSysPAddr(sCpuPAddr);

			/* unmap the CPU mapping */
			OSUnMapPhysToLin(psDevInfo->pvBRN31620DummyPTCpuVAddr,
								SGX_MMU_PAGE_SIZE,
                                PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
								psDevInfo->hBRN31620DummyPTOSMemHandle);
			/* and free the memory */
			RA_Free (psMMUContext->psDeviceNode->psLocalDevMemArena, sSysPAddr.uiAddr, IMG_FALSE);
		}
#endif
	}

	PVR_DPF ((PVR_DBG_MESSAGE, "MMU_Finalise"));

	/* remove the MMU context from the list of MMU contexts */
	ppsMMUContext = (MMU_CONTEXT**)&psMMUContext->psDevInfo->pvMMUContextList;
	while(*ppsMMUContext)
	{
		if(*ppsMMUContext == psMMUContext)
		{
			/* remove item from the list */
			*ppsMMUContext = psMMUContext->psNext;
			break;
		}

		/* advance to next next */
		ppsMMUContext = &((*ppsMMUContext)->psNext);
	}

	/* free the context itself. */
	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(MMU_CONTEXT), psMMUContext, IMG_NULL);
	/*not nulling pointer, copy on stack*/
}


/*!
******************************************************************************
	FUNCTION:   MMU_InsertHeap

	PURPOSE:    Copies PDEs from shared/exported heap into current MMU context.

	PARAMETERS:	In:  psMMUContext - the mmu
	            In:  psMMUHeap - a shared/exported heap

	RETURNS:	None
******************************************************************************/
IMG_VOID
MMU_InsertHeap(MMU_CONTEXT *psMMUContext, MMU_HEAP *psMMUHeap)
{
	IMG_UINT32 *pui32PDCpuVAddr = (IMG_UINT32 *) psMMUContext->pvPDCpuVAddr;
	IMG_UINT32 *pui32KernelPDCpuVAddr = (IMG_UINT32 *) psMMUHeap->psMMUContext->pvPDCpuVAddr;
	IMG_UINT32 ui32PDEntry;
#if !defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS)
	IMG_BOOL bInvalidateDirectoryCache = IMG_FALSE;
#endif

	/* advance to the first entry */
	pui32PDCpuVAddr += psMMUHeap->psDevArena->BaseDevVAddr.uiAddr >> psMMUHeap->ui32PDShift;
	pui32KernelPDCpuVAddr += psMMUHeap->psDevArena->BaseDevVAddr.uiAddr >> psMMUHeap->ui32PDShift;

	/*
		update the PD range relating to the heap's
		device virtual address range
	*/
#if defined(PDUMP)
	PDUMPCOMMENT("Page directory shared heap range copy");
	PDUMPCOMMENT("  (Source heap MMU Context ID == %u, PT count == 0x%x)",
			psMMUHeap->psMMUContext->ui32PDumpMMUContextID,
			psMMUHeap->ui32PageTableCount);
	PDUMPCOMMENT("  (Destination MMU Context ID == %u)", psMMUContext->ui32PDumpMMUContextID);
#endif /* PDUMP */
#ifdef SUPPORT_SGX_MMU_BYPASS
	EnableHostAccess(psMMUContext);
#endif

	for (ui32PDEntry = 0; ui32PDEntry < psMMUHeap->ui32PageTableCount; ui32PDEntry++)
	{
#if (!defined(SUPPORT_SGX_MMU_DUMMY_PAGE)) && (!defined(FIX_HW_BRN_31620))
		/* check we have invalidated target PDEs */
		PVR_ASSERT(pui32PDCpuVAddr[ui32PDEntry] == 0);
#endif
		MakeKernelPageReadWrite(psMMUContext->pvPDCpuVAddr);
		/* copy over the PDEs */
		pui32PDCpuVAddr[ui32PDEntry] = pui32KernelPDCpuVAddr[ui32PDEntry];
		MakeKernelPageReadOnly(psMMUContext->pvPDCpuVAddr);
		if (pui32PDCpuVAddr[ui32PDEntry])
		{
			/* Ensure the shared heap allocation is mapped into the context/PD
			 * for the active pdump process/app. The PTs and backing physical
			 * should also be pdumped (elsewhere).
			 *		MALLOC (PT)
			 *		LDB (init PT)
			 *		MALLOC (data page)
			 *		WRW (PTE->data page)
			 *		LDB (init data page) -- could be useful to ensure page is initialised
			 */
		#if defined(PDUMP)
			//PDUMPCOMMENT("MMU_InsertHeap: Mapping shared heap to new context %d (%s)", psMMUContext->ui32PDumpMMUContextID, (psMMUContext->bPDumpActive) ? "active" : "");
		#if defined(SUPPORT_PDUMP_MULTI_PROCESS)
			if(psMMUContext->bPDumpActive)
		#endif /* SUPPORT_PDUMP_MULTI_PROCESS */
			{
				PDUMPPDENTRIES(&psMMUHeap->sMMUAttrib, psMMUContext->hPDOSMemHandle, (IMG_VOID *) &pui32PDCpuVAddr[ui32PDEntry], sizeof(IMG_UINT32), 0, IMG_FALSE, PDUMP_PD_UNIQUETAG, PDUMP_PT_UNIQUETAG);
			}
		#endif
#if !defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS)
			bInvalidateDirectoryCache = IMG_TRUE;
#endif
		}
	}

#ifdef SUPPORT_SGX_MMU_BYPASS
	DisableHostAccess(psMMUContext);
#endif

#if !defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS)
	if (bInvalidateDirectoryCache)
	{
		/* This is actually not to do with multiple mem contexts, but to do with the directory cache.
			In the 1 context implementation of the MMU, the directory "cache" is actually a copy of the
			page directory memory, and requires updating whenever the page directory changes, even if there
			was no previous value in a particular entry
		*/
		MMU_InvalidateDirectoryCache(psMMUContext->psDevInfo);
	}
#endif
}


/*!
******************************************************************************
	FUNCTION:   MMU_UnmapPagesAndFreePTs

	PURPOSE:    unmap pages, invalidate virtual address and try to free the PTs

	PARAMETERS:	In:  psMMUHeap - the mmu.
	            In:  sDevVAddr - the device virtual address.
	            In:  ui32PageCount - page count
	            In:  hUniqueTag - A unique ID for use as a tag identifier

	RETURNS:	None
******************************************************************************/
static IMG_VOID
MMU_UnmapPagesAndFreePTs (MMU_HEAP *psMMUHeap,
						  IMG_DEV_VIRTADDR sDevVAddr,
						  IMG_UINT32 ui32PageCount,
						  IMG_HANDLE hUniqueTag)
{
	IMG_DEV_VIRTADDR	sTmpDevVAddr;
	IMG_UINT32			i;
	IMG_UINT32			ui32PDIndex;
	IMG_UINT32			ui32PTIndex;
	IMG_UINT32			*pui32Tmp;
	IMG_BOOL			bInvalidateDirectoryCache = IMG_FALSE;

#if !defined (PDUMP)
	PVR_UNREFERENCED_PARAMETER(hUniqueTag);
#endif
	/* setup tmp devvaddr to base of allocation */
	sTmpDevVAddr = sDevVAddr;

	for(i=0; i<ui32PageCount; i++)
	{
		MMU_PT_INFO **ppsPTInfoList;

		/* find the index/offset in PD entries  */
		ui32PDIndex = sTmpDevVAddr.uiAddr >> psMMUHeap->ui32PDShift;

		/* and advance to the first PT info list */
		ppsPTInfoList = &psMMUHeap->psMMUContext->apsPTInfoList[ui32PDIndex];

		{
			/* find the index/offset of the first PT in the first PT page */
			ui32PTIndex = (sTmpDevVAddr.uiAddr & psMMUHeap->ui32PTMask) >> psMMUHeap->ui32PTShift;

			/* Is the PT page valid? */
			if (!ppsPTInfoList[0])
			{
				/*
					With sparse mappings we expect that the PT could be freed
					before we reach the end of it as the unmapped pages don't
					bump ui32ValidPTECount so it can reach zero before we reach
					the end of the PT.
				*/
				if (!psMMUHeap->bHasSparseMappings)
				{
					PVR_DPF((PVR_DBG_MESSAGE, "MMU_UnmapPagesAndFreePTs: Invalid PT for alloc at VAddr:0x%08X (VaddrIni:0x%08X AllocPage:%u) PDIdx:%u PTIdx:%u",sTmpDevVAddr.uiAddr, sDevVAddr.uiAddr,i, ui32PDIndex, ui32PTIndex ));
				}

				/* advance the sTmpDevVAddr by one page */
				sTmpDevVAddr.uiAddr += psMMUHeap->ui32DataPageSize;

				/* Try to unmap the remaining allocation pages */
				continue;
			}

			/* setup pointer to the first entry in the PT page */
			pui32Tmp = (IMG_UINT32*)ppsPTInfoList[0]->PTPageCpuVAddr;

			/* Is PTPageCpuVAddr valid ? */
			if (!pui32Tmp)
			{
				continue;
			}

			CheckPT(ppsPTInfoList[0]);

			/* Decrement the valid page count only if the current page is valid*/
			if (pui32Tmp[ui32PTIndex] & SGX_MMU_PTE_VALID)
			{
				ppsPTInfoList[0]->ui32ValidPTECount--;
			}
			else
			{
				if (!psMMUHeap->bHasSparseMappings)
				{
					PVR_DPF((PVR_DBG_MESSAGE, "MMU_UnmapPagesAndFreePTs: Page is already invalid for alloc at VAddr:0x%08X (VAddrIni:0x%08X AllocPage:%u) PDIdx:%u PTIdx:%u",sTmpDevVAddr.uiAddr, sDevVAddr.uiAddr,i, ui32PDIndex, ui32PTIndex ));
				}
			}

			/* The page table count should not go below zero */
			PVR_ASSERT((IMG_INT32)ppsPTInfoList[0]->ui32ValidPTECount >= 0);
			MakeKernelPageReadWrite(ppsPTInfoList[0]->PTPageCpuVAddr);
#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
			/* point the PT entry to the dummy data page */
			pui32Tmp[ui32PTIndex] = (psMMUHeap->psMMUContext->psDevInfo->sDummyDataDevPAddr.uiAddr>>SGX_MMU_PTE_ADDR_ALIGNSHIFT)
									| SGX_MMU_PTE_VALID;
#else
			/* invalidate entry */
#if defined(FIX_HW_BRN_31620)
			BRN31620InvalidatePageTableEntry(psMMUHeap->psMMUContext, ui32PDIndex, ui32PTIndex, &pui32Tmp[ui32PTIndex]);
#else
			pui32Tmp[ui32PTIndex] = 0;
#endif
#endif
			MakeKernelPageReadOnly(ppsPTInfoList[0]->PTPageCpuVAddr);
			CheckPT(ppsPTInfoList[0]);
		}

		/*
			Free a page table if we can.
		*/
		if (ppsPTInfoList[0] && (ppsPTInfoList[0]->ui32ValidPTECount == 0)
			)
		{
#if defined(FIX_HW_BRN_31620)
			if (BRN31620FreePageTable(psMMUHeap, ui32PDIndex) == IMG_TRUE)
			{
				bInvalidateDirectoryCache = IMG_TRUE;
			}
#else
			_DeferredFreePageTable(psMMUHeap, ui32PDIndex - psMMUHeap->ui32PDBaseIndex, IMG_TRUE);
			bInvalidateDirectoryCache = IMG_TRUE;
#endif
		}

		/* advance the sTmpDevVAddr by one page */
		sTmpDevVAddr.uiAddr += psMMUHeap->ui32DataPageSize;
	}

	if(bInvalidateDirectoryCache)
	{
		MMU_InvalidateDirectoryCache(psMMUHeap->psMMUContext->psDevInfo);
	}
	else
	{
		MMU_InvalidatePageTableCache(psMMUHeap->psMMUContext->psDevInfo);
	}

#if defined(PDUMP)
	MMU_PDumpPageTables(psMMUHeap,
						sDevVAddr,
						psMMUHeap->ui32DataPageSize * ui32PageCount,
						IMG_TRUE,
						hUniqueTag);
#endif /* #if defined(PDUMP) */
}


/*!
******************************************************************************
	FUNCTION:   MMU_FreePageTables

	PURPOSE:    Call back from RA_Free to zero page table entries used by freed
				spans.

	PARAMETERS: In: pvMMUHeap
				In: ui32Start
				In: ui32End
				In: hUniqueTag - A unique ID for use as a tag identifier
	RETURNS:
******************************************************************************/
static IMG_VOID MMU_FreePageTables(IMG_PVOID pvMMUHeap,
                                   IMG_SIZE_T uStart,
                                   IMG_SIZE_T uEnd,
                                   IMG_HANDLE hUniqueTag)
{
	MMU_HEAP *pMMUHeap = (MMU_HEAP*)pvMMUHeap;
	IMG_DEV_VIRTADDR Start;

	Start.uiAddr = (IMG_UINT32)uStart;

	MMU_UnmapPagesAndFreePTs(pMMUHeap, Start, (IMG_UINT32)((uEnd - uStart) >> pMMUHeap->ui32PTShift), hUniqueTag);
}

/*!
******************************************************************************
	FUNCTION:   MMU_Create

	PURPOSE:    Create an mmu device virtual heap.

	PARAMETERS: In: psMMUContext - MMU context
	            In: psDevArena - device memory resource arena
				Out: ppsVMArena - virtual mapping arena
	RETURNS:	MMU_HEAP
	RETURNS:
******************************************************************************/
MMU_HEAP *
MMU_Create (MMU_CONTEXT *psMMUContext,
			DEV_ARENA_DESCRIPTOR *psDevArena,
			RA_ARENA **ppsVMArena,
			PDUMP_MMU_ATTRIB **ppsMMUAttrib)
{
	MMU_HEAP *pMMUHeap;
	IMG_UINT32 ui32ScaleSize;

	PVR_UNREFERENCED_PARAMETER(ppsMMUAttrib);

	PVR_ASSERT (psDevArena != IMG_NULL);

	if (psDevArena == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_Create: invalid parameter"));
		return IMG_NULL;
	}

	OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
				 sizeof (MMU_HEAP),
				 (IMG_VOID **)&pMMUHeap, IMG_NULL,
				 "MMU Heap");
	if (pMMUHeap == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_Create: ERROR call to OSAllocMem failed"));
		return IMG_NULL;
	}

	pMMUHeap->psMMUContext = psMMUContext;
	pMMUHeap->psDevArena = psDevArena;

	/*
		generate page table and data page mask and shift values
		based on the data page size
	*/
	switch(pMMUHeap->psDevArena->ui32DataPageSize)
	{
		case 0x1000:
			ui32ScaleSize = 0;
			pMMUHeap->ui32PDEPageSizeCtrl = SGX_MMU_PDE_PAGE_SIZE_4K;
			break;
#if defined(SGX_FEATURE_VARIABLE_MMU_PAGE_SIZE)
		case 0x4000:
			ui32ScaleSize = 2;
			pMMUHeap->ui32PDEPageSizeCtrl = SGX_MMU_PDE_PAGE_SIZE_16K;
			break;
		case 0x10000:
			ui32ScaleSize = 4;
			pMMUHeap->ui32PDEPageSizeCtrl = SGX_MMU_PDE_PAGE_SIZE_64K;
			break;
		case 0x40000:
			ui32ScaleSize = 6;
			pMMUHeap->ui32PDEPageSizeCtrl = SGX_MMU_PDE_PAGE_SIZE_256K;
			break;
		case 0x100000:
			ui32ScaleSize = 8;
			pMMUHeap->ui32PDEPageSizeCtrl = SGX_MMU_PDE_PAGE_SIZE_1M;
			break;
		case 0x400000:
			ui32ScaleSize = 10;
			pMMUHeap->ui32PDEPageSizeCtrl = SGX_MMU_PDE_PAGE_SIZE_4M;
			break;
#endif /* #if defined(SGX_FEATURE_VARIABLE_MMU_PAGE_SIZE) */
		default:
			PVR_DPF((PVR_DBG_ERROR, "MMU_Create: invalid data page size"));
			goto ErrorFreeHeap;
	}

	/* number of bits of address offset into the data page */
	pMMUHeap->ui32DataPageSize = psDevArena->ui32DataPageSize;
	pMMUHeap->ui32DataPageBitWidth = SGX_MMU_PAGE_SHIFT + ui32ScaleSize;
	pMMUHeap->ui32DataPageMask = pMMUHeap->ui32DataPageSize - 1;
	/* number of bits of address indexing into a pagetable */
	pMMUHeap->ui32PTShift = pMMUHeap->ui32DataPageBitWidth;
	pMMUHeap->ui32PTBitWidth = SGX_MMU_PT_SHIFT - ui32ScaleSize;
	pMMUHeap->ui32PTMask = SGX_MMU_PT_MASK & (SGX_MMU_PT_MASK<<ui32ScaleSize);
	pMMUHeap->ui32PTSize = (IMG_UINT32)(1UL<<pMMUHeap->ui32PTBitWidth) * sizeof(IMG_UINT32);

	/* note: PT size must be at least 4 entries, even for 4Mb data page size */
	if(pMMUHeap->ui32PTSize < 4 * sizeof(IMG_UINT32))
	{
		pMMUHeap->ui32PTSize = 4 * sizeof(IMG_UINT32);
	}
	pMMUHeap->ui32PTNumEntriesAllocated = pMMUHeap->ui32PTSize >> 2;

	/* find the number of actual PT entries per PD entry range. For 4MB data
	 * pages we only use the first entry although the PT has 16 byte allocation/alignment
	 * (due to 4 LSbits of the PDE are reserved for control) */
	pMMUHeap->ui32PTNumEntriesUsable = (IMG_UINT32)(1UL << pMMUHeap->ui32PTBitWidth);

	/* number of bits of address indexing into a page directory */
	pMMUHeap->ui32PDShift = pMMUHeap->ui32PTBitWidth + pMMUHeap->ui32PTShift;
	pMMUHeap->ui32PDBitWidth = SGX_FEATURE_ADDRESS_SPACE_SIZE - pMMUHeap->ui32PTBitWidth - pMMUHeap->ui32DataPageBitWidth;
	pMMUHeap->ui32PDMask = SGX_MMU_PD_MASK & (SGX_MMU_PD_MASK>>(32-SGX_FEATURE_ADDRESS_SPACE_SIZE));

	/* External system cache violates this rule */
#if !defined (SUPPORT_EXTERNAL_SYSTEM_CACHE)
	/*
		The heap must start on a PT boundary to avoid PT sharing across heaps
		The only exception is the first heap which can start at any address
		from 0 to the end of the first PT boundary
	*/
	if(psDevArena->BaseDevVAddr.uiAddr > (pMMUHeap->ui32DataPageMask | pMMUHeap->ui32PTMask))
	{
		/*
			if for some reason the first heap starts after the end of the first PT boundary
			but is not aligned to a PT boundary then the assert will trigger unncessarily
		*/
		PVR_ASSERT ((psDevArena->BaseDevVAddr.uiAddr
						& (pMMUHeap->ui32DataPageMask
							| pMMUHeap->ui32PTMask)) == 0);
	}
#endif
	/* how many PT entries do we need? */
	pMMUHeap->ui32PTETotalUsable = pMMUHeap->psDevArena->ui32Size >> pMMUHeap->ui32PTShift;

	/* calculate the PD Base index for the Heap (required for page mapping) */
	pMMUHeap->ui32PDBaseIndex = (pMMUHeap->psDevArena->BaseDevVAddr.uiAddr & pMMUHeap->ui32PDMask) >> pMMUHeap->ui32PDShift;

	/*
		how many page tables?
		round up to nearest entries to the nearest page table sized block
	*/
	pMMUHeap->ui32PageTableCount = (pMMUHeap->ui32PTETotalUsable + pMMUHeap->ui32PTNumEntriesUsable - 1)
										>> pMMUHeap->ui32PTBitWidth;
	PVR_ASSERT(pMMUHeap->ui32PageTableCount > 0);

	/* Create the arena */
	pMMUHeap->psVMArena = RA_Create(psDevArena->pszName,
									psDevArena->BaseDevVAddr.uiAddr,
									psDevArena->ui32Size,
									IMG_NULL,
									MAX(HOST_PAGESIZE(), pMMUHeap->ui32DataPageSize),
									IMG_NULL,
									IMG_NULL,
									&MMU_FreePageTables,
									pMMUHeap);

	if (pMMUHeap->psVMArena == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_Create: ERROR call to RA_Create failed"));
		goto ErrorFreePagetables;
	}

#if defined(PDUMP)
	/* setup per-heap PDUMP MMU attributes */
	MMU_SetPDumpAttribs(&pMMUHeap->sMMUAttrib,
						psMMUContext->psDeviceNode,
						pMMUHeap->ui32DataPageMask,
						pMMUHeap->ui32PTSize);
	*ppsMMUAttrib = &pMMUHeap->sMMUAttrib;

	PDUMPCOMMENT("Create MMU device from arena %s (Size == 0x%x, DataPageSize == 0x%x, BaseDevVAddr == 0x%x)",
			psDevArena->pszName,
			psDevArena->ui32Size,
			pMMUHeap->ui32DataPageSize,
			psDevArena->BaseDevVAddr.uiAddr);
#endif /* PDUMP */

	/*
		And return the RA for VM arena management
	*/
	*ppsVMArena = pMMUHeap->psVMArena;

	return pMMUHeap;

	/* drop into here if errors */
ErrorFreePagetables:
	_DeferredFreePageTables (pMMUHeap);

ErrorFreeHeap:
	OSFreeMem (PVRSRV_OS_PAGEABLE_HEAP, sizeof(MMU_HEAP), pMMUHeap, IMG_NULL);
	/*not nulling pointer, out of scope*/

	return IMG_NULL;
}

/*!
******************************************************************************
	FUNCTION:   MMU_Delete

	PURPOSE:    Delete an MMU device virtual heap.

	PARAMETERS: In:  pMMUHeap - The MMU heap to delete.
	RETURNS:
******************************************************************************/
IMG_VOID
MMU_Delete (MMU_HEAP *pMMUHeap)
{
	if (pMMUHeap != IMG_NULL)
	{
		PVR_DPF ((PVR_DBG_MESSAGE, "MMU_Delete"));

		if(pMMUHeap->psVMArena)
		{
			RA_Delete (pMMUHeap->psVMArena);
		}

#if defined(PDUMP)
		PDUMPCOMMENT("Delete MMU device from arena %s (BaseDevVAddr == 0x%x, PT count for deferred free == 0x%x)",
				pMMUHeap->psDevArena->pszName,
				pMMUHeap->psDevArena->BaseDevVAddr.uiAddr,
				pMMUHeap->ui32PageTableCount);
#endif /* PDUMP */

#ifdef SUPPORT_SGX_MMU_BYPASS
		EnableHostAccess(pMMUHeap->psMMUContext);
#endif
		_DeferredFreePageTables (pMMUHeap);
#ifdef SUPPORT_SGX_MMU_BYPASS
		DisableHostAccess(pMMUHeap->psMMUContext);
#endif

		OSFreeMem (PVRSRV_OS_PAGEABLE_HEAP, sizeof(MMU_HEAP), pMMUHeap, IMG_NULL);
		/*not nulling pointer, copy on stack*/
	}
}

/*!
******************************************************************************
	FUNCTION:   MMU_Alloc
	PURPOSE:    Allocate space in an mmu's virtual address space.
	PARAMETERS:	In:  pMMUHeap - MMU to allocate on.
	            In:  uSize - Size in bytes to allocate.
	            Out: pActualSize - If non null receives actual size allocated.
	            In:  uFlags - Allocation flags.
	            In:  uDevVAddrAlignment - Required alignment.
	            Out: DevVAddr - Receives base address of allocation.
	RETURNS:	IMG_TRUE - Success
	            IMG_FALSE - Failure
******************************************************************************/
IMG_BOOL
MMU_Alloc (MMU_HEAP *pMMUHeap,
		   IMG_SIZE_T uSize,
		   IMG_SIZE_T *pActualSize,
		   IMG_UINT32 uFlags,
		   IMG_UINT32 uDevVAddrAlignment,
		   IMG_DEV_VIRTADDR *psDevVAddr)
{
	IMG_BOOL bStatus;

	PVR_DPF ((PVR_DBG_MESSAGE,
		"MMU_Alloc: uSize=0x%x, flags=0x%x, align=0x%x",
		uSize, uFlags, uDevVAddrAlignment));

	/*
		Only allocate a VM address if the caller did not supply one
	*/
	if((uFlags & PVRSRV_MEM_USER_SUPPLIED_DEVVADDR) == 0)
	{
		IMG_UINTPTR_T uiAddr;

		bStatus = RA_Alloc (pMMUHeap->psVMArena,
							uSize,
							pActualSize,
							IMG_NULL,
							0,
							uDevVAddrAlignment,
							0,
							IMG_NULL,
							0,
							&uiAddr);
		if(!bStatus)
		{
			PVR_DPF((PVR_DBG_ERROR,"MMU_Alloc: RA_Alloc of VMArena failed"));
			PVR_DPF((PVR_DBG_ERROR,"MMU_Alloc: Alloc of DevVAddr failed from heap %s ID%d",
									pMMUHeap->psDevArena->pszName,
									pMMUHeap->psDevArena->ui32HeapID));
			return bStatus;
		}

		psDevVAddr->uiAddr = IMG_CAST_TO_DEVVADDR_UINT(uiAddr);
	}

	#ifdef SUPPORT_SGX_MMU_BYPASS
	EnableHostAccess(pMMUHeap->psMMUContext);
	#endif

	/* allocate page tables to cover allocation as required */
	bStatus = _DeferredAllocPagetables(pMMUHeap, *psDevVAddr, (IMG_UINT32)uSize);

	#ifdef SUPPORT_SGX_MMU_BYPASS
	DisableHostAccess(pMMUHeap->psMMUContext);
	#endif

	if (!bStatus)
	{
		PVR_DPF((PVR_DBG_ERROR,"MMU_Alloc: _DeferredAllocPagetables failed"));
		PVR_DPF((PVR_DBG_ERROR,"MMU_Alloc: Failed to alloc pagetable(s) for DevVAddr 0x%8.8x from heap %s ID%d",
								psDevVAddr->uiAddr,
								pMMUHeap->psDevArena->pszName,
								pMMUHeap->psDevArena->ui32HeapID));
		if((uFlags & PVRSRV_MEM_USER_SUPPLIED_DEVVADDR) == 0)
		{
			/* free the VM address */
			RA_Free (pMMUHeap->psVMArena, psDevVAddr->uiAddr, IMG_FALSE);
		}
	}

	return bStatus;
}

/*!
******************************************************************************
	FUNCTION:   MMU_Free
	PURPOSE:    Free space in an mmu's virtual address space.
	PARAMETERS:	In:  pMMUHeap - MMU to deallocate on.
	            In:  DevVAddr - Base address to deallocate.
	RETURNS:	None
******************************************************************************/
IMG_VOID
MMU_Free (MMU_HEAP *pMMUHeap, IMG_DEV_VIRTADDR DevVAddr, IMG_UINT32 ui32Size)
{
	PVR_ASSERT (pMMUHeap != IMG_NULL);

	if (pMMUHeap == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_Free: invalid parameter"));
		return;
	}

	PVR_DPF((PVR_DBG_MESSAGE, "MMU_Free: Freeing DevVAddr 0x%08X from heap %s ID%d",
								DevVAddr.uiAddr,
								pMMUHeap->psDevArena->pszName,
								pMMUHeap->psDevArena->ui32HeapID));

	if((DevVAddr.uiAddr >= pMMUHeap->psDevArena->BaseDevVAddr.uiAddr) &&
		(DevVAddr.uiAddr + ui32Size <= pMMUHeap->psDevArena->BaseDevVAddr.uiAddr + pMMUHeap->psDevArena->ui32Size))
	{
		RA_Free (pMMUHeap->psVMArena, DevVAddr.uiAddr, IMG_TRUE);
		return;
	}

	PVR_DPF((PVR_DBG_ERROR,"MMU_Free: Couldn't free DevVAddr %08X from heap %s ID%d (not in range of heap))",
							DevVAddr.uiAddr,
							pMMUHeap->psDevArena->pszName,
							pMMUHeap->psDevArena->ui32HeapID));
}

/*!
******************************************************************************
	FUNCTION:   MMU_Enable

	PURPOSE:    Enable an mmu. Establishes pages tables and takes the mmu out
	            of bypass and waits for the mmu to acknowledge enabled.

	PARAMETERS: In:  pMMUHeap - the mmu
	RETURNS:    None
******************************************************************************/
IMG_VOID
MMU_Enable (MMU_HEAP *pMMUHeap)
{
	PVR_UNREFERENCED_PARAMETER(pMMUHeap);
	/* SGX mmu is always enabled (stub function) */
}

/*!
******************************************************************************
	FUNCTION:   MMU_Disable

	PURPOSE:    Disable an mmu, takes the mmu into bypass.

	PARAMETERS: In:  pMMUHeap - the mmu
	RETURNS:    None
******************************************************************************/
IMG_VOID
MMU_Disable (MMU_HEAP *pMMUHeap)
{
	PVR_UNREFERENCED_PARAMETER(pMMUHeap);
	/* SGX mmu is always enabled (stub function) */
}

#if defined(FIX_HW_BRN_31620)
/*!
******************************************************************************
	FUNCTION:   MMU_GetCacheFlushRange

	PURPOSE:    Gets device physical address of the mmu context.

	PARAMETERS: In:  pMMUContext - the mmu context
	            Out:  pui32RangeMask - Bit mask showing which PD cache
		          lines have changed
	RETURNS:    None
******************************************************************************/

IMG_VOID MMU_GetCacheFlushRange(MMU_CONTEXT *pMMUContext, IMG_UINT32 *pui32RangeMask)
{
	IMG_UINT32 i;

	for (i=0;i<BRN31620_CACHE_FLUSH_INDEX_SIZE;i++)
	{
		pui32RangeMask[i] = pMMUContext->ui32PDChangeMask[i];

		/* Clear bit mask for the next set of allocations */
		pMMUContext->ui32PDChangeMask[i] = 0;
	}
}

/*!
******************************************************************************
	FUNCTION:   MMU_GetPDPhysAddr

	PURPOSE:    Gets device physical address of the mmu contexts PD.

	PARAMETERS: In:  pMMUContext - the mmu context
	            Out:  psDevPAddr - Address of PD
	RETURNS:    None
******************************************************************************/

IMG_VOID MMU_GetPDPhysAddr(MMU_CONTEXT *pMMUContext, IMG_DEV_PHYADDR *psDevPAddr)
{
	*psDevPAddr = pMMUContext->sPDDevPAddr;
}

#endif
#if defined(PDUMP)
/*!
******************************************************************************
	FUNCTION:   MMU_PDumpPageTables

	PURPOSE:    PDump the linear mapping for a range of pages at a specified
	            virtual address.

	PARAMETERS: In:  pMMUHeap - the mmu.
	            In:  DevVAddr - the device virtual address.
	            In:  uSize - size of memory range in bytes
	            In:  hUniqueTag - A unique ID for use as a tag identifier
	RETURNS:    None
******************************************************************************/
static IMG_VOID
MMU_PDumpPageTables	(MMU_HEAP *pMMUHeap,
					 IMG_DEV_VIRTADDR DevVAddr,
					 IMG_SIZE_T uSize,
					 IMG_BOOL bForUnmap,
					 IMG_HANDLE hUniqueTag)
{
	IMG_UINT32	ui32NumPTEntries;
	IMG_UINT32	ui32PTIndex;
	IMG_UINT32	*pui32PTEntry;

	MMU_PT_INFO **ppsPTInfoList;
	IMG_UINT32 ui32PDIndex;
	IMG_UINT32 ui32PTDumpCount;

#if defined(FIX_HW_BRN_31620)
	PVRSRV_SGXDEV_INFO *psDevInfo = pMMUHeap->psMMUContext->psDevInfo;
#endif
	/* find number of PT entries to dump */
	ui32NumPTEntries = (IMG_UINT32)((uSize + pMMUHeap->ui32DataPageMask) >> pMMUHeap->ui32PTShift);

	/* find the index/offset in PD entries  */
	ui32PDIndex = DevVAddr.uiAddr >> pMMUHeap->ui32PDShift;

	/* set the base PT info */
	ppsPTInfoList = &pMMUHeap->psMMUContext->apsPTInfoList[ui32PDIndex];

	/* find the index/offset of the first PT entry in the first PT page */
	ui32PTIndex = (DevVAddr.uiAddr & pMMUHeap->ui32PTMask) >> pMMUHeap->ui32PTShift;

	/* pdump the PT Page modification */
	PDUMPCOMMENT("Page table mods (num entries == %08X) %s", ui32NumPTEntries, bForUnmap ? "(for unmap)" : "");

	/* walk the PT pages, dumping as we go */
	while(ui32NumPTEntries > 0)
	{
		MMU_PT_INFO* psPTInfo = *ppsPTInfoList++;

		if(ui32NumPTEntries <= pMMUHeap->ui32PTNumEntriesUsable - ui32PTIndex)
		{
			ui32PTDumpCount = ui32NumPTEntries;
		}
		else
		{
			ui32PTDumpCount = pMMUHeap->ui32PTNumEntriesUsable - ui32PTIndex;
		}

		if (psPTInfo)
		{
#if defined(FIX_HW_BRN_31620)
			IMG_UINT32 i;
#endif
			IMG_UINT32 ui32Flags = 0;
#if defined(SUPPORT_PDUMP_MULTI_PROCESS)
			ui32Flags |= ( MMU_IsHeapShared(pMMUHeap) ) ? PDUMP_FLAGS_PERSISTENT : 0;
#endif
			pui32PTEntry = (IMG_UINT32*)psPTInfo->PTPageCpuVAddr;
#if defined(FIX_HW_BRN_31620)
			if ((ui32PDIndex % (BRN31620_PDE_CACHE_FILL_SIZE/BRN31620_PT_ADDRESS_RANGE_SIZE)) == BRN31620_DUMMY_PDE_INDEX)
			{
				for (i=ui32PTIndex;i<(ui32PTIndex + ui32PTDumpCount);i++)
				{
					if (pui32PTEntry[i] == ((psDevInfo->sBRN31620DummyPageDevPAddr.uiAddr>>SGX_MMU_PTE_ADDR_ALIGNSHIFT)
											| SGX_MMU_PTE_DUMMY_PAGE
											| SGX_MMU_PTE_READONLY
											| SGX_MMU_PTE_VALID))
					{
						PDUMPMEMPTENTRIES(&pMMUHeap->sMMUAttrib, psPTInfo->hPTPageOSMemHandle, (IMG_VOID *) &pui32PTEntry[i], sizeof(IMG_UINT32), ui32Flags, IMG_FALSE, PDUMP_PT_UNIQUETAG, PDUMP_PD_UNIQUETAG);
					}
					else
					{
						PDUMPMEMPTENTRIES(&pMMUHeap->sMMUAttrib, psPTInfo->hPTPageOSMemHandle, (IMG_VOID *) &pui32PTEntry[i], sizeof(IMG_UINT32), ui32Flags, IMG_FALSE, PDUMP_PT_UNIQUETAG, hUniqueTag);
					}
				}
			}
			else
#endif
			{
				PDUMPMEMPTENTRIES(&pMMUHeap->sMMUAttrib, psPTInfo->hPTPageOSMemHandle, (IMG_VOID *) &pui32PTEntry[ui32PTIndex], ui32PTDumpCount * sizeof(IMG_UINT32), ui32Flags, IMG_FALSE, PDUMP_PT_UNIQUETAG, hUniqueTag);
			}
		}

		/* decrement PT entries left */
		ui32NumPTEntries -= ui32PTDumpCount;

		/* reset offset in page */
		ui32PTIndex = 0;

#if defined(FIX_HW_BRN_31620)
		/* For 31620 we need to know which PD index we're working on */
		ui32PDIndex++;
#endif
	}

	PDUMPCOMMENT("Finished page table mods %s", bForUnmap ? "(for unmap)" : "");
}
#endif /* #if defined(PDUMP) */


/*!
******************************************************************************
	FUNCTION:   MMU_MapPage

	PURPOSE:    Create a mapping for one page at a specified virtual address.

	PARAMETERS: In:  pMMUHeap - the mmu.
	            In:  DevVAddr - the device virtual address.
	            In:  DevPAddr - the device physical address of the page to map.
	            In:  ui32MemFlags - BM r/w/cache flags
	RETURNS:    None
******************************************************************************/
static IMG_VOID
MMU_MapPage (MMU_HEAP *pMMUHeap,
			 IMG_DEV_VIRTADDR DevVAddr,
			 IMG_DEV_PHYADDR DevPAddr,
			 IMG_UINT32 ui32MemFlags)
{
	IMG_UINT32 ui32Index;
	IMG_UINT32 *pui32Tmp;
	IMG_UINT32 ui32MMUFlags = 0;
	MMU_PT_INFO **ppsPTInfoList;

	/* check the physical alignment of the memory to map */
	PVR_ASSERT((DevPAddr.uiAddr & pMMUHeap->ui32DataPageMask) == 0);

	/*
		unravel the read/write/cache flags
	*/
	if(((PVRSRV_MEM_READ|PVRSRV_MEM_WRITE) & ui32MemFlags) == (PVRSRV_MEM_READ|PVRSRV_MEM_WRITE))
	{
		/* read/write */
		ui32MMUFlags = 0;
	}
	else if(PVRSRV_MEM_READ & ui32MemFlags)
	{
		/* read only */
		ui32MMUFlags |= SGX_MMU_PTE_READONLY;
	}
	else if(PVRSRV_MEM_WRITE & ui32MemFlags)
	{
		/* write only */
		ui32MMUFlags |= SGX_MMU_PTE_WRITEONLY;
	}

	/* cache coherency */
	if(PVRSRV_MEM_CACHE_CONSISTENT & ui32MemFlags)
	{
		ui32MMUFlags |= SGX_MMU_PTE_CACHECONSISTENT;
	}

#if !defined(FIX_HW_BRN_25503)
	/* EDM protection */
	if(PVRSRV_MEM_EDM_PROTECT & ui32MemFlags)
	{
		ui32MMUFlags |= SGX_MMU_PTE_EDMPROTECT;
	}
#endif

	/*
		we receive a device physical address for the page that is to be mapped
		and a device virtual address representing where it should be mapped to
	*/

	/* find the index/offset in PD entries  */
	ui32Index = DevVAddr.uiAddr >> pMMUHeap->ui32PDShift;

	/* and advance to the first PT info list */
	ppsPTInfoList = &pMMUHeap->psMMUContext->apsPTInfoList[ui32Index];

	CheckPT(ppsPTInfoList[0]);

	/* find the index/offset of the first PT in the first PT page */
	ui32Index = (DevVAddr.uiAddr & pMMUHeap->ui32PTMask) >> pMMUHeap->ui32PTShift;

	/* setup pointer to the first entry in the PT page */
	pui32Tmp = (IMG_UINT32*)ppsPTInfoList[0]->PTPageCpuVAddr;

#if !defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
	{
		IMG_UINT32 uTmp = pui32Tmp[ui32Index];
		
		/* Is the current page already valid? (should not be unless it was allocated and not deallocated) */
#if defined(FIX_HW_BRN_31620)
		if ((uTmp & SGX_MMU_PTE_VALID) && ((DevVAddr.uiAddr & BRN31620_PDE_CACHE_FILL_MASK) != BRN31620_DUMMY_PAGE_OFFSET))
#else
 		if ((uTmp & SGX_MMU_PTE_VALID) != 0)
#endif

		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_MapPage: Page is already valid for alloc at VAddr:0x%08X PDIdx:%u PTIdx:%u",
									DevVAddr.uiAddr,
									DevVAddr.uiAddr >> pMMUHeap->ui32PDShift,
									ui32Index ));
			PVR_DPF((PVR_DBG_ERROR, "MMU_MapPage: Page table entry value: 0x%08X", uTmp));

			PVR_DPF((PVR_DBG_ERROR, "MMU_MapPage: Physical page to map: 0x" DEVPADDR_FMT,
						DevPAddr.uiAddr));

#if PT_DUMP
			DumpPT(ppsPTInfoList[0]);
#endif
		}
#if !defined(FIX_HW_BRN_31620)
		PVR_ASSERT((uTmp & SGX_MMU_PTE_VALID) == 0);
#endif
	}
#endif

	/* One more valid entry in the page table. */
	ppsPTInfoList[0]->ui32ValidPTECount++;

	MakeKernelPageReadWrite(ppsPTInfoList[0]->PTPageCpuVAddr);
	/* map in the physical page */
	pui32Tmp[ui32Index] = ((IMG_UINT32)(DevPAddr.uiAddr>>SGX_MMU_PTE_ADDR_ALIGNSHIFT)
						& ((~pMMUHeap->ui32DataPageMask)>>SGX_MMU_PTE_ADDR_ALIGNSHIFT))
						| SGX_MMU_PTE_VALID
						| ui32MMUFlags;
	MakeKernelPageReadOnly(ppsPTInfoList[0]->PTPageCpuVAddr);
	CheckPT(ppsPTInfoList[0]);
}


/*!
******************************************************************************
	FUNCTION:   MMU_MapScatter

	PURPOSE:    Create a linear mapping for a range of pages at a specified
	            virtual address.

	PARAMETERS: In:  pMMUHeap - the mmu.
	            In:  DevVAddr - the device virtual address.
	            In:  psSysAddr - the device physical address of the page to
	                 map.
	            In:  uSize - size of memory range in bytes
                In:  ui32MemFlags - page table flags.
	            In:  hUniqueTag - A unique ID for use as a tag identifier
	RETURNS:    None
******************************************************************************/
IMG_VOID
MMU_MapScatter (MMU_HEAP *pMMUHeap,
				IMG_DEV_VIRTADDR DevVAddr,
				IMG_SYS_PHYADDR *psSysAddr,
				IMG_SIZE_T uSize,
				IMG_UINT32 ui32MemFlags,
				IMG_HANDLE hUniqueTag)
{
#if defined(PDUMP)
	IMG_DEV_VIRTADDR MapBaseDevVAddr;
#endif /*PDUMP*/
	IMG_UINT32 uCount, i;
	IMG_DEV_PHYADDR DevPAddr;

	PVR_ASSERT (pMMUHeap != IMG_NULL);

#if defined(PDUMP)
	MapBaseDevVAddr = DevVAddr;
#else
	PVR_UNREFERENCED_PARAMETER(hUniqueTag);
#endif /*PDUMP*/

	for (i=0, uCount=0; uCount<uSize; i++, uCount+=pMMUHeap->ui32DataPageSize)
	{
		IMG_SYS_PHYADDR sSysAddr;

		sSysAddr = psSysAddr[i];


		/* check the physical alignment of the memory to map */
		PVR_ASSERT((sSysAddr.uiAddr & pMMUHeap->ui32DataPageMask) == 0);

		DevPAddr = SysSysPAddrToDevPAddr(PVRSRV_DEVICE_TYPE_SGX, sSysAddr);

		MMU_MapPage (pMMUHeap, DevVAddr, DevPAddr, ui32MemFlags);
		DevVAddr.uiAddr += pMMUHeap->ui32DataPageSize;

		PVR_DPF ((PVR_DBG_MESSAGE,
				 "MMU_MapScatter: devVAddr=%x, SysAddr=" SYSPADDR_FMT ", size=0x%x/0x%x",
				  DevVAddr.uiAddr, sSysAddr.uiAddr, uCount, uSize));
	}

#if (SGX_FEATURE_PT_CACHE_ENTRIES_PER_LINE > 1)
	MMU_InvalidatePageTableCache(pMMUHeap->psMMUContext->psDevInfo);
#endif

#if defined(PDUMP)
	MMU_PDumpPageTables (pMMUHeap, MapBaseDevVAddr, uSize, IMG_FALSE, hUniqueTag);
#endif /* #if defined(PDUMP) */
}

/*!
******************************************************************************
	FUNCTION:   MMU_MapPages

	PURPOSE:    Create a linear mapping for a ranege of pages at a specified
	            virtual address.

	PARAMETERS: In:  pMMUHeap - the mmu.
	            In:  DevVAddr - the device virtual address.
	            In:  SysPAddr - the system physical address of the page to
	                 map.
	            In:  uSize - size of memory range in bytes
                In:  ui32MemFlags - page table flags.
	            In:  hUniqueTag - A unique ID for use as a tag identifier
	RETURNS:    None
******************************************************************************/
IMG_VOID
MMU_MapPages (MMU_HEAP *pMMUHeap,
			  IMG_DEV_VIRTADDR DevVAddr,
			  IMG_SYS_PHYADDR SysPAddr,
			  IMG_SIZE_T uSize,
			  IMG_UINT32 ui32MemFlags,
			  IMG_HANDLE hUniqueTag)
{
	IMG_DEV_PHYADDR DevPAddr;
#if defined(PDUMP)
	IMG_DEV_VIRTADDR MapBaseDevVAddr;
#endif /*PDUMP*/
	IMG_UINT32 uCount;
	IMG_UINT32 ui32VAdvance;
	IMG_UINT32 ui32PAdvance;

	PVR_ASSERT (pMMUHeap != IMG_NULL);

	PVR_DPF ((PVR_DBG_MESSAGE, "MMU_MapPages: heap:%s, heap_id:%d devVAddr=%08X, SysPAddr=" SYSPADDR_FMT ", size=0x%x",
								pMMUHeap->psDevArena->pszName,
								pMMUHeap->psDevArena->ui32HeapID,
								DevVAddr.uiAddr, 
								SysPAddr.uiAddr,
								uSize));

	/* set the virtual and physical advance */
	ui32VAdvance = pMMUHeap->ui32DataPageSize;
	ui32PAdvance = pMMUHeap->ui32DataPageSize;

#if defined(PDUMP)
	MapBaseDevVAddr = DevVAddr;
#else
	PVR_UNREFERENCED_PARAMETER(hUniqueTag);
#endif /*PDUMP*/

	DevPAddr = SysSysPAddrToDevPAddr(PVRSRV_DEVICE_TYPE_SGX, SysPAddr);

	/* check the physical alignment of the memory to map */
	PVR_ASSERT((DevPAddr.uiAddr & pMMUHeap->ui32DataPageMask) == 0);

	/*
		for dummy allocations there is only one physical
		page backing the virtual range
	*/
	if(ui32MemFlags & PVRSRV_MEM_DUMMY)
	{
		ui32PAdvance = 0;
	}

	for (uCount=0; uCount<uSize; uCount+=ui32VAdvance)
	{
		MMU_MapPage (pMMUHeap, DevVAddr, DevPAddr, ui32MemFlags);
		DevVAddr.uiAddr += ui32VAdvance;
		DevPAddr.uiAddr += ui32PAdvance;
	}

#if (SGX_FEATURE_PT_CACHE_ENTRIES_PER_LINE > 1)
	MMU_InvalidatePageTableCache(pMMUHeap->psMMUContext->psDevInfo);
#endif

#if defined(PDUMP)
	MMU_PDumpPageTables (pMMUHeap, MapBaseDevVAddr, uSize, IMG_FALSE, hUniqueTag);
#endif /* #if defined(PDUMP) */
}


/*!
******************************************************************************
	FUNCTION:   MMU_MapPagesSparse

	PURPOSE:    Create a linear mapping for a ranege of pages at a specified
	            virtual address.

	PARAMETERS: In:  pMMUHeap - the mmu.
	            In:  DevVAddr - the device virtual address.
	            In:  SysPAddr - the system physical address of the page to
	                 map.
				In:  ui32ChunkSize - Size of the chunk (must be page multiple)
				In:  ui32NumVirtChunks - Number of virtual chunks
				In:  ui32NumPhysChunks - Number of physical chunks
				In:  pabMapChunk - Mapping array
                In:  ui32MemFlags - page table flags.
	            In:  hUniqueTag - A unique ID for use as a tag identifier
	RETURNS:    None
******************************************************************************/
IMG_VOID
MMU_MapPagesSparse (MMU_HEAP *pMMUHeap,
					IMG_DEV_VIRTADDR DevVAddr,
					IMG_SYS_PHYADDR SysPAddr,
					IMG_UINT32 ui32ChunkSize,
					IMG_UINT32 ui32NumVirtChunks,
					IMG_UINT32 ui32NumPhysChunks,
					IMG_BOOL *pabMapChunk,
					IMG_UINT32 ui32MemFlags,
					IMG_HANDLE hUniqueTag)
{
	IMG_DEV_PHYADDR DevPAddr;
#if defined(PDUMP)
	IMG_DEV_VIRTADDR MapBaseDevVAddr;
#endif /*PDUMP*/
	IMG_UINT32 uCount;
	IMG_UINT32 ui32VAdvance;
	IMG_UINT32 ui32PAdvance;
	IMG_SIZE_T uSizeVM = ui32ChunkSize * ui32NumVirtChunks;
#if !defined(PVRSRV_NEED_PVR_DPF)
	PVR_UNREFERENCED_PARAMETER(ui32NumPhysChunks);
#endif

	PVR_ASSERT (pMMUHeap != IMG_NULL);

	PVR_DPF ((PVR_DBG_MESSAGE, "MMU_MapPagesSparse: heap:%s, heap_id:%d devVAddr=%08X, SysPAddr=" SYSPADDR_FMT ", VM space=0x%x, PHYS space=0x%x",
								pMMUHeap->psDevArena->pszName,
								pMMUHeap->psDevArena->ui32HeapID,
								DevVAddr.uiAddr, 
								SysPAddr.uiAddr,
								uSizeVM,
								ui32ChunkSize * ui32NumPhysChunks));

	/* set the virtual and physical advance */
	ui32VAdvance = pMMUHeap->ui32DataPageSize;
	ui32PAdvance = pMMUHeap->ui32DataPageSize;

#if defined(PDUMP)
	MapBaseDevVAddr = DevVAddr;
#else
	PVR_UNREFERENCED_PARAMETER(hUniqueTag);
#endif /*PDUMP*/

	DevPAddr = SysSysPAddrToDevPAddr(PVRSRV_DEVICE_TYPE_SGX, SysPAddr);

	/* check the physical alignment of the memory to map */
	PVR_ASSERT((DevPAddr.uiAddr & pMMUHeap->ui32DataPageMask) == 0);

	/*
		for dummy allocations there is only one physical
		page backing the virtual range
	*/
	if(ui32MemFlags & PVRSRV_MEM_DUMMY)
	{
		ui32PAdvance = 0;
	}

	for (uCount=0; uCount<uSizeVM; uCount+=ui32VAdvance)
	{
		if (pabMapChunk[uCount/ui32ChunkSize])
		{
			MMU_MapPage (pMMUHeap, DevVAddr, DevPAddr, ui32MemFlags);
			DevPAddr.uiAddr += ui32PAdvance;
		}
		DevVAddr.uiAddr += ui32VAdvance;
	}
	pMMUHeap->bHasSparseMappings = IMG_TRUE;

#if (SGX_FEATURE_PT_CACHE_ENTRIES_PER_LINE > 1)
	MMU_InvalidatePageTableCache(pMMUHeap->psMMUContext->psDevInfo);
#endif

#if defined(PDUMP)
	MMU_PDumpPageTables (pMMUHeap, MapBaseDevVAddr, uSizeVM, IMG_FALSE, hUniqueTag);
#endif /* #if defined(PDUMP) */
}

/*!
******************************************************************************
	FUNCTION:   MMU_MapShadow

	PURPOSE:    Create a mapping for a range of pages from either a CPU
				virtual adddress, (or if NULL a hOSMemHandle) to a specified
				device virtual address.

	PARAMETERS: In:  pMMUHeap - the mmu.
                In:  MapBaseDevVAddr - A page aligned device virtual address
                                       to start mapping from.
                In:  uByteSize - A page aligned mapping length in bytes.
                In:  CpuVAddr - A page aligned CPU virtual address.
                In:  hOSMemHandle - An alternative OS specific memory handle
                                    for mapping RAM without a CPU virtual
                                    address
                Out: pDevVAddr - deprecated - It used to return a byte aligned
                                 device virtual address corresponding to the
                                 cpu virtual address (When CpuVAddr wasn't
                                 constrained to be page aligned.) Now it just
                                 returns MapBaseDevVAddr. Unaligned semantics
                                 can easily be handled above this API if required.
                In: hUniqueTag - A unique ID for use as a tag identifier
                In: ui32MemFlags - page table flags.
	RETURNS:    None
******************************************************************************/
IMG_VOID
MMU_MapShadow (MMU_HEAP          *pMMUHeap,
			   IMG_DEV_VIRTADDR   MapBaseDevVAddr,
			   IMG_SIZE_T         uByteSize,
			   IMG_CPU_VIRTADDR   CpuVAddr,
			   IMG_HANDLE         hOSMemHandle,
			   IMG_DEV_VIRTADDR  *pDevVAddr,
			   IMG_UINT32         ui32MemFlags,
			   IMG_HANDLE         hUniqueTag)
{
	IMG_UINT32			i;
	IMG_UINT32			uOffset = 0;
	IMG_DEV_VIRTADDR	MapDevVAddr;
	IMG_UINT32			ui32VAdvance;
	IMG_UINT32			ui32PAdvance;

#if !defined (PDUMP)
	PVR_UNREFERENCED_PARAMETER(hUniqueTag);
#endif

	PVR_DPF ((PVR_DBG_MESSAGE,
			"MMU_MapShadow: DevVAddr:%08X, Bytes:0x%x, CPUVAddr:%p",
			MapBaseDevVAddr.uiAddr,
			uByteSize,
			CpuVAddr));

	/* set the virtual and physical advance */
	ui32VAdvance = pMMUHeap->ui32DataPageSize;
	ui32PAdvance = pMMUHeap->ui32DataPageSize;

	/* note: can't do useful check on the CPU Addr other than it being at least 4k alignment */
	PVR_ASSERT(((IMG_UINTPTR_T)CpuVAddr & (SGX_MMU_PAGE_SIZE - 1)) == 0);
	PVR_ASSERT(((IMG_UINT32)uByteSize & pMMUHeap->ui32DataPageMask) == 0);
	pDevVAddr->uiAddr = MapBaseDevVAddr.uiAddr;

	/*
		for dummy allocations there is only one physical
		page backing the virtual range
	*/
	if(ui32MemFlags & PVRSRV_MEM_DUMMY)
	{
		ui32PAdvance = 0;
	}

	/* Loop through cpu memory and map page by page */
	MapDevVAddr = MapBaseDevVAddr;
	for (i=0; i<uByteSize; i+=ui32VAdvance)
	{
		IMG_CPU_PHYADDR CpuPAddr;
		IMG_DEV_PHYADDR DevPAddr;

		if(CpuVAddr)
		{
			CpuPAddr = OSMapLinToCPUPhys (hOSMemHandle,
										  (IMG_VOID *)((IMG_UINTPTR_T)CpuVAddr + uOffset));
		}
		else
		{
			CpuPAddr = OSMemHandleToCpuPAddr(hOSMemHandle, uOffset);
		}
		DevPAddr = SysCpuPAddrToDevPAddr (PVRSRV_DEVICE_TYPE_SGX, CpuPAddr);

		/* check the physical alignment of the memory to map */
		PVR_ASSERT((DevPAddr.uiAddr & pMMUHeap->ui32DataPageMask) == 0);

		PVR_DPF ((PVR_DBG_MESSAGE,
				"Offset=0x%x: CpuVAddr=%p, CpuPAddr=" CPUPADDR_FMT ", DevVAddr=%08X, DevPAddr=" DEVPADDR_FMT,
				uOffset,
				(IMG_PVOID)((IMG_UINTPTR_T)CpuVAddr + uOffset),
				CpuPAddr.uiAddr,
				MapDevVAddr.uiAddr,
				DevPAddr.uiAddr));

		MMU_MapPage (pMMUHeap, MapDevVAddr, DevPAddr, ui32MemFlags);

		/* loop update */
		MapDevVAddr.uiAddr += ui32VAdvance;
		uOffset += ui32PAdvance;
	}

#if (SGX_FEATURE_PT_CACHE_ENTRIES_PER_LINE > 1)
	MMU_InvalidatePageTableCache(pMMUHeap->psMMUContext->psDevInfo);
#endif

#if defined(PDUMP)
	MMU_PDumpPageTables (pMMUHeap, MapBaseDevVAddr, uByteSize, IMG_FALSE, hUniqueTag);
#endif /* #if defined(PDUMP) */
}

/*!
******************************************************************************
	FUNCTION:   MMU_MapShadowSparse

	PURPOSE:    Create a mapping for a range of pages from either a CPU
				virtual adddress, (or if NULL a hOSMemHandle) to a specified
				device virtual address.

	PARAMETERS: In:  pMMUHeap - the mmu.
                In:  MapBaseDevVAddr - A page aligned device virtual address
                                       to start mapping from.
				In:  ui32ChunkSize - Size of the chunk (must be page multiple)
				In:  ui32NumVirtChunks - Number of virtual chunks
				In:  ui32NumPhysChunks - Number of physical chunks
				In:  pabMapChunk - Mapping array
                In:  CpuVAddr - A page aligned CPU virtual address.
                In:  hOSMemHandle - An alternative OS specific memory handle
                                    for mapping RAM without a CPU virtual
                                    address
                Out: pDevVAddr - deprecated - It used to return a byte aligned
                                 device virtual address corresponding to the
                                 cpu virtual address (When CpuVAddr wasn't
                                 constrained to be page aligned.) Now it just
                                 returns MapBaseDevVAddr. Unaligned semantics
                                 can easily be handled above this API if required.
                In: hUniqueTag - A unique ID for use as a tag identifier
                In: ui32MemFlags - page table flags.
	RETURNS:    None
******************************************************************************/
IMG_VOID
MMU_MapShadowSparse (MMU_HEAP          *pMMUHeap,
					 IMG_DEV_VIRTADDR   MapBaseDevVAddr,
					 IMG_UINT32         ui32ChunkSize,
					 IMG_UINT32         ui32NumVirtChunks,
					 IMG_UINT32         ui32NumPhysChunks,
					 IMG_BOOL          *pabMapChunk,
					 IMG_CPU_VIRTADDR   CpuVAddr,
					 IMG_HANDLE         hOSMemHandle,
					 IMG_DEV_VIRTADDR  *pDevVAddr,
					 IMG_UINT32         ui32MemFlags,
					 IMG_HANDLE         hUniqueTag)
{
	IMG_UINT32			i;
	IMG_UINT32			uOffset = 0;
	IMG_DEV_VIRTADDR	MapDevVAddr;
	IMG_UINT32			ui32VAdvance;
	IMG_UINT32			ui32PAdvance;
	IMG_SIZE_T			uiSizeVM = ui32ChunkSize * ui32NumVirtChunks;
	IMG_UINT32			ui32ChunkIndex = 0;
	IMG_UINT32			ui32ChunkOffset = 0;
#if !defined(PVRSRV_NEED_PVR_DPF)
	PVR_UNREFERENCED_PARAMETER(ui32NumPhysChunks);
#endif
#if !defined (PDUMP)
	PVR_UNREFERENCED_PARAMETER(hUniqueTag);
#endif

	PVR_DPF ((PVR_DBG_MESSAGE,
			"MMU_MapShadowSparse: DevVAddr:%08X, VM space:0x%x, CPUVAddr:%p PHYS space:0x%x",
			MapBaseDevVAddr.uiAddr,
			uiSizeVM,
			CpuVAddr,
			ui32ChunkSize * ui32NumPhysChunks));

	/* set the virtual and physical advance */
	ui32VAdvance = pMMUHeap->ui32DataPageSize;
	ui32PAdvance = pMMUHeap->ui32DataPageSize;

	/* note: can't do useful check on the CPU Addr other than it being at least 4k alignment */
	PVR_ASSERT(((IMG_UINTPTR_T)CpuVAddr & (SGX_MMU_PAGE_SIZE - 1)) == 0);
	PVR_ASSERT(((IMG_UINT32)uiSizeVM & pMMUHeap->ui32DataPageMask) == 0);
	pDevVAddr->uiAddr = MapBaseDevVAddr.uiAddr;

	/* Shouldn't come through the sparse interface */
	PVR_ASSERT((ui32MemFlags & PVRSRV_MEM_DUMMY) == 0);

	/* Loop through cpu memory and map page by page */
	MapDevVAddr = MapBaseDevVAddr;
	for (i=0; i<uiSizeVM; i+=ui32VAdvance)
	{
		IMG_CPU_PHYADDR CpuPAddr;
		IMG_DEV_PHYADDR DevPAddr;

		if (pabMapChunk[i/ui32ChunkSize])
		/*if (pabMapChunk[ui32ChunkIndex])*/
		{
			if(CpuVAddr)
			{
				CpuPAddr = OSMapLinToCPUPhys (hOSMemHandle,
											  (IMG_VOID *)((IMG_UINTPTR_T)CpuVAddr + uOffset));
			}
			else
			{
				CpuPAddr = OSMemHandleToCpuPAddr(hOSMemHandle, uOffset);
			}
			DevPAddr = SysCpuPAddrToDevPAddr (PVRSRV_DEVICE_TYPE_SGX, CpuPAddr);
	
			/* check the physical alignment of the memory to map */
			PVR_ASSERT((DevPAddr.uiAddr & pMMUHeap->ui32DataPageMask) == 0);
	
			PVR_DPF ((PVR_DBG_MESSAGE,
					"Offset=0x%x: CpuVAddr=%p, CpuPAddr=" CPUPADDR_FMT ", DevVAddr=%08X, DevPAddr=" DEVPADDR_FMT,
					uOffset,
					(void *)((IMG_UINTPTR_T)CpuVAddr + uOffset),
					CpuPAddr.uiAddr,
					MapDevVAddr.uiAddr,
					DevPAddr.uiAddr));
	
			MMU_MapPage (pMMUHeap, MapDevVAddr, DevPAddr, ui32MemFlags);
			uOffset += ui32PAdvance;
		}

		/* loop update */
		MapDevVAddr.uiAddr += ui32VAdvance;

		if (ui32ChunkOffset == ui32ChunkSize)
		{
			ui32ChunkIndex++;
			ui32ChunkOffset = 0;
		}
	}

	pMMUHeap->bHasSparseMappings = IMG_TRUE;

#if (SGX_FEATURE_PT_CACHE_ENTRIES_PER_LINE > 1)
	MMU_InvalidatePageTableCache(pMMUHeap->psMMUContext->psDevInfo);
#endif

#if defined(PDUMP)
	MMU_PDumpPageTables (pMMUHeap, MapBaseDevVAddr, uiSizeVM, IMG_FALSE, hUniqueTag);
#endif /* #if defined(PDUMP) */
}

/*!
******************************************************************************
	FUNCTION:   MMU_UnmapPages

	PURPOSE:    unmap pages and invalidate virtual address

	PARAMETERS:	In:  psMMUHeap - the mmu.
	            In:  sDevVAddr - the device virtual address.
	            In:  ui32PageCount - page count
	            In:  hUniqueTag - A unique ID for use as a tag identifier

	RETURNS:	None
******************************************************************************/
IMG_VOID
MMU_UnmapPages (MMU_HEAP *psMMUHeap,
				IMG_DEV_VIRTADDR sDevVAddr,
				IMG_UINT32 ui32PageCount,
				IMG_HANDLE hUniqueTag)
{
	IMG_UINT32			uPageSize = psMMUHeap->ui32DataPageSize;
	IMG_DEV_VIRTADDR	sTmpDevVAddr;
	IMG_UINT32			i;
	IMG_UINT32			ui32PDIndex;
	IMG_UINT32			ui32PTIndex;
	IMG_UINT32			*pui32Tmp;

#if !defined (PDUMP)
	PVR_UNREFERENCED_PARAMETER(hUniqueTag);
#endif

	/* setup tmp devvaddr to base of allocation */
	sTmpDevVAddr = sDevVAddr;

	for(i=0; i<ui32PageCount; i++)
	{
		MMU_PT_INFO **ppsPTInfoList;

		/* find the index/offset in PD entries  */
		ui32PDIndex = sTmpDevVAddr.uiAddr >> psMMUHeap->ui32PDShift;

		/* and advance to the first PT info list */
		ppsPTInfoList = &psMMUHeap->psMMUContext->apsPTInfoList[ui32PDIndex];

		/* find the index/offset of the first PT in the first PT page */
		ui32PTIndex = (sTmpDevVAddr.uiAddr & psMMUHeap->ui32PTMask) >> psMMUHeap->ui32PTShift;

		/* Is the PT page valid? */
		if ((!ppsPTInfoList[0]) && (!psMMUHeap->bHasSparseMappings))
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_UnmapPages: ERROR Invalid PT for alloc at VAddr:0x%08X (VaddrIni:0x%08X AllocPage:%u) PDIdx:%u PTIdx:%u",
									sTmpDevVAddr.uiAddr,
									sDevVAddr.uiAddr,
									i,
									ui32PDIndex,
									ui32PTIndex));

			/* advance the sTmpDevVAddr by one page */
			sTmpDevVAddr.uiAddr += uPageSize;

			/* Try to unmap the remaining allocation pages */
			continue;
		}

		CheckPT(ppsPTInfoList[0]);

		/* setup pointer to the first entry in the PT page */
		pui32Tmp = (IMG_UINT32*)ppsPTInfoList[0]->PTPageCpuVAddr;

		/* Decrement the valid page count only if the current page is valid*/
		if (pui32Tmp[ui32PTIndex] & SGX_MMU_PTE_VALID)
		{
			ppsPTInfoList[0]->ui32ValidPTECount--;
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_UnmapPages: Page is already invalid for alloc at VAddr:0x%08X (VAddrIni:0x%08X AllocPage:%u) PDIdx:%u PTIdx:%u",
									sTmpDevVAddr.uiAddr,
									sDevVAddr.uiAddr,
									i,
									ui32PDIndex,
									ui32PTIndex));
			PVR_DPF((PVR_DBG_ERROR, "MMU_UnmapPages: Page table entry value: 0x%08X", pui32Tmp[ui32PTIndex]));
		}

		/* The page table count should not go below zero */
		PVR_ASSERT((IMG_INT32)ppsPTInfoList[0]->ui32ValidPTECount >= 0);

		MakeKernelPageReadWrite(ppsPTInfoList[0]->PTPageCpuVAddr);
#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
		/* point the PT entry to the dummy data page */
		pui32Tmp[ui32PTIndex] = (psMMUHeap->psMMUContext->psDevInfo->sDummyDataDevPAddr.uiAddr>>SGX_MMU_PTE_ADDR_ALIGNSHIFT)
								| SGX_MMU_PTE_VALID;
#else
		/* invalidate entry */
#if defined(FIX_HW_BRN_31620)
		BRN31620InvalidatePageTableEntry(psMMUHeap->psMMUContext, ui32PDIndex, ui32PTIndex, &pui32Tmp[ui32PTIndex]);
#else
		pui32Tmp[ui32PTIndex] = 0;
#endif
#endif
		MakeKernelPageReadOnly(ppsPTInfoList[0]->PTPageCpuVAddr);

		CheckPT(ppsPTInfoList[0]);

		/* advance the sTmpDevVAddr by one page */
		sTmpDevVAddr.uiAddr += uPageSize;
	}

	MMU_InvalidatePageTableCache(psMMUHeap->psMMUContext->psDevInfo);

#if defined(PDUMP)
	MMU_PDumpPageTables (psMMUHeap, sDevVAddr, uPageSize*ui32PageCount, IMG_TRUE, hUniqueTag);
#endif /* #if defined(PDUMP) */
}


/*!
******************************************************************************
	FUNCTION:   MMU_GetPhysPageAddr

	PURPOSE:    extracts physical address from MMU page tables

	PARAMETERS: In:  pMMUHeap - the mmu
	PARAMETERS: In:  sDevVPageAddr - the virtual address to extract physical
					page mapping from
	RETURNS:    None
******************************************************************************/
IMG_DEV_PHYADDR
MMU_GetPhysPageAddr(MMU_HEAP *pMMUHeap, IMG_DEV_VIRTADDR sDevVPageAddr)
{
	IMG_UINT32 *pui32PageTable;
	IMG_UINT32 ui32Index;
	IMG_DEV_PHYADDR sDevPAddr;
	MMU_PT_INFO **ppsPTInfoList;

	/* find the index/offset in PD entries  */
	ui32Index = sDevVPageAddr.uiAddr >> pMMUHeap->ui32PDShift;

	/* and advance to the first PT info list */
	ppsPTInfoList = &pMMUHeap->psMMUContext->apsPTInfoList[ui32Index];
	if (!ppsPTInfoList[0])
	{
		/* Heaps with sparse mappings are allowed invalid pages */
		if (!pMMUHeap->bHasSparseMappings)
		{
			PVR_DPF((PVR_DBG_ERROR,"MMU_GetPhysPageAddr: Not mapped in at 0x%08x", sDevVPageAddr.uiAddr));
		}
		sDevPAddr.uiAddr = 0;
		return sDevPAddr;
	}

	/* find the index/offset of the first PT in the first PT page */
	ui32Index = (sDevVPageAddr.uiAddr & pMMUHeap->ui32PTMask) >> pMMUHeap->ui32PTShift;

	/* setup pointer to the first entry in the PT page */
	pui32PageTable = (IMG_UINT32*)ppsPTInfoList[0]->PTPageCpuVAddr;

	/* read back physical page */
	sDevPAddr.uiAddr = pui32PageTable[ui32Index];

	/* Mask off non-address bits */
	sDevPAddr.uiAddr &= ~(pMMUHeap->ui32DataPageMask>>SGX_MMU_PTE_ADDR_ALIGNSHIFT);

	/* and align the address */
	sDevPAddr.uiAddr <<= SGX_MMU_PTE_ADDR_ALIGNSHIFT;

	return sDevPAddr;
}


IMG_DEV_PHYADDR MMU_GetPDDevPAddr(MMU_CONTEXT *pMMUContext)
{
	return (pMMUContext->sPDDevPAddr);
}


/*!
******************************************************************************
	FUNCTION:   SGXGetPhysPageAddr

	PURPOSE:    Gets DEV and CPU physical address of sDevVAddr

	PARAMETERS: In:  hDevMemHeap - device mem heap handle
	PARAMETERS: In:  sDevVAddr - the base virtual address to unmap from
	PARAMETERS: Out: pDevPAddr - DEV physical address
	PARAMETERS: Out: pCpuPAddr - CPU physical address
	RETURNS:    None
******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR SGXGetPhysPageAddrKM (IMG_HANDLE hDevMemHeap,
								   IMG_DEV_VIRTADDR sDevVAddr,
								   IMG_DEV_PHYADDR *pDevPAddr,
								   IMG_CPU_PHYADDR *pCpuPAddr)
{
	MMU_HEAP *pMMUHeap;
	IMG_DEV_PHYADDR DevPAddr;

	/*
		Get MMU Heap From hDevMemHeap
	*/
	pMMUHeap = (MMU_HEAP*)BM_GetMMUHeap(hDevMemHeap);

	DevPAddr = MMU_GetPhysPageAddr(pMMUHeap, sDevVAddr);
	pCpuPAddr->uiAddr = DevPAddr.uiAddr; /* SysDevPAddrToCPUPAddr(DevPAddr) */
	pDevPAddr->uiAddr = DevPAddr.uiAddr;

	return (pDevPAddr->uiAddr != 0) ? PVRSRV_OK : PVRSRV_ERROR_INVALID_PARAMS;
}


/*!
******************************************************************************
    FUNCTION:   SGXGetMMUPDAddrKM

    PURPOSE:    Gets PD device physical address of hDevMemContext

    PARAMETERS: In:  hDevCookie - device cookie
	PARAMETERS: In:  hDevMemContext - memory context
	PARAMETERS: Out: psPDDevPAddr - MMU PD address
    RETURNS:    None
******************************************************************************/
PVRSRV_ERROR SGXGetMMUPDAddrKM(IMG_HANDLE		hDevCookie,
								IMG_HANDLE 		hDevMemContext,
								IMG_DEV_PHYADDR *psPDDevPAddr)
{
	if (!hDevCookie || !hDevMemContext || !psPDDevPAddr)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* return the address */
	*psPDDevPAddr = ((BM_CONTEXT*)hDevMemContext)->psMMUContext->sPDDevPAddr;

	return PVRSRV_OK;
}

/*!
******************************************************************************
	FUNCTION:   MMU_BIFResetPDAlloc

	PURPOSE:    Allocate a dummy Page Directory, Page Table and Page which can
				be used for dynamic dummy page mapping during SGX reset.
				Note: since this is only used for hardware recovery, no
				pdumping is performed.

	PARAMETERS: In:  psDevInfo - device info
	RETURNS:    PVRSRV_OK or error
******************************************************************************/
PVRSRV_ERROR MMU_BIFResetPDAlloc(PVRSRV_SGXDEV_INFO *psDevInfo)
{
	PVRSRV_ERROR eError;
	SYS_DATA *psSysData;
	RA_ARENA *psLocalDevMemArena;
	IMG_HANDLE hOSMemHandle = IMG_NULL;
	IMG_BYTE *pui8MemBlock = IMG_NULL;
	IMG_SYS_PHYADDR sMemBlockSysPAddr;
	IMG_CPU_PHYADDR sMemBlockCpuPAddr;

	SysAcquireData(&psSysData);

	psLocalDevMemArena = psSysData->apsLocalDevMemArena[0];

	/* allocate 3 pages - for the PD, PT and dummy page */
	if(psLocalDevMemArena == IMG_NULL)
	{
		/* UMA system */
		eError = OSAllocPages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
						      3 * SGX_MMU_PAGE_SIZE,
						      SGX_MMU_PAGE_SIZE,
							  IMG_NULL,
							  0,
							  IMG_NULL,
						      (IMG_VOID **)&pui8MemBlock,
						      &hOSMemHandle);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_BIFResetPDAlloc: ERROR call to OSAllocPages failed"));
			return eError;
		}

		/* translate address to device physical */
		if(pui8MemBlock)
		{
			sMemBlockCpuPAddr = OSMapLinToCPUPhys(hOSMemHandle,
												  pui8MemBlock);
		}
		else
		{
			/* This isn't used in all cases since not all ports currently support
			 * OSMemHandleToCpuPAddr() */
			sMemBlockCpuPAddr = OSMemHandleToCpuPAddr(hOSMemHandle, 0);
		}
	}
	else
	{
		/* non-UMA system */

		/* 
		   We cannot use IMG_SYS_PHYADDR here, as that is 64-bit for 32-bit PAE builds.
		   The physical address in this call to RA_Alloc is specifically the SysPAddr 
		   of local (card) space, and it is highly unlikely we would ever need to 
		   support > 4GB of local (card) memory (this does assume that such local
		   memory will be mapped into System physical memory space at a low address so
		   that any and all local memory exists within the 4GB SYSPAddr range).
		 */
		IMG_UINTPTR_T uiLocalPAddr;

		if(RA_Alloc(psLocalDevMemArena,
					3 * SGX_MMU_PAGE_SIZE,
					IMG_NULL,
					IMG_NULL,
					0,
					SGX_MMU_PAGE_SIZE,
					0,
					IMG_NULL,
					0,
					&uiLocalPAddr) != IMG_TRUE)
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_BIFResetPDAlloc: ERROR call to RA_Alloc failed"));
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}

		/* Munge the local PAddr back into the SysPAddr */
		sMemBlockSysPAddr.uiAddr = uiLocalPAddr;

		/* derive the CPU virtual address */
		sMemBlockCpuPAddr = SysSysPAddrToCpuPAddr(sMemBlockSysPAddr);
		pui8MemBlock = OSMapPhysToLin(sMemBlockCpuPAddr,
									  SGX_MMU_PAGE_SIZE * 3,
									  PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
									  &hOSMemHandle);
		if(!pui8MemBlock)
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_BIFResetPDAlloc: ERROR failed to map page tables"));
			return PVRSRV_ERROR_BAD_MAPPING;
		}
	}

	psDevInfo->hBIFResetPDOSMemHandle = hOSMemHandle;
	psDevInfo->sBIFResetPDDevPAddr = SysCpuPAddrToDevPAddr(PVRSRV_DEVICE_TYPE_SGX, sMemBlockCpuPAddr);
	psDevInfo->sBIFResetPTDevPAddr.uiAddr = psDevInfo->sBIFResetPDDevPAddr.uiAddr + SGX_MMU_PAGE_SIZE;
	psDevInfo->sBIFResetPageDevPAddr.uiAddr = psDevInfo->sBIFResetPTDevPAddr.uiAddr + SGX_MMU_PAGE_SIZE;
	/* override pointer cast warnings */
	/* PRQA S 3305,509 2 */
	psDevInfo->pui32BIFResetPD = (IMG_UINT32 *)pui8MemBlock;
	psDevInfo->pui32BIFResetPT = (IMG_UINT32 *)(pui8MemBlock + SGX_MMU_PAGE_SIZE);

	/* Invalidate entire PD and PT. */
	OSMemSet(psDevInfo->pui32BIFResetPD, 0, SGX_MMU_PAGE_SIZE);
	OSMemSet(psDevInfo->pui32BIFResetPT, 0, SGX_MMU_PAGE_SIZE);
	/* Fill dummy page with markers. */
	OSMemSet(pui8MemBlock + (2 * SGX_MMU_PAGE_SIZE), 0xDB, SGX_MMU_PAGE_SIZE);

	return PVRSRV_OK;
}

/*!
******************************************************************************
	FUNCTION:   MMU_BIFResetPDFree

	PURPOSE:    Free resources allocated in MMU_BIFResetPDAlloc.

	PARAMETERS: In:  psDevInfo - device info
	RETURNS:
******************************************************************************/
IMG_VOID MMU_BIFResetPDFree(PVRSRV_SGXDEV_INFO *psDevInfo)
{
	SYS_DATA *psSysData;
	RA_ARENA *psLocalDevMemArena;
	IMG_SYS_PHYADDR sPDSysPAddr;

	SysAcquireData(&psSysData);

	psLocalDevMemArena = psSysData->apsLocalDevMemArena[0];

	/* free the page directory */
	if(psLocalDevMemArena == IMG_NULL)
	{
		OSFreePages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
					3 * SGX_MMU_PAGE_SIZE,
					psDevInfo->pui32BIFResetPD,
					psDevInfo->hBIFResetPDOSMemHandle);
	}
	else
	{
		OSUnMapPhysToLin(psDevInfo->pui32BIFResetPD,
                         3 * SGX_MMU_PAGE_SIZE,
                         PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
                         psDevInfo->hBIFResetPDOSMemHandle);

		sPDSysPAddr = SysDevPAddrToSysPAddr(PVRSRV_DEVICE_TYPE_SGX, psDevInfo->sBIFResetPDDevPAddr);
		/* Note that the cast to IMG_UINTPTR_T is ok as we're local mem. */
		RA_Free(psLocalDevMemArena, (IMG_UINTPTR_T)sPDSysPAddr.uiAddr, IMG_FALSE);
	}
}

IMG_VOID MMU_CheckFaultAddr(PVRSRV_SGXDEV_INFO *psDevInfo, IMG_UINT32 ui32PDDevPAddr, IMG_UINT32 ui32FaultAddr)
{
	MMU_CONTEXT *psMMUContext = psDevInfo->pvMMUContextList;

	while (psMMUContext && (psMMUContext->sPDDevPAddr.uiAddr != ui32PDDevPAddr))
	{
		psMMUContext = psMMUContext->psNext;
	}

	if (psMMUContext)
	{
		IMG_UINT32 ui32PTIndex;
		IMG_UINT32 ui32PDIndex;

		PVR_LOG(("Found MMU context for page fault 0x%08x", ui32FaultAddr));
		PVR_LOG(("GPU memory context is for PID=%d (%s)", psMMUContext->ui32PID, psMMUContext->szName));

		ui32PTIndex = (ui32FaultAddr & SGX_MMU_PT_MASK) >> SGX_MMU_PAGE_SHIFT;
		ui32PDIndex = (ui32FaultAddr & SGX_MMU_PD_MASK) >> (SGX_MMU_PT_SHIFT + SGX_MMU_PAGE_SHIFT);

		if (psMMUContext->apsPTInfoList[ui32PDIndex])
		{
			if (psMMUContext->apsPTInfoList[ui32PDIndex]->PTPageCpuVAddr)
			{
				IMG_UINT32 *pui32Ptr = psMMUContext->apsPTInfoList[ui32PDIndex]->PTPageCpuVAddr;
				IMG_UINT32 ui32PTE = pui32Ptr[ui32PTIndex];

				PVR_LOG(("PDE valid: PTE = 0x%08x (PhysAddr = 0x%08x, %s)",
						  ui32PTE,
						  ui32PTE & SGX_MMU_PTE_ADDR_MASK,
						  ui32PTE & SGX_MMU_PTE_VALID?"valid":"Invalid"));
			}
			else
			{
				PVR_LOG(("Found PT info but no CPU address"));
			}
		}
		else
		{
			PVR_LOG(("No PDE found"));
		}
	}
}

#if defined(SUPPORT_EXTERNAL_SYSTEM_CACHE)
/*!
******************************************************************************
	FUNCTION:   MMU_MapExtSystemCacheRegs

	PURPOSE:    maps external system cache control registers into SGX MMU

	PARAMETERS: In:  psDeviceNode - device node
	RETURNS:
******************************************************************************/
PVRSRV_ERROR MMU_MapExtSystemCacheRegs(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	IMG_UINT32 *pui32PT;
	PVRSRV_SGXDEV_INFO *psDevInfo;
	IMG_UINT32 ui32PDIndex;
	IMG_UINT32 ui32PTIndex;
	PDUMP_MMU_ATTRIB sMMUAttrib;

	psDevInfo = (PVRSRV_SGXDEV_INFO*)psDeviceNode->pvDevice;

	sMMUAttrib = psDevInfo->sMMUAttrib;
#if defined(PDUMP)
	MMU_SetPDumpAttribs(&sMMUAttrib, psDeviceNode,
						SGX_MMU_PAGE_MASK,
						SGX_MMU_PT_SIZE * sizeof(IMG_UINT32));
#endif

#if defined(PDUMP)
	{
		IMG_CHAR		szScript[128];

		sprintf(szScript, "MALLOC :EXTSYSCACHE:PA_%08X%08X %u %u 0x%p\r\n", 0, psDevInfo->sExtSysCacheRegsDevPBase.uiAddr, SGX_MMU_PAGE_SIZE, SGX_MMU_PAGE_SIZE, psDevInfo->sExtSysCacheRegsDevPBase.uiAddr);
		PDumpOSWriteString2(szScript, PDUMP_FLAGS_CONTINUOUS);
	}
#endif

	ui32PDIndex = (SGX_EXT_SYSTEM_CACHE_REGS_DEVVADDR_BASE & SGX_MMU_PD_MASK) >> (SGX_MMU_PAGE_SHIFT + SGX_MMU_PT_SHIFT);
	ui32PTIndex = (SGX_EXT_SYSTEM_CACHE_REGS_DEVVADDR_BASE & SGX_MMU_PT_MASK) >> SGX_MMU_PAGE_SHIFT;

	pui32PT = (IMG_UINT32 *) psDeviceNode->sDevMemoryInfo.pBMKernelContext->psMMUContext->apsPTInfoList[ui32PDIndex]->PTPageCpuVAddr;

	MakeKernelPageReadWrite(pui32PT);
	/* map the PT to the registers */
	pui32PT[ui32PTIndex] = (psDevInfo->sExtSysCacheRegsDevPBase.uiAddr>>SGX_MMU_PTE_ADDR_ALIGNSHIFT)
							| SGX_MMU_PTE_VALID;
	MakeKernelPageReadOnly(pui32PT);
#if defined(PDUMP)
	/* Add the entery to the PT */
	{
		IMG_DEV_PHYADDR sDevPAddr;
		IMG_CPU_PHYADDR sCpuPAddr;
		IMG_UINT32 ui32PageMask;
		IMG_UINT32 ui32PTE;
		PVRSRV_ERROR eErr;

		PDUMP_GET_SCRIPT_AND_FILE_STRING();

		ui32PageMask = sMMUAttrib.ui32PTSize - 1;
		sCpuPAddr = OSMapLinToCPUPhys(psDeviceNode->sDevMemoryInfo.pBMKernelContext->psMMUContext->apsPTInfoList[ui32PDIndex]->hPTPageOSMemHandle, &pui32PT[ui32PTIndex]);
		sDevPAddr = SysCpuPAddrToDevPAddr(sMMUAttrib.sDevId.eDeviceType, sCpuPAddr);
		ui32PTE = *((IMG_UINT32 *) (&pui32PT[ui32PTIndex]));

		eErr = PDumpOSBufprintf(hScript,
								ui32MaxLenScript,
								"WRW :%s:PA_%p%p:0x%08X :%s:PA_%p%08X:0x%08X\r\n",
								sMMUAttrib.sDevId.pszPDumpDevName,
								PDUMP_PT_UNIQUETAG,
								(IMG_PVOID)((sDevPAddr.uiAddr) & ~ui32PageMask),
								(sDevPAddr.uiAddr) & ui32PageMask,
								"EXTSYSCACHE",
								PDUMP_PD_UNIQUETAG,
								(ui32PTE & sMMUAttrib.ui32PDEMask) << sMMUAttrib.ui32PTEAlignShift,
								ui32PTE & ~sMMUAttrib.ui32PDEMask);
					if(eErr != PVRSRV_OK)
					{
						return eErr;
					}
					PDumpOSWriteString2(hScript, PDUMP_FLAGS_CONTINUOUS);
	}
#endif

	return PVRSRV_OK;
}


/*!
******************************************************************************
	FUNCTION:   MMU_UnmapExtSystemCacheRegs

	PURPOSE:    unmaps external system cache control registers

	PARAMETERS: In:  psDeviceNode - device node
	RETURNS:
******************************************************************************/
PVRSRV_ERROR MMU_UnmapExtSystemCacheRegs(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	SYS_DATA *psSysData;
	RA_ARENA *psLocalDevMemArena;
	PVRSRV_SGXDEV_INFO *psDevInfo;
	IMG_UINT32 ui32PDIndex;
	IMG_UINT32 ui32PTIndex;
	IMG_UINT32 *pui32PT;
	PDUMP_MMU_ATTRIB sMMUAttrib;

	psDevInfo = (PVRSRV_SGXDEV_INFO*)psDeviceNode->pvDevice;

	sMMUAttrib = psDevInfo->sMMUAttrib;

#if defined(PDUMP)
	MMU_SetPDumpAttribs(&sMMUAttrib, psDeviceNode,
						SGX_MMU_PAGE_MASK,
						SGX_MMU_PT_SIZE * sizeof(IMG_UINT32));
#endif
	SysAcquireData(&psSysData);

	psLocalDevMemArena = psSysData->apsLocalDevMemArena[0];

	/* unmap the MMU page table from the PD */
	ui32PDIndex = (SGX_EXT_SYSTEM_CACHE_REGS_DEVVADDR_BASE & SGX_MMU_PD_MASK) >> (SGX_MMU_PAGE_SHIFT + SGX_MMU_PT_SHIFT);
	ui32PTIndex = (SGX_EXT_SYSTEM_CACHE_REGS_DEVVADDR_BASE & SGX_MMU_PT_MASK) >> SGX_MMU_PAGE_SHIFT;

	/* Only unmap it if the PT hasn't already been freed */
	if (psDeviceNode->sDevMemoryInfo.pBMKernelContext->psMMUContext->apsPTInfoList[ui32PDIndex])
	{
		if (psDeviceNode->sDevMemoryInfo.pBMKernelContext->psMMUContext->apsPTInfoList[ui32PDIndex]->PTPageCpuVAddr)
		{
			pui32PT = (IMG_UINT32 *) psDeviceNode->sDevMemoryInfo.pBMKernelContext->psMMUContext->apsPTInfoList[ui32PDIndex]->PTPageCpuVAddr;
		}
	}

	MakeKernelPageReadWrite(pui32PT);
	pui32PT[ui32PTIndex] = 0;
	MakeKernelPageReadOnly(pui32PT);

	PDUMPMEMPTENTRIES(&sMMUAttrib, psDeviceNode->sDevMemoryInfo.pBMKernelContext->psMMUContext->hPDOSMemHandle, &pui32PT[ui32PTIndex], sizeof(IMG_UINT32), 0, IMG_FALSE, PDUMP_PD_UNIQUETAG, PDUMP_PT_UNIQUETAG);

	return PVRSRV_OK;
}
#endif


#if PAGE_TEST
/*!
******************************************************************************
	FUNCTION:   PageTest

	PURPOSE:    Tests page table memory, for use during device bring-up.

	PARAMETERS: In:  void* pMem - page address (CPU mapped)
	PARAMETERS: In:  IMG_DEV_PHYADDR sDevPAddr - page device phys address
	RETURNS:    None, provides debug output and breaks if an error is detected.
******************************************************************************/
static IMG_VOID PageTest(IMG_VOID* pMem, IMG_DEV_PHYADDR sDevPAddr)
{
	volatile IMG_UINT32 ui32WriteData;
	volatile IMG_UINT32 ui32ReadData;
	volatile IMG_UINT32 *pMem32 = (volatile IMG_UINT32 *)pMem;
	IMG_INT n;
	IMG_BOOL bOK=IMG_TRUE;

	ui32WriteData = 0xffffffff;

	for (n=0; n<1024; n++)
	{
		pMem32[n] = ui32WriteData;
		ui32ReadData = pMem32[n];

		if (ui32WriteData != ui32ReadData)
		{
			// Mem fault
			PVR_DPF ((PVR_DBG_ERROR, "Error - memory page test failed at device phys address 0x" DEVPADDR_FMT, sDevPAddr.uiAddr + (n<<2) ));
			PVR_DBG_BREAK;
			bOK = IMG_FALSE;
		}
 	}

	ui32WriteData = 0;

	for (n=0; n<1024; n++)
	{
		pMem32[n] = ui32WriteData;
		ui32ReadData = pMem32[n];

		if (ui32WriteData != ui32ReadData)
		{
			// Mem fault
			PVR_DPF ((PVR_DBG_ERROR, "Error - memory page test failed at device phys address 0x" DEVPADDR_FMT, sDevPAddr.uiAddr + (n<<2)));
			PVR_DBG_BREAK;
			bOK = IMG_FALSE;
		}
 	}

	if (bOK)
	{
		PVR_DPF ((PVR_DBG_VERBOSE, "MMU Page 0x" DEVPADDR_FMT " is OK", sDevPAddr.uiAddr));
	}
	else
	{
		PVR_DPF ((PVR_DBG_VERBOSE, "MMU Page 0x" DEVPADDR_FMT " *** FAILED ***", sDevPAddr.uiAddr));
	}
}
#endif

/******************************************************************************
 End of file (mmu.c)
******************************************************************************/


