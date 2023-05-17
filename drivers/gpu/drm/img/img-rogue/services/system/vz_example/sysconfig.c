/*************************************************************************/ /*!
@File           sysconfig.c
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

#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include "img_defs.h"
#include "physheap.h"
#include "pvrsrv.h"
#include "rgxdevice.h"
#include "interrupt_support.h"
#include "sysconfig.h"

#if defined(SUPPORT_GPUVIRT_VALIDATION)
#include "vz_validation.h"
#endif

#define VZ_EXAMPLE_SYSTEM_NAME "vz_example"

static void SysCpuPAToDevPA(IMG_HANDLE hPrivData, IMG_UINT32 ui32NumOfAddr,
                            IMG_DEV_PHYADDR *psDevPA, IMG_CPU_PHYADDR *psCpuPA);
static void SysDevPAToCpuPA(IMG_HANDLE hPrivData, IMG_UINT32 ui32NumOfAddr,
                            IMG_CPU_PHYADDR *psCpuPA, IMG_DEV_PHYADDR *psDevPA);

typedef struct _SYS_DATA_
{
	IMG_HANDLE hSysLISRData;
	PFN_LISR pfnDeviceLISR;
	void *pvDeviceLISRData;
#if defined(SUPPORT_GPUVIRT_VALIDATION)
	IMG_HANDLE hValidationData;
#endif
} SYS_DATA;

typedef enum _PHYS_HEAP_IDX_
{
	PHYS_HEAP_IDX_SYSMEM,
	PHYS_HEAP_IDX_FIRMWARE,
#if defined(RGX_PREMAP_FW_HEAPS)
	PHYS_HEAP_IDX_FW_PT,
#endif
	PHYS_HEAP_IDX_COUNT,
} PHYS_HEAP_IDX;

static PHYS_HEAP_FUNCTIONS      gsPhysHeapFuncs = {
	SysCpuPAToDevPA,        /* pfnCpuPAddrToDevPAddr */
	SysDevPAToCpuPA,        /* pfnDevPAddrToCpuPAddr */
};

/*
 * CPU Physical to Device Physical address translation:
 * Template implementation below assumes CPU and GPU views of physical memory are identical
 */
static void SysCpuPAToDevPA(IMG_HANDLE hPrivData, IMG_UINT32 ui32NumOfAddr,
                            IMG_DEV_PHYADDR *psDevPA, IMG_CPU_PHYADDR *psCpuPA)
{
	/* Optimise common case */
	psDevPA[0].uiAddr = psCpuPA[0].uiAddr;
	if (ui32NumOfAddr > 1)
	{
		IMG_UINT32 ui32Idx;
		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
		{
			psDevPA[ui32Idx].uiAddr = psCpuPA[ui32Idx].uiAddr;
		}
	}

	PVR_UNREFERENCED_PARAMETER(hPrivData);
}

/*
 * Device Physical to CPU Physical address translation:
 * Template implementation below assumes CPU and GPU views of physical memory are identical
 */
static void SysDevPAToCpuPA(IMG_HANDLE hPrivData, IMG_UINT32 ui32NumOfAddr,
                            IMG_CPU_PHYADDR *psCpuPA, IMG_DEV_PHYADDR *psDevPA)
{
	/* Optimise common case */
	psCpuPA[0].uiAddr = psDevPA[0].uiAddr;
	if (ui32NumOfAddr > 1)
	{
		IMG_UINT32 ui32Idx;
		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
		{
			psCpuPA[ui32Idx].uiAddr = psDevPA[ui32Idx].uiAddr;
		}
	}

	PVR_UNREFERENCED_PARAMETER(hPrivData);
}

static IMG_BOOL SystemISRHandler(void *pvData)
{
	SYS_DATA *psSysData = pvData;
	IMG_BOOL bHandled;

	/* Any special system interrupt handling goes here */

	bHandled = psSysData->pfnDeviceLISR(psSysData->pvDeviceLISRData);
	return bHandled;
}

