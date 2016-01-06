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

#define TC_SYSTEM_NAME		"Rogue Test Chip"

/* Valid values for the TC_MEMORY_CONFIG configuration option */
#define TC_MEMORY_LOCAL		(1)
#define TC_MEMORY_HOST		(2)
#define TC_MEMORY_HYBRID	(3)
#define TC_MEMORY_DIRECT_MAPPED	(4)

#if TC_MEMORY_CONFIG != TC_MEMORY_LOCAL
#error Apollo only supports TC_MEMORY_LOCAL at the minute
#endif

#define SYS_RGX_ACTIVE_POWER_LATENCY_MS (10)

static PVRSRV_SYSTEM_CONFIG gsSysConfig;

static RGX_TIMING_INFORMATION gsRGXTimingInfo =
{
	/* Initialise to 0, real value will be set in PCIInitDev() */
	.ui32CoreClockSpeed = 0,
	.bEnableActivePM = IMG_FALSE,
	.bEnableRDPowIsland = IMG_FALSE,
	.ui32ActivePMLatencyms = SYS_RGX_ACTIVE_POWER_LATENCY_MS,
};

static RGX_DATA gsRGXData =
{
	.psRGXTimingInfo = &gsRGXTimingInfo
};
static PVRSRV_DEVICE_CONFIG gsDevices[] =
{
	{
		.uiFlags = 0,
		.pszName = "RGX",
		.eDeviceType = PVRSRV_DEVICE_TYPE_RGX,

		/* Device setup information */
		.sRegsCpuPBase = { 0 },
		.ui32RegsSize = 0,

		.ui32IRQ = APOLLO_INTERRUPT_ROGUE,
		.bIRQIsShared = IMG_TRUE,
		.eIRQActiveLevel = PVRSRV_DEVICE_IRQ_ACTIVE_SYSDEFAULT,

		.hDevData = &gsRGXData,
		.hSysData = IMG_NULL,

		.aui32PhysHeapID = { 0, 0 },

		.pfnPrePowerState = IMG_NULL,
		.pfnPostPowerState = IMG_NULL,
		.pfnClockFreqGet = IMG_NULL,
		.pfnInterruptHandled = IMG_NULL,
		.pfnCheckMemAllocSize = SysCheckMemAllocSize,

		.eBPDM = RGXFWIF_DM_TA,
		.bBPSet = IMG_FALSE
	}
};

static void TCLocalCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
				      IMG_UINT32 ui32NumOfAddr,
				      IMG_DEV_PHYADDR *psDevPAddr,
				      IMG_CPU_PHYADDR *psCpuPAddr);

static void TCLocalDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
				      IMG_UINT32 ui32NumOfAddr,
				      IMG_CPU_PHYADDR *psCpuPAddr,
				      IMG_DEV_PHYADDR *psDevPAddr);
static PHYS_HEAP_FUNCTIONS gsLocalPhysHeapFuncs =
{
	.pfnCpuPAddrToDevPAddr = TCLocalCpuPAddrToDevPAddr,
	.pfnDevPAddrToCpuPAddr = TCLocalDevPAddrToCpuPAddr,
};

static void TCIonCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
				    IMG_UINT32 ui32NumOfAddr,
				    IMG_DEV_PHYADDR *psDevPAddr,
				    IMG_CPU_PHYADDR *psCpuPAddr);

static void TCIonDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
				    IMG_UINT32 ui32NumOfAddr,
				    IMG_CPU_PHYADDR *psCpuPAddr,
				    IMG_DEV_PHYADDR *psDevPAddr);

static PHYS_HEAP_FUNCTIONS gsIonPhysHeapFuncs =
{
	.pfnCpuPAddrToDevPAddr = TCIonCpuPAddrToDevPAddr,
	.pfnDevPAddrToCpuPAddr = TCIonDevPAddrToCpuPAddr,
};

