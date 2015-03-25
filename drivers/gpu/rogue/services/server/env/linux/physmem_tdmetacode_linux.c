/*************************************************************************/ /*!
@File
@Title          Implementation of PMR functions for Trusted Device firmware code memory
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Part of the memory management.  This module is responsible for
                implementing the function callbacks for physical memory borrowed
                from that normally managed by the operating system.
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

/* include5/ */
#include "img_types.h"
#include "pvr_debug.h"
#include "pvrsrv_error.h"
#include "pvrsrv_memallocflags.h"

/* services/server/include/ */
#include "allocmem.h"
#include "osfunc.h"
#include "pdump_physmem.h"
#include "pdump_km.h"
#include "pmr.h"
#include "pmr_impl.h"

/* ourselves */
#include "physmem_osmem.h"

#if defined(PVR_RI_DEBUG)
#include "ri_server.h"
#endif

#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/mm_types.h>
#include <linux/vmalloc.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <asm/io.h>
#if defined(CONFIG_X86)
#include <asm/cacheflush.h>
#endif
#if defined(__arm__)
#include "osfunc.h"
#endif

#include "rgxdevice.h"

/* This is a placeholder implementation of a PMR factory to wrap allocations into
   the protected META code regions. It has been consciously heavily inspired by the
   standard osmem PMR factory to supply dummy functionality. Most things here will
   change in a real implementation.

   Your starting point for re-implementing this module should be by inspecting the 
   sTDMETACodePMRFuncTab structure below and determining which callbacks you need
   to implement for your system.
*/

typedef struct {
	void *token;
	IMG_UINT32 ui32Log2PageSizeBytes;
	struct page **apsPageArray;
	IMG_UINT64 ui64NumPages;

	PHYS_HEAP *psTDMetaCodePhysHeap;
    IMG_HANDLE hPDumpAllocInfo;
} sTDMetaCodePageList;

static void
_FreeTDMetaCodePageContainer(void *pvPagecontainer)
{
	sTDMetaCodePageList *psPageContainer = (sTDMetaCodePageList *) pvPagecontainer;

	if(! psPageContainer)
	{
		return;
	}

	if(psPageContainer->apsPageArray)
	{
		IMG_UINT64 i;
		for(i = 0; i < psPageContainer->ui64NumPages; i++)
		{
			if(psPageContainer->apsPageArray[i])
			{
				__free_page(psPageContainer->apsPageArray[i]);
			}
		}
		OSFreeMem(psPageContainer->apsPageArray);
	}

	PhysHeapRelease(psPageContainer->psTDMetaCodePhysHeap);

    PDumpPMRFree(psPageContainer->hPDumpAllocInfo);

    OSFreeMem(psPageContainer);
}

static PVRSRV_ERROR
PMRSysPhysAddrTDMetaCode(PMR_IMPL_PRIVDATA pvPriv,
                         IMG_DEVMEM_OFFSET_T uiOffset,
                         IMG_DEV_PHYADDR *psDevPAddr)
{
	sTDMetaCodePageList *psPageContainer = (sTDMetaCodePageList *) pvPriv;
	IMG_UINT64 ui64PageNum = uiOffset >> psPageContainer->ui32Log2PageSizeBytes;
	IMG_UINT32 ui32PageOffset = uiOffset - (ui64PageNum << psPageContainer->ui32Log2PageSizeBytes);
	
	PVR_ASSERT(ui64PageNum < psPageContainer->ui64NumPages);
	psDevPAddr->uiAddr = page_to_phys(psPageContainer->apsPageArray[ui64PageNum]) + ui32PageOffset;
	return PVRSRV_OK;
}


static PVRSRV_ERROR
PMRFinalizeTDMetaCode(PMR_IMPL_PRIVDATA pvPriv)
{
	_FreeTDMetaCodePageContainer((void *) pvPriv);
	return PVRSRV_OK;
}

