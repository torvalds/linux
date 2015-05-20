/*************************************************************************/ /*!
@File
@Title          System Description Header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    This header provides system-specific declarations and macros
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

#if !defined(__SYSCONFIG_H__)
#define __SYSCONFIG_H__

#include "pvrsrv_device.h"
#include "rgxdevice.h"

#define SYS_RGX_ACTIVE_POWER_LATENCY_MS (10)
static PVRSRV_SYSTEM_CONFIG gsSysConfig;

static RGX_TIMING_INFORMATION gsRGXTimingInfo =
{
	/* ui32CoreClockSpeed */
	0,	/* Initialize to 0, real value will be set in PCIInitDev() */
	/* bEnableActivePM */ 
#if defined (VIRTUAL_PLATFORM)
	IMG_FALSE,
#else
	IMG_TRUE,
#endif
	/* bEnableRDPowIsland */ 
	IMG_FALSE,
	/* ui32ActivePMLatencyms */
	SYS_RGX_ACTIVE_POWER_LATENCY_MS
};

static RGX_DATA gsRGXData =
{
	/* psRGXTimingInfo */
	&gsRGXTimingInfo
};
static PVRSRV_DEVICE_CONFIG gsDevices[] = 
{
	{
		/* uiFlags */
#if (TC_MEMORY_CONFIG != TC_MEMORY_DIRECT_MAPPED)
		0,
#else
		PVRSRV_DEVICE_CONFIG_LMA_USE_CPU_ADDR,
#endif
		/* pszName */
		"RGX",
		/* eDeviceType */
		PVRSRV_DEVICE_TYPE_RGX,

		/* Device setup information */
		/* sRegsCpuPBase.uiAddr */
		{ 0 },
		/* ui32RegsSize */
		0,

		/* ui32IRQ */
		0,
		/* bIRQIsShared */
		IMG_TRUE,
		/* eIRQActiveLevel */
		PVRSRV_DEVICE_IRQ_ACTIVE_SYSDEFAULT,

		/* hDevData */
		&gsRGXData,
		/* hSysData */
		IMG_NULL,

#if (TC_MEMORY_CONFIG == TC_MEMORY_HYBRID)
		/*
		 *  PhysHeapIDs.
		 *  NB. Where allowing both LMA
		 *      and a UMA physical heaps, be
		 *      sure to provide the ID of
		 *      the LMA Heap first and that
		 *      of the UMA heap second.
		 */
		{ 1, 0 },
#else
		/* usable ui32PhysHeapIDs */
		{ 0, 0 },
#endif
		/* pfnPrePowerState */
		IMG_NULL,
		/* pfnPostPowerState */
		IMG_NULL,

		/* pfnClockFreqGet */
		IMG_NULL,

		/* pfnInterruptHandled */
		IMG_NULL,
		
		/* pfnCheckMemAllocSize */
		SysCheckMemAllocSize,

		/* eBPDM */
		RGXFWIF_DM_TA,
		/* bBPSet */
		IMG_FALSE
	}
};

#if (TC_MEMORY_CONFIG == TC_MEMORY_LOCAL)
static IMG_VOID TCLocalCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
					  IMG_DEV_PHYADDR *psDevPAddr,
					  IMG_CPU_PHYADDR *psCpuPAddr);

static IMG_VOID TCLocalDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
					  IMG_CPU_PHYADDR *psCpuPAddr,
					  IMG_DEV_PHYADDR *psDevPAddr);
#elif (TC_MEMORY_CONFIG == TC_MEMORY_HOST) || (TC_MEMORY_CONFIG == TC_MEMORY_HYBRID) || (TC_MEMORY_CONFIG == TC_MEMORY_DIRECT_MAPPED)
static IMG_VOID TCSystemCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
					   IMG_DEV_PHYADDR *psDevPAddr,
					   IMG_CPU_PHYADDR *psCpuPAddr);

static IMG_VOID TCSystemDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
					   IMG_CPU_PHYADDR *psCpuPAddr,
					   IMG_DEV_PHYADDR *psDevPAddr);
#endif /* (TC_MEMORY_CONFIG == TC_MEMORY_HOST) || (TC_MEMORY_CONFIG == TC_MEMORY_HYBRID) || (TC_MEMORY_CONFIG == TC_MEMORY_DIRECT_MAPPED) */

#if (TC_MEMORY_CONFIG == TC_MEMORY_LOCAL)
static PHYS_HEAP_FUNCTIONS gsLocalPhysHeapFuncs =
{
	/* pfnCpuPAddrToDevPAddr */
	TCLocalCpuPAddrToDevPAddr,
	/* pfnDevPAddrToCpuPAddr */
	TCLocalDevPAddrToCpuPAddr,
};