static PHYS_HEAP_CONFIG	gsPhysHeapConfig[] =
{
	{
		.ui32PhysHeapID = 0,
		.eType = PHYS_HEAP_TYPE_LMA,
		.sStartAddr = { 0 },
		.uiSize = 0,
		.pszPDumpMemspaceName = "LMA",
		.psMemFuncs = &gsLocalPhysHeapFuncs,
		.hPrivData = (IMG_HANDLE)&gsSysConfig,
	},
	{
		.ui32PhysHeapID = 1,
		.eType = PHYS_HEAP_TYPE_LMA,
		.sStartAddr = { 0 },
		.uiSize = 0,
		.pszPDumpMemspaceName = "LMA",
		.psMemFuncs = &gsIonPhysHeapFuncs,
		.hPrivData = (IMG_HANDLE)&gsSysConfig,
	}
};

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
	.uiSysFlags = 0,
	.pszSystemName = TC_SYSTEM_NAME,
	.uiDeviceCount = ARRAY_SIZE(gsDevices),
	.pasDevices = &gsDevices[0],

	.pfnSysPrePowerState = IMG_NULL,
	.pfnSysPostPowerState = IMG_NULL,

	.eCacheSnoopingMode = PVRSRV_SYSTEM_SNOOP_NONE,

	.pasPhysHeaps = &gsPhysHeapConfig[0],
	.ui32PhysHeapCount = ARRAY_SIZE(gsPhysHeapConfig),

	.pui32BIFTilingHeapConfigs = &gauiBIFTilingHeapXStrides[0],
	.ui32BIFTilingHeapCount = ARRAY_SIZE(gauiBIFTilingHeapXStrides),
};


typedef struct _SYS_DATA_ SYS_DATA;

extern struct platform_device *gpsPVRLDMDev;

struct _SYS_DATA_
{
	IMG_UINT32		uiRefCount;

	struct pci_dev *pcidev;

	struct platform_device *pdev;

	struct apollo_rogue_platform_data *pdata;

	IMG_CHAR		*pszSystemInfoString;

	PVRSRV_SYS_POWER_STATE	ePowerState;

#if defined(SUPPORT_ION)
	struct ion_client *ion_client;
	struct ion_handle *ion_rogue_allocation;
#endif

	IMG_HANDLE		hFlashData;

};

