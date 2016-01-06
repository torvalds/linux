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

#if defined(DEBUG)
IMG_UINT32 gPMRAllocFail = 0;
#endif /* defined(DEBUG) */

PVRSRV_ERROR
PhysmemNewRamBackedPMR(PVRSRV_DEVICE_NODE *psDevNode,
						IMG_DEVMEM_SIZE_T uiSize,
						PMR_SIZE_T uiChunkSize,
						IMG_UINT32 ui32NumPhysChunks,
						IMG_UINT32 ui32NumVirtChunks,
						IMG_BOOL *pabMappingTable,
						IMG_UINT32 uiLog2PageSize,
						PVRSRV_MEMALLOCFLAGS_T uiFlags,
						PMR **ppsPMRPtr)
{
	PVRSRV_DEVICE_PHYS_HEAP ePhysHeapIdx = (uiFlags & PVRSRV_MEMALLOCFLAG_CPU_LOCAL) ? 1: 0;
	PFN_SYS_DEV_CHECK_MEM_ALLOC_SIZE pfnCheckMemAllocSize = \
										psDevNode->psDevConfig->pfnCheckMemAllocSize;
#if defined(DEBUG)
	static IMG_UINT32 ui32AllocCount = 1;
#endif /* defined(DEBUG) */
	/********************************
	 * Sanity check the cache flags *
	 ********************************/
	/* Check if we can honour cached cache-coherent allocations */
	if ((PVRSRV_CPU_CACHE_MODE(uiFlags) == PVRSRV_MEMALLOCFLAG_CPU_CACHED_CACHE_COHERENT) &&
		(!PVRSRVSystemHasCacheSnooping()))
	{
		return PVRSRV_ERROR_UNSUPPORTED_CACHE_MODE;
	}

	/* Both or neither have to be cache-coherent */
	if ((PVRSRV_CPU_CACHE_MODE(uiFlags) == PVRSRV_MEMALLOCFLAG_CPU_CACHE_COHERENT) ^
		(PVRSRV_GPU_CACHE_MODE(uiFlags) == PVRSRV_MEMALLOCFLAG_GPU_CACHE_COHERENT))
	{
		return PVRSRV_ERROR_UNSUPPORTED_CACHE_MODE;
	}

	if ((PVRSRV_CPU_CACHE_MODE(uiFlags) == PVRSRV_MEMALLOCFLAG_CPU_CACHED_CACHE_COHERENT) ^
		(PVRSRV_GPU_CACHE_MODE(uiFlags) == PVRSRV_MEMALLOCFLAG_GPU_CACHED_CACHE_COHERENT))
	{
		return PVRSRV_ERROR_UNSUPPORTED_CACHE_MODE;
	}

	/* Apply memory budgeting policy */
	if (pfnCheckMemAllocSize)
	{
		PVRSRV_ERROR eError = \
						pfnCheckMemAllocSize(psDevNode, (IMG_UINT64)uiChunkSize*ui32NumPhysChunks);
		if (eError != PVRSRV_OK)
		{
			return eError;
		}
	}

#if defined(DEBUG)
	if (gPMRAllocFail > 0)
	{
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
											pabMappingTable,
											uiLog2PageSize,
											uiFlags,
											ppsPMRPtr);
}
