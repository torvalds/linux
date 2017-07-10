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

#include "sysinfo.h"
#include "apollo_regs.h"

#include "pvrsrv_device.h"
#include "rgxdevice.h"
#include "syscommon.h"
#include "allocmem.h"
#include "pvr_debug.h"

#if defined(SUPPORT_ION)
#include PVR_ANDROID_ION_HEADER
#include "ion_support.h"
#include "ion_sys.h"
#endif

#include "apollo_drv.h"

#include <linux/platform_device.h>

#if !defined(LMA)
#error Apollo only supports LMA at the minute
#endif

/* Valid values for the TC_MEMORY_CONFIG configuration option */
#define TC_MEMORY_LOCAL		(1)
#define TC_MEMORY_HOST		(2)
#define TC_MEMORY_HYBRID	(3)

#if TC_MEMORY_CONFIG != TC_MEMORY_LOCAL
#error Apollo only supports TC_MEMORY_LOCAL at the minute
#endif

/* These must be consecutive */
#define PHYS_HEAP_IDX_GENERAL	0
#define PHYS_HEAP_IDX_DMABUF	1
#define PHYS_HEAP_IDX_COUNT		2

#define SYS_RGX_ACTIVE_POWER_LATENCY_MS (10)

#if defined(PVR_DVFS) || defined(SUPPORT_PDVFS)

/* Dummy DVFS configuration used purely for testing purposes */

static const IMG_OPP asOPPTable[] =
{
	{ 8,  25000000},
	{ 16, 50000000},
	{ 32, 75000000},
	{ 64, 100000000},
};

#define LEVEL_COUNT (sizeof(asOPPTable) / sizeof(IMG_OPP))

static void SetFrequency(IMG_UINT32 ui32Frequency)
{
	PVR_DPF((PVR_DBG_ERROR, "SetFrequency %u", ui32Frequency));
}

static void SetVoltage(IMG_UINT32 ui32Voltage)
{
	PVR_DPF((PVR_DBG_ERROR, "SetVoltage %u", ui32Voltage));
}

#endif

static void TCLocalCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
				      IMG_UINT32 ui32NumOfAddr,
				      IMG_DEV_PHYADDR *psDevPAddr,
				      IMG_CPU_PHYADDR *psCpuPAddr);

static void TCLocalDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
				      IMG_UINT32 ui32NumOfAddr,
				      IMG_CPU_PHYADDR *psCpuPAddr,
				      IMG_DEV_PHYADDR *psDevPAddr);

static IMG_UINT32 TCLocalGetRegionId(IMG_HANDLE hPrivData,
					  PVRSRV_MEMALLOCFLAGS_T uiAllocFlags);

static PHYS_HEAP_FUNCTIONS gsLocalPhysHeapFuncs =
{
	.pfnCpuPAddrToDevPAddr = TCLocalCpuPAddrToDevPAddr,
	.pfnDevPAddrToCpuPAddr = TCLocalDevPAddrToCpuPAddr,
	.pfnGetRegionId = TCLocalGetRegionId,
};

static void TCIonCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
				    IMG_UINT32 ui32NumOfAddr,
				    IMG_DEV_PHYADDR *psDevPAddr,
				    IMG_CPU_PHYADDR *psCpuPAddr);

static void TCIonDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
				    IMG_UINT32 ui32NumOfAddr,
				    IMG_CPU_PHYADDR *psCpuPAddr,
				    IMG_DEV_PHYADDR *psDevPAddr);

static IMG_UINT32 TCIonGetRegionId(IMG_HANDLE hPrivData,
					  PVRSRV_MEMALLOCFLAGS_T uiAllocFlags);

static PHYS_HEAP_FUNCTIONS gsIonPhysHeapFuncs =
{
	.pfnCpuPAddrToDevPAddr = TCIonCpuPAddrToDevPAddr,
	.pfnDevPAddrToCpuPAddr = TCIonDevPAddrToCpuPAddr,
	.pfnGetRegionId = TCIonGetRegionId,
};

/* BIF Tiling mode configuration */
static RGXFWIF_BIFTILINGMODE geBIFTilingMode = RGXFWIF_BIFTILINGMODE_256x16;

