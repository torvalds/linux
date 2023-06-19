/*************************************************************************/ /*!
@File
@Title          System Configuration
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    System Configuration functions
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
#include "pvrsrv_device.h"
#include "syscommon.h"
#include "vz_vmm_pvz.h"
#include "allocmem.h"
#include "sysinfo.h"
#include "sysconfig.h"
#include "physheap.h"
#include "pvr_debug.h"
#if defined(SUPPORT_ION)
#include "ion_support.h"
#endif
#if defined(__linux__)
#include <linux/dma-mapping.h>
#endif
/*
 * In systems that support trusted device address protection, there are three
 * physical heaps from which pages should be allocated:
 * - one heap for normal allocations
 * - one heap for allocations holding META code memory
 * - one heap for allocations holding secured DRM data
 */

#if defined(SUPPORT_VALIDATION) && defined(PDUMP)
#include "validation_soc.h"
#endif

/* Change to test CPU_LOCAL sys layers*/
#define UMA_HEAP_USAGE_FLAG PHYS_HEAP_USAGE_GPU_LOCAL
//#define UMA_HEAP_USAGE_FLAG PHYS_HEAP_USAGE_CPU_LOCAL
#define UMA_DEFAULT_HEAP PVRSRV_PHYS_HEAP_GPU_LOCAL
//#define UMA_DEFAULT_HEAP PVRSRV_PHYS_HEAP_CPU_LOCAL

/*
	CPU to Device physical address translation
*/
static
void UMAPhysHeapCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
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

/*
	Device to CPU physical address translation
*/
static
void UMAPhysHeapDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
								   IMG_UINT32 ui32NumOfAddr,
								   IMG_CPU_PHYADDR *psCpuPAddr,
								   IMG_DEV_PHYADDR *psDevPAddr)
{
	PVR_UNREFERENCED_PARAMETER(hPrivData);

	/* Optimise common case */
	psCpuPAddr[0].uiAddr = psDevPAddr[0].uiAddr;
	if (ui32NumOfAddr > 1)
	{
		IMG_UINT32 ui32Idx;
		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
		{
			psCpuPAddr[ui32Idx].uiAddr = psDevPAddr[ui32Idx].uiAddr;
		}
	}
}

static PHYS_HEAP_FUNCTIONS gsPhysHeapFuncs =
{
	/* pfnCpuPAddrToDevPAddr */
	UMAPhysHeapCpuPAddrToDevPAddr,
	/* pfnDevPAddrToCpuPAddr */
	UMAPhysHeapDevPAddrToCpuPAddr,
};

