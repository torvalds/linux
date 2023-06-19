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

#include <linux/version.h>

#include "sysinfo.h"
#include "apollo_regs.h"

#include "pvrsrv.h"
#include "pvrsrv_device.h"
#include "rgxdevice.h"
#include "syscommon.h"

#if defined(SUPPORT_ION)
#include PVR_ANDROID_ION_HEADER
#include "ion_support.h"
#include "ion_sys.h"
#endif

#include "tc_drv.h"
#include "fpga.h"

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>

/* Must be consecutive and start from 0 */
#define PHY_HEAP_CARD_GPU 0
#define PHY_HEAP_CARD_EXT 1
#if defined(SUPPORT_SECURITY_VALIDATION)
#define PHY_HEAP_SEC_FW_CODE 2
#define PHY_HEAP_SEC_FW_DATA 3
#define PHY_HEAP_SEC_MEM 4
#define PHY_HEAP_SYSTEM   5
#else
#define PHY_HEAP_CARD_FW 2
#define PHY_HEAP_SYSTEM 3
#endif

#if defined(SUPPORT_SECURITY_VALIDATION)
#define PHY_HEAP_LMA_NUM  5
#define PHY_HEAP_HYBRID_NUM 6
#else
#define PHY_HEAP_LMA_NUM  3
#define PHY_HEAP_HYBRID_NUM 4
#endif

#if defined(SUPPORT_SECURITY_VALIDATION) && (RGX_NUM_DRIVERS_SUPPORTED > 1)
#error "Security support and virtualization are currently mutually exclusive."
#endif

#define ODIN_MEMORY_HYBRID_DEVICE_BASE 0x400000000

#define SYS_RGX_ACTIVE_POWER_LATENCY_MS (10)

#if defined(SUPPORT_SECURITY_VALIDATION)
#define SECURE_FW_CODE_MEM_SIZE (0x200000)  /* 2MB  (max HMMU page size) */
#define SECURE_FW_DATA_MEM_SIZE (0x200000)  /* 2MB  (max HMMU page size) */
#define SECURE_MEM_SIZE         (0x4000000) /* 32MB (multiple of max HMMU page size) */
#endif

#if defined(SUPPORT_LINUX_DVFS) || defined(SUPPORT_PDVFS)

/* Fake DVFS configuration used purely for testing purposes */

static const IMG_OPP asOPPTable[] =
{
	{ 8,  25000000},
	{ 16, 50000000},
	{ 32, 75000000},
	{ 64, 100000000},
};

#define LEVEL_COUNT (sizeof(asOPPTable) / sizeof(IMG_OPP))

static void SetFrequency(IMG_HANDLE hSysData, IMG_UINT32 ui32Frequency)
{
	PVR_UNREFERENCED_PARAMETER(hSysData);

	PVR_DPF((PVR_DBG_ERROR, "SetFrequency %u", ui32Frequency));
}

static void SetVoltage(IMG_HANDLE hSysData, IMG_UINT32 ui32Voltage)
{
	PVR_UNREFERENCED_PARAMETER(hSysData);

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

static PHYS_HEAP_FUNCTIONS gsLocalPhysHeapFuncs =
{
	.pfnCpuPAddrToDevPAddr = TCLocalCpuPAddrToDevPAddr,
	.pfnDevPAddrToCpuPAddr = TCLocalDevPAddrToCpuPAddr,
};

static void TCHostCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
									 IMG_UINT32 ui32NumOfAddr,
									 IMG_DEV_PHYADDR *psDevPAddr,
									 IMG_CPU_PHYADDR *psCpuPAddr);

static void TCHostDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
									 IMG_UINT32 ui32NumOfAddr,
									 IMG_CPU_PHYADDR *psCpuPAddr,
									 IMG_DEV_PHYADDR *psDevPAddr);

static PHYS_HEAP_FUNCTIONS gsHostPhysHeapFuncs =
{
	.pfnCpuPAddrToDevPAddr = TCHostCpuPAddrToDevPAddr,
	.pfnDevPAddrToCpuPAddr = TCHostDevPAddrToCpuPAddr,
};

static void TCHybridCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
                                       IMG_UINT32 ui32NumOfAddr,
                                       IMG_DEV_PHYADDR *psDevPAddr,
                                       IMG_CPU_PHYADDR *psCpuPAddr);

static void TCHybridDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
                                       IMG_UINT32 ui32NumOfAddr,
                                       IMG_CPU_PHYADDR *psCpuPAddr,
                                       IMG_DEV_PHYADDR *psDevPAddr);

static PHYS_HEAP_FUNCTIONS gsHybridPhysHeapFuncs =
{
	.pfnCpuPAddrToDevPAddr = TCHybridCpuPAddrToDevPAddr,
	.pfnDevPAddrToCpuPAddr = TCHybridDevPAddrToCpuPAddr
};