/* Default BIF tiling heap x-stride configurations. */
static IMG_UINT32 gauiBIFTilingHeapXStrides[RGXFWIF_NUM_BIF_TILING_CONFIGS] =
{
	0, /* BIF tiling heap 1 x-stride */
	1, /* BIF tiling heap 2 x-stride */
	2, /* BIF tiling heap 3 x-stride */
	3  /* BIF tiling heap 4 x-stride */
};

typedef struct _SYS_DATA_ SYS_DATA;

struct _SYS_DATA_
{
	struct platform_device *pdev;

	struct apollo_rogue_platform_data *pdata;

	struct resource *registers;

#if defined(SUPPORT_ION)
	struct ion_client *ion_client;
	struct ion_handle *ion_rogue_allocation;
#endif
};

#define SYSTEM_INFO_FORMAT_STRING	"FPGA Revision: %s\tTCF Core Revision: %s\tTCF Core Target Build ID: %s\tPCI Version: %s\tMacro Version: %s"
static IMG_CHAR *GetDeviceVersionString(SYS_DATA *psSysData)
{
	int err;
	char str_fpga_rev[12];
	char str_tcf_core_rev[12];
	char str_tcf_core_target_build_id[4];
	char str_pci_ver[4];
	char str_macro_ver[8];

	IMG_CHAR *pszVersion;
	IMG_UINT32 ui32StringLength;

	err = apollo_sys_strings(psSysData->pdev->dev.parent,
							 str_fpga_rev, sizeof(str_fpga_rev),
							 str_tcf_core_rev, sizeof(str_tcf_core_rev),
							 str_tcf_core_target_build_id, sizeof(str_tcf_core_target_build_id),
							 str_pci_ver, sizeof(str_pci_ver),
							 str_macro_ver, sizeof(str_macro_ver));
	if (err)
	{
		return NULL;
	}

	ui32StringLength = OSStringLength(SYSTEM_INFO_FORMAT_STRING);
	ui32StringLength += OSStringLength(str_fpga_rev);
	ui32StringLength += OSStringLength(str_tcf_core_rev);
	ui32StringLength += OSStringLength(str_tcf_core_target_build_id);
	ui32StringLength += OSStringLength(str_pci_ver);
	ui32StringLength += OSStringLength(str_macro_ver);

	/* Create the version string */
	pszVersion = OSAllocZMem(ui32StringLength * sizeof(IMG_CHAR));
	if (pszVersion)
	{
		OSSNPrintf(&pszVersion[0], ui32StringLength,
				   SYSTEM_INFO_FORMAT_STRING,
				   str_fpga_rev,
				   str_tcf_core_rev,
				   str_tcf_core_target_build_id,
				   str_pci_ver,
				   str_macro_ver);
	}

	return pszVersion;
}

#if defined(SUPPORT_ION)
static SYS_DATA *gpsIonPrivateData;

PVRSRV_ERROR IonInit(void *pvPrivateData)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	SYS_DATA *psSysData = pvPrivateData;
	gpsIonPrivateData = psSysData;

	psSysData->ion_client = ion_client_create(psSysData->pdata->ion_device, SYS_RGX_DEV_NAME);
	if (IS_ERR(psSysData->ion_client))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to create ION client (%ld)", __func__, PTR_ERR(psSysData->ion_client)));
		/* FIXME: Find a better matching error code */
		eError = PVRSRV_ERROR_PCI_CALL_FAILED;
		goto err_out;
	}
	/* Allocate the whole rogue ion heap and pass that to services to manage */
	psSysData->ion_rogue_allocation = ion_alloc(psSysData->ion_client, psSysData->pdata->rogue_heap_memory_size, 4096, (1 << psSysData->pdata->ion_heap_id), 0);
	if (IS_ERR(psSysData->ion_rogue_allocation))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to allocate ION rogue buffer (%ld)", __func__, PTR_ERR(psSysData->ion_rogue_allocation)));
		/* FIXME: Find a better matching error code */
		eError = PVRSRV_ERROR_PCI_CALL_FAILED;
		goto err_destroy_client;

	}

	return PVRSRV_OK;
err_destroy_client:
	ion_client_destroy(psSysData->ion_client);
	psSysData->ion_client = NULL;
err_out:
	return eError;
}

void IonDeinit(void)
{
	SYS_DATA *psSysData = gpsIonPrivateData;
	ion_free(psSysData->ion_client, psSysData->ion_rogue_allocation);
	psSysData->ion_rogue_allocation = NULL;
	ion_client_destroy(psSysData->ion_client);
	psSysData->ion_client = NULL;
}

