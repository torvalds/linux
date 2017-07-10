/*************************************************************************/ /*!
@File           physmem.c
@Title          Physmem
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Common entry point for creation of RAM backed PMR's
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
*/ /***************************************************************************/
#include "img_types.h"
#include "pvrsrv_error.h"
#include "pvrsrv_memallocflags.h"
#include "device.h"
#include "physmem.h"
#include "pvrsrv.h"
#include "osfunc.h"
#include "pdump_physmem.h"
#include "pdump_km.h"
#include "rgx_heaps.h"

#if defined(DEBUG)
IMG_UINT32 gPMRAllocFail = 0;
#endif /* defined(DEBUG) */

PVRSRV_ERROR DevPhysMemAlloc(PVRSRV_DEVICE_NODE	*psDevNode,
							IMG_UINT32 ui32MemSize,
							const IMG_UINT8 u8Value,
							IMG_BOOL bInitPage,
#if defined(PDUMP)
							const IMG_CHAR *pszDevSpace,
							const IMG_CHAR *pszSymbolicAddress,
							IMG_HANDLE *phHandlePtr,
#endif
							IMG_HANDLE hMemHandle,
							IMG_DEV_PHYADDR *psDevPhysAddr)
{
	void	*pvCpuVAddr;
	PVRSRV_ERROR eError;
#if defined(PDUMP)
    IMG_CHAR szFilenameOut[PDUMP_PARAM_MAX_FILE_NAME];
    PDUMP_FILEOFFSET_T uiOffsetOut;
#endif
	PG_HANDLE *psMemHandle;
	IMG_UINT32 ui32PageSize;

	psMemHandle = hMemHandle;
	ui32PageSize = OSGetPageSize();

	/*Allocate the page */
	eError = psDevNode->pfnDevPxAlloc(psDevNode,
										TRUNCATE_64BITS_TO_SIZE_T(ui32MemSize),
										psMemHandle,
										psDevPhysAddr);
	if(PVRSRV_OK != eError)
	{
		PVR_DPF((PVR_DBG_ERROR,"Unable to allocate the pages"));
		return eError;
	}

#if defined(PDUMP)
	eError = PDumpMalloc(pszDevSpace,
								pszSymbolicAddress,
								ui32MemSize,
								ui32PageSize,
								IMG_FALSE,
								0,
								IMG_FALSE,
								phHandlePtr);
	if(PVRSRV_OK != eError)
	{
		PDUMPCOMMENT("Allocating pages failed");
		*phHandlePtr = NULL;
	}
#endif

	if(bInitPage)
	{
		/*Map the page to the CPU VA space */
		eError = psDevNode->pfnDevPxMap(psDevNode,
										psMemHandle,
										ui32MemSize,
										psDevPhysAddr,
										&pvCpuVAddr);
		if(PVRSRV_OK != eError)
		{
			PVR_DPF((PVR_DBG_ERROR,"Unable to map the allocated page"));
			psDevNode->pfnDevPxFree(psDevNode, psMemHandle);
			return eError;
		}

		/*Fill the memory with given content */
		/*NOTE: Wrong for the LMA + ARM64 combination, but this is unlikely */
		OSCachedMemSet(pvCpuVAddr, u8Value, ui32MemSize);

		/*Map the page to the CPU VA space */
		eError = psDevNode->pfnDevPxClean(psDevNode,
		                                  psMemHandle,
		                                  0,
		                                  ui32MemSize);
		if(PVRSRV_OK != eError)
		{
			PVR_DPF((PVR_DBG_ERROR,"Unable to clean the allocated page"));
			psDevNode->pfnDevPxUnMap(psDevNode, psMemHandle, pvCpuVAddr);
			psDevNode->pfnDevPxFree(psDevNode, psMemHandle);
			return eError;
		}

#if defined(PDUMP)
		/*P-Dumping of the page contents can be done in two ways
		 * 1. Store the single byte init value to the .prm file
		 * 	  and load the same value to the entire dummy page buffer
		 * 	  This method requires lot of LDB's inserted into the out2.txt
		 *
		 * 2. Store the entire contents of the buffer to the .prm file
		 *    and load them back.
		 *    This only needs a single LDB instruction in the .prm file
		 *    and chosen this method
		 *    size of .prm file might go up but that's not huge at least
		 * 	  for this allocation
		 */
		/*Write the buffer contents to the prm file */
		eError = PDumpWriteBuffer(pvCpuVAddr,
									ui32MemSize,
									PDUMP_FLAGS_CONTINUOUS,
									szFilenameOut,
									sizeof(szFilenameOut),
									&uiOffsetOut);
		if(PVRSRV_OK == eError)
		{
			/* Load the buffer back to the allocated memory when playing the pdump */
			eError = PDumpPMRLDB(pszDevSpace,
										pszSymbolicAddress,
										0,
										ui32MemSize,
										szFilenameOut,
										uiOffsetOut,
										PDUMP_FLAGS_CONTINUOUS);
			if(PVRSRV_OK != eError)
			{
				PDUMP_ERROR(eError, "Failed to write LDB statement to script file");
				PVR_DPF((PVR_DBG_ERROR, "Failed to write LDB statement to script file, error %d", eError));
			}

		}
		else if (eError != PVRSRV_ERROR_PDUMP_NOT_ALLOWED)
		{
			PDUMP_ERROR(eError, "Failed to write device allocation to parameter file");
			PVR_DPF((PVR_DBG_ERROR, "Failed to write device allocation to parameter file, error %d", eError));
		}
		else
		{
			/* else Write to parameter file prevented under the flags and
			 * current state of the driver so skip write to script and error IF.
			 */
			eError = PVRSRV_OK;
		}
#endif

		/*UnMap the page */
		psDevNode->pfnDevPxUnMap(psDevNode,
										psMemHandle,
										pvCpuVAddr);
	}

	return PVRSRV_OK;

}

