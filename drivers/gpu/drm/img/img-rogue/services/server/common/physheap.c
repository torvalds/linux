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
#include "img_defs.h"
#include "physheap.h"
#include "allocmem.h"
#include "pvr_debug.h"
#include "osfunc.h"
#include "pvrsrv.h"
#include "physmem.h"
#include "physmem_hostmem.h"
#include "physmem_lma.h"
#include "physmem_osmem.h"

struct _PHYS_HEAP_
{
	/*! The type of this heap */
	PHYS_HEAP_TYPE			eType;
	/* Config flags */
	PHYS_HEAP_USAGE_FLAGS		ui32UsageFlags;

	/*! Pointer to device node struct */
	PPVRSRV_DEVICE_NODE         psDevNode;
	/*! PDump name of this physical memory heap */
	IMG_CHAR					*pszPDumpMemspaceName;
	/*! Private data for the translate routines */
	IMG_HANDLE					hPrivData;
	/*! Function callbacks */
	PHYS_HEAP_FUNCTIONS			*psMemFuncs;

	/*! Refcount */
	IMG_UINT32					ui32RefCount;

	/*! Implementation specific */
	PHEAP_IMPL_DATA             pvImplData;
	PHEAP_IMPL_FUNCS            *psImplFuncs;

	/*! Pointer to next physical heap */
	struct _PHYS_HEAP_		*psNext;
};

static PHYS_HEAP *g_psPhysHeapList;
static POS_LOCK g_hPhysHeapLock;

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



typedef struct PHYS_HEAP_PROPERTIES_TAG
{
	PVRSRV_PHYS_HEAP eFallbackHeap;
	IMG_BOOL bPVRLayerAcquire;
	IMG_BOOL bUserModeAlloc;
} PHYS_HEAP_PROPERTIES;

static PHYS_HEAP_PROPERTIES gasHeapProperties[PVRSRV_PHYS_HEAP_LAST] =
{
	/* eFallbackHeap,               bPVRLayerAcquire, bUserModeAlloc */
    {  PVRSRV_PHYS_HEAP_GPU_LOCAL,  IMG_TRUE,         IMG_TRUE  }, /* GPU_LOCAL */
    {  PVRSRV_PHYS_HEAP_GPU_LOCAL,  IMG_TRUE,         IMG_TRUE  }, /* CPU_LOCAL */
    {  PVRSRV_PHYS_HEAP_GPU_LOCAL,  IMG_FALSE,        IMG_FALSE }, /* FW_MAIN */
    {  PVRSRV_PHYS_HEAP_GPU_LOCAL,  IMG_TRUE,         IMG_FALSE }, /* EXTERNAL */
    {  PVRSRV_PHYS_HEAP_GPU_LOCAL,  IMG_TRUE,         IMG_FALSE }, /* GPU_PRIVATE */
    {  PVRSRV_PHYS_HEAP_GPU_LOCAL,  IMG_TRUE,         IMG_FALSE }, /* GPU_COHERENT */
    {  PVRSRV_PHYS_HEAP_GPU_LOCAL,  IMG_TRUE,         IMG_TRUE  }, /* GPU_SECURE */
    {  PVRSRV_PHYS_HEAP_FW_MAIN,    IMG_FALSE,        IMG_FALSE }, /* FW_CONFIG */
    {  PVRSRV_PHYS_HEAP_FW_MAIN,    IMG_FALSE,        IMG_FALSE }, /* FW_CODE */
    {  PVRSRV_PHYS_HEAP_FW_MAIN,    IMG_FALSE,        IMG_FALSE }, /* FW_DATA */
    {  PVRSRV_PHYS_HEAP_FW_PREMAP0, IMG_FALSE,        IMG_FALSE }, /* FW_PREMAP0 */
    {  PVRSRV_PHYS_HEAP_FW_PREMAP1, IMG_FALSE,        IMG_FALSE }, /* FW_PREMAP1 */
    {  PVRSRV_PHYS_HEAP_FW_PREMAP2, IMG_FALSE,        IMG_FALSE }, /* FW_PREMAP2 */
    {  PVRSRV_PHYS_HEAP_FW_PREMAP3, IMG_FALSE,        IMG_FALSE }, /* FW_PREMAP3 */
    {  PVRSRV_PHYS_HEAP_FW_PREMAP4, IMG_FALSE,        IMG_FALSE }, /* FW_PREMAP4 */
    {  PVRSRV_PHYS_HEAP_FW_PREMAP5, IMG_FALSE,        IMG_FALSE }, /* FW_PREMAP5 */
    {  PVRSRV_PHYS_HEAP_FW_PREMAP6, IMG_FALSE,        IMG_FALSE }, /* FW_PREMAP6 */
    {  PVRSRV_PHYS_HEAP_FW_PREMAP7, IMG_FALSE,        IMG_FALSE }, /* FW_PREMAP7 */
};

