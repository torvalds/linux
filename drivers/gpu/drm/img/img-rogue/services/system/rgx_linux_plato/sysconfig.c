/*************************************************************************/ /*!
@File
@Title			System Configuration
@Copyright		Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description	System Configuration functions
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

#include "pvr_debug.h"
#include "osfunc.h"
#include "allocmem.h"
#include "pvrsrv_device.h"
#include "pvrsrv_memallocflags.h"
#include "syscommon.h"
#include "power.h"
#include "sysinfo.h"
#include "sysconfig.h"
#include "physheap.h"
#include "pci_support.h"
#include "interrupt_support.h"
#include "plato_drv.h"
#include <linux/dma-mapping.h>

#define PLATO_HAS_NON_MAPPABLE(sys) (sys->pdata->has_nonmappable == true)

#if (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HOST)
#define PHYS_HEAP_ID_CPU_LOCAL 0
#elif (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HYBRID)
#define PHYS_HEAP_ID_GPU_LOCAL 0
#define PHYS_HEAP_ID_CPU_LOCAL 1
#if defined(SUPPORT_PLATO_DISPLAY)
#define PHYS_HEAP_ID_PDP_LOCAL 2
#define PHYS_HEAP_ID_NON_MAPPABLE 3
#else
#define PHYS_HEAP_ID_NON_MAPPABLE 2
#endif
#elif (PLATO_MEMORY_CONFIG == PLATO_MEMORY_LOCAL)
#define PHYS_HEAP_ID_GPU_LOCAL 0
#if defined(SUPPORT_PLATO_DISPLAY)
#define PHYS_HEAP_ID_PDP_LOCAL 1
#define PHYS_HEAP_ID_NON_MAPPABLE 2
#else
#define PHYS_HEAP_ID_NON_MAPPABLE 1
#endif
#endif

#if defined(SUPPORT_PLATO_DISPLAY)
static_assert(PHYS_HEAP_ID_PDP_LOCAL == PVRSRV_PHYS_HEAP_CONFIG_PDP_LOCAL_ID, "PDP heap ID mismatch.");
#endif

static struct plato_debug_register plato_noc_regs[] = {
	{"NOC Offset 0x00", 0x00, 0},
	{"NOC Offset 0x04", 0x04, 0},
	{"NOC Offset 0x08", 0x08, 0},
	{"NOC Offset 0x0C", 0x0C, 0},
	{"NOC Offset 0x10", 0x10, 0},
	{"NOC Offset 0x14", 0x14, 0},
	{"NOC Offset 0x18", 0x18, 0},
	{"NOC Offset 0x1C", 0x1C, 0},
	{"NOC Offset 0x50", 0x50, 0},
	{"NOC Offset 0x54", 0x54, 0},
	{"NOC Offset 0x58", 0x58, 0},
	{"DDR A Ctrl", SYS_PLATO_REG_NOC_DBG_DDR_A_CTRL_OFFSET, 0},
	{"DDR A Data", SYS_PLATO_REG_NOC_DBG_DDR_A_DATA_OFFSET, 0},
	{"DDR A Publ", SYS_PLATO_REG_NOC_DBG_DDR_A_PUBL_OFFSET, 0},
	{"DDR B Ctrl", SYS_PLATO_REG_NOC_DBG_DDR_B_CTRL_OFFSET, 0},
	{"DDR B Data", SYS_PLATO_REG_NOC_DBG_DDR_B_DATA_OFFSET, 0},
	{"DDR B Publ", SYS_PLATO_REG_NOC_DBG_DDR_B_PUBL_OFFSET, 0},
	{"Display S", SYS_PLATO_REG_NOC_DBG_DISPLAY_S_OFFSET, 0},
	{"GPIO 0 S", SYS_PLATO_REG_NOC_DBG_GPIO_0_S_OFFSET, 0},
	{"GPIO 1 S", SYS_PLATO_REG_NOC_DBG_GPIO_1_S_OFFSET, 0},
	{"GPU S", SYS_PLATO_REG_NOC_DBG_GPU_S_OFFSET, 0},
	{"PCI PHY", SYS_PLATO_REG_NOC_DBG_PCI_PHY_OFFSET, 0},
	{"PCI Reg", SYS_PLATO_REG_NOC_DBG_PCI_REG_OFFSET, 0},
	{"PCI S", SYS_PLATO_REG_NOC_DBG_PCI_S_OFFSET, 0},
	{"Periph S", SYS_PLATO_REG_NOC_DBG_PERIPH_S_OFFSET, 0},
	{"Ret Reg", SYS_PLATO_REG_NOC_DBG_RET_REG_OFFSET, 0},
	{"Service", SYS_PLATO_REG_NOC_DBG_SERVICE_OFFSET, 0},
};

static struct plato_debug_register plato_aon_regs[] = {
	{"AON Offset 0x0000", 0x0000, 0},
	{"AON Offset 0x0070", 0x0070, 0},
};

typedef struct _SYS_DATA_ {
	struct platform_device *pdev;
	struct resource *registers;
	struct plato_rogue_platform_data *pdata;
} SYS_DATA;

typedef struct {
	struct device *psDev;
	int iInterruptID;
	void *pvData;
	PFN_LISR pfnLISR;
} LISR_DATA;

static IMG_CHAR *GetDeviceVersionString(SYS_DATA *psSysData)
{
	return NULL;
}


PVRSRV_ERROR SysDebugInfo(PVRSRV_DEVICE_CONFIG *psDevConfig,
				DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile)
{
	SYS_DATA *psSysData = psDevConfig->hSysData;
	IMG_UINT32 i = 0;

	PVR_DUMPDEBUG_LOG("------[ Plato System Debug ]------");

	if (plato_debug_info(psSysData->pdev->dev.parent, &plato_noc_regs[0], &plato_aon_regs[0]))
		return PVRSRV_ERROR_INVALID_PARAMS;

	for (i = 0; i < ARRAY_SIZE(plato_noc_regs); i++)
		PVR_DUMPDEBUG_LOG("%s: 0x%x", plato_noc_regs[i].description, plato_noc_regs[i].value);

	for (i = 0; i < ARRAY_SIZE(plato_aon_regs); i++)
		PVR_DUMPDEBUG_LOG("%s: 0x%x", plato_aon_regs[i].description, plato_aon_regs[i].value);

	return PVRSRV_OK;
}


#if (PLATO_MEMORY_CONFIG == PLATO_MEMORY_LOCAL) || (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HYBRID)

static PVRSRV_ERROR InitLocalHeaps(SYS_DATA *psSysData,
									PHYS_HEAP_CONFIG *pasPhysHeaps,
									IMG_UINT32 uiPhysHeapCount,
									IMG_HANDLE hPhysHeapPrivData)
{
	PHYS_HEAP_CONFIG *psPhysHeap;

	psPhysHeap = &pasPhysHeaps[PHYS_HEAP_ID_GPU_LOCAL];
	psPhysHeap->eType = PHYS_HEAP_TYPE_LMA;
	psPhysHeap->pszPDumpMemspaceName = "LMA";
	psPhysHeap->psMemFuncs = &gsLocalPhysHeapFuncs;
	psPhysHeap->hPrivData = hPhysHeapPrivData;
	psPhysHeap->ui32UsageFlags = PHYS_HEAP_USAGE_GPU_LOCAL;

	/* Configure mappable heap region */
	psPhysHeap->sStartAddr.uiAddr = psSysData->pdata->rogue_heap_mappable.base;
	psPhysHeap->sCardBase.uiAddr = psSysData->pdata->rogue_heap_dev_addr;
	psPhysHeap->uiSize = psSysData->pdata->rogue_heap_mappable.size;

	PVR_LOG(("Added mappable local memory heap. Base = 0x%016llx, Size=0x%016llx",
			psPhysHeap->sCardBase.uiAddr,
			psPhysHeap->uiSize));

	/* Setup non-mappable region if BAR size is less than actual memory size (8GB) */
	if (PLATO_HAS_NON_MAPPABLE(psSysData))
	{
		psPhysHeap = &pasPhysHeaps[PHYS_HEAP_ID_NON_MAPPABLE];
		psPhysHeap->eType = PHYS_HEAP_TYPE_LMA;
		psPhysHeap->pszPDumpMemspaceName = "LMA";
		psPhysHeap->psMemFuncs = &gsLocalPhysHeapFuncs;
		psPhysHeap->hPrivData = hPhysHeapPrivData;
		psPhysHeap->ui32UsageFlags = PHYS_HEAP_USAGE_GPU_PRIVATE;

		psPhysHeap->sCardBase.uiAddr = psSysData->pdata->rogue_heap_nonmappable.base;
		psPhysHeap->uiSize = psSysData->pdata->rogue_heap_nonmappable.size;
		psPhysHeap->sStartAddr.uiAddr = 0;

		PVR_LOG(("Added non-mappable local memory heap. Base = 0x%016llx, Size=0x%016llx",
					psPhysHeap->sCardBase.uiAddr,
					psPhysHeap->uiSize));

		PVR_ASSERT(psPhysHeap->uiSize < SYS_DEV_MEM_REGION_SIZE);
	}