struct ion_device *IonDevAcquire(void)
{
	return gpsIonPrivateData->pdata->ion_device;
}

void IonDevRelease(struct ion_device *ion_device)
{
	PVR_ASSERT(ion_device == gpsIonPrivateData->pdata->ion_device);
}
#endif /* defined(SUPPORT_ION) */

static void TCLocalCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
				      IMG_UINT32 ui32NumOfAddr,
				      IMG_DEV_PHYADDR *psDevPAddr,
				      IMG_CPU_PHYADDR *psCpuPAddr)
{
	PVRSRV_DEVICE_CONFIG *psDevConfig = (PVRSRV_DEVICE_CONFIG *)hPrivData;

	/* Optimise common case */
	psDevPAddr[0].uiAddr = psCpuPAddr[0].uiAddr - psDevConfig->pasPhysHeaps[0].pasRegions[0].sStartAddr.uiAddr;
	if (ui32NumOfAddr > 1)
	{
		IMG_UINT32 ui32Idx;
		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
		{
			psDevPAddr[ui32Idx].uiAddr = psCpuPAddr[ui32Idx].uiAddr - psDevConfig->pasPhysHeaps[0].pasRegions[0].sStartAddr.uiAddr;
		}
	}
}

static void TCLocalDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
				      IMG_UINT32 ui32NumOfAddr,
				      IMG_CPU_PHYADDR *psCpuPAddr,
				      IMG_DEV_PHYADDR *psDevPAddr)
{
	PVRSRV_DEVICE_CONFIG *psDevConfig = (PVRSRV_DEVICE_CONFIG *)hPrivData;
	
	/* Optimise common case */
	psCpuPAddr[0].uiAddr = psDevPAddr[0].uiAddr + psDevConfig->pasPhysHeaps[0].pasRegions[0].sStartAddr.uiAddr;
	if (ui32NumOfAddr > 1)
	{
		IMG_UINT32 ui32Idx;
		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
		{
			psCpuPAddr[ui32Idx].uiAddr = psDevPAddr[ui32Idx].uiAddr + psDevConfig->pasPhysHeaps[0].pasRegions[0].sStartAddr.uiAddr;
		}
	}
}

static IMG_UINT32 TCLocalGetRegionId(IMG_HANDLE hPrivData,
					  PVRSRV_MEMALLOCFLAGS_T uiAllocFlags)
{
	/* Return first region which is always valid */
	return 0;
}

static void TCIonCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
				    IMG_UINT32 ui32NumOfAddr,
				    IMG_DEV_PHYADDR *psDevPAddr,
				    IMG_CPU_PHYADDR *psCpuPAddr)
{
	PVRSRV_DEVICE_CONFIG *psDevConfig = (PVRSRV_DEVICE_CONFIG *)hPrivData;
	SYS_DATA *psSysData = psDevConfig->hSysData;
	
	/* Optimise common case */
	psDevPAddr[0].uiAddr = psCpuPAddr[0].uiAddr - psSysData->pdata->apollo_memory_base;	
	if (ui32NumOfAddr > 1)
	{
		IMG_UINT32 ui32Idx;
		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
		{
			psDevPAddr[ui32Idx].uiAddr = psCpuPAddr[ui32Idx].uiAddr - psSysData->pdata->apollo_memory_base;
		}
	}
}

static void TCIonDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
				    IMG_UINT32 ui32NumOfAddr,
				    IMG_CPU_PHYADDR *psCpuPAddr,
				    IMG_DEV_PHYADDR *psDevPAddr)
{
	PVRSRV_DEVICE_CONFIG *psDevConfig = (PVRSRV_DEVICE_CONFIG *)hPrivData;
	SYS_DATA *psSysData = psDevConfig->hSysData;

	/* Optimise common case */
	psCpuPAddr[0].uiAddr = psDevPAddr[0].uiAddr + psSysData->pdata->apollo_memory_base;
	if (ui32NumOfAddr > 1)
	{
		IMG_UINT32 ui32Idx;
		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
		{
			psCpuPAddr[ui32Idx].uiAddr = psDevPAddr[ui32Idx].uiAddr + psSysData->pdata->apollo_memory_base;
		}
	}
}

static IMG_UINT32 TCIonGetRegionId(IMG_HANDLE hPrivData,
					  PVRSRV_MEMALLOCFLAGS_T uiAllocFlags)
{
	/* Return first region which is always valid */
	return 0;
}