static PHEAP_IMPL_FUNCS _sPHEAPImplFuncs =
{
	.pfnDestroyData = NULL,
	.pfnCreatePMR = PhysmemNewOSRamBackedPMR,
};

PVRSRV_ERROR
PhysHeapCreateHeapFromConfig(PVRSRV_DEVICE_NODE *psDevNode,
							 PHYS_HEAP_CONFIG *psConfig,
							 PHYS_HEAP **ppsPhysHeap)
{
	PVRSRV_ERROR eResult;

	if (psConfig->eType == PHYS_HEAP_TYPE_UMA
#if defined(SUPPORT_WRAP_EXTMEMOBJECT)
		|| psConfig->eType == PHYS_HEAP_TYPE_WRAP
#endif
		)
	{
		eResult = PhysHeapCreate(psDevNode, psConfig, NULL,
								   &_sPHEAPImplFuncs, ppsPhysHeap);
	}
	else if (psConfig->eType == PHYS_HEAP_TYPE_LMA ||
			 psConfig->eType == PHYS_HEAP_TYPE_DMA)
	{
		eResult = PhysmemCreateHeapLMA(psDevNode, psConfig, "GPU LMA (Sys)", ppsPhysHeap);
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "%s Invalid phys heap type: %d",
				 __func__, psConfig->eType));
		eResult = PVRSRV_ERROR_INVALID_PARAMS;
	}

	return eResult;
}

PVRSRV_ERROR
PhysHeapCreateHeapsFromConfigs(PPVRSRV_DEVICE_NODE psDevNode,
					PHYS_HEAP_CONFIG *pasConfigs,
					IMG_UINT32 ui32NumConfigs,
					PHYS_HEAP **papsPhysHeaps,
					IMG_UINT32 *pui32NumHeaps)
{
	IMG_UINT32 i;
	PVRSRV_ERROR eError;

	*pui32NumHeaps = 0;

	for (i = 0; i < ui32NumConfigs; i++)
	{
		eError = PhysHeapCreateHeapFromConfig(psDevNode,
											  pasConfigs + i,
											  papsPhysHeaps + i);
		PVR_LOG_RETURN_IF_ERROR(eError, "PhysmemCreateHeap");

		(*pui32NumHeaps)++;
	}

	return PVRSRV_OK;
}

PVRSRV_ERROR PhysHeapCreate(PPVRSRV_DEVICE_NODE psDevNode,
							  PHYS_HEAP_CONFIG *psConfig,
							  PHEAP_IMPL_DATA pvImplData,
							  PHEAP_IMPL_FUNCS *psImplFuncs,
							  PHYS_HEAP **ppsPhysHeap)
{
	PHYS_HEAP *psNew;

	PVR_DPF_ENTERED;

	PVR_LOG_RETURN_IF_INVALID_PARAM(psDevNode != NULL, "psDevNode");

	if (psConfig->eType == PHYS_HEAP_TYPE_UNKNOWN)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	PVR_LOG_RETURN_IF_INVALID_PARAM(psImplFuncs != NULL, "psImplFuncs");
	PVR_LOG_RETURN_IF_INVALID_PARAM(psImplFuncs->pfnCreatePMR != NULL, "psImplFuncs->pfnCreatePMR");

	psNew = OSAllocMem(sizeof(PHYS_HEAP));
	PVR_RETURN_IF_NOMEM(psNew);
	psNew->psDevNode = psDevNode;
	psNew->eType = psConfig->eType;
	psNew->psMemFuncs = psConfig->psMemFuncs;
	psNew->hPrivData = psConfig->hPrivData;
	psNew->ui32RefCount = 0;
	psNew->pszPDumpMemspaceName = psConfig->pszPDumpMemspaceName;
	psNew->ui32UsageFlags = psConfig->ui32UsageFlags;

	psNew->pvImplData = pvImplData;
	psNew->psImplFuncs = psImplFuncs;

	psNew->psNext = g_psPhysHeapList;
	g_psPhysHeapList = psNew;

	*ppsPhysHeap = psNew;

	PVR_DPF_RETURN_RC1(PVRSRV_OK, *ppsPhysHeap);
}

