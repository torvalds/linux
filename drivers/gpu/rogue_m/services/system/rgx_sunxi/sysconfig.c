/*************************************************************************/ /*!
@Title          System Configuration
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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
@Description    System Configuration functions
*/ /**************************************************************************/

#include "pvrsrv_device.h"
#include "syscommon.h"
#include "sysconfig.h"
#include "physheap.h"
#if defined(SUPPORT_ION)
#include "ion_support.h"
#endif

#include <mach/platform.h>
#include <mach/irqs.h>
#include "sunxi_init.h"

static RGX_TIMING_INFORMATION	gsRGXTimingInfo;
static RGX_DATA                 gsRGXData;
static PVRSRV_DEVICE_CONFIG 	gsDevices[1];
static PVRSRV_SYSTEM_CONFIG 	gsSysConfig;

static PHYS_HEAP_FUNCTIONS      gsPhysHeapFuncs;
static PHYS_HEAP_CONFIG         gsPhysHeapConfig[1];


/*
	CPU to Device physcial address translation
*/
static
IMG_VOID UMAPhysHeapCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
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
	Device to CPU physcial address translation
*/
static
IMG_VOID UMAPhysHeapDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
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

/*
	SysCreateConfigData
*/
PVRSRV_ERROR SysCreateConfigData(PVRSRV_SYSTEM_CONFIG **ppsSysConfig, void *hDevice)
{
	PVR_UNREFERENCED_PARAMETER(hDevice);

	/* Sunxi Init */
	RgxSunxiInit(&gsDevices[0]);
	
	/*
	 * Setup information about physical memory heap(s) we have
	 */
	gsPhysHeapFuncs.pfnCpuPAddrToDevPAddr = UMAPhysHeapCpuPAddrToDevPAddr;
	gsPhysHeapFuncs.pfnDevPAddrToCpuPAddr = UMAPhysHeapDevPAddrToCpuPAddr;

	gsPhysHeapConfig[0].ui32PhysHeapID = 0;
	gsPhysHeapConfig[0].pszPDumpMemspaceName = "SYSMEM";
	gsPhysHeapConfig[0].eType = PHYS_HEAP_TYPE_UMA;
	gsPhysHeapConfig[0].psMemFuncs = &gsPhysHeapFuncs;
	gsPhysHeapConfig[0].hPrivData = IMG_NULL;

	gsSysConfig.pasPhysHeaps = &(gsPhysHeapConfig[0]);
	gsSysConfig.ui32PhysHeapCount = IMG_ARR_NUM_ELEMS(gsPhysHeapConfig);

	gsSysConfig.pui32BIFTilingHeapConfigs = gauiBIFTilingHeapXStrides;
	gsSysConfig.ui32BIFTilingHeapCount = IMG_ARR_NUM_ELEMS(gauiBIFTilingHeapXStrides);

	/*
	 * Setup RGX specific timing data
	 */
	gsRGXTimingInfo.ui32CoreClockSpeed        = GetConfigFreq();
	gsRGXTimingInfo.bEnableActivePM           = IMG_TRUE;
	gsRGXTimingInfo.bEnableRDPowIsland        = IMG_TRUE;
	gsRGXTimingInfo.ui32ActivePMLatencyms     = SYS_RGX_ACTIVE_POWER_LATENCY_MS;

	/*
	 *Setup RGX specific data
	 */
	gsRGXData.psRGXTimingInfo = &gsRGXTimingInfo;

	/*
	 * Setup RGX device
	 */
	gsDevices[0].eDeviceType            = PVRSRV_DEVICE_TYPE_RGX;
	gsDevices[0].pszName                = "RGX";

	/* Device setup information */
	gsDevices[0].sRegsCpuPBase.uiAddr   = SUNXI_GPU_PBASE;
	gsDevices[0].ui32RegsSize           = SUNXI_GPU_SIZE;
	gsDevices[0].ui32IRQ                = SUNXI_IRQ_GPU;
	gsDevices[0].bIRQIsShared           = IMG_FALSE;

	/* Device's physical heap IDs */
	gsDevices[0].aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL] = 0;
	gsDevices[0].aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_CPU_LOCAL] = 0;

	/* Power management on SUNXI system */
	gsDevices[0].pfnPrePowerState       = AwPrePowerState;
	gsDevices[0].pfnPostPowerState      = AwPostPowerState;

	/* No clock frequency either */
	gsDevices[0].pfnClockFreqGet        = IMG_NULL;

	/* No interrupt handled either */
	gsDevices[0].pfnInterruptHandled    = IMG_NULL;

	gsDevices[0].pfnCheckMemAllocSize   = SysCheckMemAllocSize;

	gsDevices[0].hDevData               = &gsRGXData;