#if defined(SUPPORT_PLATO_DISPLAY)
	psPhysHeap = &pasPhysHeaps[PHYS_HEAP_ID_PDP_LOCAL];
	psPhysHeap->eType = PHYS_HEAP_TYPE_LMA;
	psPhysHeap->pszPDumpMemspaceName = "LMA";
	psPhysHeap->psMemFuncs = &gsLocalPhysHeapFuncs;
	psPhysHeap->hPrivData = hPhysHeapPrivData;
	psPhysHeap->ui32UsageFlags = PHYS_HEAP_USAGE_EXTERNAL;

	psPhysHeap->sCardBase.uiAddr = PLATO_DDR_DEV_PHYSICAL_BASE;
	psPhysHeap->sStartAddr.uiAddr = psSysData->pdata->pdp_heap.base;
	psPhysHeap->uiSize = psSysData->pdata->pdp_heap.size;

	PVR_LOG(("Added PDP heap. Base = 0x%016llx, Size=0x%016llx",
			psPhysHeap->sStartAddr.uiAddr,
			psPhysHeap->uiSize));
#endif

	return PVRSRV_OK;
}
#endif /* (PLATO_MEMORY_CONFIG == PLATO_MEMORY_LOCAL) || (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HYBRID) */

#if (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HOST) || (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HYBRID)
static PVRSRV_ERROR InitHostHeaps(SYS_DATA *psSysData,
								PHYS_HEAP_CONFIG *pasPhysHeaps,
								IMG_UINT32 uiPhysHeapCount,
								IMG_HANDLE hPhysHeapPrivData)
{
	PHYS_HEAP_CONFIG *psPhysHeap;

	PVR_ASSERT(uiPhysHeapCount == 1);

	psPhysHeap = &pasPhysHeaps[0];
	psPhysHeap->eType = PHYS_HEAP_TYPE_UMA;
	psPhysHeap->pszPDumpMemspaceName = "SYSMEM";
	psPhysHeap->psMemFuncs = &gsHostPhysHeapFuncs;
	psPhysHeap->hPrivData = hPhysHeapPrivData;
#if (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HOST)
	psPhysHeap->ui32UsageFlags = PHYS_HEAP_USAGE_GPU_LOCAL;
#elif (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HYBRID)
	psPhysHeap->ui32UsageFlags = PHYS_HEAP_USAGE_CPU_LOCAL;
	PVR_DPF((PVR_DBG_WARNING, "Initialising CPU_LOCAL UMA Host PhysHeaps"));
#if !defined(SUPPORT_PLATO_DISPLAY)
	psPhysHeap->ui32UsageFlags |= PHYS_HEAP_USAGE_EXTERNAL;
#endif
#endif
	psPhysHeap->sCardBase.uiAddr = PLATO_HOSTRAM_DEV_PHYSICAL_BASE;

	return PVRSRV_OK;
}
#endif /* (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HOST) */