void PhysHeapDestroy(PHYS_HEAP *psPhysHeap)
{
	PHEAP_IMPL_FUNCS *psImplFuncs = psPhysHeap->psImplFuncs;

	PVR_DPF_ENTERED1(psPhysHeap);

#if defined(PVRSRV_FORCE_UNLOAD_IF_BAD_STATE)
	if (PVRSRVGetPVRSRVData()->eServicesState == PVRSRV_SERVICES_STATE_OK)
#endif
	{
		PVR_ASSERT(psPhysHeap->ui32RefCount == 0);
	}

	if (g_psPhysHeapList == psPhysHeap)
	{
		g_psPhysHeapList = psPhysHeap->psNext;
	}
	else
	{
		PHYS_HEAP *psTmp = g_psPhysHeapList;

		while (psTmp->psNext != psPhysHeap)
		{
			psTmp = psTmp->psNext;
		}
		psTmp->psNext = psPhysHeap->psNext;
	}

	if (psImplFuncs->pfnDestroyData != NULL)
	{
		psImplFuncs->pfnDestroyData(psPhysHeap->pvImplData);
	}

	OSFreeMem(psPhysHeap);

	PVR_DPF_RETURN;
}

PVRSRV_ERROR PhysHeapAcquire(PHYS_HEAP *psPhysHeap)
{
	PVR_LOG_RETURN_IF_INVALID_PARAM(psPhysHeap != NULL, "psPhysHeap");

	psPhysHeap->ui32RefCount++;

	return PVRSRV_OK;
}

PVRSRV_ERROR PhysHeapAcquireByUsage(PHYS_HEAP_USAGE_FLAGS ui32UsageFlag,
									PHYS_HEAP **ppsPhysHeap)
{
	PHYS_HEAP *psNode = g_psPhysHeapList;
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_DPF_ENTERED1(ui32UsageFlag);

	OSLockAcquire(g_hPhysHeapLock);

	while (psNode)
	{
		if (BITMASK_ANY(psNode->ui32UsageFlags, ui32UsageFlag))
		{
			break;
		}
		psNode = psNode->psNext;
	}

	if (psNode == NULL)
	{
		eError = PVRSRV_ERROR_PHYSHEAP_ID_INVALID;
	}
	else
	{
		psNode->ui32RefCount++;
		PHYSHEAP_REFCOUNT_PRINT("%s: Heap %p, refcount = %d",
								__func__, psNode, psNode->ui32RefCount);
	}

	OSLockRelease(g_hPhysHeapLock);

	*ppsPhysHeap = psNode;
	PVR_DPF_RETURN_RC1(eError, *ppsPhysHeap);
}

static PHYS_HEAP * _PhysHeapFindHeap(PVRSRV_PHYS_HEAP ePhysHeap,
								   PPVRSRV_DEVICE_NODE psDevNode)
{
	PHYS_HEAP *psPhysHeapNode = g_psPhysHeapList;
	PVRSRV_PHYS_HEAP eFallback;

	while (psPhysHeapNode)
	{
		if ((psPhysHeapNode->psDevNode == psDevNode) &&
			BIT_ISSET(psPhysHeapNode->ui32UsageFlags, ePhysHeap))
		{
			return psPhysHeapNode;
		}

		psPhysHeapNode = psPhysHeapNode->psNext;
	}

	eFallback = gasHeapProperties[ePhysHeap].eFallbackHeap;

	if (ePhysHeap == eFallback)
	{
		return NULL;
	}
	else
	{
		return _PhysHeapFindHeap(eFallback, psDevNode);
	}
}