static PVRSRV_ERROR PhysHeapsCreate(SYS_DATA *psSysData,
									void *pvPrivData,
									PHYS_HEAP_CONFIG **ppasPhysHeapsOut,
									IMG_UINT32 *puiPhysHeapCountOut)
{
	static IMG_UINT32 uiHeapIDBase = 0;
	PHYS_HEAP_CONFIG *pasPhysHeaps;
	PHYS_HEAP_REGION *psRegion;
	PVRSRV_ERROR eError;

	pasPhysHeaps = OSAllocMem(sizeof(*pasPhysHeaps) * PHYS_HEAP_IDX_COUNT);
	if (!pasPhysHeaps)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psRegion = OSAllocMem(sizeof(*psRegion));
	if (!psRegion)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorFreePhysHeaps;
	}

	psRegion->sStartAddr.uiAddr = psSysData->pdata->rogue_heap_memory_base;
	psRegion->sCardBase.uiAddr = 0;
	psRegion->uiSize = psSysData->pdata->rogue_heap_memory_size;

	pasPhysHeaps[PHYS_HEAP_IDX_GENERAL].ui32PhysHeapID =
		uiHeapIDBase + PHYS_HEAP_IDX_GENERAL;
	pasPhysHeaps[PHYS_HEAP_IDX_GENERAL].eType = PHYS_HEAP_TYPE_LMA;
	pasPhysHeaps[PHYS_HEAP_IDX_GENERAL].pszPDumpMemspaceName = "LMA";
	pasPhysHeaps[PHYS_HEAP_IDX_GENERAL].psMemFuncs = &gsLocalPhysHeapFuncs;
	pasPhysHeaps[PHYS_HEAP_IDX_GENERAL].pasRegions = psRegion;
	pasPhysHeaps[PHYS_HEAP_IDX_GENERAL].ui32NumOfRegions = 1;
	pasPhysHeaps[PHYS_HEAP_IDX_GENERAL].hPrivData = pvPrivData;

	psRegion = OSAllocMem(sizeof(*psRegion));
	if (!psRegion)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorGeneralPhysHeapDestroy;
	}

	psRegion->sStartAddr.uiAddr = psSysData->pdata->pdp_heap_memory_base;
	psRegion->sCardBase.uiAddr = 0;
	psRegion->uiSize = psSysData->pdata->pdp_heap_memory_size;

	pasPhysHeaps[PHYS_HEAP_IDX_DMABUF].ui32PhysHeapID =
		uiHeapIDBase + PHYS_HEAP_IDX_DMABUF;
	pasPhysHeaps[PHYS_HEAP_IDX_DMABUF].eType = PHYS_HEAP_TYPE_LMA;
	pasPhysHeaps[PHYS_HEAP_IDX_DMABUF].pszPDumpMemspaceName = "LMA";
	pasPhysHeaps[PHYS_HEAP_IDX_DMABUF].psMemFuncs = &gsIonPhysHeapFuncs;
	pasPhysHeaps[PHYS_HEAP_IDX_DMABUF].pasRegions = psRegion;
	pasPhysHeaps[PHYS_HEAP_IDX_DMABUF].ui32NumOfRegions = 1;
	pasPhysHeaps[PHYS_HEAP_IDX_DMABUF].hPrivData = pvPrivData;

	uiHeapIDBase += PHYS_HEAP_IDX_COUNT;

	*ppasPhysHeapsOut = pasPhysHeaps;
	*puiPhysHeapCountOut = PHYS_HEAP_IDX_COUNT;

	return PVRSRV_OK;

ErrorGeneralPhysHeapDestroy:
	OSFreeMem(pasPhysHeaps[PHYS_HEAP_IDX_GENERAL].pasRegions);

ErrorFreePhysHeaps:
	OSFreeMem(pasPhysHeaps);
	return eError;
}

static void PhysHeapsDestroy(PHYS_HEAP_CONFIG *pasPhysHeaps,
							 IMG_UINT32 uiPhysHeapCount)
{
	IMG_UINT32 i;

	for (i = 0; i < uiPhysHeapCount; i++)
	{
		if (pasPhysHeaps[i].pasRegions)
		{
			OSFreeMem(pasPhysHeaps[i].pasRegions);
		}
	}

	OSFreeMem(pasPhysHeaps);
}

