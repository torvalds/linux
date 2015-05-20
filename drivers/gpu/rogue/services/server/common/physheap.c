/*************************************************************************/ /*!
@File           physheap.c
@Title          Physical heap management
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Management functions for the physical heap(s). A heap contains
                all the information required by services when using memory from
                that heap (such as CPU <> Device physical address translation).
                A system must register one heap but can have more then one which
                is why a heap must register with a (system) unique ID.
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
#include "physheap.h"
#include "allocmem.h"
#include "pvr_debug.h"
#include "osfunc.h"

struct _PHYS_HEAP_
{
	/*! ID of this physcial memory heap */
	IMG_UINT32					ui32PhysHeapID;
	/*! The type of this heap */
	PHYS_HEAP_TYPE			eType;

	/*! Start address of the physcial memory heap (LMA only) */
	IMG_CPU_PHYADDR				sStartAddr;
	/*! Size of the physcial memory heap (LMA only) */
	IMG_UINT64					uiSize;

	/*! PDump name of this physcial memory heap */
	IMG_CHAR					*pszPDumpMemspaceName;
	/*! Private data for the translate routines */
	IMG_HANDLE					hPrivData;
	/*! Function callbacks */
	PHYS_HEAP_FUNCTIONS			*psMemFuncs;


	/*! Refcount */
	IMG_UINT32					ui32RefCount;
	/*! Pointer to next physcial heap */
	struct _PHYS_HEAP_		*psNext;
};

PHYS_HEAP *g_psPhysHeapList;

#if defined(REFCOUNT_DEBUG)
#define PHYSHEAP_REFCOUNT_PRINT(fmt, ...)	\
	PVRSRVDebugPrintf(PVR_DBG_WARNING,	\
			  __FILE__,		\
			  __LINE__,		\
			  fmt,			\
			  __VA_ARGS__)
#else
#define PHYSHEAP_REFCOUNT_PRINT(fmt, ...)
#endif