#if (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HYBRID)
static PVRSRV_ERROR InitHybridHeaps(SYS_DATA *psSysData,
									PHYS_HEAP_CONFIG *pasPhysHeaps,
									IMG_UINT32 uiPhysHeapCount,
									IMG_HANDLE hPhysHeapPrivData)
{
	PVRSRV_ERROR eError;

	PVR_ASSERT(uiPhysHeapCount >= PHYS_HEAP_ID_NON_MAPPABLE);

	eError = InitHostHeaps(psSysData,
							&pasPhysHeaps[PHYS_HEAP_ID_CPU_LOCAL], 1,
							hPhysHeapPrivData);
	if (eError != PVRSRV_OK)
		return eError;

	/*
	 * InitLocalHeaps should set up the correct heaps regardless of whether the
	 * memory configuration is 'local' or 'hybrid'.
	 */
	eError = InitLocalHeaps(psSysData, pasPhysHeaps,
							 uiPhysHeapCount, hPhysHeapPrivData);
	if (eError != PVRSRV_OK) {
		return eError;
	}

	/* Adjust the pdump memory space names */
	pasPhysHeaps[PHYS_HEAP_ID_GPU_LOCAL].pszPDumpMemspaceName = "LMA0";
#if defined(SUPPORT_PLATO_DISPLAY)
	pasPhysHeaps[PHYS_HEAP_ID_PDP_LOCAL].pszPDumpMemspaceName = "LMA1";
#endif
	return PVRSRV_OK;
}
#endif /* (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HYBRID) */