PVRSRV_ERROR SysInstallDeviceLISR(IMG_HANDLE hSysData,
                                  IMG_UINT32 ui32IRQ,
                                  const IMG_CHAR *pszName,
                                  PFN_LISR pfnLISR,
                                  void *pvData,
                                  IMG_HANDLE *phLISRData)
{
	SYS_DATA *psSysData = (SYS_DATA *)hSysData;
	PVRSRV_ERROR eError;

	if (psSysData->hSysLISRData)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: ISR for %s already installed!", __func__, pszName));
		return PVRSRV_ERROR_CANT_REGISTER_CALLBACK;
	}

	/* Wrap the device LISR */
	psSysData->pfnDeviceLISR = pfnLISR;
	psSysData->pvDeviceLISRData = pvData;

	eError = OSInstallSystemLISR(&psSysData->hSysLISRData, ui32IRQ, pszName,
	                             SystemISRHandler, psSysData,
	                             SYS_IRQ_FLAG_TRIGGER_DEFAULT);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	*phLISRData = psSysData;

	PVR_LOG(("Installed device LISR %s on IRQ %d", pszName, ui32IRQ));

	return PVRSRV_OK;
}

PVRSRV_ERROR SysUninstallDeviceLISR(IMG_HANDLE hLISRData)
{
	SYS_DATA *psSysData = (SYS_DATA *)hLISRData;
	PVRSRV_ERROR eError;

	PVR_ASSERT(psSysData);

	eError = OSUninstallSystemLISR(psSysData->hSysLISRData);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	/* clear interrupt data */
	psSysData->pfnDeviceLISR    = NULL;
	psSysData->pvDeviceLISRData = NULL;
	psSysData->hSysLISRData     = NULL;

	return PVRSRV_OK;
}