void DevPhysMemFree(PVRSRV_DEVICE_NODE *psDevNode,
#if defined(PDUMP)
							IMG_HANDLE hPDUMPMemHandle,
#endif
							IMG_HANDLE	hMemHandle)
{
	PG_HANDLE *psMemHandle;

	psMemHandle = hMemHandle;
	psDevNode->pfnDevPxFree(psDevNode, psMemHandle);
#if defined(PDUMP)
	if(NULL != hPDUMPMemHandle)
	{
		PDumpFree(hPDUMPMemHandle);
	}
#endif

}

PVRSRV_ERROR
PhysmemNewRamBackedPMR(CONNECTION_DATA * psConnection,
                       PVRSRV_DEVICE_NODE *psDevNode,
                       IMG_DEVMEM_SIZE_T uiSize,
                       PMR_SIZE_T uiChunkSize,
                       IMG_UINT32 ui32NumPhysChunks,
                       IMG_UINT32 ui32NumVirtChunks,
                       IMG_UINT32 *pui32MappingTable,
                       IMG_UINT32 uiLog2PageSize,
                       PVRSRV_MEMALLOCFLAGS_T uiFlags,
                       IMG_UINT32 uiAnnotationLength,
                       const IMG_CHAR *pszAnnotation,
                       PMR **ppsPMRPtr)
{
	PVRSRV_DEVICE_PHYS_HEAP ePhysHeapIdx;
	PFN_SYS_DEV_CHECK_MEM_ALLOC_SIZE pfnCheckMemAllocSize =
		psDevNode->psDevConfig->pfnCheckMemAllocSize;

	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(uiAnnotationLength);

	/* We don't currently support sparse memory with non OS page sized heaps */
	if (ui32NumVirtChunks > 1 && (uiLog2PageSize != OSGetPageShift()))
	{
		PVR_DPF((PVR_DBG_ERROR,
				"Requested page size for sparse 2^%u is not OS page size.",
				uiLog2PageSize));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Protect against ridiculous page sizes */
	if (uiLog2PageSize > RGX_HEAP_2MB_PAGE_SHIFT)
	{
		PVR_DPF((PVR_DBG_ERROR, "Page size is too big: 2^%u.", uiLog2PageSize));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Lookup the requested physheap index to use for this PMR allocation */
	if (PVRSRV_CHECK_FW_LOCAL(uiFlags))
	{
		ePhysHeapIdx = PVRSRV_DEVICE_PHYS_HEAP_FW_LOCAL;
	}
	else if (PVRSRV_CHECK_CPU_LOCAL(uiFlags))
	{
		ePhysHeapIdx = PVRSRV_DEVICE_PHYS_HEAP_CPU_LOCAL;
	}
	else
	{
		ePhysHeapIdx = PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL;
	}

	/* Fail if requesting coherency on one side but uncached on the other */
	if ( (PVRSRV_CHECK_CPU_CACHE_COHERENT(uiFlags) &&
	         (PVRSRV_CHECK_GPU_UNCACHED(uiFlags) || PVRSRV_CHECK_GPU_WRITE_COMBINE(uiFlags))) )
	{
		PVR_DPF((PVR_DBG_ERROR, "Request for CPU coherency but specifying GPU uncached "
				"Please use GPU cached flags for coherency."));
		return PVRSRV_ERROR_UNSUPPORTED_CACHE_MODE;
	}

	if ( (PVRSRV_CHECK_GPU_CACHE_COHERENT(uiFlags) &&
	         (PVRSRV_CHECK_CPU_UNCACHED(uiFlags) || PVRSRV_CHECK_CPU_WRITE_COMBINE(uiFlags))) )
	{
		PVR_DPF((PVR_DBG_ERROR, "Request for GPU coherency but specifying CPU uncached "
				"Please use CPU cached flags for coherency."));
		return PVRSRV_ERROR_UNSUPPORTED_CACHE_MODE;
	}

	/* Apply memory budgeting policy */
	if (pfnCheckMemAllocSize)
	{
		IMG_UINT64 uiMemSize = (IMG_UINT64)uiChunkSize * ui32NumPhysChunks;
		PVRSRV_ERROR eError;

		eError = pfnCheckMemAllocSize(psDevNode->psDevConfig->hSysData, uiMemSize);
		if (eError != PVRSRV_OK)
		{
			return eError;
		}
	}

#if defined(DEBUG)
	if (gPMRAllocFail > 0)
	{
		static IMG_UINT32 ui32AllocCount = 1;

		if (ui32AllocCount < gPMRAllocFail)
		{
			ui32AllocCount++;
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR, "%s failed on %d allocation.",
			         __func__, ui32AllocCount));
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}
	}
#endif /* defined(DEBUG) */

	return psDevNode->pfnCreateRamBackedPMR[ePhysHeapIdx](psDevNode,
											uiSize,
											uiChunkSize,
											ui32NumPhysChunks,
											ui32NumVirtChunks,
											pui32MappingTable,
											uiLog2PageSize,
											uiFlags,
											pszAnnotation,
											ppsPMRPtr);
}