static PVRSRV_ERROR PhysHeapsCreate(SYS_DATA *psSysData,
									PVRSRV_DEVICE_CONFIG *psDevConfig,
									PHYS_HEAP_CONFIG **ppasPhysHeapsOut,
									IMG_UINT32 *puiPhysHeapCountOut)
{
	IMG_UINT32 uiHeapCount = 1;
	PHYS_HEAP_CONFIG *pasPhysHeaps;
	PVRSRV_ERROR eError;

#if (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HYBRID)
	uiHeapCount++;
#endif

#if defined(SUPPORT_PLATO_DISPLAY)
	uiHeapCount++;
#endif

	if (PLATO_HAS_NON_MAPPABLE(psSysData))
	{
		uiHeapCount++;
	}

	pasPhysHeaps = OSAllocZMem(sizeof(*pasPhysHeaps) * uiHeapCount);
	if (!pasPhysHeaps)
		return PVRSRV_ERROR_OUT_OF_MEMORY;

#if (PLATO_MEMORY_CONFIG == PLATO_MEMORY_LOCAL)
	eError = InitLocalHeaps(psSysData, pasPhysHeaps,
							uiHeapCount, psDevConfig);
#elif (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HOST)
	eError = InitHostHeaps(psSysData, pasPhysHeaps,
							uiHeapCount, psDevConfig);
#elif (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HYBRID)
	eError = InitHybridHeaps(psSysData, pasPhysHeaps,
							uiHeapCount, psDevConfig);
#endif

	if (eError != PVRSRV_OK) {
		OSFreeMem(pasPhysHeaps);
		return eError;
	}

	*ppasPhysHeapsOut = pasPhysHeaps;
	*puiPhysHeapCountOut = uiHeapCount;

	return PVRSRV_OK;
}

