/*************************************************************************/ /*!
@File			vmm_pvz_client.c
@Title          VM manager client para-virtualization
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header provides VMM client para-virtualization APIs
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

#include "pvrsrv.h"
#include "img_types.h"
#include "img_defs.h"
#include "pvrsrv_error.h"

#include "vmm_impl.h"
#include "vz_vmm_pvz.h"
#include "vmm_pvz_client.h"


static inline void
PvzClientLockAcquire(void)
{
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
	OSLockAcquire(psPVRSRVData->hPvzConnectionLock);
}

static inline void
PvzClientLockRelease(void)
{
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
	OSLockRelease(psPVRSRVData->hPvzConnectionLock);
}

/*
 * ===========================================================
 *  The following client para-virtualization (pvz) functions
 *  are exclusively called by guests to initiate a pvz call
 *  to the host via hypervisor (guest -> vm manager -> host)
 * ===========================================================
 */

PVRSRV_ERROR
PvzClientMapDevPhysHeap(PVRSRV_DEVICE_CONFIG *psDevConfig)
{
	PVRSRV_ERROR eError;
	IMG_DEV_PHYADDR sDevPAddr;
	VMM_PVZ_CONNECTION *psVmmPvz;
	IMG_UINT32 uiFuncID = PVZ_BRIDGE_MAPDEVICEPHYSHEAP;
	PHYS_HEAP *psFwPhysHeap = psDevConfig->psDevNode->apsPhysHeap[PVRSRV_PHYS_HEAP_FW_CONFIG];

	eError = PhysHeapGetDevPAddr(psFwPhysHeap, &sDevPAddr);

#if defined(PVR_PMR_TRANSLATE_UMA_ADDRESSES)
{
	/* Host expects PA rather than IPA address, so on the platforms where
	 * IPA-PA translation is not done in hw, performs a software translation */

	IMG_DEV_PHYADDR sDevPAddrTranslated;

	PhysHeapCpuPAddrToDevPAddr(psFwPhysHeap, 1, &sDevPAddrTranslated, (IMG_CPU_PHYADDR *)&sDevPAddr);
	sDevPAddr.uiAddr = sDevPAddrTranslated.uiAddr;
}
#endif

	PVR_LOG_RETURN_IF_ERROR(eError, "PhysHeapGetDevPAddr");
	PVR_LOG_RETURN_IF_FALSE((sDevPAddr.uiAddr != 0), "PhysHeapGetDevPAddr", PVRSRV_ERROR_INVALID_PARAMS);

	psVmmPvz = PvzConnectionAcquire();
	PvzClientLockAcquire();

	eError = psVmmPvz->sClientFuncTab.pfnMapDevPhysHeap(uiFuncID,
													    0,
													    RGX_FIRMWARE_RAW_HEAP_SIZE,
													    sDevPAddr.uiAddr);

	PvzClientLockRelease();
	PvzConnectionRelease(psVmmPvz);

	return eError;
}

PVRSRV_ERROR
PvzClientUnmapDevPhysHeap(PVRSRV_DEVICE_CONFIG *psDevConfig)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 uiFuncID = PVZ_BRIDGE_UNMAPDEVICEPHYSHEAP;
	VMM_PVZ_CONNECTION *psVmmPvz = PvzConnectionAcquire();
	PVR_ASSERT(psVmmPvz);

	PvzClientLockAcquire();

	PVR_ASSERT(psVmmPvz->sClientFuncTab.pfnUnmapDevPhysHeap);

	eError = psVmmPvz->sClientFuncTab.pfnUnmapDevPhysHeap(uiFuncID, 0);

	PvzClientLockRelease();
	PvzConnectionRelease(psVmmPvz);

	return eError;
}

/******************************************************************************
 End of file (vmm_pvz_client.c)
******************************************************************************/