typedef struct _SYS_DATA_ SYS_DATA;

struct _SYS_DATA_
{
	struct platform_device *pdev;

	struct tc_rogue_platform_data *pdata;

	struct resource *registers;

#if defined(SUPPORT_ION) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0))
	struct ion_client *ion_client;
	struct ion_handle *ion_rogue_allocation;
#endif
};

#define SYSTEM_INFO_FORMAT_STRING "FPGA Revision: %s - TCF Core Revision: %s - TCF Core Target Build ID: %s - PCI Version: %s - Macro Version: %s"
#define FPGA_REV_MAX_LEN      8 /* current longest format: "x.y.z" */
#define TCF_CORE_REV_MAX_LEN  8 /* current longest format: "x.y.z" */
#define TCF_CORE_CFG_MAX_LEN  4 /* current longest format: "x" */
#define PCI_VERSION_MAX_LEN   4 /* current longest format: "x" */
#define MACRO_VERSION_MAX_LEN 8 /* current longest format: "x.yz" */

static IMG_CHAR *GetDeviceVersionString(SYS_DATA *psSysData)
{
	int err;
	char str_fpga_rev[FPGA_REV_MAX_LEN]={0};
	char str_tcf_core_rev[TCF_CORE_REV_MAX_LEN]={0};
	char str_tcf_core_target_build_id[TCF_CORE_CFG_MAX_LEN]={0};
	char str_pci_ver[PCI_VERSION_MAX_LEN]={0};
	char str_macro_ver[MACRO_VERSION_MAX_LEN]={0};

	IMG_CHAR *pszVersion;
	IMG_UINT32 ui32StringLength;

	err = tc_sys_strings(psSysData->pdev->dev.parent,
							 str_fpga_rev, sizeof(str_fpga_rev),
							 str_tcf_core_rev, sizeof(str_tcf_core_rev),
							 str_tcf_core_target_build_id, sizeof(str_tcf_core_target_build_id),
							 str_pci_ver, sizeof(str_pci_ver),
							 str_macro_ver, sizeof(str_macro_ver));
	if (err)
	{
		return NULL;
	}

	/* Calculate how much space we need to allocate for the string */
	ui32StringLength = OSStringLength(SYSTEM_INFO_FORMAT_STRING);
	ui32StringLength += OSStringLength(str_fpga_rev);
	ui32StringLength += OSStringLength(str_tcf_core_rev);
	ui32StringLength += OSStringLength(str_tcf_core_target_build_id);
	ui32StringLength += OSStringLength(str_pci_ver);
	ui32StringLength += OSStringLength(str_macro_ver);

	/* Create the version string */
	pszVersion = OSAllocMem(ui32StringLength * sizeof(IMG_CHAR));
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
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: failed to create format string", __func__));
	}

	return pszVersion;
}

#if defined(SUPPORT_ION) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0))
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
		eError = PVRSRV_ERROR_ION_NO_CLIENT;
		goto err_out;
	}
	/* Allocate the whole rogue ion heap and pass that to services to manage */
	psSysData->ion_rogue_allocation = ion_alloc(psSysData->ion_client, psSysData->pdata->rogue_heap_memory_size, 4096, (1 << psSysData->pdata->ion_heap_id), 0);
	if (IS_ERR(psSysData->ion_rogue_allocation))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to allocate ION rogue buffer (%ld)", __func__, PTR_ERR(psSysData->ion_rogue_allocation)));
		eError = PVRSRV_ERROR_ION_FAILED_TO_ALLOC;
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
	SYS_DATA *psSysData = psDevConfig->hSysData;
	IMG_UINT32 ui32Idx;

	for (ui32Idx = 0; ui32Idx < ui32NumOfAddr; ui32Idx++)
	{
		psDevPAddr[ui32Idx].uiAddr =
			psCpuPAddr[ui32Idx].uiAddr - psSysData->pdata->tc_memory_base;
	}
}

static void TCLocalDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
				      IMG_UINT32 ui32NumOfAddr,
				      IMG_CPU_PHYADDR *psCpuPAddr,
				      IMG_DEV_PHYADDR *psDevPAddr)
{
	PVRSRV_DEVICE_CONFIG *psDevConfig = (PVRSRV_DEVICE_CONFIG *)hPrivData;
	SYS_DATA *psSysData = psDevConfig->hSysData;
	IMG_UINT32 ui32Idx;

	for (ui32Idx = 0; ui32Idx < ui32NumOfAddr; ui32Idx++)
	{
		psCpuPAddr[ui32Idx].uiAddr =
			psDevPAddr[ui32Idx].uiAddr + psSysData->pdata->tc_memory_base;
	}
}