#if (PLATO_MEMORY_CONFIG == PLATO_MEMORY_LOCAL) || (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HYBRID)
static void PlatoLocalCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
					  IMG_UINT32 ui32NumOfAddr,
					  IMG_DEV_PHYADDR *psDevPAddr,
					  IMG_CPU_PHYADDR *psCpuPAddr)
{
	PVRSRV_DEVICE_CONFIG *psDevConfig = (PVRSRV_DEVICE_CONFIG *)hPrivData;

	/* Optimise common case */
	psDevPAddr[0].uiAddr = psCpuPAddr[0].uiAddr -
		psDevConfig->pasPhysHeaps[PHYS_HEAP_ID_GPU_LOCAL].sStartAddr.uiAddr +
		psDevConfig->pasPhysHeaps[PHYS_HEAP_ID_GPU_LOCAL].sCardBase.uiAddr;

	if (ui32NumOfAddr > 1) {
		IMG_UINT32 ui32Idx;

		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx) {
			psDevPAddr[ui32Idx].uiAddr = psCpuPAddr[ui32Idx].uiAddr -
				psDevConfig->pasPhysHeaps[PHYS_HEAP_ID_GPU_LOCAL].sStartAddr.uiAddr +
				psDevConfig->pasPhysHeaps[PHYS_HEAP_ID_GPU_LOCAL].sCardBase.uiAddr;
		}
	}
}

static void PlatoLocalDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
					IMG_UINT32 ui32NumOfAddr,
					IMG_CPU_PHYADDR *psCpuPAddr,
					IMG_DEV_PHYADDR *psDevPAddr)
{
	PVRSRV_DEVICE_CONFIG *psDevConfig = (PVRSRV_DEVICE_CONFIG *)hPrivData;

	/* Optimise common case */
	psCpuPAddr[0].uiAddr = psDevPAddr[0].uiAddr -
		psDevConfig->pasPhysHeaps[PHYS_HEAP_ID_GPU_LOCAL].sCardBase.uiAddr +
		psDevConfig->pasPhysHeaps[PHYS_HEAP_ID_GPU_LOCAL].sStartAddr.uiAddr;

	if (ui32NumOfAddr > 1) {
		IMG_UINT32 ui32Idx;

		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx) {
			psCpuPAddr[ui32Idx].uiAddr = psDevPAddr[ui32Idx].uiAddr -
				psDevConfig->pasPhysHeaps[PHYS_HEAP_ID_GPU_LOCAL].sCardBase.uiAddr +
				psDevConfig->pasPhysHeaps[PHYS_HEAP_ID_GPU_LOCAL].sStartAddr.uiAddr;
		}
	}
}

#endif

#if (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HOST) || (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HYBRID)
static void PlatoSystemCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
					   IMG_UINT32 ui32NumOfAddr,
					   IMG_DEV_PHYADDR *psDevPAddr,
					   IMG_CPU_PHYADDR *psCpuPAddr)
{
	PVR_UNREFERENCED_PARAMETER(hPrivData);

	/* Optimise common case */
	psDevPAddr[0].uiAddr = psCpuPAddr[0].uiAddr + PLATO_HOSTRAM_DEV_PHYSICAL_BASE;
	if (ui32NumOfAddr > 1) {
		IMG_UINT32 ui32Idx;

		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
			psDevPAddr[ui32Idx].uiAddr = psCpuPAddr[ui32Idx].uiAddr + PLATO_HOSTRAM_DEV_PHYSICAL_BASE;
	}
}

static void PlatoSystemDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
					   IMG_UINT32 ui32NumOfAddr,
					   IMG_CPU_PHYADDR *psCpuPAddr,
					   IMG_DEV_PHYADDR *psDevPAddr)
{
	PVR_UNREFERENCED_PARAMETER(hPrivData);

	/* Optimise common case */
	psCpuPAddr[0].uiAddr = IMG_CAST_TO_CPUPHYADDR_UINT(psDevPAddr[0].uiAddr - PLATO_HOSTRAM_DEV_PHYSICAL_BASE);
	if (ui32NumOfAddr > 1) {
		IMG_UINT32 ui32Idx;

		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
			psCpuPAddr[ui32Idx].uiAddr = IMG_CAST_TO_CPUPHYADDR_UINT(psDevPAddr[ui32Idx].uiAddr - PLATO_HOSTRAM_DEV_PHYSICAL_BASE);
	}
}

#endif /* (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HOST) || (PLATO_MEMORY_CONFIG == PLATO_MEMORY_HYBRID) */

