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

#include "interrupt_support.h"
#include "pvrsrv_device.h"
#include "syscommon.h"
#include "sysconfig.h"
#include "physheap.h"
#if defined(SUPPORT_ION)
#include "ion_support.h"
#endif
#include "rk_init_v2.h"

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
#include <linux/platform_device.h>
extern struct platform_device *gpsPVRLDMDev;
#endif

static RGX_TIMING_INFORMATION	gsRGXTimingInfo;
static RGX_DATA			gsRGXData;
static PVRSRV_DEVICE_CONFIG 	gsDevices[1];

static PHYS_HEAP_FUNCTIONS	gsPhysHeapFuncs;
#if defined(TDMETACODE)
static PHYS_HEAP_CONFIG		gsPhysHeapConfig[3];
#else
static PHYS_HEAP_CONFIG		gsPhysHeapConfig[1];
#endif

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

PVRSRV_ERROR SysDevInit(void *pvOSDevice, PVRSRV_DEVICE_CONFIG **ppsDevConfig)
{
	if (gsDevices[0].pvOSDevice)
	{
		return PVRSRV_ERROR_INVALID_DEVICE;
	}

	/*
	 * Setup information about physical memory heap(s) we have
	 */
	gsPhysHeapFuncs.pfnCpuPAddrToDevPAddr = UMAPhysHeapCpuPAddrToDevPAddr;
	gsPhysHeapFuncs.pfnDevPAddrToCpuPAddr = UMAPhysHeapDevPAddrToCpuPAddr;

	gsPhysHeapConfig[0].ui32PhysHeapID = 0;
	gsPhysHeapConfig[0].pszPDumpMemspaceName = "SYSMEM";
	gsPhysHeapConfig[0].eType = PHYS_HEAP_TYPE_UMA;
	gsPhysHeapConfig[0].psMemFuncs = &gsPhysHeapFuncs;
	gsPhysHeapConfig[0].hPrivData = NULL;

#if defined(TDMETACODE)
	gsPhysHeapConfig[1].ui32PhysHeapID = 1;
	gsPhysHeapConfig[1].pszPDumpMemspaceName = "TDMETACODEMEM";
	gsPhysHeapConfig[1].eType = PHYS_HEAP_TYPE_UMA;
	gsPhysHeapConfig[1].psMemFuncs = &gsPhysHeapFuncs;
	gsPhysHeapConfig[1].hPrivData = NULL;

	gsPhysHeapConfig[2].ui32PhysHeapID = 2;
	gsPhysHeapConfig[2].pszPDumpMemspaceName = "TDSECUREBUFMEM";
	gsPhysHeapConfig[2].eType = PHYS_HEAP_TYPE_UMA;
	gsPhysHeapConfig[2].psMemFuncs = &gsPhysHeapFuncs;
	gsPhysHeapConfig[2].hPrivData = NULL;
#endif

	/*
	 * Setup RGX specific timing data
	 */
	gsRGXTimingInfo.ui32CoreClockSpeed        = RGX_RK_CORE_CLOCK_SPEED;
	gsRGXTimingInfo.bEnableActivePM           = IMG_TRUE;
	gsRGXTimingInfo.bEnableRDPowIsland        = IMG_FALSE;
	gsRGXTimingInfo.ui32ActivePMLatencyms     = SYS_RGX_ACTIVE_POWER_LATENCY_MS;

	/*
	 * Setup RGX specific data
	 */
	gsRGXData.psRGXTimingInfo = &gsRGXTimingInfo;
#if defined(TDMETACODE)
	gsRGXData.bHasTDMetaCodePhysHeap = IMG_TRUE;
	gsRGXData.uiTDMetaCodePhysHeapID = 1;

	gsRGXData.bHasTDSecureBufPhysHeap = IMG_TRUE;
	gsRGXData.uiTDSecureBufPhysHeapID = 2;
#endif

	/*
	 * Setup RGX device
	 */
	gsDevices[0].pvOSDevice             = pvOSDevice;
	gsDevices[0].pszName                = "rk3368";

	/* Device setup information */
	gsDevices[0].sRegsCpuPBase.uiAddr   = RK_GPU_PBASE;
	gsDevices[0].ui32RegsSize           = RK_GPU_SIZE;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	gsDevices[0].ui32IRQ                = RK_IRQ_GPU;
#else
    gsDevices[0].ui32IRQ                = platform_get_irq(gpsPVRLDMDev, 0);
#endif


	/* Device's physical heaps */
	gsDevices[0].pasPhysHeaps           = &gsPhysHeapConfig[0];
	gsDevices[0].ui32PhysHeapCount      = IMG_ARR_NUM_ELEMS(gsPhysHeapConfig);

	/* Device's physical heap IDs */
	gsDevices[0].aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL] = 0;
	gsDevices[0].aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_CPU_LOCAL] = 0;

	gsDevices[0].eBIFTilingMode = geBIFTilingMode;
	gsDevices[0].pui32BIFTilingHeapConfigs = gauiBIFTilingHeapXStrides;
	gsDevices[0].ui32BIFTilingHeapCount    = IMG_ARR_NUM_ELEMS(gauiBIFTilingHeapXStrides);

	/* No power management on RK system */
	gsDevices[0].pfnPrePowerState       = RkPrePowerState;
	gsDevices[0].pfnPostPowerState      = RkPostPowerState;

	/* No clock frequency either */
	gsDevices[0].pfnClockFreqGet        = NULL;

	gsDevices[0].pfnCheckMemAllocSize   = NULL;

	gsDevices[0].hDevData               = &gsRGXData;

	/* Rk Init */
	gsDevices[0].hSysData = (IMG_HANDLE)RgxRkInit(&gsDevices[0]);
	if (!gsDevices[0].hSysData)
	{
		gsDevices[0].pvOSDevice = NULL;
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

#if defined(PVR_DVFS)
	gsDevices[0].sDVFS.sDVFSDeviceCfg.ui32PollMs = 100;
	gsDevices[0].sDVFS.sDVFSDeviceCfg.bIdleReq = IMG_FALSE;

	gsDevices[0].sDVFS.sDVFSGovernorCfg.ui32UpThreshold = 90;
	gsDevices[0].sDVFS.sDVFSGovernorCfg.ui32DownDifferential = 10;
#endif

	/* Setup other system specific stuff */
#if defined(SUPPORT_ION)
	IonInit(NULL);
#endif

	*ppsDevConfig = &gsDevices[0];

	return PVRSRV_OK;
}

void SysDevDeInit(PVRSRV_DEVICE_CONFIG *psDevConfig)
{
	PVR_UNREFERENCED_PARAMETER(psDevConfig);

	/* Rk UnInit */
	RgxRkUnInit(psDevConfig->hSysData);
	psDevConfig->hSysData = NULL;

#if defined(SUPPORT_ION)
	IonDeinit();
#endif

	psDevConfig->pvOSDevice = NULL;
}

PVRSRV_ERROR SysInstallDeviceLISR(IMG_HANDLE hSysData,
								  IMG_UINT32 ui32IRQ,
								  const IMG_CHAR *pszName,
								  PFN_LISR pfnLISR,
								  void *pvData,
								  IMG_HANDLE *phLISRData)
{
	PVR_UNREFERENCED_PARAMETER(hSysData);

	return OSInstallSystemLISR(phLISRData, ui32IRQ, pszName, pfnLISR, pvData,
							   SYS_IRQ_FLAG_TRIGGER_DEFAULT);
}

PVRSRV_ERROR SysUninstallDeviceLISR(IMG_HANDLE hLISRData)
{
	return OSUninstallSystemLISR(hLISRData);
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