static void TCHostCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
									 IMG_UINT32 uiNumOfAddr,
									 IMG_DEV_PHYADDR *psDevPAddr,
									 IMG_CPU_PHYADDR *psCpuPAddr)
{
	PVR_ASSERT(sizeof(*psDevPAddr) == sizeof(*psCpuPAddr));
	OSCachedMemCopy(psDevPAddr, psCpuPAddr, uiNumOfAddr * sizeof(*psDevPAddr));
}

static void TCHostDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
									 IMG_UINT32 uiNumOfAddr,
									 IMG_CPU_PHYADDR *psCpuPAddr,
									 IMG_DEV_PHYADDR *psDevPAddr)
{
	PVR_ASSERT(sizeof(*psCpuPAddr) == sizeof(*psDevPAddr));
	OSCachedMemCopy(psCpuPAddr, psDevPAddr, uiNumOfAddr * sizeof(*psCpuPAddr));
}

static void TCHybridCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
                                       IMG_UINT32 ui32NumOfAddr,
                                       IMG_DEV_PHYADDR *psDevPAddr,
                                       IMG_CPU_PHYADDR *psCpuPAddr)
{
	PVRSRV_DEVICE_CONFIG *psDevConfig = (PVRSRV_DEVICE_CONFIG *)hPrivData;
	SYS_DATA *psSysData = psDevConfig->hSysData;
	IMG_UINT32 ui32Idx;

	for (ui32Idx = 0; ui32Idx < ui32NumOfAddr; ui32Idx++)
	{
		psDevPAddr[ui32Idx].uiAddr =
		    (psCpuPAddr[ui32Idx].uiAddr - psSysData->pdata->tc_memory_base) +
		    ODIN_MEMORY_HYBRID_DEVICE_BASE;
	}
}

static void TCHybridDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
                                       IMG_UINT32 ui32NumOfAddr,
                                       IMG_CPU_PHYADDR *psCpuPAddr,
                                       IMG_DEV_PHYADDR *psDevPAddr)
{
	PVRSRV_DEVICE_CONFIG *psDevConfig = (PVRSRV_DEVICE_CONFIG *)hPrivData;
	SYS_DATA *psSysData = psDevConfig->hSysData;
	IMG_UINT32 ui32Idx;

	for (ui32Idx = 0; ui32Idx < ui32NumOfAddr; ui32Idx++)
	{
		psCpuPAddr[ui32Idx].uiAddr =
		    (psDevPAddr[ui32Idx].uiAddr - ODIN_MEMORY_HYBRID_DEVICE_BASE) +
		    psSysData->pdata->tc_memory_base;
	}
}

static PVRSRV_ERROR
InitLocalHeap(PHYS_HEAP_CONFIG *psPhysHeap,
			  IMG_UINT64 uiBaseAddr, IMG_UINT64 uiStartAddr,
			  IMG_UINT64 uiSize, PHYS_HEAP_FUNCTIONS *psFuncs,
			  PHYS_HEAP_USAGE_FLAGS ui32Flags)
{
	psPhysHeap->sCardBase.uiAddr = uiBaseAddr;
	psPhysHeap->sStartAddr.uiAddr = IMG_CAST_TO_CPUPHYADDR_UINT(uiStartAddr);
	psPhysHeap->uiSize = uiSize;
	psPhysHeap->eType = PHYS_HEAP_TYPE_LMA;
	psPhysHeap->pszPDumpMemspaceName = "LMA";
	psPhysHeap->psMemFuncs = psFuncs;
	psPhysHeap->ui32UsageFlags = ui32Flags;

	return PVRSRV_OK;
}

