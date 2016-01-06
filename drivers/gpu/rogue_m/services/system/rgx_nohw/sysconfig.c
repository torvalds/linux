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

#include "pvrsrv_device.h"
#include "syscommon.h"
#include "sysconfig.h"
#include "physheap.h"
#if defined(SUPPORT_ION)
#include "ion_support.h"
#endif

static RGX_TIMING_INFORMATION	gsRGXTimingInfo;
static RGX_DATA			gsRGXData;
static PVRSRV_DEVICE_CONFIG 	gsDevices[1];
static PVRSRV_SYSTEM_CONFIG 	gsSysConfig;

static PHYS_HEAP_FUNCTIONS	gsPhysHeapFuncs;
#if defined(TDMETACODE)
static PHYS_HEAP_CONFIG		gsPhysHeapConfig[3];
#else
static PHYS_HEAP_CONFIG		gsPhysHeapConfig[1];
#endif

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
PVRSRV_ERROR SysCreateConfigData(PVRSRV_SYSTEM_CONFIG **ppsSysConfig, void *device)
{
	PVR_UNREFERENCED_PARAMETER(device);

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

#if defined(TDMETACODE)
	gsPhysHeapConfig[1].ui32PhysHeapID = 1;
	gsPhysHeapConfig[1].pszPDumpMemspaceName = "TDMETACODEMEM";
	gsPhysHeapConfig[1].eType = PHYS_HEAP_TYPE_UMA;
	gsPhysHeapConfig[1].psMemFuncs = &gsPhysHeapFuncs;
	gsPhysHeapConfig[1].hPrivData = IMG_NULL;

	gsPhysHeapConfig[2].ui32PhysHeapID = 2;
	gsPhysHeapConfig[2].pszPDumpMemspaceName = "TDSECUREBUFMEM";
	gsPhysHeapConfig[2].eType = PHYS_HEAP_TYPE_UMA;
	gsPhysHeapConfig[2].psMemFuncs = &gsPhysHeapFuncs;
	gsPhysHeapConfig[2].hPrivData = IMG_NULL;
#endif

	gsSysConfig.pasPhysHeaps = &(gsPhysHeapConfig[0]);
	gsSysConfig.ui32PhysHeapCount = IMG_ARR_NUM_ELEMS(gsPhysHeapConfig);

	gsSysConfig.pui32BIFTilingHeapConfigs = gauiBIFTilingHeapXStrides;
	gsSysConfig.ui32BIFTilingHeapCount = IMG_ARR_NUM_ELEMS(gauiBIFTilingHeapXStrides);

	/*
	 * Setup RGX specific timing data
	 */
	gsRGXTimingInfo.ui32CoreClockSpeed        = RGX_NOHW_CORE_CLOCK_SPEED;
	gsRGXTimingInfo.bEnableActivePM           = IMG_FALSE;
	gsRGXTimingInfo.bEnableRDPowIsland        = IMG_FALSE;
	gsRGXTimingInfo.ui32ActivePMLatencyms     = SYS_RGX_ACTIVE_POWER_LATENCY_MS;

	/*
	 *Setup RGX specific data
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
	gsDevices[0].eDeviceType            = PVRSRV_DEVICE_TYPE_RGX;
	gsDevices[0].pszName                = "RGX";

	/* Device setup information */
	gsDevices[0].sRegsCpuPBase.uiAddr   = 0x00f00baa;
	gsDevices[0].ui32RegsSize           = 0x4000;
	gsDevices[0].ui32IRQ                = 0x00000bad;
	gsDevices[0].bIRQIsShared           = IMG_FALSE;

	/* Device's physical heap IDs */
	gsDevices[0].aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL] = 0;
	gsDevices[0].aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_CPU_LOCAL] = 0;

	/* No power management on no HW system */
	gsDevices[0].pfnPrePowerState       = IMG_NULL;
	gsDevices[0].pfnPostPowerState      = IMG_NULL;

	/* No clock frequency either */
	gsDevices[0].pfnClockFreqGet        = IMG_NULL;

	/* No interrupt handled either */
	gsDevices[0].pfnInterruptHandled    = IMG_NULL;

	gsDevices[0].pfnCheckMemAllocSize   = SysCheckMemAllocSize;

	gsDevices[0].hDevData               = &gsRGXData;

	/*
	 * Setup system config
	 */
	gsSysConfig.pszSystemName = RGX_NOHW_SYSTEM_NAME;
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
IMG_VOID SysDestroyConfigData(PVRSRV_SYSTEM_CONFIG *psSysConfig)
{
	PVR_UNREFERENCED_PARAMETER(psSysConfig);

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