PVRSRV_ERROR PhysHeapAcquireByDevPhysHeap(PVRSRV_PHYS_HEAP eDevPhysHeap,
										  PPVRSRV_DEVICE_NODE psDevNode,
										  PHYS_HEAP **ppsPhysHeap)
{
	PHYS_HEAP *psPhysHeap;
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_LOG_RETURN_IF_INVALID_PARAM(eDevPhysHeap < PVRSRV_PHYS_HEAP_LAST, "eDevPhysHeap");
	PVR_LOG_RETURN_IF_INVALID_PARAM(psDevNode != NULL, "psDevNode");

	PVR_DPF_ENTERED1(ui32Flags);

	OSLockAcquire(g_hPhysHeapLock);

	psPhysHeap = _PhysHeapFindHeap(eDevPhysHeap, psDevNode);

	if (psPhysHeap != NULL)
	{
		psPhysHeap->ui32RefCount++;
		PHYSHEAP_REFCOUNT_PRINT("%s: Heap %p, refcount = %d",
								__func__, psPhysHeap, psPhysHeap->ui32RefCount);
	}
	else
	{
		eError = PVRSRV_ERROR_PHYSHEAP_ID_INVALID;
	}

	OSLockRelease(g_hPhysHeapLock);

	*ppsPhysHeap = psPhysHeap;
	PVR_DPF_RETURN_RC1(eError, *ppsPhysHeap);
}

void PhysHeapRelease(PHYS_HEAP *psPhysHeap)
{
	PVR_DPF_ENTERED1(psPhysHeap);

	OSLockAcquire(g_hPhysHeapLock);
	psPhysHeap->ui32RefCount--;
	PHYSHEAP_REFCOUNT_PRINT("%s: Heap %p, refcount = %d",
							__func__, psPhysHeap, psPhysHeap->ui32RefCount);
	OSLockRelease(g_hPhysHeapLock);

	PVR_DPF_RETURN;
}

PHEAP_IMPL_DATA PhysHeapGetImplData(PHYS_HEAP *psPhysHeap)
{
	return psPhysHeap->pvImplData;
}

PHYS_HEAP_TYPE PhysHeapGetType(PHYS_HEAP *psPhysHeap)
{
	PVR_ASSERT(psPhysHeap->eType != PHYS_HEAP_TYPE_UNKNOWN);
	return psPhysHeap->eType;
}

PHYS_HEAP_USAGE_FLAGS PhysHeapGetFlags(PHYS_HEAP *psPhysHeap)
{
	return psPhysHeap->ui32UsageFlags;
}

/*
 * This function will set the psDevPAddr to whatever the system layer
 * has set it for the referenced region.
 * It will not fail if the psDevPAddr is invalid.
 */
PVRSRV_ERROR PhysHeapGetDevPAddr(PHYS_HEAP *psPhysHeap,
									   IMG_DEV_PHYADDR *psDevPAddr)
{
	PHEAP_IMPL_FUNCS *psImplFuncs = psPhysHeap->psImplFuncs;
	PVRSRV_ERROR eResult = PVRSRV_ERROR_NOT_IMPLEMENTED;

	if (psImplFuncs->pfnGetDevPAddr != NULL)
	{
		eResult = psImplFuncs->pfnGetDevPAddr(psPhysHeap->pvImplData,
											  psDevPAddr);
	}

	return eResult;
}

/*
 * This function will set the psCpuPAddr to whatever the system layer
 * has set it for the referenced region.
 * It will not fail if the psCpuPAddr is invalid.
 */
PVRSRV_ERROR PhysHeapGetCpuPAddr(PHYS_HEAP *psPhysHeap,
								IMG_CPU_PHYADDR *psCpuPAddr)
{
	PHEAP_IMPL_FUNCS *psImplFuncs = psPhysHeap->psImplFuncs;
	PVRSRV_ERROR eResult = PVRSRV_ERROR_NOT_IMPLEMENTED;

	if (psImplFuncs->pfnGetCPUPAddr != NULL)
	{
		eResult = psImplFuncs->pfnGetCPUPAddr(psPhysHeap->pvImplData,
											  psCpuPAddr);
	}

	return eResult;
}

PVRSRV_ERROR PhysHeapGetSize(PHYS_HEAP *psPhysHeap,
								   IMG_UINT64 *puiSize)
{
	PHEAP_IMPL_FUNCS *psImplFuncs = psPhysHeap->psImplFuncs;
	PVRSRV_ERROR eResult = PVRSRV_ERROR_NOT_IMPLEMENTED;

	if (psImplFuncs->pfnGetSize != NULL)
	{
		eResult = psImplFuncs->pfnGetSize(psPhysHeap->pvImplData,
										  puiSize);
	}

	return eResult;
}