PVRSRV_ERROR PhysHeapRegister(PHYS_HEAP_CONFIG *psConfig,
							  PHYS_HEAP **ppsPhysHeap)
{
	PHYS_HEAP *psNew;
	PHYS_HEAP *psTmp;

	PVR_DPF_ENTERED;

	if (psConfig->eType == PHYS_HEAP_TYPE_UNKNOWN)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Check this heap ID isn't already in use */
	psTmp = g_psPhysHeapList;
	while (psTmp)
	{
		if (psTmp->ui32PhysHeapID == psConfig->ui32PhysHeapID)
		{
			return PVRSRV_ERROR_PHYSHEAP_ID_IN_USE;
		}
		psTmp = psTmp->psNext;
	}

	psNew = OSAllocMem(sizeof(PHYS_HEAP));
	if (psNew == IMG_NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psNew->ui32PhysHeapID = psConfig->ui32PhysHeapID;
	psNew->eType = psConfig->eType;
	psNew->sStartAddr = psConfig->sStartAddr;
	psNew->uiSize = psConfig->uiSize;
	psNew->psMemFuncs = psConfig->psMemFuncs;
	psNew->hPrivData = psConfig->hPrivData;
	psNew->ui32RefCount = 0;
	psNew->pszPDumpMemspaceName = psConfig->pszPDumpMemspaceName;

	psNew->psNext = g_psPhysHeapList;
	g_psPhysHeapList = psNew;

	*ppsPhysHeap = psNew;

	PVR_DPF_RETURN_RC1(PVRSRV_OK, *ppsPhysHeap);
}

IMG_VOID PhysHeapUnregister(PHYS_HEAP *psPhysHeap)
{
	PVR_DPF_ENTERED1(psPhysHeap);

	PVR_ASSERT(psPhysHeap->ui32RefCount == 0);

	if (g_psPhysHeapList == psPhysHeap)
	{
		g_psPhysHeapList = psPhysHeap->psNext;
	}
	else
	{
		PHYS_HEAP *psTmp = g_psPhysHeapList;

		while(psTmp->psNext != psPhysHeap)
		{
			psTmp = psTmp->psNext;
		}
		psTmp->psNext = psPhysHeap->psNext;
	}

	OSFreeMem(psPhysHeap);

	PVR_DPF_RETURN;
}

PVRSRV_ERROR PhysHeapAcquire(IMG_UINT32 ui32PhysHeapID,
							 PHYS_HEAP **ppsPhysHeap)
{
	PHYS_HEAP *psTmp = g_psPhysHeapList;
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_DPF_ENTERED1(ui32PhysHeapID);

	while (psTmp)
	{
		if (psTmp->ui32PhysHeapID == ui32PhysHeapID)
		{
			break;
		}
		psTmp = psTmp->psNext;
	}
	
	if (psTmp == IMG_NULL)
	{
		eError = PVRSRV_ERROR_PHYSHEAP_ID_INVALID;
	}
	else
	{
		psTmp->ui32RefCount++;
		PHYSHEAP_REFCOUNT_PRINT("%s: Heap %p, refcount = %d", __FUNCTION__, psTmp, psTmp->ui32RefCount);
	}

	*ppsPhysHeap = psTmp;
	PVR_DPF_RETURN_RC1(eError, *ppsPhysHeap);
}

IMG_VOID PhysHeapRelease(PHYS_HEAP *psPhysHeap)
{
	PVR_DPF_ENTERED1(psPhysHeap);

	psPhysHeap->ui32RefCount--;
	PHYSHEAP_REFCOUNT_PRINT("%s: Heap %p, refcount = %d", __FUNCTION__, psPhysHeap, psPhysHeap->ui32RefCount);

	PVR_DPF_RETURN;
}

PHYS_HEAP_TYPE PhysHeapGetType(PHYS_HEAP *psPhysHeap)
{
	return psPhysHeap->eType;
}

PVRSRV_ERROR PhysHeapGetAddress(PHYS_HEAP *psPhysHeap,
								IMG_CPU_PHYADDR *psCpuPAddr)
{
	if (psPhysHeap->eType == PHYS_HEAP_TYPE_LMA)
	{
		*psCpuPAddr = psPhysHeap->sStartAddr;
		return PVRSRV_OK;
	}

	return PVRSRV_ERROR_INVALID_PARAMS;
}

PVRSRV_ERROR PhysHeapGetSize(PHYS_HEAP *psPhysHeap,
							   IMG_UINT64 *puiSize)
{
	if (psPhysHeap->eType == PHYS_HEAP_TYPE_LMA)
	{
		*puiSize = psPhysHeap->uiSize;
		return PVRSRV_OK;
	}

	return PVRSRV_ERROR_INVALID_PARAMS;
}

IMG_VOID PhysHeapCpuPAddrToDevPAddr(PHYS_HEAP *psPhysHeap,
									IMG_DEV_PHYADDR *psDevPAddr,
									IMG_CPU_PHYADDR *psCpuPAddr)
{
	psPhysHeap->psMemFuncs->pfnCpuPAddrToDevPAddr(psPhysHeap->hPrivData,
												 psDevPAddr,
												 psCpuPAddr);
}

IMG_VOID PhysHeapDevPAddrToCpuPAddr(PHYS_HEAP *psPhysHeap,
									IMG_CPU_PHYADDR *psCpuPAddr,
									IMG_DEV_PHYADDR *psDevPAddr)
{
	psPhysHeap->psMemFuncs->pfnDevPAddrToCpuPAddr(psPhysHeap->hPrivData,
												 psCpuPAddr,
												 psDevPAddr);
}

IMG_CHAR *PhysHeapPDumpMemspaceName(PHYS_HEAP *psPhysHeap)
{
	return psPhysHeap->pszPDumpMemspaceName;
}

PVRSRV_ERROR PhysHeapInit(IMG_VOID)
{
	g_psPhysHeapList = IMG_NULL;

	return PVRSRV_OK;
}

PVRSRV_ERROR PhysHeapDeinit(IMG_VOID)
{
	PVR_ASSERT(g_psPhysHeapList == IMG_NULL);

	return PVRSRV_OK;
}