static PHYS_HEAP_CONFIG	gsPhysHeapConfig[] =
{
	{
		/* ui32PhysHeapID */
		0,
		/* eType */
		PHYS_HEAP_TYPE_LMA,
		/* sStartAddr */
		{ 0 },
		/* uiSize */
		 0,
		/* pszPDumpMemspaceName */
		"LMA",
		/* psMemFuncs */
		&gsLocalPhysHeapFuncs,
		/* hPrivData */
		(IMG_HANDLE)&gsSysConfig,
	},
#if defined(SUPPORT_DISPLAY_CLASS) || defined(SUPPORT_DRM_DC_MODULE)
	{
		/* ui32PhysHeapID */
		1,
		/* eType */
		PHYS_HEAP_TYPE_LMA,
		/* sStartAddr */
		{ 0 },
		/* uiSize */
		0,
		/* pszPDumpMemspaceName */
		"LMA",
		/* psMemFuncs */
		&gsLocalPhysHeapFuncs,
		/* hPrivData */
		(IMG_HANDLE)&gsSysConfig,
	},
#endif
#if defined(SUPPORT_ION)
	{
		/* ui32PhysHeapID */
		2,
		/* eType */
		PHYS_HEAP_TYPE_LMA,
		/* sStartAddr */
		{ 0 },
		/* uiSize */
		0,
		/* pszPDumpMemspaceName */
		"LMA",
		/* psMemFuncs */
		&gsLocalPhysHeapFuncs,
		/* hPrivData */
		(IMG_HANDLE)&gsSysConfig,
	},
#endif
};
#elif (TC_MEMORY_CONFIG == TC_MEMORY_HOST)
static PHYS_HEAP_FUNCTIONS gsSystemPhysHeapFuncs =
{
	/* pfnCpuPAddrToDevPAddr */
	TCSystemCpuPAddrToDevPAddr,
	/* pfnDevPAddrToCpuPAddr */
	TCSystemDevPAddrToCpuPAddr,
};

static PHYS_HEAP_CONFIG	gsPhysHeapConfig[] =
{
	{
		/* ui32PhysHeapID */
		0,
		/* eType */
		PHYS_HEAP_TYPE_UMA,
		/* sStartAddr */
		{ 0 },
		/* uiSize */
		0,
		/* pszPDumpMemspaceName */
		"SYSMEM",
		/* psMemFuncs */
		&gsSystemPhysHeapFuncs,
		/* hPrivData */
		(IMG_HANDLE)&gsSysConfig,
	}
};
#elif (TC_MEMORY_CONFIG == TC_MEMORY_HYBRID)
static PHYS_HEAP_FUNCTIONS gsHybridPhysHeapFuncs =
{
	/* pfnCpuPAddrToDevPAddr */
	TCSystemCpuPAddrToDevPAddr,
	/* pfnDevPAddrToCpuPAddr */
	TCSystemDevPAddrToCpuPAddr,
};

static PHYS_HEAP_CONFIG	gsPhysHeapConfig[] =
{
	{
		/* ui32PhysHeapID */
		0,
		/* eType */
		PHYS_HEAP_TYPE_UMA,
		/* sStartAddr */
		{ 0 },
		/* uiSize */
		0,
		/* pszPDumpMemspaceName */
		"SYSMEM",
		/* psMemFuncs */
		&gsHybridPhysHeapFuncs,
		/* hPrivData */
		(IMG_HANDLE)&gsSysConfig,
	},
	{
		/* ui32PhysHeapID */
		1,
		/* eType */
		PHYS_HEAP_TYPE_LMA,
		/* sStartAddr */
		{ 0 },
		/* uiSize */
		0,
		/* pszPDumpMemspaceName */
		"LMA",
		/* psMemFuncs */
		&gsHybridPhysHeapFuncs,
		/* hPrivData */
		(IMG_HANDLE)&gsSysConfig,
	}
};
#elif (TC_MEMORY_CONFIG == TC_MEMORY_DIRECT_MAPPED)
static PHYS_HEAP_FUNCTIONS gsDirectMappedPhysHeapFuncs =
{
	/* pfnCpuPAddrToDevPAddr */
	TCSystemCpuPAddrToDevPAddr,
	/* pfnDevPAddrToCpuPAddr */
	TCSystemDevPAddrToCpuPAddr,
};

static PHYS_HEAP_CONFIG	gsPhysHeapConfig[] =
{
	{
		/* ui32PhysHeapID */
		0,
		/* eType */
		PHYS_HEAP_TYPE_LMA,
		/* sStartAddr */
		{ 0 },
		/* uiSize */
		 0,
		/* pszPDumpMemspaceName */
		"LMA",
		/* psMemFuncs */
		&gsDirectMappedPhysHeapFuncs,
		/* hPrivData */
		(IMG_HANDLE)&gsSysConfig,
	}
};
#else
#error "TC_MEMORY_CONFIG not valid"
#endif

/* default BIF tiling heap x-stride configurations. */
static IMG_UINT32 gauiBIFTilingHeapXStrides[RGXFWIF_NUM_BIF_TILING_CONFIGS] =
{
	0, /* BIF tiling heap 1 x-stride */
	1, /* BIF tiling heap 2 x-stride */
	2, /* BIF tiling heap 3 x-stride */
	3  /* BIF tiling heap 4 x-stride */
};

static PVRSRV_SYSTEM_CONFIG gsSysConfig =
{
	/* uiSysFlags */
	0,
	/* pszSystemName */
	TC_SYSTEM_NAME,
	/* uiDeviceCount */
	(IMG_UINT32)(sizeof(gsDevices) / sizeof(PVRSRV_DEVICE_CONFIG)),
	/* pasDevices */
	&gsDevices[0],

	/* No power management on no HW system */
	/* pfnSysPrePowerState */
	IMG_NULL,
	/* pfnSysPostPowerState */
	IMG_NULL,

	/* no cache snooping */
	/* eCacheSnoopingMode */
	PVRSRV_SYSTEM_SNOOP_NONE,
	
	/* Physcial memory heaps */
	/* pasPhysHeaps */
	&gsPhysHeapConfig[0],
	/* ui32PhysHeapCount */
	(IMG_UINT32)(sizeof(gsPhysHeapConfig) / sizeof(PHYS_HEAP_CONFIG)),

	/* BIF tiling heap config */
	&gauiBIFTilingHeapXStrides[0],
	IMG_ARR_NUM_ELEMS(gauiBIFTilingHeapXStrides),
};

/*****************************************************************************
 * system specific data structures
 *****************************************************************************/

#endif /* !defined(__SYSCONFIG_H__) */