static PVRSRV_ERROR DeviceConfigCreate(void *pvOSDevice,
									   SYS_DATA *psSysData,
									   PVRSRV_DEVICE_CONFIG **ppsDevConfigOut)
{
	PVRSRV_DEVICE_CONFIG *psDevConfig;
	RGX_DATA *psRGXData;
	RGX_TIMING_INFORMATION *psRGXTimingInfo;
	PVRSRV_ERROR eError;

	psDevConfig = OSAllocZMem(sizeof(*psDevConfig) +
							  sizeof(*psRGXData) +
							  sizeof(*psRGXTimingInfo));
	if (!psDevConfig)
		return PVRSRV_ERROR_OUT_OF_MEMORY;

	psRGXData = (RGX_DATA *)((IMG_CHAR *)psDevConfig + sizeof(*psDevConfig));
	psRGXTimingInfo = (RGX_TIMING_INFORMATION *)((IMG_CHAR *)psRGXData + sizeof(*psRGXData));

	/* Set up the RGX timing information */
	psRGXTimingInfo->ui32CoreClockSpeed = plato_core_clock_speed(&psSysData->pdev->dev);
	psRGXTimingInfo->bEnableActivePM = IMG_FALSE;
	psRGXTimingInfo->bEnableRDPowIsland = IMG_FALSE;
	psRGXTimingInfo->ui32ActivePMLatencyms = SYS_RGX_ACTIVE_POWER_LATENCY_MS;

	/* Set up the RGX data */
	psRGXData->psRGXTimingInfo = psRGXTimingInfo;

	/* Initialize heaps */
	eError = PhysHeapsCreate(psSysData, psDevConfig, &psDevConfig->pasPhysHeaps,
						&psDevConfig->ui32PhysHeapCount);
	if (eError != PVRSRV_OK) {
		OSFreeMem(psDevConfig);
		return eError;
	}

	psDevConfig->pvOSDevice = pvOSDevice;
	psDevConfig->pszName = PLATO_SYSTEM_NAME;
	psDevConfig->pszVersion = GetDeviceVersionString(psSysData);

	psDevConfig->sRegsCpuPBase.uiAddr = psSysData->registers->start;
	psDevConfig->ui32RegsSize = SYS_PLATO_REG_RGX_SIZE;
	psDevConfig->eDefaultHeap = PVRSRV_PHYS_HEAP_GPU_LOCAL;

	psDevConfig->eCacheSnoopingMode = PVRSRV_DEVICE_SNOOP_NONE;
	psDevConfig->bHasNonMappableLocalMemory = PLATO_HAS_NON_MAPPABLE(psSysData);
	psDevConfig->bHasFBCDCVersion31 = IMG_FALSE;

	psDevConfig->ui32IRQ = PLATO_INTERRUPT_GPU;

	psDevConfig->hDevData = psRGXData;
	psDevConfig->hSysData = psSysData;

	*ppsDevConfigOut = psDevConfig;

	return PVRSRV_OK;
}

static void DeviceConfigDestroy(PVRSRV_DEVICE_CONFIG *psDevConfig)
{
	if (psDevConfig->pszVersion)
		OSFreeMem(psDevConfig->pszVersion);

	if (psDevConfig->pasPhysHeaps)
		OSFreeMem(psDevConfig->pasPhysHeaps);

	/*
	 * The device config, RGX data and RGX timing info are part of the same
	 * allocation so do only one free.
	 */
	OSFreeMem(psDevConfig);
}