static PVRSRV_ERROR PhysHeapsCreate(PHYS_HEAP_CONFIG **ppasPhysHeapsOut,
									IMG_UINT32 *puiPhysHeapCountOut)
{
	PHYS_HEAP_CONFIG *pasPhysHeaps;
	IMG_UINT32 ui32NextHeapID = 0;
	IMG_UINT32 uiHeapCount = 1;

#if defined(SUPPORT_TRUSTED_DEVICE)
	uiHeapCount += 2;
#elif defined(SUPPORT_DEDICATED_FW_MEMORY)
	uiHeapCount += 1;
#endif
	uiHeapCount += !PVRSRV_VZ_MODE_IS(NATIVE) ? 1:0;

	pasPhysHeaps = OSAllocZMem(sizeof(*pasPhysHeaps) * uiHeapCount);
	if (!pasPhysHeaps)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	pasPhysHeaps[ui32NextHeapID].pszPDumpMemspaceName = "SYSMEM";
	pasPhysHeaps[ui32NextHeapID].eType = PHYS_HEAP_TYPE_UMA;
	pasPhysHeaps[ui32NextHeapID].psMemFuncs = &gsPhysHeapFuncs;
	pasPhysHeaps[ui32NextHeapID].ui32UsageFlags = UMA_HEAP_USAGE_FLAG;
	ui32NextHeapID++;

#if defined(SUPPORT_TRUSTED_DEVICE)
	pasPhysHeaps[ui32NextHeapID].pszPDumpMemspaceName = "TDFWMEM";
	pasPhysHeaps[ui32NextHeapID].eType = PHYS_HEAP_TYPE_UMA;
	pasPhysHeaps[ui32NextHeapID].psMemFuncs = &gsPhysHeapFuncs;
	pasPhysHeaps[ui32NextHeapID].ui32UsageFlags =
		PHYS_HEAP_USAGE_FW_CODE | PHYS_HEAP_USAGE_FW_PRIV_DATA;
	ui32NextHeapID++;

	pasPhysHeaps[ui32NextHeapID].pszPDumpMemspaceName = "TDSECBUFMEM";
	pasPhysHeaps[ui32NextHeapID].eType = PHYS_HEAP_TYPE_UMA;
	pasPhysHeaps[ui32NextHeapID].psMemFuncs = &gsPhysHeapFuncs;
	pasPhysHeaps[ui32NextHeapID].ui32UsageFlags =
		PHYS_HEAP_USAGE_GPU_SECURE;
	ui32NextHeapID++;

#elif defined(SUPPORT_DEDICATED_FW_MEMORY)
	pasPhysHeaps[ui32NextHeapID].pszPDumpMemspaceName = "DEDICATEDFWMEM";
	pasPhysHeaps[ui32NextHeapID].eType = PHYS_HEAP_TYPE_UMA;
	pasPhysHeaps[ui32NextHeapID].psMemFuncs = &gsPhysHeapFuncs;
	pasPhysHeaps[ui32NextHeapID].ui32UsageFlags =
		PHYS_HEAP_USAGE_FW_CODE | PHYS_HEAP_USAGE_FW_PRIV_DATA;
	ui32NextHeapID++;
#endif

	if (! PVRSRV_VZ_MODE_IS(NATIVE))
	{
		pasPhysHeaps[ui32NextHeapID].pszPDumpMemspaceName = "SYSMEM";
		pasPhysHeaps[ui32NextHeapID].eType = PHYS_HEAP_TYPE_UMA;
		pasPhysHeaps[ui32NextHeapID].psMemFuncs = &gsPhysHeapFuncs;
		pasPhysHeaps[ui32NextHeapID].ui32UsageFlags = PHYS_HEAP_USAGE_FW_SHARED;
		ui32NextHeapID++;
	}

	*ppasPhysHeapsOut = pasPhysHeaps;
	*puiPhysHeapCountOut = uiHeapCount;

	return PVRSRV_OK;
}

static void PhysHeapsDestroy(PHYS_HEAP_CONFIG *pasPhysHeaps)
{
	OSFreeMem(pasPhysHeaps);
}