static PVRSRV_ERROR SysPrePower(IMG_HANDLE hSysData,
                                PVRSRV_SYS_POWER_STATE eNewPowerState,
                                PVRSRV_SYS_POWER_STATE eCurrentPowerState,
                                PVRSRV_POWER_FLAGS ePwrFlags)
{
	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_OK);
	PVR_LOG_RETURN_IF_FALSE((eNewPowerState != eCurrentPowerState), "no power change", PVRSRV_OK);

	PVR_UNREFERENCED_PARAMETER(hSysData);
	PVR_UNREFERENCED_PARAMETER(ePwrFlags);

	/* on powering down */
	if (eNewPowerState != PVRSRV_SYS_POWER_STATE_ON)
	{
		IMG_CPU_PHYADDR sSoCRegBase = {SOC_REGBANK_BASE};

		void* pvSocRegs = OSMapPhysToLin(sSoCRegBase,
		                                 SOC_REGBANK_SIZE,
		                                 PVRSRV_MEMALLOCFLAG_CPU_UNCACHED);

		OSWriteHWReg32(pvSocRegs, POW_DOMAIN_DISABLE_REG, POW_DOMAIN_GPU);
		OSUnMapPhysToLin(pvSocRegs, SOC_REGBANK_SIZE);
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR SysPostPower(IMG_HANDLE hSysData,
                                 PVRSRV_SYS_POWER_STATE eNewPowerState,
                                 PVRSRV_SYS_POWER_STATE eCurrentPowerState,
                                 PVRSRV_POWER_FLAGS ePwrFlags)
{
	PVRSRV_VZ_RET_IF_MODE(GUEST, PVRSRV_OK);
	PVR_LOG_RETURN_IF_FALSE((eNewPowerState != eCurrentPowerState), "no power change", PVRSRV_OK);

	PVR_UNREFERENCED_PARAMETER(hSysData);
	PVR_UNREFERENCED_PARAMETER(ePwrFlags);

	/* on powering up */
	if (eCurrentPowerState != PVRSRV_SYS_POWER_STATE_ON)
	{
		IMG_CPU_PHYADDR sSoCRegBase = {SOC_REGBANK_BASE};

		void* pvSocRegs = OSMapPhysToLin(sSoCRegBase,
		                                 SOC_REGBANK_SIZE,
		                                 PVRSRV_MEMALLOCFLAG_CPU_UNCACHED);

		OSWriteHWReg32(pvSocRegs, POW_DOMAIN_ENABLE_REG, POW_DOMAIN_GPU);
		OSUnMapPhysToLin(pvSocRegs, SOC_REGBANK_SIZE);
	}

	return PVRSRV_OK;
}

static PVRSRV_SYS_POWER_STATE RGXGpuDomainPower(PVRSRV_DEVICE_NODE *psDevNode)
{
	IMG_CPU_PHYADDR sSoCRegBase = {SOC_REGBANK_BASE};

	void* pvSocRegs = OSMapPhysToLin(sSoCRegBase,
	                                 SOC_REGBANK_SIZE,
	                                 PVRSRV_MEMALLOCFLAG_CPU_UNCACHED);

	IMG_UINT32 ui32SocDomainPower = OSReadHWReg32(pvSocRegs, POW_DOMAIN_STATUS_REG);

	bool bGpuDomainIsPowered = BITMASK_HAS(ui32SocDomainPower, POW_DOMAIN_GPU);

	PVR_UNREFERENCED_PARAMETER(psDevNode);
	OSUnMapPhysToLin(pvSocRegs, SOC_REGBANK_SIZE);

	return (bGpuDomainIsPowered) ? PVRSRV_SYS_POWER_STATE_ON : PVRSRV_SYS_POWER_STATE_OFF;
}

static void SysDevFeatureDepInit(PVRSRV_DEVICE_CONFIG *psDevConfig, IMG_UINT64 ui64Features)
{
	PVR_UNREFERENCED_PARAMETER(ui64Features);
	psDevConfig->eCacheSnoopingMode = PVRSRV_DEVICE_SNOOP_NONE;
}

static PVRSRV_DRIVER_MODE GetDriverMode(struct platform_device *psDev)
{
	PVRSRV_DRIVER_MODE eDriverMode;

#if (RGX_NUM_DRIVERS_SUPPORTED > 1)
	if (of_property_read_u32(psDev->dev.of_node, "vz-mode", (IMG_UINT32*) &eDriverMode))
	{
		PVR_DPF((PVR_DBG_WARNING, "%s: Cannot retrieve driver mode from Device Tree. "
								  "Default to native mode.", __func__));
		eDriverMode = DRIVER_MODE_NATIVE;
	}
#else
	eDriverMode = DRIVER_MODE_NATIVE;
#endif

	return eDriverMode;
}

static PVRSRV_ERROR DeviceConfigCreate(void *pvOSDevice,
									   PVRSRV_DEVICE_CONFIG **ppsDevConfig)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	struct platform_device *psDev;
	struct resource *dev_res = NULL;
	int dev_irq;
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_DEVICE_CONFIG *psDevConfig;
	RGX_DATA *psRGXData;
	RGX_TIMING_INFORMATION *psRGXTimingInfo;
	SYS_DATA *psSysData;
	PHYS_HEAP_CONFIG *psPhysHeapConfig;

	psDevConfig = OSAllocZMem(sizeof(*psDevConfig) +
							  sizeof(*psRGXData) +
							  sizeof(*psRGXTimingInfo) +
							  sizeof(*psSysData) +
							  sizeof(*psPhysHeapConfig) * PHYS_HEAP_IDX_COUNT);
	if (!psDevConfig)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psRGXData = (RGX_DATA *)((IMG_CHAR *)psDevConfig + sizeof(*psDevConfig));
	psRGXTimingInfo = (RGX_TIMING_INFORMATION *)((IMG_CHAR *)psRGXData + sizeof(*psRGXData));
	psSysData = (SYS_DATA *)((IMG_CHAR *)psRGXTimingInfo + sizeof(*psRGXTimingInfo));
	psPhysHeapConfig = (PHYS_HEAP_CONFIG *)((IMG_CHAR *)psSysData + sizeof(*psSysData));

	psDev = to_platform_device((struct device *)pvOSDevice);

	dma_set_mask(pvOSDevice, DMA_BIT_MASK(40));

	dev_irq = platform_get_irq(psDev, 0);
	if (dev_irq < 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: platform_get_irq failed (%d)", __func__, -dev_irq));
		eError = PVRSRV_ERROR_INVALID_DEVICE;
		return eError;
	}

	dev_res = platform_get_resource(psDev, IORESOURCE_MEM, 0);
	if (dev_res == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: platform_get_resource failed", __func__));
		eError =  PVRSRV_ERROR_INVALID_DEVICE;
		return eError;
	}

	/* Device Setup */
	psDevConfig->pvOSDevice              = pvOSDevice;
	psDevConfig->pszName                 = VZ_EXAMPLE_SYSTEM_NAME;
	psDevConfig->pszVersion              = NULL;

	/* Device setup information */
	psDevConfig->sRegsCpuPBase.uiAddr    = dev_res->start;
	psDevConfig->ui32IRQ                 = dev_irq;

#if defined(SUPPORT_GPUVIRT_VALIDATION_MTS)
	psDevConfig->ui32RegsSize            = RGX_VIRTUALISATION_REG_SIZE_PER_OS * GPUVIRT_VALIDATION_NUM_OS;
#else
	psDevConfig->ui32RegsSize            = (unsigned int)(dev_res->end - dev_res->start);
#endif

	PVR_ASSERT(psDevConfig->ui32RegsSize >= RGX_VIRTUALISATION_REG_SIZE_PER_OS);

	/* Power management */
	psDevConfig->pfnPrePowerState        = SysPrePower;
	psDevConfig->pfnPostPowerState       = SysPostPower;
	psDevConfig->pfnGpuDomainPower       = PVRSRV_VZ_MODE_IS(GUEST) ? NULL : RGXGpuDomainPower;

	/* Setup RGX specific timing data */
	psRGXTimingInfo->ui32CoreClockSpeed        = DEFAULT_CLOCK_RATE;
	psRGXTimingInfo->bEnableActivePM           = IMG_FALSE;
	psRGXTimingInfo->bEnableRDPowIsland        = IMG_FALSE;
	psRGXTimingInfo->ui32ActivePMLatencyms     = 0;

	/* Setup RGX specific data */
	psRGXData->psRGXTimingInfo = psRGXTimingInfo;

	/* Minimal configuration */
	psDevConfig->pfnClockFreqGet         = NULL;
	psDevConfig->hDevData                = psRGXData;
	psDevConfig->hSysData                = psSysData;
	psDevConfig->pfnSysDevFeatureDepInit = SysDevFeatureDepInit;
	psDevConfig->bHasFBCDCVersion31      = IMG_FALSE;

	psDevConfig->pasPhysHeaps = psPhysHeapConfig;

	/* If driver mode is not overridden by the apphint, set it here */
	if (!psPVRSRVData->bForceApphintDriverMode)
	{
		psPVRSRVData->eDriverMode = GetDriverMode(psDev);
	}

#if defined(SUPPORT_GPUVIRT_VALIDATION)
	psDevConfig->pfnSysDevVirtInit = SysInitValidation;

	eError = CreateMPUWatchdogThread(&psSysData->hValidationData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Create MPU watchdog thread failed.", __func__));
		OSFreeMem(psDevConfig);
		return eError;
	}
#endif

	return eError;
}