static PVRSRV_ERROR PlatoLocalMemoryTest(PVRSRV_DEVICE_CONFIG *psDevConfig)
{
	IMG_UINT64 i, j = 0;
	IMG_UINT32 tmp = 0;
	IMG_UINT32 chunk = sizeof(IMG_UINT32) * 10;

	IMG_UINT64 ui64TestMemoryBase = psDevConfig->pasPhysHeaps[PHYS_HEAP_ID_GPU_LOCAL].sStartAddr.uiAddr;
	IMG_UINT64 ui64TestMemorySize = psDevConfig->pasPhysHeaps[PHYS_HEAP_ID_GPU_LOCAL].uiSize;

	PVR_LOG(("%s: Starting Local memory test from 0x%llx to 0x%llx (in CPU space)",
			 __func__, ui64TestMemoryBase, ui64TestMemoryBase + ui64TestMemorySize));

	while (j < ui64TestMemorySize) {
		IMG_CPU_PHYADDR myPaddr;
		IMG_UINT32 *pui32Virt;

		myPaddr.uiAddr = ui64TestMemoryBase + j;
		pui32Virt = OSMapPhysToLin(myPaddr, chunk, PVRSRV_MEMALLOCFLAG_CPU_UNCACHED);

		for (i = 0; i < chunk/sizeof(IMG_UINT32); i++) {
			*(pui32Virt + i) = 0xdeadbeef;
			OSWriteMemoryBarrier(pui32Virt);
			tmp = *(pui32Virt + i);
			if (tmp != 0xdeadbeef) {
				PVR_DPF((PVR_DBG_ERROR,
						"Local memory read-write test failed at address=0x%llx: written 0x%x, read 0x%x",
						ui64TestMemoryBase + ((i * sizeof(IMG_UINT32)) + j), (IMG_UINT32) 0xdeadbeef, tmp));

				OSUnMapPhysToLin(pui32Virt, chunk);
				return PVRSRV_ERROR_SYSTEM_LOCAL_MEMORY_INIT_FAIL;
			}
		}

		OSUnMapPhysToLin(pui32Virt, chunk);

		j += (1024 * 1024 * 500);
	}

	PVR_LOG(("Local memory read-write test passed!"));
	return PVRSRV_OK;
}

PVRSRV_ERROR SysDevInit(void *pvOSDevice, PVRSRV_DEVICE_CONFIG **ppsDevConfig)
{
	PVRSRV_DEVICE_CONFIG *psDevConfig;
	SYS_DATA *psSysData;
	IMG_UINT32 uiRegistersSize;
	PVRSRV_ERROR eError;

	PVR_ASSERT(pvOSDevice);

	psSysData = OSAllocZMem(sizeof(*psSysData));
	if (psSysData == NULL)
		return PVRSRV_ERROR_OUT_OF_MEMORY;

	dma_set_mask(pvOSDevice, DMA_BIT_MASK(40));

	/* Retrieve platform device and data */
	psSysData->pdev = to_platform_device((struct device *) pvOSDevice);
	psSysData->pdata = psSysData->pdev->dev.platform_data;

	/* Enable plato PCI */
	if (plato_enable(psSysData->pdev->dev.parent)) {
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to enable PCI device", __func__));
		eError = PVRSRV_ERROR_PCI_CALL_FAILED;
		goto ErrFreeSysData;
	}

	psSysData->registers = platform_get_resource_byname(psSysData->pdev, IORESOURCE_MEM, PLATO_ROGUE_RESOURCE_REGS);
	if (!psSysData->registers) {
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: Failed to get Rogue register information",
				 __func__));
		eError = PVRSRV_ERROR_PCI_REGION_UNAVAILABLE;
		goto ErrorDevDisable;
	}

	/* Check the address range is large enough. */
	uiRegistersSize = resource_size(psSysData->registers);
	if (uiRegistersSize < SYS_PLATO_REG_RGX_SIZE) {
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: Rogue register region isn't big enough (was %08X, required 0x%08x)",
				 __func__, uiRegistersSize, SYS_PLATO_REG_RGX_SIZE));

		eError = PVRSRV_ERROR_PCI_REGION_TOO_SMALL;
		goto ErrorDevDisable;
	}

#if !defined(VIRTUAL_PLATFORM)
	/* Reserve the rogue registers address range */
	if (!request_mem_region(psSysData->registers->start,
							uiRegistersSize,
							PVRSRV_MODNAME)) {
		PVR_DPF((PVR_DBG_ERROR, "%s: Rogue register memory region not available", __func__));
		eError = PVRSRV_ERROR_PCI_CALL_FAILED;
		goto ErrorDevDisable;
	}