#define SYSTEM_INFO_FORMAT_STRING	"%s\tFPGA Revision: %s\tTCF Core Revision: %s\tTCF Core Target Build ID: %s\tPCI Version: %s\tMacro Version: %s"
static IMG_CHAR *GetSystemInfoString(SYS_DATA *psSysData)
{
	int err;
	char str_fpga_rev[12];
	char str_tcf_core_rev[12];
	char str_tcf_core_target_build_id[4];
	char str_pci_ver[4];
	char str_macro_ver[8];

	IMG_CHAR *pszSystemInfoString;
	IMG_UINT32 ui32StringLength;

	struct apollo_rogue_platform_data *pdata = ((struct platform_device*)gpsPVRLDMDev)->dev.platform_data;

	err = apollo_sys_strings(&pdata->pdev->dev, //&psSysData->pdev->dev,
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
	ui32StringLength += OSStringLength(TC_SYSTEM_NAME);
	ui32StringLength += OSStringLength(str_fpga_rev);
	ui32StringLength += OSStringLength(str_tcf_core_rev);
	ui32StringLength += OSStringLength(str_tcf_core_target_build_id);
	ui32StringLength += OSStringLength(str_pci_ver);
	ui32StringLength += OSStringLength(str_macro_ver);

	/* Create the system info string */
	pszSystemInfoString = OSAllocZMem(ui32StringLength * sizeof(IMG_CHAR));
	if (pszSystemInfoString)
	{
		OSSNPrintf(&pszSystemInfoString[0], ui32StringLength, SYSTEM_INFO_FORMAT_STRING, TC_SYSTEM_NAME,
				   str_fpga_rev,
				   str_tcf_core_rev,
				   str_tcf_core_target_build_id,
				   str_pci_ver,
				   str_macro_ver);
	}

	return pszSystemInfoString;
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

IMG_DEV_PHYADDR IonCPUPhysToDevPhys(IMG_CPU_PHYADDR sCPUPhysAddr,
                                    IMG_UINT32 ui32Offset)
{
	IMG_DEV_PHYADDR sDevPAddr =
	{
			.uiAddr = sCPUPhysAddr.uiAddr + ui32Offset -
					  gpsIonPrivateData->pdata->apollo_memory_base,
	};

	PVR_ASSERT(sCPUPhysAddr.uiAddr + ui32Offset
		>= gpsIonPrivateData->pdata->pdp_heap_memory_base);
	PVR_ASSERT(sCPUPhysAddr.uiAddr + ui32Offset
		< gpsIonPrivateData->pdata->pdp_heap_memory_base
			+ gpsIonPrivateData->pdata->pdp_heap_memory_size);

	return sDevPAddr;
}

struct ion_device *IonDevAcquire(void)
{
	return gpsIonPrivateData->pdata->ion_device;
}

void IonDevRelease(struct ion_device *ion_device)
{
	PVR_ASSERT(ion_device == gpsIonPrivateData->pdata->ion_device);
}

IMG_UINT32 IonPhysHeapID(void)
{
	/* This seems to be the services heap that ION allocations should be mapped into
	 * FIXME: This may depend on the allocation? Eg. if allocations can come
	 * from more than one heap?*/
	return 1;
}
#endif /* defined(SUPPORT_ION) */

static PVRSRV_ERROR PCIInitDev(SYS_DATA *psSysData)
{
	PVRSRV_DEVICE_CONFIG *psDevice = &gsSysConfig.pasDevices[0];
	PVRSRV_ERROR eError;
	int err = 0;

	err = pci_enable_device(psSysData->pcidev);
	if (err)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to enable PCI device (%d)", __func__, err));
		eError = PVRSRV_ERROR_PCI_CALL_FAILED;
		goto err_out;
	}

#if defined(SUPPORT_ION)
	eError = IonInit(psSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to initialise ION", __func__));
		goto ErrorPCIReleaseDevice;
	}
#endif

	/* FIXME: Use ion_phys on the allocation? */
	gsPhysHeapConfig[0].uiSize =
		psSysData->pdata->rogue_heap_memory_size;

	gsPhysHeapConfig[0].sStartAddr.uiAddr =
		psSysData->pdata->rogue_heap_memory_base;



	gsPhysHeapConfig[1].uiSize = psSysData->pdata->pdp_heap_memory_size;
	gsPhysHeapConfig[1].sStartAddr.uiAddr = psSysData->pdata->pdp_heap_memory_base;

	/* Setup Rogue register information */
	psDevice->sRegsCpuPBase.uiAddr	= pci_resource_start(psSysData->pcidev, SYS_RGX_REG_PCI_BASENUM);
	psDevice->ui32RegsSize		= pci_resource_len(psSysData->pcidev, SYS_RGX_REG_PCI_BASENUM);

	/* Save data for this device */
	psDevice->hSysData = (IMG_HANDLE)psSysData;

	/* Check the address range is large enough. */
	if (psDevice->ui32RegsSize < SYS_RGX_REG_REGION_SIZE)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: Rogue register region isn't big enough (was 0x%08x, required 0x%08x)",
			 __FUNCTION__, psDevice->ui32RegsSize, SYS_RGX_REG_REGION_SIZE));

		eError = PVRSRV_ERROR_PCI_REGION_TOO_SMALL;
		goto ErrorDeInitION;
	}

	/* Reserve the address range */
	err = pci_request_region(psSysData->pcidev, SYS_RGX_REG_PCI_BASENUM, SYS_RGX_DEV_NAME);
	if (err)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Rogue register memory region not available", __FUNCTION__));
		eError = PVRSRV_ERROR_PCI_CALL_FAILED;

		goto ErrorDeInitION;
	}

	/* Override the system name if we can get the system info string */
	psSysData->pszSystemInfoString = GetSystemInfoString(psSysData);
	if (psSysData->pszSystemInfoString)
	{
		gsSysConfig.pszSystemName = psSysData->pszSystemInfoString;
	}

	/* Make the core clock speed value available to the driver */
	((RGX_DATA *)psDevice->hDevData)->psRGXTimingInfo->ui32CoreClockSpeed = apollo_core_clock_speed(&psSysData->pdev->dev) * 6;

	return PVRSRV_OK;