static PVRSRV_ERROR
CreateCardGPUHeaps(const SYS_DATA *psSysData,
				   PHYS_HEAP_CONFIG *pasPhysHeaps,
				   PHYS_HEAP_FUNCTIONS *psHeapFuncs)
{
	PVRSRV_ERROR eError;
	IMG_UINT64 ui64CardAddr;
	IMG_UINT64 ui64StartAddr = psSysData->pdata->rogue_heap_memory_base;
	IMG_UINT64 ui64OriginalRogueHeapSize = psSysData->pdata->rogue_heap_memory_size;
	IMG_UINT64 ui64RogueHeapSize = 0;
	IMG_UINT64 uiReserved;
#if defined(SUPPORT_SECURITY_VALIDATION)
	IMG_UINT64 uiTDFWCodeSize = SECURE_FW_CODE_MEM_SIZE;
	IMG_UINT64 uiTDFWDataSize = SECURE_FW_DATA_MEM_SIZE;
	IMG_UINT64 uiTDSecBufSize = SECURE_MEM_SIZE;
#else
	IMG_UINT64 uiFwCarveoutSize;

#if defined(SUPPORT_AUTOVZ)
	/* Carveout out enough LMA memory to hold the heaps of
	 * all supported OSIDs and the FW page tables */
	uiFwCarveoutSize = (RGX_NUM_DRIVERS_SUPPORTED * RGX_FIRMWARE_RAW_HEAP_SIZE) +
						RGX_FIRMWARE_MAX_PAGETABLE_SIZE;
#elif defined(RGX_VZ_STATIC_CARVEOUT_FW_HEAPS)
	/* Carveout out enough LMA memory to hold the heaps of all supported OSIDs */
	uiFwCarveoutSize = (RGX_NUM_DRIVERS_SUPPORTED * RGX_FIRMWARE_RAW_HEAP_SIZE);
#else
	/* Create a memory carveout just for the Host's Firmware heap.
	 * Guests will allocate their own physical memory. */
	uiFwCarveoutSize = RGX_FIRMWARE_RAW_HEAP_SIZE;
#endif
#endif /* defined(SUPPORT_SECURITY_VALIDATION) */

	if (psSysData->pdata->baseboard == TC_BASEBOARD_ODIN &&
	    psSysData->pdata->mem_mode == TC_MEMORY_HYBRID)
	{
		ui64CardAddr = ODIN_MEMORY_HYBRID_DEVICE_BASE;
	}
	else
	{
		ui64CardAddr = 0;
	}

#if defined(SUPPORT_SECURITY_VALIDATION)
	uiReserved = uiTDFWCodeSize + uiTDFWDataSize + uiTDSecBufSize;
#else
	uiReserved = uiFwCarveoutSize;
#endif

	ui64RogueHeapSize = SysRestrictGpuLocalPhysheap(ui64OriginalRogueHeapSize);

	if (ui64OriginalRogueHeapSize != ui64RogueHeapSize)
	{
		IMG_UINT32 uiDynamicPrivateArrayPos;
		IMG_UINT64 uiPrivHeapSize = ui64OriginalRogueHeapSize - (ui64RogueHeapSize + uiReserved);

		/* Will always be the last entry in the array */
		uiDynamicPrivateArrayPos =
			psSysData->pdata->mem_mode == TC_MEMORY_HYBRID ?
				PHY_HEAP_HYBRID_NUM : PHY_HEAP_LMA_NUM;

		eError = InitLocalHeap(&pasPhysHeaps[uiDynamicPrivateArrayPos],
							   ui64CardAddr,
							   0,
							   uiPrivHeapSize,
							   psHeapFuncs,
							   PHYS_HEAP_USAGE_GPU_PRIVATE);
		if (eError != PVRSRV_OK)
		{
			return eError;
		}

		ui64CardAddr  += uiPrivHeapSize;
		ui64StartAddr += uiPrivHeapSize;
	}
	else
	{
		ui64RogueHeapSize -= uiReserved;
	}

	eError = InitLocalHeap(&pasPhysHeaps[PHY_HEAP_CARD_GPU],
						   ui64CardAddr,
						   IMG_CAST_TO_CPUPHYADDR_UINT(ui64StartAddr),
						   ui64RogueHeapSize,
						   psHeapFuncs,
						   PHYS_HEAP_USAGE_GPU_LOCAL);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	ui64CardAddr  += ui64RogueHeapSize;
	ui64StartAddr += ui64RogueHeapSize;

#if defined(SUPPORT_SECURITY_VALIDATION)
	/* Setup the secure FW code heap */
	eError = InitLocalHeap(&pasPhysHeaps[PHY_HEAP_SEC_FW_CODE],
						   ui64CardAddr,
						   IMG_CAST_TO_CPUPHYADDR_UINT(ui64StartAddr),
						   uiTDFWCodeSize, psHeapFuncs,
						   PHYS_HEAP_USAGE_FW_CODE);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	ui64CardAddr  += uiTDFWCodeSize;
	ui64StartAddr += uiTDFWCodeSize;

	/* Setup the secure FW data heap */
	eError = InitLocalHeap(&pasPhysHeaps[PHY_HEAP_SEC_FW_DATA],
						   ui64CardAddr,
						   IMG_CAST_TO_CPUPHYADDR_UINT(ui64StartAddr),
						   uiTDFWDataSize, psHeapFuncs,
						   PHYS_HEAP_USAGE_FW_PRIV_DATA);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	ui64CardAddr  += uiTDFWDataSize;
	ui64StartAddr += uiTDFWDataSize;

	/* Setup the secure buffers heap */
	eError = InitLocalHeap(&pasPhysHeaps[PHY_HEAP_SEC_MEM],
						   ui64CardAddr,
						   IMG_CAST_TO_CPUPHYADDR_UINT(ui64StartAddr),
						   uiTDSecBufSize, psHeapFuncs,
						   PHYS_HEAP_USAGE_GPU_SECURE);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	ui64CardAddr  += uiTDSecBufSize;
	ui64StartAddr += uiTDSecBufSize;
#else
	/* allocate the Host Driver's Firmware Heap from the reserved carveout */
	eError = InitLocalHeap(&pasPhysHeaps[PHY_HEAP_CARD_FW],
						   ui64CardAddr,
						   IMG_CAST_TO_CPUPHYADDR_UINT(ui64StartAddr),
						   RGX_FIRMWARE_RAW_HEAP_SIZE,
						   psHeapFuncs,
						   PHYS_HEAP_USAGE_FW_SHARED);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif /* defined(SUPPORT_SECURITY_VALIDATION) */

	return PVRSRV_OK;
}