static PVRSRV_ERROR DeviceConfigCreate(SYS_DATA *psSysData,
									   PVRSRV_DEVICE_CONFIG **ppsDevConfigOut)
{
	PVRSRV_DEVICE_CONFIG *psDevConfig;
	RGX_DATA *psRGXData;
	RGX_TIMING_INFORMATION *psRGXTimingInfo;
	PHYS_HEAP_CONFIG *pasPhysHeaps;
	IMG_UINT32 uiPhysHeapCount;
	PVRSRV_ERROR eError;

	psDevConfig = OSAllocZMem(sizeof(*psDevConfig) +
							  sizeof(*psRGXData) +
							  sizeof(*psRGXTimingInfo));
	if (!psDevConfig)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psRGXData = (RGX_DATA *)((IMG_CHAR *)psDevConfig + sizeof(*psDevConfig));
	psRGXTimingInfo = (RGX_TIMING_INFORMATION *)((IMG_CHAR *)psRGXData + sizeof(*psRGXData));

	eError = PhysHeapsCreate(psSysData, psDevConfig, &pasPhysHeaps, &uiPhysHeapCount);
	if (eError != PVRSRV_OK)
	{
		goto ErrorFreeDevConfig;
	}

	/* Setup RGX specific timing data */
	psRGXTimingInfo->ui32CoreClockSpeed = apollo_core_clock_speed(&psSysData->pdev->dev) * 6;
	psRGXTimingInfo->bEnableActivePM = IMG_FALSE;
	psRGXTimingInfo->bEnableRDPowIsland = IMG_FALSE;
	psRGXTimingInfo->ui32ActivePMLatencyms = SYS_RGX_ACTIVE_POWER_LATENCY_MS;

	/* Set up the RGX data */
	psRGXData->psRGXTimingInfo = psRGXTimingInfo;

	/* Setup the device config */
	psDevConfig->pvOSDevice = &psSysData->pdev->dev;
	psDevConfig->pszName = "apollo";
	psDevConfig->pszVersion = GetDeviceVersionString(psSysData);

	psDevConfig->sRegsCpuPBase.uiAddr = psSysData->registers->start;
	psDevConfig->ui32RegsSize = resource_size(psSysData->registers);

	psDevConfig->ui32IRQ = APOLLO_INTERRUPT_EXT;

	psDevConfig->eCacheSnoopingMode = PVRSRV_DEVICE_SNOOP_NONE;

	psDevConfig->pasPhysHeaps = pasPhysHeaps;
	psDevConfig->ui32PhysHeapCount = uiPhysHeapCount;

	psDevConfig->aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL] =
		pasPhysHeaps[PHYS_HEAP_IDX_GENERAL].ui32PhysHeapID;
	psDevConfig->aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_CPU_LOCAL] =
		pasPhysHeaps[PHYS_HEAP_IDX_GENERAL].ui32PhysHeapID;
	psDevConfig->aui32PhysHeapID[PVRSRV_DEVICE_PHYS_HEAP_FW_LOCAL] =
		pasPhysHeaps[PHYS_HEAP_IDX_GENERAL].ui32PhysHeapID;

	psDevConfig->eBIFTilingMode = geBIFTilingMode;
	psDevConfig->pui32BIFTilingHeapConfigs = &gauiBIFTilingHeapXStrides[0];
	psDevConfig->ui32BIFTilingHeapCount = IMG_ARR_NUM_ELEMS(gauiBIFTilingHeapXStrides);

	psDevConfig->hDevData = psRGXData;
	psDevConfig->hSysData = psSysData;

#if defined(PVR_DVFS) || defined(SUPPORT_PDVFS)
	/* Dummy DVFS configuration used purely for testing purposes */
	psDevConfig->sDVFS.sDVFSDeviceCfg.pasOPPTable = asOPPTable;
	psDevConfig->sDVFS.sDVFSDeviceCfg.ui32OPPTableSize = LEVEL_COUNT;
	psDevConfig->sDVFS.sDVFSDeviceCfg.pfnSetFrequency = SetFrequency;
	psDevConfig->sDVFS.sDVFSDeviceCfg.pfnSetVoltage = SetVoltage;