#if defined(RGX_VZ_STATIC_CARVEOUT_FW_HEAPS) || defined(SUPPORT_GPUVIRT_VALIDATION)
/* Obtain the IPA of the carveout range reserved for this VM */
static IMG_UINT64 GetFwCarveoutBase(PVRSRV_DEVICE_CONFIG *psDevConfig)
{
	struct platform_device *psDev = to_platform_device((struct device *)psDevConfig->pvOSDevice);
	IMG_UINT64 ui64BaseAddress;

	if (of_property_read_u64(psDev->dev.of_node, "fw-carveout", &ui64BaseAddress))
	{
		PVR_DPF((PVR_DBG_WARNING, "%s: Cannot retrieve firmware carveout address from Device Tree."
								  " Using default Base Address: 0x%llX",
								  __func__, FW_CARVEOUT_IPA_BASE));
		ui64BaseAddress = FW_CARVEOUT_IPA_BASE;
	}

	return ui64BaseAddress;
}

#if defined(RGX_PREMAP_FW_HEAPS)
static IMG_UINT64 GetFwPageTableCarveoutBase(PVRSRV_DEVICE_CONFIG *psDevConfig)
{
	struct platform_device *psDev = to_platform_device((struct device *)psDevConfig->pvOSDevice);
	IMG_UINT64 ui64BaseAddress;

	if (of_property_read_u64(psDev->dev.of_node, "fw-pt-carveout", &ui64BaseAddress))
	{
		PVR_DPF((PVR_DBG_WARNING, "%s: Cannot retrieve firmware page table carveout address"
								  " from Device Tree. Using default Base Address: 0x%llX",
								  __func__, FW_PT_CARVEOUT_IPA_BASE));
		ui64BaseAddress = FW_PT_CARVEOUT_IPA_BASE;
	}

	return ui64BaseAddress;
}
#endif
#endif