static PVRSRV_ERROR
PMRReadBytesTDMetaCode(PMR_IMPL_PRIVDATA pvPriv,
                       IMG_DEVMEM_OFFSET_T uiOffset,
                       IMG_UINT8 *pcBuffer,
                       IMG_SIZE_T uiBufSz,
                       IMG_SIZE_T *puiNumBytes)
{
	sTDMetaCodePageList *psPageContainer = (sTDMetaCodePageList *) pvPriv;
    IMG_UINT8 *pvMapping;
	IMG_UINT32 uiPageSize = 1 << psPageContainer->ui32Log2PageSizeBytes;
   	IMG_UINT32 uiPageIndex;
	IMG_UINT32 uiReadOffset;
	IMG_UINT32 uiReadBytes;

	*puiNumBytes = 0;
	
	while(uiBufSz)
	{
    	uiPageIndex = uiOffset >> psPageContainer->ui32Log2PageSizeBytes;
		uiReadOffset = uiOffset - uiPageIndex * uiPageSize;
		uiReadBytes = uiPageSize - uiReadOffset;

		if(uiReadBytes > uiBufSz)
		{
			uiReadBytes = uiBufSz;
		}
		
        pvMapping = kmap(psPageContainer->apsPageArray[uiPageIndex]);
        PVR_ASSERT(pvMapping);
        memcpy(pcBuffer, pvMapping + uiReadOffset, uiReadBytes);
        kunmap(psPageContainer->apsPageArray[uiPageIndex]);
		
		uiBufSz -= uiReadBytes;
		pcBuffer += uiReadBytes;
		*puiNumBytes += uiReadBytes;

		uiOffset += uiReadBytes;
	}
    return PVRSRV_OK;
}

static PVRSRV_ERROR
PMRKernelMapTDMetaCode(PMR_IMPL_PRIVDATA pvPriv,
                       IMG_SIZE_T uiOffset,
                       IMG_SIZE_T uiSize,
                       void **ppvKernelAddressOut,
                       IMG_HANDLE *phHandleOut,
                       PMR_FLAGS_T ulFlags)
{
    sTDMetaCodePageList *psPageContainer;
    void *pvAddress;

    psPageContainer = pvPriv;

	pvAddress = vm_map_ram(psPageContainer->apsPageArray,
						   psPageContainer->ui64NumPages,
						   -1,
						   PAGE_KERNEL);

	if(! pvAddress)
	{
		return PVRSRV_ERROR_MAP_TDMETACODE_PAGES_FAIL;
	}

    *ppvKernelAddressOut = pvAddress + uiOffset;
    *phHandleOut = pvAddress;

    return PVRSRV_OK;
}

static void
PMRKernelUnmapTDMetaCode(PMR_IMPL_PRIVDATA pvPriv,
                         IMG_HANDLE hHandle)
{
    sTDMetaCodePageList *psPageContainer;
    psPageContainer = pvPriv;
    vm_unmap_ram(hHandle, psPageContainer->ui64NumPages);
}

static PMR_IMPL_FUNCTAB sTDMETACodePMRFuncTab = {
	.pfnLockPhysAddresses = IMG_NULL,           /* pages are always available in these PMRs */
	.pfnUnlockPhysAddresses = IMG_NULL,         /* as above */
	.pfnDevPhysAddr = PMRSysPhysAddrTDMetaCode,
	.pfnPDumpSymbolicAddr = IMG_NULL,           /* nothing special needed */
	.pfnAcquireKernelMappingData = PMRKernelMapTDMetaCode,
	.pfnReleaseKernelMappingData = PMRKernelUnmapTDMetaCode,
	.pfnReadBytes = PMRReadBytesTDMetaCode,
	.pfnFinalize = PMRFinalizeTDMetaCode
};


static PVRSRV_ERROR
_AllocTDMetaCodePageContainer(IMG_UINT64 ui64NumPages,
                              IMG_UINT32 uiLog2PageSize,
                              PHYS_HEAP *psTDMetaCodePhysHeap,
                              void **ppvPageContainer)
{
	IMG_UINT64 i;
	PVRSRV_ERROR eStatus = PVRSRV_OK;
	sTDMetaCodePageList *psPageContainer;

	psPageContainer = OSAllocMem(sizeof(sTDMetaCodePageList));
	if(!psPageContainer)
	{
		eStatus = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail;
	}
	psPageContainer->ui32Log2PageSizeBytes = uiLog2PageSize;
	psPageContainer->ui64NumPages = ui64NumPages;
	psPageContainer->psTDMetaCodePhysHeap = psTDMetaCodePhysHeap;
	psPageContainer->apsPageArray = OSAllocMem(ui64NumPages * sizeof(psPageContainer->apsPageArray[0]));
	if(!psPageContainer->apsPageArray)
	{
		eStatus = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail;
	}
	for(i = 0; i < ui64NumPages; i++)
	{
		psPageContainer->apsPageArray[i] = IMG_NULL;
	}

	for(i = 0; i < ui64NumPages; i++)
	{
		psPageContainer->apsPageArray[i] = alloc_page(GFP_KERNEL);
		if(! psPageContainer->apsPageArray[i])
		{
			eStatus = PVRSRV_ERROR_REQUEST_TDMETACODE_PAGES_FAIL;
			goto fail;
		}
	}

	*ppvPageContainer = psPageContainer;
	return eStatus;

fail:
	_FreeTDMetaCodePageContainer((void *) psPageContainer);
	*ppvPageContainer = IMG_NULL;
	return eStatus;
}