static PVRSRV_ERROR
CreateCardEXTHeap(const SYS_DATA *psSysData,
				  PHYS_HEAP_CONFIG *pasPhysHeaps,
				  PHYS_HEAP_FUNCTIONS *psHeapFuncs)
{
	IMG_UINT64 ui64CardAddr = 0;
	IMG_UINT64 ui64StartAddr = psSysData->pdata->pdp_heap_memory_base;
	IMG_UINT64 ui64Size = psSysData->pdata->pdp_heap_memory_size;
	PVRSRV_ERROR eError;

	if (psSysData->pdata->baseboard == TC_BASEBOARD_ODIN &&
	    psSysData->pdata->mem_mode == TC_MEMORY_HYBRID)
	{
		ui64CardAddr = ODIN_MEMORY_HYBRID_DEVICE_BASE;
	}
	else
	{
		ui64CardAddr = 0;
	}

	eError = InitLocalHeap(&pasPhysHeaps[PHY_HEAP_CARD_EXT],
						   ui64CardAddr,
						   IMG_CAST_TO_CPUPHYADDR_UINT(ui64StartAddr),
						   ui64Size, psHeapFuncs,
						   PHYS_HEAP_USAGE_EXTERNAL | PHYS_HEAP_USAGE_DISPLAY);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR
InitLocalHeaps(const SYS_DATA *psSysData, PHYS_HEAP_CONFIG *pasPhysHeaps)
{
	PHYS_HEAP_FUNCTIONS *psHeapFuncs;
	PVRSRV_ERROR eError;

	if (psSysData->pdata->baseboard == TC_BASEBOARD_ODIN &&
	    psSysData->pdata->mem_mode == TC_MEMORY_HYBRID)
	{
		psHeapFuncs = &gsHybridPhysHeapFuncs;
	}
	else if (psSysData->pdata->mem_mode == TC_MEMORY_HYBRID)
	{
		psHeapFuncs = &gsHostPhysHeapFuncs;
	}
	else
	{
		psHeapFuncs = &gsLocalPhysHeapFuncs;
	}

	eError = CreateCardGPUHeaps(psSysData, pasPhysHeaps, psHeapFuncs);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	eError = CreateCardEXTHeap(psSysData, pasPhysHeaps, psHeapFuncs);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR
InitHostHeaps(const SYS_DATA *psSysData, PHYS_HEAP_CONFIG *pasPhysHeaps)
{
	PVR_UNREFERENCED_PARAMETER(psSysData);

	pasPhysHeaps[PHY_HEAP_SYSTEM].eType = PHYS_HEAP_TYPE_UMA;
	pasPhysHeaps[PHY_HEAP_SYSTEM].pszPDumpMemspaceName = "SYSMEM";
	pasPhysHeaps[PHY_HEAP_SYSTEM].psMemFuncs = &gsHostPhysHeapFuncs;
	pasPhysHeaps[PHY_HEAP_SYSTEM].ui32UsageFlags = PHYS_HEAP_USAGE_CPU_LOCAL;

	PVR_DPF((PVR_DBG_WARNING,
	         "Initialising CPU_LOCAL UMA Host PhysHeaps with memory mode: %d",
	         psSysData->pdata->mem_mode));

	return PVRSRV_OK;
}

static PVRSRV_ERROR
PhysHeapsInit(const SYS_DATA *psSysData, PHYS_HEAP_CONFIG *pasPhysHeaps,
			  void *pvPrivData, IMG_UINT32 ui32NumHeaps)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 i;

	eError = InitLocalHeaps(psSysData, pasPhysHeaps);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	if (psSysData->pdata->mem_mode == TC_MEMORY_HYBRID)
	{
		eError = InitHostHeaps(psSysData, pasPhysHeaps);
		if (eError != PVRSRV_OK)
		{
			return eError;
		}
	}

	/* Initialise fields that don't change between memory modes.
	 * Fix up heap IDs. This is needed for multi-testchip systems to
	 * ensure the heap IDs are unique as this is what Services expects.
	 */
	for (i = 0; i < ui32NumHeaps; i++)
	{
		pasPhysHeaps[i].hPrivData = pvPrivData;
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR
PhysHeapsCreate(const SYS_DATA *psSysData, PVRSRV_DEVICE_CONFIG *psDevConfig,
				PHYS_HEAP_CONFIG **ppasPhysHeapsOut,
				IMG_UINT32 *puiPhysHeapCountOut)
{
	PHYS_HEAP_CONFIG *pasPhysHeaps;
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32NumHeaps = psSysData->pdata->mem_mode == TC_MEMORY_HYBRID ?
		PHY_HEAP_HYBRID_NUM : PHY_HEAP_LMA_NUM;

	if (SysRestrictGpuLocalAddPrivateHeap())
	{
		ui32NumHeaps++;
		psDevConfig->bHasNonMappableLocalMemory = IMG_TRUE;
	}
	else
	{
		psDevConfig->bHasNonMappableLocalMemory = IMG_FALSE;
	}

	pasPhysHeaps = OSAllocMem(sizeof(*pasPhysHeaps) * ui32NumHeaps);
	if (!pasPhysHeaps)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	eError = PhysHeapsInit(psSysData, pasPhysHeaps, psDevConfig, ui32NumHeaps);
	if (eError != PVRSRV_OK)
	{
		OSFreeMem(pasPhysHeaps);
		return eError;
	}

	*ppasPhysHeapsOut = pasPhysHeaps;
	*puiPhysHeapCountOut = ui32NumHeaps;

	return PVRSRV_OK;
}

static void DeviceConfigDestroy(PVRSRV_DEVICE_CONFIG *psDevConfig)
{
	if (psDevConfig->pszVersion)
	{
		OSFreeMem(psDevConfig->pszVersion);
	}

	OSFreeMem(psDevConfig->pasPhysHeaps);

	OSFreeMem(psDevConfig);
}

static void odinTCDevPhysAddr2DmaAddr(PVRSRV_DEVICE_CONFIG *psDevConfig,
									  IMG_DMA_ADDR *psDmaAddr,
									  IMG_DEV_PHYADDR *psDevPAddr,
									  IMG_BOOL *pbValid,
									  IMG_UINT32 ui32NumAddr,
									  IMG_BOOL bSparseAlloc)
{
	IMG_CPU_PHYADDR sCpuPAddr = {0};
	IMG_UINT32 ui32Idx;

	/* Fast path */
	if (!bSparseAlloc)
	{
		/* In Odin, DMA address space is the same as host CPU */
		TCLocalDevPAddrToCpuPAddr(psDevConfig,
								  1,
								  &sCpuPAddr,
								  psDevPAddr);
		psDmaAddr->uiAddr = sCpuPAddr.uiAddr;
	}
	else
	{
		for (ui32Idx = 0; ui32Idx < ui32NumAddr; ui32Idx++)
		{
			if (pbValid[ui32Idx])
			{
				TCLocalDevPAddrToCpuPAddr(psDevConfig,
										  1,
										  &sCpuPAddr,
										  &psDevPAddr[ui32Idx]);
				psDmaAddr[ui32Idx].uiAddr = sCpuPAddr.uiAddr;
			}
			else
			{
				/* Invalid DMA address marker */
				psDmaAddr[ui32Idx].uiAddr = ~((IMG_UINT64)0x0);
			}
		}
	}
}

static void * odinTCgetCDMAChan(PVRSRV_DEVICE_CONFIG *psDevConfig, char *name)
{
	struct device* psDev = (struct device*) psDevConfig->pvOSDevice;
	return tc_dma_chan(psDev->parent, name);
}

static void odinTCFreeCDMAChan(PVRSRV_DEVICE_CONFIG *psDevConfig,
							   void* channel)
{

	struct device* psDev = (struct device*) psDevConfig->pvOSDevice;
	struct dma_chan *chan = (struct dma_chan*) channel;

	tc_dma_chan_free(psDev->parent, chan);
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

	psRGXData = IMG_OFFSET_ADDR(psDevConfig, sizeof(*psDevConfig));
	psRGXTimingInfo = IMG_OFFSET_ADDR(psRGXData, sizeof(*psRGXData));

	eError = PhysHeapsCreate(psSysData, psDevConfig, &pasPhysHeaps, &uiPhysHeapCount);
	if (eError != PVRSRV_OK)
	{
		goto ErrorFreeDevConfig;
	}

	/* Setup RGX specific timing data */
	psRGXTimingInfo->ui32CoreClockSpeed = tc_core_clock_speed(&psSysData->pdev->dev) /
											tc_core_clock_multiplex(&psSysData->pdev->dev);
	psRGXTimingInfo->bEnableActivePM = IMG_FALSE;
	psRGXTimingInfo->bEnableRDPowIsland = IMG_FALSE;
	psRGXTimingInfo->ui32ActivePMLatencyms = SYS_RGX_ACTIVE_POWER_LATENCY_MS;

	/* Set up the RGX data */
	psRGXData->psRGXTimingInfo = psRGXTimingInfo;

	/* Setup the device config */
	psDevConfig->pvOSDevice = &psSysData->pdev->dev;
	psDevConfig->pszName = "tc";
	psDevConfig->pszVersion = GetDeviceVersionString(psSysData);

	psDevConfig->sRegsCpuPBase.uiAddr = psSysData->registers->start;
	psDevConfig->ui32RegsSize = resource_size(psSysData->registers);

	psDevConfig->ui32IRQ = TC_INTERRUPT_EXT;

	psDevConfig->pasPhysHeaps = pasPhysHeaps;
	psDevConfig->ui32PhysHeapCount = uiPhysHeapCount;

	if (psSysData->pdata->baseboard == TC_BASEBOARD_ODIN &&
	    psSysData->pdata->mem_mode == TC_MEMORY_HYBRID)
	{
		psDevConfig->eDefaultHeap = PVRSRV_PHYS_HEAP_CPU_LOCAL;
	}
	else
	{
		psDevConfig->eDefaultHeap = PVRSRV_PHYS_HEAP_GPU_LOCAL;
	}

	/* Only required for LMA but having this always set shouldn't be a problem */
	psDevConfig->bDevicePA0IsValid = IMG_TRUE;

	psDevConfig->hDevData = psRGXData;
	psDevConfig->hSysData = psSysData;

	psDevConfig->pfnSysDevFeatureDepInit = NULL;

#if defined(SUPPORT_LINUX_DVFS) || defined(SUPPORT_PDVFS)
	/* Fake DVFS configuration used purely for testing purposes */
	psDevConfig->sDVFS.sDVFSDeviceCfg.pasOPPTable = asOPPTable;
	psDevConfig->sDVFS.sDVFSDeviceCfg.ui32OPPTableSize = LEVEL_COUNT;
	psDevConfig->sDVFS.sDVFSDeviceCfg.pfnSetFrequency = SetFrequency;
	psDevConfig->sDVFS.sDVFSDeviceCfg.pfnSetVoltage = SetVoltage;
#endif
#if defined(SUPPORT_LINUX_DVFS)
	psDevConfig->sDVFS.sDVFSDeviceCfg.ui32PollMs = 1000;
	psDevConfig->sDVFS.sDVFSDeviceCfg.bIdleReq = IMG_TRUE;
	psDevConfig->sDVFS.sDVFSGovernorCfg.ui32UpThreshold = 90;
	psDevConfig->sDVFS.sDVFSGovernorCfg.ui32DownDifferential = 10;
#endif

	psDevConfig->bHasFBCDCVersion31 = IMG_FALSE;

	/* DMA channel config */
	psDevConfig->pfnSlaveDMAGetChan = odinTCgetCDMAChan;
	psDevConfig->pfnSlaveDMAFreeChan = odinTCFreeCDMAChan;
	psDevConfig->pfnDevPhysAddr2DmaAddr = odinTCDevPhysAddr2DmaAddr;
	psDevConfig->pszDmaTxChanName = psSysData->pdata->tc_dma_tx_chan_name;
	psDevConfig->pszDmaRxChanName = psSysData->pdata->tc_dma_rx_chan_name;
	psDevConfig->bHasDma = true;
	/* Following two values are expressed in number of bytes */
	psDevConfig->ui32DmaTransferUnit = 1;
	psDevConfig->ui32DmaAlignment = 1;

	*ppsDevConfigOut = psDevConfig;

	return PVRSRV_OK;

ErrorFreeDevConfig:
	OSFreeMem(psDevConfig);
	return eError;
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

	/*
	 * The device cannot address system memory, so there is no DMA
	 * limitation.
	 */
	if (psSysData->pdata->mem_mode == TC_MEMORY_LOCAL)
	{
		dma_set_mask(pvOSDevice, DMA_BIT_MASK(64));
	}
	else
	{
		dma_set_mask(pvOSDevice, DMA_BIT_MASK(32));
	}

	err = tc_enable(psSysData->pdev->dev.parent);
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
				 __func__, &uiRegistersSize, SYS_RGX_REG_REGION_SIZE));

		eError = PVRSRV_ERROR_PCI_REGION_TOO_SMALL;
		goto ErrorDevDisable;
	}

	/* Reserve the address range */
	if (!request_mem_region(psSysData->registers->start,
							resource_size(psSysData->registers),
							SYS_RGX_DEV_NAME))
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: Rogue register memory region not available",
				 __func__));
		eError = PVRSRV_ERROR_PCI_CALL_FAILED;

		goto ErrorDevDisable;
	}

    /*
     * Reset the device as required.
     */
    eError = DevReset(psSysData, IMG_TRUE);
    if (eError != PVRSRV_OK)
    {
        PVR_DPF((PVR_DBG_ERROR, "%s: Couldn't reset device", __func__));
        goto ErrorDevDisable;
    }

	eError = DeviceConfigCreate(psSysData, &psDevConfig);
	if (eError != PVRSRV_OK)
	{
		goto ErrorReleaseMemRegion;
	}