ErrorDeInitION:
#if defined(SUPPORT_ION)
	IonDeinit();
ErrorPCIReleaseDevice:
#endif
	pci_disable_device(psSysData->pcidev);
err_out:
	return eError;
}

static void PCIDeInitDev(SYS_DATA *psSysData)
{
	PVRSRV_DEVICE_CONFIG *psDevice = &gsSysConfig.pasDevices[0];

	if (psSysData->pszSystemInfoString)
	{
		OSFreeMem(psSysData->pszSystemInfoString);
		psSysData->pszSystemInfoString = NULL;
	}

#if defined(SUPPORT_ION)
	IonDeinit();
#endif

	pci_release_region(psSysData->pcidev, SYS_RGX_REG_PCI_BASENUM);
	psDevice->sRegsCpuPBase.uiAddr	= 0;
	psDevice->ui32RegsSize		= 0;

	pci_disable_device(psSysData->pcidev);
	psSysData->pcidev = NULL;
}

static void TCLocalCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
				      IMG_UINT32 ui32NumOfAddr,
				      IMG_DEV_PHYADDR *psDevPAddr,
				      IMG_CPU_PHYADDR *psCpuPAddr)
{
	PVRSRV_SYSTEM_CONFIG *psSysConfig = (PVRSRV_SYSTEM_CONFIG *)hPrivData;

	/* Optimise common case */
	psDevPAddr[0].uiAddr = psCpuPAddr[0].uiAddr - psSysConfig->pasPhysHeaps[0].sStartAddr.uiAddr;
	if (ui32NumOfAddr > 1)
	{
		IMG_UINT32 ui32Idx;
		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
		{
			psDevPAddr[ui32Idx].uiAddr = psCpuPAddr[ui32Idx].uiAddr - psSysConfig->pasPhysHeaps[0].sStartAddr.uiAddr;
		}
	}
}

static void TCLocalDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
				      IMG_UINT32 ui32NumOfAddr,
				      IMG_CPU_PHYADDR *psCpuPAddr,
				      IMG_DEV_PHYADDR *psDevPAddr)
{
	PVRSRV_SYSTEM_CONFIG *psSysConfig = (PVRSRV_SYSTEM_CONFIG *)hPrivData;
	
	/* Optimise common case */
	psCpuPAddr[0].uiAddr = psDevPAddr[0].uiAddr + psSysConfig->pasPhysHeaps[0].sStartAddr.uiAddr;
	if (ui32NumOfAddr > 1)
	{
		IMG_UINT32 ui32Idx;
		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
		{
			psCpuPAddr[ui32Idx].uiAddr = psDevPAddr[ui32Idx].uiAddr + psSysConfig->pasPhysHeaps[0].sStartAddr.uiAddr;
		}
	}
}