#endif

	eError = DeviceConfigCreate(pvOSDevice, psSysData, &psDevConfig);
	if (eError != PVRSRV_OK) {
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to create device config", __func__));
		goto ErrorReleaseMemRegion;
	}

	PlatoLocalMemoryTest(psDevConfig);

	*ppsDevConfig = psDevConfig;

	return PVRSRV_OK;

ErrorReleaseMemRegion:
	release_mem_region(psSysData->registers->start,
					   resource_size(psSysData->registers));
ErrorDevDisable:
	plato_disable(psSysData->pdev->dev.parent);
ErrFreeSysData:
	OSFreeMem(psSysData);
	return eError;
}

void SysDevDeInit(PVRSRV_DEVICE_CONFIG *psDevConfig)
{
	SYS_DATA *psSysData = (SYS_DATA *)psDevConfig->hSysData;

	DeviceConfigDestroy(psDevConfig);

	release_mem_region(psSysData->registers->start,
					   resource_size(psSysData->registers));
	plato_disable(psSysData->pdev->dev.parent);

	OSFreeMem(psSysData);
}

static void PlatoInterruptHandler(void *pvData)
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

	/* Should only accept GPU interrupts through this API */
	if (ui32IRQ != PLATO_INTERRUPT_GPU) {
		PVR_DPF((PVR_DBG_ERROR, "%s: Invalid %d", __func__, ui32IRQ));
		return PVRSRV_ERROR_UNABLE_TO_INSTALL_ISR;
	}

	psLISRData = OSAllocZMem(sizeof(*psLISRData));
	if (!psLISRData)
		return PVRSRV_ERROR_OUT_OF_MEMORY;

	psLISRData->pfnLISR = pfnLISR;
	psLISRData->pvData = pvData;
	psLISRData->iInterruptID = ui32IRQ;
	psLISRData->psDev = psSysData->pdev->dev.parent;

	if (plato_set_interrupt_handler(psLISRData->psDev, psLISRData->iInterruptID, PlatoInterruptHandler, psLISRData)) {
		PVR_DPF((PVR_DBG_ERROR, "%s: plato_set_interrupt_handler() failed", __func__));
		eError = PVRSRV_ERROR_UNABLE_TO_INSTALL_ISR;
		goto err_free_data;
	}

	if (plato_enable_interrupt(psLISRData->psDev, psLISRData->iInterruptID)) {
		PVR_DPF((PVR_DBG_ERROR, "%s: plato_enable_interrupt() failed", __func__));
		eError = PVRSRV_ERROR_UNABLE_TO_INSTALL_ISR;
		goto err_unset_interrupt_handler;
	}

	*phLISRData = psLISRData;

	PVR_LOG(("Installed device LISR %s on IRQ %d", pszName, ui32IRQ));

	return PVRSRV_OK;

err_unset_interrupt_handler:
	plato_set_interrupt_handler(psLISRData->psDev, psLISRData->iInterruptID, NULL, NULL);
err_free_data:
	OSFreeMem(psLISRData);
	return eError;
}

PVRSRV_ERROR SysUninstallDeviceLISR(IMG_HANDLE hLISRData)
{
	LISR_DATA *psLISRData = (LISR_DATA *) hLISRData;
	int err;

	err = plato_disable_interrupt(psLISRData->psDev, psLISRData->iInterruptID);
	if (err)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: plato_enable_interrupt() failed (%d)", __func__, err));
	}

	err = plato_set_interrupt_handler(psLISRData->psDev, psLISRData->iInterruptID, NULL, NULL);
	if (err)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: plato_set_interrupt_handler() failed (%d)", __func__, err));
	}

	PVR_TRACE(("Uninstalled device LISR " IMG_PFN_FMTSPEC " from irq %u", psLISRData->pfnLISR, psLISRData->iInterruptID));

	OSFreeMem(psLISRData);

	return PVRSRV_OK;
}