#if defined(SUPPORT_ION) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0))
	eError = IonInit(psSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to initialise ION", __func__));
		goto ErrorDeviceConfigDestroy;
	}
#endif

	/* Set psDevConfig->pfnSysDevErrorNotify callback */
	psDevConfig->pfnSysDevErrorNotify = SysRGXErrorNotify;

	*ppsDevConfig = psDevConfig;

	return PVRSRV_OK;

#if defined(SUPPORT_ION) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0))
ErrorDeviceConfigDestroy:
	DeviceConfigDestroy(psDevConfig);
#endif
ErrorReleaseMemRegion:
	release_mem_region(psSysData->registers->start,
					   resource_size(psSysData->registers));
ErrorDevDisable:
	tc_disable(psSysData->pdev->dev.parent);
ErrFreeSysData:
	OSFreeMem(psSysData);
	return eError;
}

void SysDevDeInit(PVRSRV_DEVICE_CONFIG *psDevConfig)
{
	SYS_DATA *psSysData = (SYS_DATA *)psDevConfig->hSysData;

#if defined(SUPPORT_ION) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0))
	IonDeinit();
#endif

	DeviceConfigDestroy(psDevConfig);

	release_mem_region(psSysData->registers->start,
					   resource_size(psSysData->registers));
	tc_disable(psSysData->pdev->dev.parent);

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

	if (tc_sys_info(psSysData->pdev->dev.parent, &tmp, &pll))
		goto err_out;

	if (tmp > 0)
		PVR_DUMPDEBUG_LOG("Chip temperature: %d degrees C", tmp);
	PVR_DUMPDEBUG_LOG("PLL status: %x", pll);

    eError = FPGA_SysDebugInfo(psSysData->registers,
                               pfnDumpDebugPrintf,
                               pvDumpDebugFile);
    if (eError != PVRSRV_OK)
    {
        PVR_DPF((PVR_DBG_ERROR, "%s: Couldn't dump registers from device", __func__));
        goto err_out;
    }

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

static void TCInterruptHandler(void* pvData)
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

	if (ui32IRQ != TC_INTERRUPT_EXT)
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

	err = tc_set_interrupt_handler(psLISRData->psDev, psLISRData->iInterruptID, TCInterruptHandler, psLISRData);
	if (err)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: tc_set_interrupt_handler() failed (%d)", __func__, err));
		eError = PVRSRV_ERROR_UNABLE_TO_INSTALL_ISR;
		goto err_free_data;
	}

	err = tc_enable_interrupt(psLISRData->psDev, psLISRData->iInterruptID);
	if (err)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: tc_enable_interrupt() failed (%d)", __func__, err));
		eError = PVRSRV_ERROR_UNABLE_TO_INSTALL_ISR;
		goto err_unset_interrupt_handler;
	}

	*phLISRData = psLISRData;
	eError = PVRSRV_OK;

	PVR_TRACE(("Installed device LISR " IMG_PFN_FMTSPEC " to irq %u", pfnLISR, ui32IRQ));

err_out:
	return eError;
err_unset_interrupt_handler:
	tc_set_interrupt_handler(psLISRData->psDev, psLISRData->iInterruptID, NULL, NULL);
err_free_data:
	OSFreeMem(psLISRData);
	goto err_out;
}

PVRSRV_ERROR SysUninstallDeviceLISR(IMG_HANDLE hLISRData)
{
	LISR_DATA *psLISRData = (LISR_DATA *) hLISRData;
	int err;

	err = tc_disable_interrupt(psLISRData->psDev, psLISRData->iInterruptID);
	if (err)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: tc_disable_interrupt() failed (%d)", __func__, err));
	}

	err = tc_set_interrupt_handler(psLISRData->psDev, psLISRData->iInterruptID, NULL, NULL);
	if (err)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: tc_set_interrupt_handler() failed (%d)", __func__, err));
	}

	PVR_TRACE(("Uninstalled device LISR " IMG_PFN_FMTSPEC " from irq %u", psLISRData->pfnLISR, psLISRData->iInterruptID));

	OSFreeMem(psLISRData);

	return PVRSRV_OK;
}