static void TCIonCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
				    IMG_UINT32 ui32NumOfAddr,
				    IMG_DEV_PHYADDR *psDevPAddr,
				    IMG_CPU_PHYADDR *psCpuPAddr)
{
	PVRSRV_SYSTEM_CONFIG *psSysConfig = (PVRSRV_SYSTEM_CONFIG *)hPrivData;
	SYS_DATA *psSysData = psSysConfig->pasDevices[0].hSysData;
	
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
	PVRSRV_SYSTEM_CONFIG *psSysConfig = (PVRSRV_SYSTEM_CONFIG *)hPrivData;
	SYS_DATA *psSysData = psSysConfig->pasDevices[0].hSysData;

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

PVRSRV_ERROR SysCreateConfigData(PVRSRV_SYSTEM_CONFIG **ppsSysConfig, void *hDevice)
{
	SYS_DATA *psSysData;
	PVRSRV_ERROR eError;

	PVR_UNREFERENCED_PARAMETER(hDevice);

	psSysData = OSAllocZMem(sizeof(*psSysData));
	if (psSysData == IMG_NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	PVR_ASSERT(gpsPVRLDMDev != NULL);

	psSysData->pdev = gpsPVRLDMDev;
	psSysData->pdata = ((struct platform_device*)gpsPVRLDMDev)->dev.platform_data;
	psSysData->pcidev = psSysData->pdata->pdev;

	eError = PCIInitDev(psSysData);
	if (eError != PVRSRV_OK)
	{
		goto ErrorFreeSysData;
	}

	(void)SysAcquireSystemData((IMG_HANDLE)psSysData);

	*ppsSysConfig = &gsSysConfig;

	return PVRSRV_OK;

ErrorFreeSysData:
	OSFreeMem(psSysData);

	return eError;
}

void SysDestroyConfigData(PVRSRV_SYSTEM_CONFIG *psSysConfig)
{
	SYS_DATA *psSysData = (SYS_DATA *)psSysConfig->pasDevices[0].hSysData;

	PCIDeInitDev(psSysData);

	(void)SysReleaseSystemData((IMG_HANDLE)psSysData);
}

PVRSRV_ERROR SysAcquireSystemData(IMG_HANDLE hSysData)
{
	SYS_DATA *psSysData = (SYS_DATA *)hSysData;

	if (psSysData == IMG_NULL)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psSysData->uiRefCount++;

	return PVRSRV_OK;
}

PVRSRV_ERROR SysReleaseSystemData(IMG_HANDLE hSysData)
{
	SYS_DATA *psSysData = (SYS_DATA *)hSysData;

	if (psSysData == IMG_NULL)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	PVR_ASSERT(psSysData->uiRefCount != 0);
	psSysData->uiRefCount--;

	if (psSysData->uiRefCount == 0)
	{
		OSFreeMem(psSysData);
	}

	return PVRSRV_OK;
}

PVRSRV_ERROR SysDebugInfo(PVRSRV_SYSTEM_CONFIG *psSysConfig, DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	u32 tmp = 0;
	u32 pll;

	struct apollo_rogue_platform_data *pdata = ((struct platform_device*)gpsPVRLDMDev)->dev.platform_data;

	PVR_UNREFERENCED_PARAMETER(psSysConfig);

	PVR_DUMPDEBUG_LOG(("------[ rgx_tc system debug ]------"));

	if (apollo_sys_info(&pdata->pdev->dev, &tmp, &pll))
		goto err_out;

	if (tmp > 0)
		PVR_DUMPDEBUG_LOG(("Chip temperature: %d degrees C", tmp));
	PVR_DUMPDEBUG_LOG(("PLL status: %x", pll));

err_out:
	return eError;
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

PVRSRV_ERROR SysInstallDeviceLISR(IMG_UINT32 ui32IRQ,
				  IMG_CHAR *pszName,
				  PFN_LISR pfnLISR,
				  void *pvData,
				  IMG_HANDLE *phLISRData)
{
	LISR_DATA *psLISRData;
	PVRSRV_ERROR eError;
	int err;
	struct apollo_rogue_platform_data *pdata = ((struct platform_device*)gpsPVRLDMDev)->dev.platform_data;

	PVR_DPF((PVR_DBG_ERROR, "%s: Trying to install LISR %p to irq %u", __func__, pfnLISR, ui32IRQ));
	psLISRData = OSAllocZMem(sizeof(*psLISRData));
	if (!psLISRData)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto err_out;
	}
	psLISRData->pfnLISR = pfnLISR;
	psLISRData->pvData = pvData;
	psLISRData->iInterruptID = APOLLO_INTERRUPT_ROGUE;
	psLISRData->psDev = &pdata->pdev->dev;
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
	int err = apollo_disable_interrupt(psLISRData->psDev, psLISRData->iInterruptID);
	if (err)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: apollo_enable_interrupt() failed (%d)", __func__, err));
	}
	err = apollo_set_interrupt_handler(psLISRData->psDev, psLISRData->iInterruptID, NULL, NULL);
	if (err)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: apollo_set_interrupt_handler() failed (%d)", __func__, err));
	}
	OSFreeMem(psLISRData);
	return PVRSRV_OK;
}