PVRSRV_ERROR
PhysmemNewTDMetaCodePMR(PVRSRV_DEVICE_NODE *psDevNode,
                        IMG_DEVMEM_SIZE_T uiSize,
                        IMG_UINT32 uiLog2PageSize,
                        PVRSRV_MEMALLOCFLAGS_T uiFlags,
                        PMR **ppsPMRPtr)
{
	sTDMetaCodePageList *psPageContainer = IMG_NULL;
	IMG_UINT32 uiPageSize = 1 << uiLog2PageSize;
	IMG_UINT64 ui64NumPages = (uiSize >> uiLog2PageSize) + ((uiSize & (uiPageSize-1)) != 0);
	PVRSRV_ERROR eStatus;
	PHYS_HEAP *psTDMetaCodePhysHeap;
	IMG_BOOL bMappingTable = IMG_TRUE;
    IMG_HANDLE hPDumpAllocInfo = IMG_NULL;
	RGX_DATA *psRGXData;

    IMG_UINT32 uiPMRFlags = (PMR_FLAGS_T)(uiFlags & PVRSRV_MEMALLOCFLAGS_PMRFLAGSMASK);
    /* check no significant bits were lost in cast due to different
       bit widths for flags */
    PVR_ASSERT(uiPMRFlags == (uiFlags & PVRSRV_MEMALLOCFLAGS_PMRFLAGSMASK));
	
	/* get the physical heap for TD Meta Code */
	psRGXData = (RGX_DATA *)(psDevNode->psDevConfig->hDevData);
	if(! psRGXData->bHasTDMetaCodePhysHeap)
	{
		PVR_DPF((PVR_DBG_ERROR,"Failed allocation from non-existent Trusted Device physical heap!"));
		eStatus = PVRSRV_ERROR_REQUEST_TDMETACODE_PAGES_FAIL;
		goto fail;
	}
	eStatus = PhysHeapAcquire(psRGXData->uiTDMetaCodePhysHeapID,
	                          &psTDMetaCodePhysHeap);
	if(eStatus)
	{
		goto fail;
	}

	/* allocate and initialize the page container structure */
	eStatus = _AllocTDMetaCodePageContainer(ui64NumPages,
	                                        uiLog2PageSize,
	                                        psTDMetaCodePhysHeap,
	                                        (void *)&psPageContainer);
	if(eStatus)
	{
		goto fail;
	}

	/* wrap the container in a PMR */
    eStatus = PMRCreatePMR(psTDMetaCodePhysHeap,
                           ui64NumPages * uiPageSize,
                           ui64NumPages * uiPageSize,
                           1,
                           1,
                           &bMappingTable,
                           uiLog2PageSize,
                           uiPMRFlags,
                           "PMRTDMETACODE",
                           &sTDMETACodePMRFuncTab,
                           (void *)psPageContainer,
                           ppsPMRPtr,
                           &hPDumpAllocInfo,
                           IMG_FALSE);
	
#if defined(PVR_RI_DEBUG)
	{
		RIWritePMREntryKM (*ppsPMRPtr,
						   sizeof("TD META Code"),
					   	   "TD META Code",
						   (ui64NumPages * uiPageSize));
	}
#endif

	/* this is needed when the allocation is finalized and we need to free it. */
	psPageContainer->hPDumpAllocInfo = hPDumpAllocInfo;

	return eStatus;

	/* error cleanup */

fail:
	return eStatus;
}