#endif
#if defined(PVR_DVFS)
	psDevConfig->sDVFS.sDVFSDeviceCfg.ui32PollMs = 1000;
	psDevConfig->sDVFS.sDVFSDeviceCfg.bIdleReq = IMG_TRUE;
	psDevConfig->sDVFS.sDVFSGovernorCfg.ui32UpThreshold = 90;
	psDevConfig->sDVFS.sDVFSGovernorCfg.ui32DownDifferential = 10;
#endif

	*ppsDevConfigOut = psDevConfig;

	return PVRSRV_OK;

ErrorFreeDevConfig:
	OSFreeMem(psDevConfig);
	return eError;
}

static void DeviceConfigDestroy(PVRSRV_DEVICE_CONFIG *psDevConfig)
{
	if (psDevConfig->pszVersion)
	{
		OSFreeMem(psDevConfig->pszVersion);
	}

	PhysHeapsDestroy(psDevConfig->pasPhysHeaps, psDevConfig->ui32PhysHeapCount);

	OSFreeMem(psDevConfig);
}

PVRSRV_ERROR SysDevInit(void *pvOSDevice, PVRSRV_DEVICE_CONFIG **ppsDevConfig)
{
	PVRSRV_DEVICE_CONFIG *psDevConfig;
	SYS_DATA *psSysData;
	resource_size_t uiRegistersSize;
	PVRSRV_ERROR eError;
	int err = 0;

	PVR_ASSERT(pvOSDevice);

	psSysData = OSAllocZMem(sizeof(*psSysData));
	if (psSysData == NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psSysData->pdev = to_platform_device((struct device *)pvOSDevice);
	psSysData->pdata = psSysData->pdev->dev.platform_data;

	err = apollo_enable(psSysData->pdev->dev.parent);
	if (err)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to enable PCI device (%d)", __func__, err));
		eError = PVRSRV_ERROR_PCI_CALL_FAILED;
		goto ErrFreeSysData;
	}

	psSysData->registers = platform_get_resource_byname(psSysData->pdev,
														IORESOURCE_MEM,
														"rogue-regs");
	if (!psSysData->registers)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: Failed to get Rogue register information",
				 __func__));
		eError = PVRSRV_ERROR_PCI_REGION_UNAVAILABLE;
		goto ErrorDevDisable;
	}

	/* Check the address range is large enough. */
	uiRegistersSize = resource_size(psSysData->registers);
	if (uiRegistersSize < SYS_RGX_REG_REGION_SIZE)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: Rogue register region isn't big enough (was %pa, required 0x%08x)",
				 __FUNCTION__, &uiRegistersSize, SYS_RGX_REG_REGION_SIZE));

		eError = PVRSRV_ERROR_PCI_REGION_TOO_SMALL;
		goto ErrorDevDisable;
	}

	/* Reserve the address range */
	if (!request_mem_region(psSysData->registers->start,
							resource_size(psSysData->registers),
							SYS_RGX_DEV_NAME))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Rogue register memory region not available", __FUNCTION__));
		eError = PVRSRV_ERROR_PCI_CALL_FAILED;

		goto ErrorDevDisable;
	}

	eError = DeviceConfigCreate(psSysData, &psDevConfig);
	if (eError != PVRSRV_OK)
	{
		goto ErrorReleaseMemRegion;
	}

#if defined(SUPPORT_ION)
	eError = IonInit(psSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to initialise ION", __func__));
		goto ErrorDeviceConfigDestroy;
	}
#endif

	*ppsDevConfig = psDevConfig;

	return PVRSRV_OK;

#if defined(SUPPORT_ION)
ErrorDeviceConfigDestroy:
	DeviceConfigDestroy(psDevConfig);
#endif
ErrorReleaseMemRegion:
	release_mem_region(psSysData->registers->start,
					   resource_size(psSysData->registers));
ErrorDevDisable:
	apollo_disable(psSysData->pdev->dev.parent);
ErrFreeSysData:
	OSFreeMem(psSysData);
	return eError;
}

void SysDevDeInit(PVRSRV_DEVICE_CONFIG *psDevConfig)
{
	SYS_DATA *psSysData = (SYS_DATA *)psDevConfig->hSysData;

#if defined(SUPPORT_ION)
	IonDeinit();
#endif

	DeviceConfigDestroy(psDevConfig);

	release_mem_region(psSysData->registers->start,
					   resource_size(psSysData->registers));
	apollo_disable(psSysData->pdev->dev.parent);

	OSFreeMem(psSysData);
}