#if defined(PVR_DVFS)
	gsDevices[0].sDVFS.sDVFSDeviceCfg.ui32PollMs = 100;
	gsDevices[0].sDVFS.sDVFSDeviceCfg.bIdleReq = IMG_FALSE;

	gsDevices[0].sDVFS.sDVFSGovernorCfg.ui32UpThreshold = 90;
	gsDevices[0].sDVFS.sDVFSGovernorCfg.ui32DownDifferential = 10;

#if defined(PVR_POWER_ACTOR)
	gsDevices[0].sDVFS.sDVFSPACfg.i32Ta = 113;
	gsDevices[0].sDVFS.sDVFSPACfg.i32Tb = -11375;
	gsDevices[0].sDVFS.sDVFSPACfg.i32Tc = 800472;
	gsDevices[0].sDVFS.sDVFSPACfg.i32Td = -7651043;
	gsDevices[0].sDVFS.sDVFSPACfg.ui32Other = 0;
	gsDevices[0].sDVFS.sDVFSPACfg.ui32Weight = 0;
#endif
#endif

	/*
	 * Setup system config
	 */
	gsSysConfig.pszSystemName = RGX_SUNXI_SYSTEM_NAME;
	gsSysConfig.uiDeviceCount = sizeof(gsDevices)/sizeof(gsDevices[0]);
	gsSysConfig.pasDevices = &gsDevices[0];

	/* No power management on no HW system */
	gsSysConfig.pfnSysPrePowerState = IMG_NULL;
	gsSysConfig.pfnSysPostPowerState = IMG_NULL;

	/* no cache snooping */
	gsSysConfig.eCacheSnoopingMode = PVRSRV_SYSTEM_SNOOP_NONE;

	/* Setup other system specific stuff */
#if defined(SUPPORT_ION)
	IonInit(NULL);
#endif

	*ppsSysConfig = &gsSysConfig;

	return PVRSRV_OK;
}

/*
	SysDestroyConfigData
*/
void SysDestroyConfigData(PVRSRV_SYSTEM_CONFIG *psSysConfig)
{
	PVR_UNREFERENCED_PARAMETER(psSysConfig);

	/* Sunxi DeInit */
	RgxSunxiDeInit();

#if defined(SUPPORT_ION)
	IonDeinit();
#endif
}

PVRSRV_ERROR SysAcquireSystemData(IMG_HANDLE hSysData)
{
	PVR_UNREFERENCED_PARAMETER(hSysData);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

PVRSRV_ERROR SysReleaseSystemData(IMG_HANDLE hSysData)
{
	PVR_UNREFERENCED_PARAMETER(hSysData);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

PVRSRV_ERROR SysDebugInfo(PVRSRV_SYSTEM_CONFIG *psSysConfig, DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf)
{
	PVR_UNREFERENCED_PARAMETER(psSysConfig);
	PVR_UNREFERENCED_PARAMETER(pfnDumpDebugPrintf);
	return PVRSRV_OK;
}

/******************************************************************************
 End of file (sysconfig.c)
******************************************************************************/