PVRSRV_ERROR SysDevInit(void *pvOSDevice, PVRSRV_DEVICE_CONFIG **ppsDevConfig)
{
	PVRSRV_DEVICE_CONFIG *psDevConfig;
	RGX_DATA *psRGXData;
	RGX_TIMING_INFORMATION *psRGXTimingInfo;
	PHYS_HEAP_CONFIG *pasPhysHeaps;
	IMG_UINT32 uiPhysHeapCount;
	PVRSRV_ERROR eError;

#if defined(__linux__)
	dma_set_mask(pvOSDevice, DMA_BIT_MASK(40));
#endif

	psDevConfig = OSAllocZMem(sizeof(*psDevConfig) +
							  sizeof(*psRGXData) +
							  sizeof(*psRGXTimingInfo));
	if (!psDevConfig)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psRGXData = IMG_OFFSET_ADDR(psDevConfig, sizeof(*psDevConfig));
	psRGXTimingInfo = IMG_OFFSET_ADDR(psRGXData, sizeof(*psRGXData));

	eError = PhysHeapsCreate(&pasPhysHeaps, &uiPhysHeapCount);
	if (eError)
	{
		goto ErrorFreeDevConfig;
	}

	/* Setup RGX specific timing data */
	psRGXTimingInfo->ui32CoreClockSpeed        = RGX_NOHW_CORE_CLOCK_SPEED;
	psRGXTimingInfo->bEnableActivePM           = IMG_FALSE;
	psRGXTimingInfo->bEnableRDPowIsland        = IMG_FALSE;
	psRGXTimingInfo->ui32ActivePMLatencyms     = SYS_RGX_ACTIVE_POWER_LATENCY_MS;

	/* Set up the RGX data */
	psRGXData->psRGXTimingInfo = psRGXTimingInfo;

	/* Setup the device config */
	psDevConfig->pvOSDevice				= pvOSDevice;
	psDevConfig->pszName                = "nohw";
	psDevConfig->pszVersion             = NULL;

	/* Device setup information */
	psDevConfig->sRegsCpuPBase.uiAddr   = 0x00f00baa;
	psDevConfig->ui32IRQ                = 0x00000bad;

#if defined(SUPPORT_GPUVIRT_VALIDATION_MTS)
	psDevConfig->ui32RegsSize            = RGX_VIRTUALISATION_REG_SIZE_PER_OS * GPUVIRT_VALIDATION_NUM_OS;
#else
	psDevConfig->ui32RegsSize           = 0x4000;
#endif

	psDevConfig->pasPhysHeaps			= pasPhysHeaps;
	psDevConfig->ui32PhysHeapCount		= uiPhysHeapCount;

	psDevConfig->eDefaultHeap = UMA_DEFAULT_HEAP;

	/* No power management on no HW system */
	psDevConfig->pfnPrePowerState       = NULL;
	psDevConfig->pfnPostPowerState      = NULL;

	/* No clock frequency either */
	psDevConfig->pfnClockFreqGet        = NULL;

	psDevConfig->hDevData               = psRGXData;

	psDevConfig->bDevicePA0IsValid       = IMG_FALSE;
	psDevConfig->pfnSysDevFeatureDepInit = NULL;

	/* Setup other system specific stuff */
#if defined(SUPPORT_ION)
	IonInit(NULL);
#endif

	/* Pdump validation system registers */
#if defined(SUPPORT_VALIDATION) && defined(PDUMP)
	{
		PVRSRV_DEVICE_NODE* psDeviceNode;

		/* Pretend we are calling for device 0 for now
		   (sDeviceNode.sDevId.ui32InternalID is 0 above)
		   until this code can be moved elsewhere.
		 */
		psDeviceNode = OSAllocZMem(sizeof(*psDeviceNode));
		PVR_LOG_RETURN_IF_NOMEM(psDeviceNode, "OSAllocZMem");

		PVRSRVConfigureSysCtrl(psDeviceNode, NULL, PDUMP_FLAGS_CONTINUOUS);
#if defined(SUPPORT_SECURITY_VALIDATION)
		PVRSRVConfigureTrustedDevice(psDeviceNode, NULL, PDUMP_FLAGS_CONTINUOUS);
#endif
		OSFreeMem(psDeviceNode);
	}
#endif

	psDevConfig->bHasFBCDCVersion31 = IMG_TRUE;

	/* Set psDevConfig->pfnSysDevErrorNotify callback */
	psDevConfig->pfnSysDevErrorNotify = SysRGXErrorNotify;

	*ppsDevConfig = psDevConfig;

	return PVRSRV_OK;

ErrorFreeDevConfig:
	OSFreeMem(psDevConfig);
	return eError;
}

void SysDevDeInit(PVRSRV_DEVICE_CONFIG *psDevConfig)
{
#if defined(SUPPORT_ION)
	IonDeinit();
#endif

	PhysHeapsDestroy(psDevConfig->pasPhysHeaps);
	OSFreeMem(psDevConfig);
}

PVRSRV_ERROR SysInstallDeviceLISR(IMG_HANDLE hSysData,
								  IMG_UINT32 ui32IRQ,
								  const IMG_CHAR *pszName,
								  PFN_LISR pfnLISR,
								  void *pvData,
								  IMG_HANDLE *phLISRData)
{
	PVR_UNREFERENCED_PARAMETER(hSysData);
	PVR_UNREFERENCED_PARAMETER(ui32IRQ);
	PVR_UNREFERENCED_PARAMETER(pszName);
	PVR_UNREFERENCED_PARAMETER(pfnLISR);
	PVR_UNREFERENCED_PARAMETER(pvData);
	PVR_UNREFERENCED_PARAMETER(phLISRData);

	return PVRSRV_OK;
}

PVRSRV_ERROR SysUninstallDeviceLISR(IMG_HANDLE hLISRData)
{
	PVR_UNREFERENCED_PARAMETER(hLISRData);

	return PVRSRV_OK;
}

PVRSRV_ERROR SysDebugInfo(PVRSRV_DEVICE_CONFIG *psDevConfig,
				DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile)
{
	PVR_UNREFERENCED_PARAMETER(psDevConfig);
	PVR_UNREFERENCED_PARAMETER(pfnDumpDebugPrintf);
	PVR_UNREFERENCED_PARAMETER(pvDumpDebugFile);
	return PVRSRV_OK;
}

/******************************************************************************
 End of file (sysconfig.c)
******************************************************************************/
