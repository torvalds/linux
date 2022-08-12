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

/* NOTE: Table entries and order must match enum PVRSRV_PHYS_HEAP to ensure
 * correct operation of PhysHeapCreatePMR().
 */
static PHYS_HEAP_PROPERTIES gasHeapProperties[PVRSRV_PHYS_HEAP_LAST] =
{
	/* eFallbackHeap,               bPVRLayerAcquire, bUserModeAlloc */
    {  PVRSRV_PHYS_HEAP_DEFAULT,    IMG_TRUE,         IMG_TRUE  }, /* DEFAULT */
    {  PVRSRV_PHYS_HEAP_DEFAULT,    IMG_TRUE,         IMG_TRUE  }, /* GPU_LOCAL */
    {  PVRSRV_PHYS_HEAP_DEFAULT,    IMG_TRUE,         IMG_TRUE  }, /* CPU_LOCAL */
    {  PVRSRV_PHYS_HEAP_DEFAULT,    IMG_TRUE,         IMG_TRUE  }, /* GPU_PRIVATE */
    {  PVRSRV_PHYS_HEAP_GPU_LOCAL,  IMG_FALSE,        IMG_FALSE }, /* FW_MAIN */
    {  PVRSRV_PHYS_HEAP_GPU_LOCAL,  IMG_TRUE,         IMG_FALSE }, /* EXTERNAL */
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

static_assert((ARRAY_SIZE(gasHeapProperties) == PVRSRV_PHYS_HEAP_LAST),
	"Size or order of gasHeapProperties entries incorrect for PVRSRV_PHYS_HEAP enum");

void PVRSRVGetDevicePhysHeapCount(PVRSRV_DEVICE_NODE *psDevNode,
								  IMG_UINT32 *pui32PhysHeapCount)
{
	*pui32PhysHeapCount = psDevNode->ui32UserAllocHeapCount;
}

static IMG_UINT32 PhysHeapOSGetPageShift(void)
{
	return (IMG_UINT32)OSGetPageShift();
}

static PHEAP_IMPL_FUNCS _sPHEAPImplFuncs =
{
	.pfnDestroyData = NULL,
	.pfnGetPMRFactoryMemStats = PhysmemGetOSRamMemStats,
	.pfnCreatePMR = PhysmemNewOSRamBackedPMR,
	.pfnPagesAlloc = &OSPhyContigPagesAlloc,
	.pfnPagesFree = &OSPhyContigPagesFree,
	.pfnPagesMap = &OSPhyContigPagesMap,
	.pfnPagesUnMap = &OSPhyContigPagesUnmap,
	.pfnPagesClean = &OSPhyContigPagesClean,
	.pfnGetPageShift = &PhysHeapOSGetPageShift,
};

/*************************************************************************/ /*!
@Function       _PhysHeapDebugRequest
@Description    This function is used to output debug information for a given
                device's PhysHeaps.
@Input          pfnDbgRequestHandle Data required by this function that is
                                    passed through the RegisterDeviceDbgRequestNotify
                                    function.
@Input          ui32VerbLevel       The maximum verbosity of the debug request.
@Input          pfnDumpDebugPrintf  The specified print function that should be
                                    used to dump any debug information
                                    (see PVRSRVDebugRequest).
@Input          pvDumpDebugFile     Optional file identifier to be passed to
                                    the print function if required.
@Return         void
*/ /**************************************************************************/
static void _PhysHeapDebugRequest(PVRSRV_DBGREQ_HANDLE pfnDbgRequestHandle,
                                  IMG_UINT32 ui32VerbLevel,
                                  DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                                  void *pvDumpDebugFile)
{
	static const IMG_CHAR *const pszTypeStrings[] = {
		"UNKNOWN",
		"UMA",
		"LMA",
		"DMA",
#if defined(SUPPORT_WRAP_EXTMEMOBJECT)
		"WRAP"
#endif
	};

	PPVRSRV_DEVICE_NODE psDeviceNode = (PPVRSRV_DEVICE_NODE)pfnDbgRequestHandle;
	PHYS_HEAP *psPhysHeap = NULL;
	IMG_UINT64 ui64TotalSize;
	IMG_UINT64 ui64FreeSize;
	IMG_UINT32 i;

	PVR_LOG_RETURN_VOID_IF_FALSE(psDeviceNode != NULL,
	                             "Phys Heap debug request failed. psDeviceNode was NULL");

	PVR_DUMPDEBUG_LOG("------[ Device ID: %d - Phys Heaps ]------",
	                  psDeviceNode->sDevId.i32OsDeviceID);

	for (i = 0; i < psDeviceNode->ui32RegisteredPhysHeaps; i++)
	{
		psPhysHeap = psDeviceNode->papsRegisteredPhysHeaps[i];

		if (psPhysHeap->eType >= ARRAY_SIZE(pszTypeStrings))
		{
			PVR_DPF((PVR_DBG_ERROR,
			         "PhysHeap at address %p eType is not a PHYS_HEAP_TYPE",
			         psPhysHeap));
			break;
		}

		psPhysHeap->psImplFuncs->pfnGetPMRFactoryMemStats(psPhysHeap->pvImplData,
		                                                  &ui64TotalSize,
		                                                  &ui64FreeSize);

		if (psPhysHeap->eType == PHYS_HEAP_TYPE_LMA)
		{
			IMG_CPU_PHYADDR sCPUPAddr;
			IMG_DEV_PHYADDR sGPUPAddr;
			PVRSRV_ERROR eError;

			PVR_ASSERT(psPhysHeap->psImplFuncs->pfnGetCPUPAddr != NULL);
			PVR_ASSERT(psPhysHeap->psImplFuncs->pfnGetDevPAddr != NULL);

			eError = psPhysHeap->psImplFuncs->pfnGetCPUPAddr(psPhysHeap->pvImplData,
			                                                 &sCPUPAddr);
			if (eError != PVRSRV_OK)
			{
				PVR_LOG_ERROR(eError, "pfnGetCPUPAddr");
				sCPUPAddr.uiAddr = IMG_CAST_TO_CPUPHYADDR_UINT(IMG_UINT64_MAX);
			}

			eError = psPhysHeap->psImplFuncs->pfnGetDevPAddr(psPhysHeap->pvImplData,
			                                                 &sGPUPAddr);
			if (eError != PVRSRV_OK)
			{
				PVR_LOG_ERROR(eError, "pfnGetDevPAddr");
				sGPUPAddr.uiAddr = IMG_UINT64_MAX;
			}

			PVR_DUMPDEBUG_LOG("0x%p -> Name: %s, Type: %s, "

			                  "CPU PA Base: " CPUPHYADDR_UINT_FMTSPEC", "
			                  "GPU PA Base: 0x%08"IMG_UINT64_FMTSPECx", "
			                  "Usage Flags: 0x%08x, Refs: %d, "
			                  "Free Size: %"IMG_UINT64_FMTSPEC", "
			                  "Total Size: %"IMG_UINT64_FMTSPEC,
			                  psPhysHeap,
			                  psPhysHeap->pszPDumpMemspaceName,
			                  pszTypeStrings[psPhysHeap->eType],
			                  CPUPHYADDR_FMTARG(sCPUPAddr.uiAddr),
			                  sGPUPAddr.uiAddr,
			                  psPhysHeap->ui32UsageFlags,
			                  psPhysHeap->ui32RefCount,
			                  ui64FreeSize,
			                  ui64TotalSize);
		}
		else
		{
			PVR_DUMPDEBUG_LOG("0x%p -> Name: %s, Type: %s, "
			                  "Usage Flags: 0x%08x, Refs: %d, "
			                  "Free Size: %"IMG_UINT64_FMTSPEC", "
			                  "Total Size: %"IMG_UINT64_FMTSPEC,
			                  psPhysHeap,
			                  psPhysHeap->pszPDumpMemspaceName,
			                  pszTypeStrings[psPhysHeap->eType],
			                  psPhysHeap->ui32UsageFlags,
			                  psPhysHeap->ui32RefCount,
			                  ui64FreeSize,
			                  ui64TotalSize);
		}
	}
}

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
PhysHeapCreateDeviceHeapsFromConfigs(PPVRSRV_DEVICE_NODE psDevNode,
                                     PHYS_HEAP_CONFIG *pasConfigs,
                                     IMG_UINT32 ui32NumConfigs)
{
	IMG_UINT32 i;
	PVRSRV_ERROR eError;

	/* Register the physical memory heaps */
	psDevNode->papsRegisteredPhysHeaps =
		OSAllocZMem(sizeof(*psDevNode->papsRegisteredPhysHeaps) * ui32NumConfigs);
	PVR_LOG_RETURN_IF_NOMEM(psDevNode->papsRegisteredPhysHeaps, "OSAllocZMem");

	psDevNode->ui32RegisteredPhysHeaps = 0;

	for (i = 0; i < ui32NumConfigs; i++)
	{
		eError = PhysHeapCreateHeapFromConfig(psDevNode,
											  pasConfigs + i,
											  psDevNode->papsRegisteredPhysHeaps + i);
		PVR_LOG_RETURN_IF_ERROR(eError, "PhysmemCreateHeap");

		psDevNode->ui32RegisteredPhysHeaps++;
	}

#if defined(SUPPORT_PHYSMEM_TEST)
	/* For a temporary device node there will never be a debug dump
	 * request targeting it */
	if (psDevNode->hDebugTable != NULL)
#endif
	{
		eError = PVRSRVRegisterDeviceDbgRequestNotify(&psDevNode->hPhysHeapDbgReqNotify,
		                                              psDevNode,
		                                              _PhysHeapDebugRequest,
		                                              DEBUG_REQUEST_SYS,
		                                              psDevNode);

		PVR_LOG_RETURN_IF_ERROR(eError, "PVRSRVRegisterDeviceDbgRequestNotify");
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

void PhysHeapDestroyDeviceHeaps(PPVRSRV_DEVICE_NODE psDevNode)
{
	IMG_UINT32 i;

	if (psDevNode->hPhysHeapDbgReqNotify)
	{
		PVRSRVUnregisterDeviceDbgRequestNotify(psDevNode->hPhysHeapDbgReqNotify);
	}

	/* Unregister heaps */
	for (i = 0; i < psDevNode->ui32RegisteredPhysHeaps; i++)
	{
		PhysHeapDestroy(psDevNode->papsRegisteredPhysHeaps[i]);
	}

	OSFreeMem(psDevNode->papsRegisteredPhysHeaps);
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
									PPVRSRV_DEVICE_NODE psDevNode,
									PHYS_HEAP **ppsPhysHeap)
{
	PHYS_HEAP *psNode = g_psPhysHeapList;
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_LOG_RETURN_IF_INVALID_PARAM(ui32UsageFlag != 0, "ui32UsageFlag");
	PVR_LOG_RETURN_IF_INVALID_PARAM(psDevNode != NULL, "psDevNode");

	PVR_DPF_ENTERED1(ui32UsageFlag);

	OSLockAcquire(g_hPhysHeapLock);

	while (psNode)
	{
		if (psNode->psDevNode != psDevNode)
		{
			psNode = psNode->psNext;
			continue;
		}
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

	if (ePhysHeap == PVRSRV_PHYS_HEAP_DEFAULT)
	{
		ePhysHeap = psDevNode->psDevConfig->eDefaultHeap;
	}

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

	PVR_LOG_RETURN_IF_INVALID_PARAM(eDevPhysHeap != PVRSRV_PHYS_HEAP_DEFAULT, "eDevPhysHeap");
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

IMG_BOOL PhysHeapValidateDefaultHeapExists(PPVRSRV_DEVICE_NODE psDevNode)
{
	PHYS_HEAP *psDefaultHeap;
	IMG_BOOL bDefaultHeapFound;
	PhysHeapAcquireByUsage(1<<(psDevNode->psDevConfig->eDefaultHeap), psDevNode, &psDefaultHeap);
	if (psDefaultHeap == NULL)
	{
		bDefaultHeapFound = IMG_FALSE;
	}
	else
	{
		PhysHeapRelease(psDefaultHeap);
		bDefaultHeapFound = IMG_TRUE;
	}
	return bDefaultHeapFound;
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

PVRSRV_ERROR
PhysHeapGetMemInfo(PVRSRV_DEVICE_NODE *psDevNode,
              IMG_UINT32 ui32PhysHeapCount,
			  PVRSRV_PHYS_HEAP *paePhysHeapID,
			  PHYS_HEAP_MEM_STATS_PTR paPhysHeapMemStats)
{
	IMG_UINT32 i = 0;
	PHYS_HEAP *psPhysHeap;

	PVR_ASSERT(ui32PhysHeapCount <= PVRSRV_PHYS_HEAP_LAST);

	for (i = 0; i < ui32PhysHeapCount; i++)
	{
		if (paePhysHeapID[i] >= PVRSRV_PHYS_HEAP_LAST)
		{
			return PVRSRV_ERROR_PHYSHEAP_ID_INVALID;
		}

		if (paePhysHeapID[i] == PVRSRV_PHYS_HEAP_DEFAULT)
		{
			return PVRSRV_ERROR_INVALID_PARAMS;
		}

		psPhysHeap = _PhysHeapFindHeap(paePhysHeapID[i], psDevNode);

		paPhysHeapMemStats[i].ui32PhysHeapFlags = 0;

		if (psPhysHeap && PhysHeapUserModeAlloc(paePhysHeapID[i])
				&& psPhysHeap->psImplFuncs->pfnGetPMRFactoryMemStats)
		{
			psPhysHeap->psImplFuncs->pfnGetPMRFactoryMemStats(psPhysHeap->pvImplData,
					&paPhysHeapMemStats[i].ui64TotalSize,
					&paPhysHeapMemStats[i].ui64FreeSize);
			paPhysHeapMemStats[i].ui32PhysHeapFlags |= PhysHeapGetType(psPhysHeap);

			if (paePhysHeapID[i] == psDevNode->psDevConfig->eDefaultHeap)
			{
				paPhysHeapMemStats[i].ui32PhysHeapFlags |= PVRSRV_PHYS_HEAP_FLAGS_IS_DEFAULT;
			}
		}
		else
		{
			paPhysHeapMemStats[i].ui64TotalSize = 0;
			paPhysHeapMemStats[i].ui64FreeSize = 0;
		}
	}

	return PVRSRV_OK;
}

PVRSRV_ERROR
PhysHeapGetMemInfoPkd(PVRSRV_DEVICE_NODE *psDevNode,
              IMG_UINT32 ui32PhysHeapCount,
			  PVRSRV_PHYS_HEAP *paePhysHeapID,
			  PHYS_HEAP_MEM_STATS_PKD_PTR paPhysHeapMemStats)
{
	IMG_UINT32 i = 0;
	PHYS_HEAP *psPhysHeap;

	PVR_ASSERT(ui32PhysHeapCount <= PVRSRV_PHYS_HEAP_LAST);

	for (i = 0; i < ui32PhysHeapCount; i++)
	{
		if (paePhysHeapID[i] >= PVRSRV_PHYS_HEAP_LAST)
		{
			return PVRSRV_ERROR_PHYSHEAP_ID_INVALID;
		}

		if (paePhysHeapID[i] == PVRSRV_PHYS_HEAP_DEFAULT)
		{
			return PVRSRV_ERROR_INVALID_PARAMS;
		}

		psPhysHeap = _PhysHeapFindHeap(paePhysHeapID[i], psDevNode);

		paPhysHeapMemStats[i].ui32PhysHeapFlags = 0;

		if (psPhysHeap && PhysHeapUserModeAlloc(paePhysHeapID[i])
				&& psPhysHeap->psImplFuncs->pfnGetPMRFactoryMemStats)
		{
			psPhysHeap->psImplFuncs->pfnGetPMRFactoryMemStats(psPhysHeap->pvImplData,
					&paPhysHeapMemStats[i].ui64TotalSize,
					&paPhysHeapMemStats[i].ui64FreeSize);
			paPhysHeapMemStats[i].ui32PhysHeapFlags |= PhysHeapGetType(psPhysHeap);

			if (paePhysHeapID[i] == psDevNode->psDevConfig->eDefaultHeap)
			{
				paPhysHeapMemStats[i].ui32PhysHeapFlags |= PVRSRV_PHYS_HEAP_FLAGS_IS_DEFAULT;
			}
		}
		else
		{
			paPhysHeapMemStats[i].ui64TotalSize = 0;
			paPhysHeapMemStats[i].ui64FreeSize = 0;
		}
	}

	return PVRSRV_OK;
}

void PhysheapGetPhysMemUsage(PHYS_HEAP *psPhysHeap, IMG_UINT64 *pui64TotalSize, IMG_UINT64 *pui64FreeSize)
{
	if (psPhysHeap && psPhysHeap->psImplFuncs->pfnGetPMRFactoryMemStats)
	{
		psPhysHeap->psImplFuncs->pfnGetPMRFactoryMemStats(psPhysHeap->pvImplData,
				pui64TotalSize,
				pui64FreeSize);
	}
	else
	{
		*pui64TotalSize = *pui64FreeSize = 0;
	}
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

void PhysHeapDeinit(void)
{
	PVR_ASSERT(g_psPhysHeapList == NULL);

	OSLockDestroy(g_hPhysHeapLock);
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

#if defined(SUPPORT_GPUVIRT_VALIDATION)
/*************************************************************************/ /*!
@Function       CreateGpuVirtValArenas
@Description    Create virtualization validation arenas
@Input          psDeviceNode The device node
@Return         PVRSRV_ERROR PVRSRV_OK on success
*/ /**************************************************************************/
static PVRSRV_ERROR CreateGpuVirtValArenas(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	/* aui64OSidMin and aui64OSidMax are what we program into HW registers.
	   The values are different from base/size of arenas. */
	IMG_UINT64 aui64OSidMin[GPUVIRT_VALIDATION_NUM_REGIONS][GPUVIRT_VALIDATION_NUM_OS];
	IMG_UINT64 aui64OSidMax[GPUVIRT_VALIDATION_NUM_REGIONS][GPUVIRT_VALIDATION_NUM_OS];
	PHYS_HEAP_CONFIG *psGPULocalHeap = FindPhysHeapConfig(psDeviceNode->psDevConfig, PHYS_HEAP_USAGE_GPU_LOCAL);
	PHYS_HEAP_CONFIG *psDisplayHeap = FindPhysHeapConfig(psDeviceNode->psDevConfig, PHYS_HEAP_USAGE_DISPLAY);
	IMG_UINT64 uBase;
	IMG_UINT64 uSize;
	IMG_UINT64 uBaseShared;
	IMG_UINT64 uSizeShared;
	IMG_UINT64 uSizeSharedReg;
	IMG_UINT32 i;

	/* Shared region is fixed size, the remaining space is divided amongst OSes */
	uSizeShared = PVR_ALIGN(GPUVIRT_SIZEOF_SHARED, (IMG_DEVMEM_SIZE_T)OSGetPageSize());
	uSize = psGPULocalHeap->uiSize - uSizeShared;
	uSize /= GPUVIRT_VALIDATION_NUM_OS;
	uSize = uSize & ~((IMG_UINT64)OSGetPageSize() - 1ULL); /* Align, round down */

	uBase = psGPULocalHeap->sCardBase.uiAddr;
	uBaseShared = uBase + uSize * GPUVIRT_VALIDATION_NUM_OS;
	uSizeShared = psGPULocalHeap->uiSize - (uBaseShared - uBase);

	PVR_LOG(("GPUVIRT_VALIDATION split GPU_LOCAL base: 0x%" IMG_UINT64_FMTSPECX ", size: 0x%" IMG_UINT64_FMTSPECX ".",
			 psGPULocalHeap->sCardBase.uiAddr,
			 psGPULocalHeap->uiSize));

	/* If a display heap config exists, include the display heap in the non-secure regions */
	if (psDisplayHeap)
	{
		/* Only works when DISPLAY heap follows GPU_LOCAL heap. */
		PVR_LOG(("GPUVIRT_VALIDATION include DISPLAY in shared, base: 0x%" IMG_UINT64_FMTSPECX ", size: 0x%" IMG_UINT64_FMTSPECX ".",
				 psDisplayHeap->sCardBase.uiAddr,
				 psDisplayHeap->uiSize));

		uSizeSharedReg = uSizeShared + psDisplayHeap->uiSize;
	}
	else
	{
		uSizeSharedReg = uSizeShared;
	}

	PVR_ASSERT(uSize >= GPUVIRT_MIN_SIZE);
	PVR_ASSERT(uSizeSharedReg >= GPUVIRT_SIZEOF_SHARED);

	for (i = 0; i < GPUVIRT_VALIDATION_NUM_OS; i++)
	{
		IMG_CHAR aszOSRAName[RA_MAX_NAME_LENGTH];

		PVR_LOG(("GPUVIRT_VALIDATION create arena OS: %d, base: 0x%" IMG_UINT64_FMTSPECX ", size: 0x%" IMG_UINT64_FMTSPECX ".", i, uBase, uSize));

		OSSNPrintf(aszOSRAName, RA_MAX_NAME_LENGTH, "GPUVIRT_OS%d", i);

		psDeviceNode->psOSidSubArena[i] = RA_Create_With_Span(aszOSRAName,
		                                                      OSGetPageShift(),
		                                                      0,
		                                                      uBase,
		                                                      uSize);
		PVR_LOG_RETURN_IF_NOMEM(psDeviceNode->psOSidSubArena[i], "RA_Create_With_Span");

		aui64OSidMin[GPUVIRT_VAL_REGION_SECURE][i] = uBase;

		if (i == 0)
		{
			/* OSid0 has access to all regions */
			aui64OSidMax[GPUVIRT_VAL_REGION_SECURE][i] = psGPULocalHeap->uiSize - 1ULL;
		}
		else
		{
			aui64OSidMax[GPUVIRT_VAL_REGION_SECURE][i] = uBase + uSize - 1ULL;
		}

		/* uSizeSharedReg includes display heap */
		aui64OSidMin[GPUVIRT_VAL_REGION_SHARED][i] = uBaseShared;
		aui64OSidMax[GPUVIRT_VAL_REGION_SHARED][i] = uBaseShared + uSizeSharedReg - 1ULL;

		PVR_LOG(("GPUVIRT_VALIDATION HW reg regions %d: min[0]: 0x%" IMG_UINT64_FMTSPECX ", max[0]: 0x%" IMG_UINT64_FMTSPECX ", min[1]: 0x%" IMG_UINT64_FMTSPECX ", max[1]: 0x%" IMG_UINT64_FMTSPECX ",",
				 i,
				 aui64OSidMin[GPUVIRT_VAL_REGION_SECURE][i],
				 aui64OSidMax[GPUVIRT_VAL_REGION_SECURE][i],
				 aui64OSidMin[GPUVIRT_VAL_REGION_SHARED][i],
				 aui64OSidMax[GPUVIRT_VAL_REGION_SHARED][i]));
		uBase += uSize;
	}

	PVR_LOG(("GPUVIRT_VALIDATION create arena Shared, base: 0x%" IMG_UINT64_FMTSPECX ", size: 0x%" IMG_UINT64_FMTSPECX ".", uBaseShared, uSizeShared));

	PVR_ASSERT(uSizeShared >= GPUVIRT_SIZEOF_SHARED);

	/* uSizeShared does not include  display heap */
	psDeviceNode->psOSSharedArena = RA_Create_With_Span("GPUVIRT_SHARED",
	                                                    OSGetPageShift(),
	                                                    0,
	                                                    uBaseShared,
	                                                    uSizeShared);
	PVR_LOG_RETURN_IF_NOMEM(psDeviceNode->psOSSharedArena, "RA_Create_With_Span");

	if (psDeviceNode->psDevConfig->pfnSysDevVirtInit != NULL)
	{
		psDeviceNode->psDevConfig->pfnSysDevVirtInit(aui64OSidMin, aui64OSidMax);
	}

	return PVRSRV_OK;
}

/*
 * Counter-part to CreateGpuVirtValArenas.
 */
static void DestroyGpuVirtValArenas(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	IMG_UINT32	uiCounter = 0;

	/*
	 * NOTE: We overload psOSidSubArena[0] into the psLocalMemArena so we must
	 * not free it here as it gets cleared later.
	 */
	for (uiCounter = 1; uiCounter < GPUVIRT_VALIDATION_NUM_OS; uiCounter++)
	{
		if (psDeviceNode->psOSidSubArena[uiCounter] == NULL)
		{
			continue;
		}
		RA_Delete(psDeviceNode->psOSidSubArena[uiCounter]);
	}

	if (psDeviceNode->psOSSharedArena != NULL)
	{
		RA_Delete(psDeviceNode->psOSSharedArena);
	}
}
#endif

PVRSRV_ERROR PhysHeapMMUPxSetup(PPVRSRV_DEVICE_NODE psDeviceNode)
{
	PHYS_HEAP_TYPE eHeapType;
	PVRSRV_ERROR eError;

	eError = PhysHeapAcquireByDevPhysHeap(psDeviceNode->psDevConfig->eDefaultHeap,
	                                      psDeviceNode, &psDeviceNode->psMMUPhysHeap);
	PVR_LOG_GOTO_IF_ERROR(eError, "PhysHeapAcquireByDevPhysHeap", ErrorDeinit);

	eHeapType = PhysHeapGetType(psDeviceNode->psMMUPhysHeap);

	if (eHeapType == PHYS_HEAP_TYPE_UMA)
	{
		PVR_DPF((PVR_DBG_MESSAGE, "%s: GPU physical heap uses OS System memory (UMA)", __func__));

#if defined(SUPPORT_GPUVIRT_VALIDATION)
		PVR_DPF((PVR_DBG_ERROR, "%s: Virtualisation Validation builds are currently only"
								 " supported on systems with local memory (LMA).", __func__));
		eError = PVRSRV_ERROR_NOT_SUPPORTED;
		goto ErrorDeinit;
#endif
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "%s: GPU physical heap uses local memory managed by the driver (LMA)", __func__));

#if defined(SUPPORT_GPUVIRT_VALIDATION)
		eError = CreateGpuVirtValArenas(psDeviceNode);
		PVR_LOG_GOTO_IF_ERROR(eError, "CreateGpuVirtValArenas", ErrorDeinit);
#endif
	}

	return PVRSRV_OK;
ErrorDeinit:
	return eError;
}

void PhysHeapMMUPxDeInit(PPVRSRV_DEVICE_NODE psDeviceNode)
{
#if defined(SUPPORT_GPUVIRT_VALIDATION)
	/* Remove local LMA subarenas */
	DestroyGpuVirtValArenas(psDeviceNode);
#endif	/* defined(SUPPORT_GPUVIRT_VALIDATION) */

	if (psDeviceNode->psMMUPhysHeap != NULL)
	{
		PhysHeapRelease(psDeviceNode->psMMUPhysHeap);
		psDeviceNode->psMMUPhysHeap = NULL;
	}
}

#if defined(SUPPORT_GPUVIRT_VALIDATION)
PVRSRV_ERROR PhysHeapPagesAllocGPV(PHYS_HEAP *psPhysHeap, size_t uiSize,
                                   PG_HANDLE *psMemHandle,
                                   IMG_DEV_PHYADDR *psDevPAddr,
                                   IMG_UINT32 ui32OSid, IMG_PID uiPid)
{
	PHEAP_IMPL_FUNCS *psImplFuncs = psPhysHeap->psImplFuncs;
	PVRSRV_ERROR eResult = PVRSRV_ERROR_NOT_IMPLEMENTED;

	if (psImplFuncs->pfnPagesAllocGPV != NULL)
	{
		eResult = psImplFuncs->pfnPagesAllocGPV(psPhysHeap,
		                                        uiSize, psMemHandle, psDevPAddr, ui32OSid, uiPid);
	}

	return eResult;
}
#endif

PVRSRV_ERROR PhysHeapPagesAlloc(PHYS_HEAP *psPhysHeap, size_t uiSize,
								PG_HANDLE *psMemHandle,
								IMG_DEV_PHYADDR *psDevPAddr,
								IMG_PID uiPid)
{
	PHEAP_IMPL_FUNCS *psImplFuncs = psPhysHeap->psImplFuncs;
	PVRSRV_ERROR eResult = PVRSRV_ERROR_NOT_IMPLEMENTED;

	if (psImplFuncs->pfnPagesAlloc != NULL)
	{
		eResult = psImplFuncs->pfnPagesAlloc(psPhysHeap,
		                                       uiSize, psMemHandle, psDevPAddr, uiPid);
	}

	return eResult;
}

void PhysHeapPagesFree(PHYS_HEAP *psPhysHeap, PG_HANDLE *psMemHandle)
{
	PHEAP_IMPL_FUNCS *psImplFuncs = psPhysHeap->psImplFuncs;

	PVR_ASSERT(psImplFuncs->pfnPagesFree != NULL);

	if (psImplFuncs->pfnPagesFree != NULL)
	{
		psImplFuncs->pfnPagesFree(psPhysHeap,
		                          psMemHandle);
	}
}

PVRSRV_ERROR PhysHeapPagesMap(PHYS_HEAP *psPhysHeap, PG_HANDLE *pshMemHandle, size_t uiSize, IMG_DEV_PHYADDR *psDevPAddr,
							  void **pvPtr)
{
	PHEAP_IMPL_FUNCS *psImplFuncs = psPhysHeap->psImplFuncs;
	PVRSRV_ERROR eResult = PVRSRV_ERROR_NOT_IMPLEMENTED;

	if (psImplFuncs->pfnPagesMap != NULL)
	{
		eResult = psImplFuncs->pfnPagesMap(psPhysHeap,
		                                   pshMemHandle, uiSize, psDevPAddr, pvPtr);
	}

	return eResult;
}

void PhysHeapPagesUnMap(PHYS_HEAP *psPhysHeap, PG_HANDLE *psMemHandle, void *pvPtr)
{
	PHEAP_IMPL_FUNCS *psImplFuncs = psPhysHeap->psImplFuncs;

	PVR_ASSERT(psImplFuncs->pfnPagesUnMap != NULL);

	if (psImplFuncs->pfnPagesUnMap != NULL)
	{
		psImplFuncs->pfnPagesUnMap(psPhysHeap,
		                           psMemHandle, pvPtr);
	}
}

PVRSRV_ERROR PhysHeapPagesClean(PHYS_HEAP *psPhysHeap, PG_HANDLE *pshMemHandle,
							  IMG_UINT32 uiOffset,
							  IMG_UINT32 uiLength)
{
	PHEAP_IMPL_FUNCS *psImplFuncs = psPhysHeap->psImplFuncs;
	PVRSRV_ERROR eResult = PVRSRV_ERROR_NOT_IMPLEMENTED;

	if (psImplFuncs->pfnPagesClean != NULL)
	{
		eResult = psImplFuncs->pfnPagesClean(psPhysHeap,
		                                     pshMemHandle, uiOffset, uiLength);
	}

	return eResult;
}

IMG_UINT32 PhysHeapGetPageShift(PHYS_HEAP *psPhysHeap)
{
	PHEAP_IMPL_FUNCS *psImplFuncs = psPhysHeap->psImplFuncs;
	IMG_UINT32 ui32PageShift = 0;

	PVR_ASSERT(psImplFuncs->pfnGetPageShift != NULL);

	if (psImplFuncs->pfnGetPageShift != NULL)
	{
		ui32PageShift = psImplFuncs->pfnGetPageShift();
	}

	return ui32PageShift;
}