PVRSRV_ERROR SysDebugInfo(PVRSRV_DEVICE_CONFIG *psDevConfig,
				DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile)
{
#if defined(TC_APOLLO_TCF5)
	PVR_UNREFERENCED_PARAMETER(psDevConfig);
	PVR_UNREFERENCED_PARAMETER(pfnDumpDebugPrintf);
	return PVRSRV_OK;
#else
	SYS_DATA *psSysData = psDevConfig->hSysData;
	PVRSRV_ERROR eError = PVRSRV_OK;
	u32 tmp = 0;
	u32 pll;

	PVR_DUMPDEBUG_LOG("------[ rgx_tc system debug ]------");

	if (apollo_sys_info(psSysData->pdev->dev.parent, &tmp, &pll))
		goto err_out;

	if (tmp > 0)
		PVR_DUMPDEBUG_LOG("Chip temperature: %d degrees C", tmp);
	PVR_DUMPDEBUG_LOG("PLL status: %x", pll);

err_out:
	return eError;
#endif
}

typedef struct
{
	struct device *psDev;
	int iInterruptID;
	void *pvData;
	PFN_LISR pfnLISR;
} LISR_DATA;

static void ApolloInterruptHandler(void* pvData)
{
	LISR_DATA *psLISRData = pvData;
	psLISRData->pfnLISR(psLISRData->pvData);
}

PVRSRV_ERROR SysInstallDeviceLISR(IMG_HANDLE hSysData,
				  IMG_UINT32 ui32IRQ,
				  const IMG_CHAR *pszName,
				  PFN_LISR pfnLISR,
				  void *pvData,
				  IMG_HANDLE *phLISRData)
{
	SYS_DATA *psSysData = (SYS_DATA *)hSysData;
	LISR_DATA *psLISRData;
	PVRSRV_ERROR eError;
	int err;

	if (ui32IRQ != APOLLO_INTERRUPT_EXT)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: No device matching IRQ %d", __func__, ui32IRQ));
		return PVRSRV_ERROR_UNABLE_TO_INSTALL_ISR;
	}

	psLISRData = OSAllocZMem(sizeof(*psLISRData));
	if (!psLISRData)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto err_out;
	}

	psLISRData->pfnLISR = pfnLISR;
	psLISRData->pvData = pvData;
	psLISRData->iInterruptID = ui32IRQ;
	psLISRData->psDev = psSysData->pdev->dev.parent;

	err = apollo_set_interrupt_handler(psLISRData->psDev, psLISRData->iInterruptID, ApolloInterruptHandler, psLISRData);
	if (err)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: apollo_set_interrupt_handler() failed (%d)", __func__, err));
		eError = PVRSRV_ERROR_UNABLE_TO_INSTALL_ISR;
		goto err_free_data;
	}

	err = apollo_enable_interrupt(psLISRData->psDev, psLISRData->iInterruptID);
	if (err)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: apollo_enable_interrupt() failed (%d)", __func__, err));
		eError = PVRSRV_ERROR_UNABLE_TO_INSTALL_ISR;
		goto err_unset_interrupt_handler;
	}

	*phLISRData = psLISRData;
	eError = PVRSRV_OK;

	PVR_TRACE(("Installed device LISR %pf to irq %u", pfnLISR, ui32IRQ));

err_out:
	return eError;
err_unset_interrupt_handler:
	apollo_set_interrupt_handler(psLISRData->psDev, psLISRData->iInterruptID, NULL, NULL);
err_free_data:
	OSFreeMem(psLISRData);
	goto err_out;
}

PVRSRV_ERROR SysUninstallDeviceLISR(IMG_HANDLE hLISRData)
{
	LISR_DATA *psLISRData = (LISR_DATA *) hLISRData;
	int err;

	err = apollo_disable_interrupt(psLISRData->psDev, psLISRData->iInterruptID);
	if (err)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: apollo_enable_interrupt() failed (%d)", __func__, err));
	}

	err = apollo_set_interrupt_handler(psLISRData->psDev, psLISRData->iInterruptID, NULL, NULL);
	if (err)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: apollo_set_interrupt_handler() failed (%d)", __func__, err));
	}

	PVR_TRACE(("Uninstalled device LISR %pf from irq %u", psLISRData->pfnLISR, psLISRData->iInterruptID));

	OSFreeMem(psLISRData);

	return PVRSRV_OK;
}