static PVRSRV_ERROR PhysHeapCfgCreate(PVRSRV_DEVICE_CONFIG *psDevConfig)
{
	IMG_CPU_PHYADDR sCpuBase;
	IMG_DEV_PHYADDR sDeviceBase;
	PHYS_HEAP_CONFIG *psPhysHeapConfig = psDevConfig->pasPhysHeaps;
	PVRSRV_ERROR eError = PVRSRV_OK;

#if defined(SUPPORT_GPUVIRT_VALIDATION)
	/*
	 * Validation-only driver
	 * This driver mode is designed to exercise the system's 2nd stage
	 * memory mapping or protection mechanisms without having a fully
	 * virtualized system. It is intended to run on pre-production
	 * emulation devices to validate the correct functioning of VZ
	 * hardware.
	 * It uses a native driver that relies on LMA memory.
	 * The driver partitions its assigned LMA range into equal parts,
	 * one for each RGX_NUM_DRIVERS_SUPPORTED. Each OSID partition will be
	 * protected by the 2nd stage memory access control device and
	 * the OSID based access can be tested according to the steps
	 * described in the "GPU Virtualization Validation" doc.
	 */
	psPhysHeapConfig[PHYS_HEAP_IDX_SYSMEM].pszPDumpMemspaceName = "SYSMEM";
	psPhysHeapConfig[PHYS_HEAP_IDX_SYSMEM].eType = PHYS_HEAP_TYPE_UMA;
	psPhysHeapConfig[PHYS_HEAP_IDX_SYSMEM].psMemFuncs = &gsPhysHeapFuncs;
	psPhysHeapConfig[PHYS_HEAP_IDX_SYSMEM].hPrivData = NULL;
	psPhysHeapConfig[PHYS_HEAP_IDX_SYSMEM].ui32UsageFlags = PHYS_HEAP_USAGE_GPU_LOCAL;

	sCpuBase.uiAddr = GetFwCarveoutBase(psDevConfig);
	SysCpuPAToDevPA(NULL, 1, &sDeviceBase, &sCpuBase);

	psPhysHeapConfig[PHYS_HEAP_IDX_SYSMEM].eType = PHYS_HEAP_TYPE_LMA;
	psPhysHeapConfig[PHYS_HEAP_IDX_SYSMEM].sStartAddr = sCpuBase;
	psPhysHeapConfig[PHYS_HEAP_IDX_SYSMEM].sCardBase = sDeviceBase;

	psDevConfig->eDefaultHeap = PVRSRV_PHYS_HEAP_GPU_LOCAL;
	psDevConfig->ui32PhysHeapCount = 1;
#else
	/* Virtualization driver */

	/* Heap configuration for general use */
	psPhysHeapConfig[PHYS_HEAP_IDX_SYSMEM].pszPDumpMemspaceName = "SYSMEM";
	psPhysHeapConfig[PHYS_HEAP_IDX_SYSMEM].eType = PHYS_HEAP_TYPE_UMA;
	psPhysHeapConfig[PHYS_HEAP_IDX_SYSMEM].psMemFuncs = &gsPhysHeapFuncs;
	psPhysHeapConfig[PHYS_HEAP_IDX_SYSMEM].hPrivData = NULL;
	psPhysHeapConfig[PHYS_HEAP_IDX_SYSMEM].ui32UsageFlags = PHYS_HEAP_USAGE_GPU_LOCAL;

	/* Heap configuration for memory shared with the firmware */
	psPhysHeapConfig[PHYS_HEAP_IDX_FIRMWARE].pszPDumpMemspaceName = "SYSMEM_FW";
	psPhysHeapConfig[PHYS_HEAP_IDX_FIRMWARE].psMemFuncs = &gsPhysHeapFuncs;
	psPhysHeapConfig[PHYS_HEAP_IDX_FIRMWARE].hPrivData = NULL;
	psPhysHeapConfig[PHYS_HEAP_IDX_FIRMWARE].ui32UsageFlags = PHYS_HEAP_USAGE_FW_SHARED;
	psPhysHeapConfig[PHYS_HEAP_IDX_FIRMWARE].uiSize = RGX_FIRMWARE_RAW_HEAP_SIZE;

#if defined(RGX_VZ_STATIC_CARVEOUT_FW_HEAPS)
	{
		/*
		 * In a static memory VZ setup, the size and base addresses of
		 * all Host and Guest drivers' Firmware heaps are laid out
		 * consecutively in a physically contiguous memory range known
		 * in advance by the Host driver.
		 *
		 * During the Host driver initialisation, it maps the entire range
		 * into the Firmware's virtual address space. No other mapping
		 * operations into the Firmware's VA space are needed after this.
		 * Guest driver must know only the base address of the range
		 * assign to it during system planning stage.
		 *
		 * The system integrator must ensure that:
		 *  - physically contiguous RAM region used as a Firmware heap
		 *    is not managed by any OS or Hypervisor (a carveout)
		 *  - Host driver must come online before any Guest drivers
		 *  - Host driver sets up the Firmware before Guests submits work
		 */

		sCpuBase.uiAddr = GetFwCarveoutBase(psDevConfig);
		SysCpuPAToDevPA(NULL, 1, &sDeviceBase, &sCpuBase);

		psPhysHeapConfig[PHYS_HEAP_IDX_FIRMWARE].eType = PHYS_HEAP_TYPE_LMA;
		psPhysHeapConfig[PHYS_HEAP_IDX_FIRMWARE].sStartAddr = sCpuBase;
		psPhysHeapConfig[PHYS_HEAP_IDX_FIRMWARE].sCardBase = sDeviceBase;
	}
#else
	/* Dynamic Firmware heap allocation */
	if (PVRSRV_VZ_MODE_IS(GUEST))
	{
		/*
		 * Guest drivers must provide a physically contiguous memory
		 * range to the Host via a PVZ call to have it mapped into
		 * the Firmware's address space. Guest drivers use the OS
		 * kernel's DMA/CMA allocator to obtain a DMA buffer to be
		 * used as a firmware heap. This memory will be managed
		 * internally by the Guest driver after the heap is created.
		 */
		DMA_ALLOC *psDmaAlloc = OSAllocZMem(sizeof(DMA_ALLOC));

		eError = (psDmaAlloc == NULL) ? PVRSRV_ERROR_OUT_OF_MEMORY : PVRSRV_OK;
		if (eError == PVRSRV_OK)
		{
			psDmaAlloc->pvOSDevice = psDevConfig->pvOSDevice;
			psDmaAlloc->ui64Size = RGX_FIRMWARE_RAW_HEAP_SIZE;

			eError = SysDmaAllocMem(psDmaAlloc);
			if (eError == PVRSRV_OK)
			{
				eError = SysDmaRegisterForIoRemapping(psDmaAlloc);

				if (eError == PVRSRV_OK)
				{
					sCpuBase.uiAddr = psDmaAlloc->sBusAddr.uiAddr;
					SysCpuPAToDevPA(NULL, 1, &sDeviceBase, &sCpuBase);

					psPhysHeapConfig[PHYS_HEAP_IDX_FIRMWARE].eType = PHYS_HEAP_TYPE_DMA;
					psPhysHeapConfig[PHYS_HEAP_IDX_FIRMWARE].hPrivData = psDmaAlloc;
					psPhysHeapConfig[PHYS_HEAP_IDX_FIRMWARE].sStartAddr = sCpuBase;
					psPhysHeapConfig[PHYS_HEAP_IDX_FIRMWARE].sCardBase = sDeviceBase;
				}
				else
				{
					OSFreeMem(psDmaAlloc);
					SysDmaFreeMem(psDmaAlloc);
				}
			}
			else
			{
				OSFreeMem(psDmaAlloc);
			}
		}
	}
	else
	{
		/*
		 * The Host or Native driver uses memory managed by
		 * the kernel on a page granularity and creates on-demand
		 * mappings into the Firmware's address space.
		 */
		psPhysHeapConfig[PHYS_HEAP_IDX_FIRMWARE].eType = PHYS_HEAP_TYPE_UMA;
	}
#endif /* defined(RGX_VZ_STATIC_CARVEOUT_FW_HEAPS) */

#if defined(RGX_VZ_STATIC_CARVEOUT_FW_HEAPS) && defined(RGX_PREMAP_FW_HEAPS)
	if (!PVRSRV_VZ_MODE_IS(GUEST))
	{
		sCpuBase.uiAddr = GetFwPageTableCarveoutBase(psDevConfig);
		SysCpuPAToDevPA(NULL, 1, &sDeviceBase, &sCpuBase);

		psPhysHeapConfig[PHYS_HEAP_IDX_FW_PT].pszPDumpMemspaceName = "SYSMEM_FW_PT";
		psPhysHeapConfig[PHYS_HEAP_IDX_FW_PT].psMemFuncs = &gsPhysHeapFuncs;
		psPhysHeapConfig[PHYS_HEAP_IDX_FW_PT].hPrivData = NULL;
		psPhysHeapConfig[PHYS_HEAP_IDX_FW_PT].ui32UsageFlags = PHYS_HEAP_USAGE_FW_PREMAP_PT;
		psPhysHeapConfig[PHYS_HEAP_IDX_FW_PT].eType = PHYS_HEAP_TYPE_LMA;
		psPhysHeapConfig[PHYS_HEAP_IDX_FW_PT].uiSize = RGX_FIRMWARE_MAX_PAGETABLE_SIZE;
		psPhysHeapConfig[PHYS_HEAP_IDX_FW_PT].sStartAddr = sCpuBase;
		psPhysHeapConfig[PHYS_HEAP_IDX_FW_PT].sCardBase = sDeviceBase;
	}
#endif /* defined(RGX_VZ_STATIC_CARVEOUT_FW_HEAPS) */

	/* Device's physical heaps */
	psDevConfig->eDefaultHeap = PVRSRV_PHYS_HEAP_GPU_LOCAL;
	psDevConfig->ui32PhysHeapCount = PHYS_HEAP_IDX_COUNT;
#endif /* defined(SUPPORT_GPUVIRT_VALIDATION) */

	return eError;
}