PVRSRV_ERROR
PhysmemNewRamBackedLockedPMR(CONNECTION_DATA * psConnection,
							PVRSRV_DEVICE_NODE *psDevNode,
							IMG_DEVMEM_SIZE_T uiSize,
							PMR_SIZE_T uiChunkSize,
							IMG_UINT32 ui32NumPhysChunks,
							IMG_UINT32 ui32NumVirtChunks,
							IMG_UINT32 *pui32MappingTable,
							IMG_UINT32 uiLog2PageSize,
							PVRSRV_MEMALLOCFLAGS_T uiFlags,
							IMG_UINT32 uiAnnotationLength,
							const IMG_CHAR *pszAnnotation,
							PMR **ppsPMRPtr)
{

	PVRSRV_ERROR eError;
	eError = PhysmemNewRamBackedPMR(psConnection,
									psDevNode,
									uiSize,
									uiChunkSize,
									ui32NumPhysChunks,
									ui32NumVirtChunks,
									pui32MappingTable,
									uiLog2PageSize,
									uiFlags,
									uiAnnotationLength,
									pszAnnotation,
									ppsPMRPtr);

	if (eError == PVRSRV_OK)
	{
		eError = PMRLockSysPhysAddresses(*ppsPMRPtr);
	}

	return eError;
}
