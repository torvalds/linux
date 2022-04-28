/*************************************************************************/ /*!
@File           physmem_hostmem.c
@Title          Host memory device node functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Functions relevant to device memory allocations made from host
                mem device node.
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

#include "physmem_hostmem.h"

#include "img_defs.h"
#include "img_types.h"
#include "allocmem.h"
#include "physheap.h"
#include "pvrsrv_device.h"
#include "physheap.h"
#include "physmem_osmem.h"

static void HostMemCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
                                      IMG_UINT32 ui32NumOfAddr,
                                      IMG_DEV_PHYADDR *psDevPAddr,
                                      IMG_CPU_PHYADDR *psCpuPAddr);

static void HostMemDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
                                      IMG_UINT32 ui32NumOfAddr,
                                      IMG_CPU_PHYADDR *psCpuPAddr,
                                      IMG_DEV_PHYADDR *psDevPAddr);

/* heap callbacks for host driver's device's heap */
static PHYS_HEAP_FUNCTIONS gsHostMemDevPhysHeapFuncs =
{
	/* pfnCpuPAddrToDevPAddr */
	HostMemCpuPAddrToDevPAddr,
	/* pfnDevPAddrToCpuPAddr */
	HostMemDevPAddrToCpuPAddr,
};

static PVRSRV_DEVICE_CONFIG gsHostMemDevConfig[];

/* heap configuration for host driver's device */
static PHYS_HEAP_CONFIG gsPhysHeapConfigHostMemDevice[] =
{
	{
		PHYS_HEAP_TYPE_UMA,
		"SYSMEM",
		&gsHostMemDevPhysHeapFuncs,
		{0},
		{0},
		0,
		(IMG_HANDLE)&gsHostMemDevConfig[0],
		PHYS_HEAP_USAGE_CPU_LOCAL,
	}
};

/* device configuration for host driver's device */
static PVRSRV_DEVICE_CONFIG gsHostMemDevConfig[] =
{
	{
		.pszName = "HostMemDevice",
		.eCacheSnoopingMode = PVRSRV_DEVICE_SNOOP_NONE,
		.pasPhysHeaps = &gsPhysHeapConfigHostMemDevice[0],
		.ui32PhysHeapCount = ARRAY_SIZE(gsPhysHeapConfigHostMemDevice),
	}
};

static void HostMemCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
                                      IMG_UINT32 ui32NumOfAddr,
                                      IMG_DEV_PHYADDR *psDevPAddr,
                                      IMG_CPU_PHYADDR *psCpuPAddr)
{
	PVR_UNREFERENCED_PARAMETER(hPrivData);
	/* Optimise common case */
	psDevPAddr[0].uiAddr = psCpuPAddr[0].uiAddr;
	if (ui32NumOfAddr > 1)
	{
		IMG_UINT32 ui32Idx;
		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
		{
			psDevPAddr[ui32Idx].uiAddr = psCpuPAddr[ui32Idx].uiAddr;
		}
	}
}

static void HostMemDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
                                      IMG_UINT32 ui32NumOfAddr,
                                      IMG_CPU_PHYADDR *psCpuPAddr,
                                      IMG_DEV_PHYADDR *psDevPAddr)
{
	PVR_UNREFERENCED_PARAMETER(hPrivData);
	/* Optimise common case */
	psCpuPAddr[0].uiAddr = IMG_CAST_TO_CPUPHYADDR_UINT(psDevPAddr[0].uiAddr);
	if (ui32NumOfAddr > 1)
	{
		IMG_UINT32 ui32Idx;
		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
		{
			psCpuPAddr[ui32Idx].uiAddr = IMG_CAST_TO_CPUPHYADDR_UINT(psDevPAddr[ui32Idx].uiAddr);
		}
	}
}

PVRSRV_ERROR HostMemDeviceCreate(PVRSRV_DEVICE_NODE **ppsDeviceNode)
{
	PVRSRV_ERROR eError;
	PVRSRV_DEVICE_NODE *psDeviceNode;
	PVRSRV_DEVICE_CONFIG *psDevConfig = &gsHostMemDevConfig[0];

	/* Assert ensures HostMemory device isn't already created and
	 * that data is initialised */
	PVR_ASSERT(*ppsDeviceNode == NULL);

	/* for now, we only know a single heap (UMA) config for host device */
	PVR_ASSERT(psDevConfig->ui32PhysHeapCount == 1 &&
				psDevConfig->pasPhysHeaps[0].eType == PHYS_HEAP_TYPE_UMA);

	/* N.B.- In case of any failures in this function, we just return error to
	   the caller, as clean-up is taken care by _HostMemDeviceDestroy function */

	psDeviceNode = OSAllocZMem(sizeof(*psDeviceNode));
	PVR_LOG_RETURN_IF_NOMEM(psDeviceNode, "OSAllocZMem");

	/* early save return pointer to aid clean-up */
	*ppsDeviceNode = psDeviceNode;

	psDeviceNode->psDevConfig = psDevConfig;
	psDeviceNode->papsRegisteredPhysHeaps =
		OSAllocZMem(sizeof(*psDeviceNode->papsRegisteredPhysHeaps) *
					psDevConfig->ui32PhysHeapCount);
	PVR_LOG_RETURN_IF_NOMEM(psDeviceNode->papsRegisteredPhysHeaps, "OSAllocZMem");

	eError = PhysHeapCreateHeapFromConfig(psDeviceNode,
										  &psDevConfig->pasPhysHeaps[0],
										  &psDeviceNode->papsRegisteredPhysHeaps[0]);
	PVR_LOG_RETURN_IF_ERROR(eError, "PhysHeapCreateHeapFromConfig");
	psDeviceNode->ui32RegisteredPhysHeaps = 1;

	/* Only CPU local heap is valid on host-mem DevNode, so enable minimal callbacks */
	eError = PhysHeapAcquireByDevPhysHeap(PVRSRV_PHYS_HEAP_CPU_LOCAL,
										  psDeviceNode,
										  &psDeviceNode->apsPhysHeap[PVRSRV_PHYS_HEAP_CPU_LOCAL]);
	PVR_LOG_RETURN_IF_ERROR(eError, "PhysHeapAcquire");

	return PVRSRV_OK;
}

void HostMemDeviceDestroy(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	if (!psDeviceNode)
	{
		return;
	}

	if (psDeviceNode->papsRegisteredPhysHeaps)
	{
		if (psDeviceNode->apsPhysHeap[PVRSRV_PHYS_HEAP_CPU_LOCAL])
		{
			PhysHeapRelease(psDeviceNode->apsPhysHeap[PVRSRV_PHYS_HEAP_CPU_LOCAL]);
		}

		if (psDeviceNode->papsRegisteredPhysHeaps[0])
		{
			/* clean-up function as well is aware of only one heap */
			PVR_ASSERT(psDeviceNode->ui32RegisteredPhysHeaps == 1);
			PhysHeapDestroy(psDeviceNode->papsRegisteredPhysHeaps[0]);
		}

		OSFreeMem(psDeviceNode->papsRegisteredPhysHeaps);
	}
	OSFreeMem(psDeviceNode);
}