static void PhysHeapCfgDestroy(PVRSRV_DEVICE_CONFIG *psDevConfig)
{
#if defined(SUPPORT_GPUVIRT_VALIDATION)
	SYS_DATA *psSysData = (SYS_DATA*)psDevConfig->hSysData;

	PVR_ASSERT(psSysData);
#endif

#if defined(RGX_VZ_STATIC_CARVEOUT_FW_HEAPS)
	if (PVRSRV_VZ_MODE_IS(GUEST))
	{
		DMA_ALLOC *psDmaAlloc = psDevConfig->pasPhysHeaps[PHYS_HEAP_IDX_FIRMWARE].hPrivData;

		SysDmaDeregisterForIoRemapping(psDmaAlloc);
		SysDmaFreeMem(psDmaAlloc);
		OSFreeMem(psDmaAlloc);
	}
#endif

#if defined(SUPPORT_GPUVIRT_VALIDATION)
	DestroyMPUWatchdogThread(psSysData->hValidationData);
#endif
}

PVRSRV_ERROR SysDevInit(void *pvOSDevice, PVRSRV_DEVICE_CONFIG **ppsDevConfig)
{
	PVRSRV_DEVICE_CONFIG *psDevConfig;
	PVRSRV_ERROR eError;

	eError = DeviceConfigCreate(pvOSDevice, &psDevConfig);
	if (eError == PVRSRV_OK)
	{
		eError = PhysHeapCfgCreate(psDevConfig);
		if (eError != PVRSRV_OK)
		{
			OSFreeMem(psDevConfig);
		}

		*ppsDevConfig = psDevConfig;
	}

	return eError;
}

void SysDevDeInit(PVRSRV_DEVICE_CONFIG *psDevConfig)
{
	PhysHeapCfgDestroy(psDevConfig);

	OSFreeMem(psDevConfig);
}

PVRSRV_ERROR SysDebugInfo(PVRSRV_DEVICE_CONFIG *psDevConfig,
						  DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
						  void *pvDumpDebugFile)
{
	/* print here any system information useful for debug dumps */

	PVR_UNREFERENCED_PARAMETER(psDevConfig);
	PVR_UNREFERENCED_PARAMETER(pfnDumpDebugPrintf);
	PVR_UNREFERENCED_PARAMETER(pvDumpDebugFile);
	return PVRSRV_OK;
}

/******************************************************************************
 End of file (sysconfig.c)
******************************************************************************/