void PhysHeapCpuPAddrToDevPAddr(PHYS_HEAP *psPhysHeap,
								IMG_UINT32 ui32NumOfAddr,
								IMG_DEV_PHYADDR *psDevPAddr,
								IMG_CPU_PHYADDR *psCpuPAddr)
{
	psPhysHeap->psMemFuncs->pfnCpuPAddrToDevPAddr(psPhysHeap->hPrivData,
												 ui32NumOfAddr,
												 psDevPAddr,
												 psCpuPAddr);
}

void PhysHeapDevPAddrToCpuPAddr(PHYS_HEAP *psPhysHeap,
								IMG_UINT32 ui32NumOfAddr,
								IMG_CPU_PHYADDR *psCpuPAddr,
								IMG_DEV_PHYADDR *psDevPAddr)
{
	psPhysHeap->psMemFuncs->pfnDevPAddrToCpuPAddr(psPhysHeap->hPrivData,
												 ui32NumOfAddr,
												 psCpuPAddr,
												 psDevPAddr);
}

IMG_CHAR *PhysHeapPDumpMemspaceName(PHYS_HEAP *psPhysHeap)
{
	return psPhysHeap->pszPDumpMemspaceName;
}

PVRSRV_ERROR PhysHeapCreatePMR(PHYS_HEAP *psPhysHeap,
							   struct _CONNECTION_DATA_ *psConnection,
							   IMG_DEVMEM_SIZE_T uiSize,
							   IMG_DEVMEM_SIZE_T uiChunkSize,
							   IMG_UINT32 ui32NumPhysChunks,
							   IMG_UINT32 ui32NumVirtChunks,
							   IMG_UINT32 *pui32MappingTable,
							   IMG_UINT32 uiLog2PageSize,
							   PVRSRV_MEMALLOCFLAGS_T uiFlags,
							   const IMG_CHAR *pszAnnotation,
							   IMG_PID uiPid,
							   PMR **ppsPMRPtr,
							   IMG_UINT32 ui32PDumpFlags)
{
	PHEAP_IMPL_FUNCS *psImplFuncs = psPhysHeap->psImplFuncs;

	return psImplFuncs->pfnCreatePMR(psPhysHeap,
									 psConnection,
									 uiSize,
									 uiChunkSize,
									 ui32NumPhysChunks,
									 ui32NumVirtChunks,
									 pui32MappingTable,
									 uiLog2PageSize,
									 uiFlags,
									 pszAnnotation,
									 uiPid,
									 ppsPMRPtr,
									 ui32PDumpFlags);
}

PVRSRV_ERROR PhysHeapInit(void)
{
	PVRSRV_ERROR eError;

	g_psPhysHeapList = NULL;

	eError = OSLockCreate(&g_hPhysHeapLock);
	PVR_LOG_RETURN_IF_ERROR(eError, "OSLockCreate");

	return PVRSRV_OK;
}

PVRSRV_ERROR PhysHeapDeinit(void)
{
	PVR_ASSERT(g_psPhysHeapList == NULL);

	OSLockDestroy(g_hPhysHeapLock);

	return PVRSRV_OK;
}

PPVRSRV_DEVICE_NODE PhysHeapDeviceNode(PHYS_HEAP *psPhysHeap)
{
	PVR_ASSERT(psPhysHeap != NULL);

	return psPhysHeap->psDevNode;
}

IMG_BOOL PhysHeapPVRLayerAcquire(PVRSRV_PHYS_HEAP ePhysHeap)
{
	PVR_ASSERT(ePhysHeap < PVRSRV_PHYS_HEAP_LAST);

	return gasHeapProperties[ePhysHeap].bPVRLayerAcquire;
}

IMG_BOOL PhysHeapUserModeAlloc(PVRSRV_PHYS_HEAP ePhysHeap)
{
	PVR_ASSERT(ePhysHeap < PVRSRV_PHYS_HEAP_LAST);

	return gasHeapProperties[ePhysHeap].bUserModeAlloc;
}
